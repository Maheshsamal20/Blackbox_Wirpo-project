// blackboxd - BlackBox daemon: streams events to a rotating log and writes snapshots.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "Daemon.hpp"
#include "EventFormat.hpp"

static void usage() {
    std::puts(
        "usage: blackboxd [options]\n"
        "  -D, --device PATH     device to read (default /dev/blackbox)\n"
        "  -o, --dir DIR         output directory (default /var/log/blackbox)\n"
        "  -t, --trigger LEVEL   snapshot at this level or above (default error)\n"
        "  -k, --keep N          snapshots to keep (default 10)\n"
        "  -H, --history N       local history size for fallback (default 1024)\n"
        "  -c, --cooldown MS     min ms between automatic snapshots (default 2000)\n"
        "  -d, --daemon          run in the background\n"
        "      --mock            device is a FIFO/file (testing without the kernel module)\n"
        "  -h, --help            this help\n"
        "signals: SIGUSR1 = snapshot now, SIGINT/SIGTERM = stop");
}

int main(int argc, char** argv) {
    bb::DaemonConfig cfg;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](const char* name) -> const char* {
            if (i + 1 >= argc) { std::fprintf(stderr, "blackboxd: %s needs a value\n", name); std::exit(2); }
            return argv[++i];
        };
        if (a == "-D" || a == "--device") cfg.device = next("--device");
        else if (a == "-o" || a == "--dir") cfg.dir = next("--dir");
        else if (a == "-t" || a == "--trigger") {
            auto lv = bb::parseLevel(next("--trigger"));
            if (!lv) { std::fprintf(stderr, "blackboxd: bad level\n"); return 2; }
            cfg.triggerLevel = *lv;
        }
        else if (a == "-k" || a == "--keep") cfg.keepSnapshots = std::atoi(next("--keep"));
        else if (a == "-H" || a == "--history") cfg.history = static_cast<std::size_t>(std::atol(next("--history")));
        else if (a == "-c" || a == "--cooldown") cfg.cooldownMs = std::atoi(next("--cooldown"));
        else if (a == "-d" || a == "--daemon") cfg.daemonize = true;
        else if (a == "--mock") cfg.mock = true;
        else if (a == "-h" || a == "--help") { usage(); return 0; }
        else { std::fprintf(stderr, "blackboxd: unknown option %s\n", a.c_str()); usage(); return 2; }
    }
    if (cfg.history == 0) cfg.history = 1;
    bb::Daemon d(cfg);
    return d.run();
}
