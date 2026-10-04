#include "Daemon.hpp"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <thread>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "BlackBoxDevice.hpp"
#include "EventFormat.hpp"

namespace fs = std::filesystem;

namespace bb {

// Signal flags: plain lock-free atomics are async-signal-safe.
static std::atomic<bool> g_stop{false};
static std::atomic<bool> g_snapshot{false};

void Daemon::requestStop() { g_stop.store(true); }
void Daemon::requestSnapshot() { g_snapshot.store(true); }

const char* stateName(State s) {
    switch (s) {
        case State::Recording: return "RECORDING";
        case State::Frozen:    return "FROZEN";
        case State::Dumping:   return "DUMPING";
    }
    return "?";
}

Daemon::Daemon(DaemonConfig cfg) : cfg_(std::move(cfg)) {
    out_ = stderr;
}

Daemon::~Daemon() {
    if (out_ && out_ != stderr) std::fclose(out_);
}

void Daemon::say(const std::string& msg) {
    std::fprintf(out_, "blackboxd: %s\n", msg.c_str());
    std::fflush(out_);
}

void Daemon::setState(State s) {
    if (s == state_) return;
    say(std::string("state ") + stateName(state_) + " -> " + stateName(s));
    state_ = s;
}

// ---- reader thread: poll + read, push to the queue ------------------------

void Daemon::readerLoop(const std::string& devicePath) {
    try {
        BlackBoxDevice dev(devicePath, /*nonblocking=*/true);
        std::vector<bb_event> evs;
        while (!g_stop.load()) {
            if (g_snapshot.exchange(false)) {
                Item it;
                it.kind = Item::Kind::Snapshot;
                if (!queue_.push(it)) break;
            }
            if (!dev.pollReadable(100)) continue;      // timeout: re-check flags
            evs.clear();
            int n = dev.readEvents(evs, 64);
            if (n < 0) {
                if (errno == EINTR) continue;
                say(std::string("read error: ") + std::strerror(errno));
                readerFailed_ = true;
                break;
            }
            if (n == 0) {                              // EOF / hang-up on a mock FIFO
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }
            for (const auto& e : evs) {
                Item it;
                it.kind = Item::Kind::Event;
                it.ev = e;
                if (!queue_.push(it)) return;
            }
        }
    } catch (const std::exception& e) {
        say(std::string("reader: ") + e.what());
        readerFailed_ = true;
    }
    queue_.close();                                    // lets the worker finish
}

// ---- worker thread: format, log, check trigger ----------------------------

void Daemon::workerLoop() {
    Item it;
    while (queue_.pop(it)) {
        if (it.kind == Item::Kind::Snapshot) {
            takeSnapshot("manual request (SIGUSR1)");
            continue;
        }
        const bb_event& ev = it.ev;
        ++eventsSeen_;
        try {
            log_->append(formatEvent(ev));
        } catch (const std::exception& e) {
            say(std::string("log error: ") + e.what());
        }
        history_.push_back(ev);
        while (history_.size() > cfg_.history) history_.pop_front();

        if (ev.level >= cfg_.triggerLevel) {
            auto now = std::chrono::steady_clock::now();
            bool cooled = !haveLastAuto_ ||
                std::chrono::duration_cast<std::chrono::milliseconds>(now - lastAuto_).count()
                    >= cfg_.cooldownMs;
            if (cooled) {
                haveLastAuto_ = true;
                lastAuto_ = now;
                takeSnapshot("trigger: " + levelName(ev.level) + " event #" +
                             std::to_string(ev.seq) + " \"" + eventMessage(ev) + "\"");
            }
        }
    }
}

// ---- snapshot: freeze -> read all history -> write -> unfreeze ------------

namespace {
// Guarantees the recorder is unfrozen even if something throws.
struct FreezeGuard {
    BlackBoxDevice* dev;
    bool active = false;
    explicit FreezeGuard(BlackBoxDevice* d) : dev(d) {}
    ~FreezeGuard() { if (dev && active) dev->unfreeze(); }
};
}  // namespace

void Daemon::takeSnapshot(const std::string& reason) {
    std::vector<bb_event> events;
    bb_stats st{};
    bool haveStats = false;
    setState(State::Frozen);

    bool fromKernel = false;
    if (!cfg_.mock) {
        try {
            BlackBoxDevice dev(cfg_.device, /*nonblocking=*/true);
            FreezeGuard guard(&dev);
            if (dev.freeze()) {
                guard.active = true;
                haveStats = dev.stats(st);
                if (dev.seekOldest()) {
                    setState(State::Dumping);
                    for (;;) {
                        int n = dev.readEvents(events, 256);
                        if (n <= 0) break;            // 0 = drained (EAGAIN)
                    }
                    fromKernel = true;
                }
            } else {
                say("freeze ioctl failed, using local history");
            }
        } catch (const std::exception& e) {
            say(std::string("snapshot device error: ") + e.what());
        }
    }
    if (!fromKernel) {
        setState(State::Dumping);
        events.assign(history_.begin(), history_.end());
        haveStats = false;
    }

    try {
        fs::path p = snaps_->write(events, reason + (fromKernel ? "" : " [local history]"),
                                   haveStats ? &st : nullptr);
        ++snapshotsTaken_;
        say("snapshot written: " + p.string() + " (" + std::to_string(events.size()) + " events)");
    } catch (const std::exception& e) {
        say(std::string("snapshot failed: ") + e.what());
    }
    setState(State::Recording);
}

// ---- main entry -----------------------------------------------------------

int Daemon::run() {
    cfg_.dir = fs::absolute(cfg_.dir);
    try {
        snaps_ = std::make_unique<SnapshotWriter>(cfg_.dir, cfg_.keepSnapshots);
        log_ = std::make_unique<RotatingLog>(cfg_.dir / "blackbox.log", cfg_.logMaxBytes, cfg_.logKeep);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "blackboxd: %s\n", e.what());
        return 1;
    }

