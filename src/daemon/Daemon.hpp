#pragma once
// Daemon - orchestrates the reader thread, worker thread and snapshots.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "BoundedQueue.hpp"
#include "RotatingLog.hpp"
#include "SnapshotWriter.hpp"
#include "bb_uapi.h"

namespace bb {

struct DaemonConfig {
    std::string device = BB_DEVICE_PATH;
    std::filesystem::path dir = "/var/log/blackbox";
    int triggerLevel = BB_LVL_ERROR;      // snapshot when an event reaches this level
    int keepSnapshots = 10;
    std::size_t history = 1024;           // local mirror size (fallback source for snapshots)
    std::size_t logMaxBytes = 1024 * 1024;
    int logKeep = 3;
    int cooldownMs = 2000;                // minimum time between automatic snapshots
    bool mock = false;                    // device is a plain FIFO/file, no ioctl, no 2nd open
    bool daemonize = false;
};

enum class State { Recording, Frozen, Dumping };
const char* stateName(State s);

class Daemon {
public:
    explicit Daemon(DaemonConfig cfg);
    ~Daemon();

    // Runs until SIGINT/SIGTERM. Returns the process exit code.
    int run();

    // Asynchronous-signal-safe entry points used by the signal handlers.
    static void requestStop();
    static void requestSnapshot();

private:
    struct Item {
        enum class Kind { Event, Snapshot } kind = Kind::Event;
        bb_event ev{};
    };

    void readerLoop(const std::string& devicePath);
    void workerLoop();
    void takeSnapshot(const std::string& reason);
    void setState(State s);
    void say(const std::string& msg);

    DaemonConfig cfg_;
    BoundedQueue<Item> queue_{4096};
    std::unique_ptr<RotatingLog> log_;
    std::unique_ptr<SnapshotWriter> snaps_;
    std::deque<bb_event> history_;        // touched only by the worker thread
    State state_ = State::Recording;      // touched only by the worker thread
    std::chrono::steady_clock::time_point lastAuto_{};
    bool haveLastAuto_ = false;
    std::FILE* out_ = nullptr;            // diagnostics (stderr, or a file when daemonized)

    std::atomic<std::uint64_t> eventsSeen_{0};
    std::atomic<std::uint64_t> snapshotsTaken_{0};
    std::atomic<bool> readerFailed_{false};
};

}  // namespace bb
