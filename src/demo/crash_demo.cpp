// crash_demo - simulates a small service that logs, degrades, and then crashes.
// With blackboxd running, the final ERROR event triggers a snapshot that shows
// the events leading up to the crash.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <system_error>
#include <thread>

#include "BlackBoxDevice.hpp"

int main(int argc, char** argv) {
    std::string device = BB_DEVICE_PATH;
    int events = 40;
    bool crash = true;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "-d") && i + 1 < argc) device = argv[++i];
        else if (!std::strcmp(argv[i], "-n") && i + 1 < argc) events = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--no-crash")) crash = false;
        else { std::fprintf(stderr, "usage: crash_demo [-d device] [-n events] [--no-crash]\n"); return 2; }
    }
    try {
        bb::BlackBoxDevice dev(device);
        dev.write("service started", BB_LVL_INFO);
        dev.write("connected to database", BB_LVL_INFO);
        for (int i = 1; i <= events; ++i) {
            std::string msg = "handled request id=" + std::to_string(1000 + i);
            int level = BB_LVL_DEBUG;
            if (i > events * 3 / 4) { msg += " latency rising"; level = BB_LVL_WARN; }
            dev.write(msg, level);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        dev.write("connection pool exhausted", BB_LVL_WARN);
        dev.write("fatal: null session while saving order", BB_LVL_ERROR);
        std::puts(crash ? "crash_demo: logged events, crashing now" : "crash_demo: logged events");
        std::fflush(stdout);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));  // let the daemon react
    } catch (const std::system_error& e) {
        std::fprintf(stderr, "crash_demo: %s\n", e.what());
        return 1;
    }
    if (crash) {
        volatile int* p = nullptr;
        *p = 42;                                       // deliberate crash (SIGSEGV)
    }
    return 0;
}
