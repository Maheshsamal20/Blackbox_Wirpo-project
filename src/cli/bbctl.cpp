// bbctl - command-line tool for the BlackBox recorder.
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "BlackBoxDevice.hpp"
#include "EventFormat.hpp"

static void usage() {
    std::puts(
        "usage: bbctl [-d DEVICE] [-p PIDFILE] <command> [args]\n"
        "  log [-l LEVEL] MESSAGE...   record an event (LEVEL: debug|info|warn|error)\n"
        "  tail                        follow new events until Ctrl-C\n"
        "  dump [-n N]                 print stored history (last N events if given)\n"
        "  freeze | unfreeze           stop / resume recording\n"
        "  clear                       discard stored events\n"
        "  stats                       show recorder statistics\n"
        "  snapshot                    ask blackboxd to write a snapshot (SIGUSR1)");
}

static int cmdStats(bb::BlackBoxDevice& dev) {
    bb_stats st;
    if (!dev.stats(st)) { std::perror("bbctl: stats"); return 1; }
    std::printf("state:          %s\n", st.frozen ? "FROZEN" : "RECORDING");
    std::printf("capacity:       %u events\n", st.capacity);
    std::printf("used:           %u events\n", st.used);
    std::printf("total written:  %llu\n", (unsigned long long)st.total_written);
    std::printf("dropped frozen: %llu\n", (unsigned long long)st.dropped_frozen);
    std::printf("oldest seq:     %llu\n", (unsigned long long)st.oldest_seq);
    std::printf("head seq:       %llu\n", (unsigned long long)st.head_seq);
    return 0;
}

static int cmdDump(const std::string& device, long lastN) {
    bb::BlackBoxDevice dev(device, /*nonblocking=*/true);
    if (!dev.seekOldest()) { std::perror("bbctl: seek"); return 1; }
    std::vector<bb_event> all;
    for (;;) {
        int n = dev.readEvents(all, 256);
        if (n < 0) { std::perror("bbctl: read"); return 1; }
        if (n == 0) break;
    }
    std::size_t start = 0;
    if (lastN > 0 && static_cast<std::size_t>(lastN) < all.size()) start = all.size() - lastN;
    for (std::size_t i = start; i < all.size(); ++i) std::puts(bb::formatEvent(all[i]).c_str());
    return 0;
}

static int cmdSnapshot(const std::string& pidfile) {
    std::ifstream in(pidfile);
    long pid = 0;
    if (!(in >> pid) || pid <= 0) {
        std::fprintf(stderr, "bbctl: cannot read pid from %s (is blackboxd running?)\n", pidfile.c_str());
        return 1;
    }
    if (kill(static_cast<pid_t>(pid), SIGUSR1) != 0) { std::perror("bbctl: kill"); return 1; }
    std::puts("snapshot requested");
    return 0;
}

int main(int argc, char** argv) {
    std::string device = BB_DEVICE_PATH;
    std::string pidfile = "/var/log/blackbox/blackboxd.pid";
    int i = 1;
    while (i + 1 < argc && (std::strcmp(argv[i], "-d") == 0 || std::strcmp(argv[i], "-p") == 0)) {
        (argv[i][1] == 'd' ? device : pidfile) = argv[i + 1];
        i += 2;
    }
    if (i >= argc) { usage(); return 2; }
    std::string cmd = argv[i++];

    try {
        if (cmd == "snapshot") return cmdSnapshot(pidfile);

        if (cmd == "log") {
            int level = BB_LVL_INFO;
            if (i + 1 < argc && std::strcmp(argv[i], "-l") == 0) {
                auto lv = bb::parseLevel(argv[i + 1]);
                if (!lv) { std::fprintf(stderr, "bbctl: bad level '%s'\n", argv[i + 1]); return 2; }
                level = *lv;
                i += 2;
            }
            if (i >= argc) { usage(); return 2; }
            std::string msg;
            for (; i < argc; ++i) { if (!msg.empty()) msg += ' '; msg += argv[i]; }
            bb::BlackBoxDevice dev(device);
            if (!dev.write(msg, level)) { std::perror("bbctl: write"); return 1; }
            return 0;
        }
        if (cmd == "tail") {
            bb::BlackBoxDevice dev(device);
            std::vector<bb_event> evs;
            for (;;) {
                evs.clear();
                int n = dev.readEvents(evs, 32);   // blocks until events arrive
                if (n < 0) { if (errno == EINTR) continue; std::perror("bbctl: read"); return 1; }
                for (const auto& e : evs) std::puts(bb::formatEvent(e).c_str());
                std::fflush(stdout);
            }
        }
        if (cmd == "dump") {
            long n = 0;
            if (i + 1 < argc && std::strcmp(argv[i], "-n") == 0) n = std::atol(argv[i + 1]);
            return cmdDump(device, n);
        }

        bb::BlackBoxDevice dev(device);
        if (cmd == "freeze")   { if (!dev.freeze())   { std::perror("bbctl: freeze");   return 1; } return 0; }
        if (cmd == "unfreeze") { if (!dev.unfreeze()) { std::perror("bbctl: unfreeze"); return 1; } return 0; }
        if (cmd == "clear")    { if (!dev.clear())    { std::perror("bbctl: clear");    return 1; } return 0; }
        if (cmd == "stats")    return cmdStats(dev);
    } catch (const std::system_error& e) {
        std::fprintf(stderr, "bbctl: %s\n", e.what());
        return 1;
    }
    usage();
    return 2;
}