    if (cfg_.daemonize) {
        pid_t pid = fork();
        if (pid < 0) { std::perror("fork"); return 1; }
        if (pid > 0) { std::printf("blackboxd started, pid %d\n", pid); std::_Exit(0); }
        setsid();
        umask(022);
        int null = ::open("/dev/null", O_RDWR);
        if (null >= 0) { dup2(null, 0); dup2(null, 1); dup2(null, 2); if (null > 2) close(null); }
        out_ = std::fopen((cfg_.dir / "blackboxd.out").c_str(), "a");
        if (!out_) out_ = stderr;
    }

    // pidfile so `bbctl snapshot` can find us
    const fs::path pidfile = cfg_.dir / "blackboxd.pid";
    if (FILE* pf = std::fopen(pidfile.c_str(), "w")) {
        std::fprintf(pf, "%d\n", static_cast<int>(getpid()));
        std::fclose(pf);
    }

    struct sigaction sa{};
    sa.sa_handler = [](int sig) {
        if (sig == SIGUSR1) Daemon::requestSnapshot();
        else Daemon::requestStop();
    };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGUSR1, &sa, nullptr);

    say("started: device=" + cfg_.device + " dir=" + cfg_.dir.string() +
        " trigger>=" + levelName(cfg_.triggerLevel) + (cfg_.mock ? " (mock mode)" : ""));

    std::thread worker(&Daemon::workerLoop, this);
    std::thread reader(&Daemon::readerLoop, this, cfg_.device);

    reader.join();            // returns on stop signal or read failure
    queue_.close();
    worker.join();

    std::error_code ec;
    fs::remove(pidfile, ec);
    say("stopped: " + std::to_string(eventsSeen_.load()) + " events, " +
        std::to_string(snapshotsTaken_.load()) + " snapshots");
    return readerFailed_ ? 1 : 0;
}

}  // namespace bb
