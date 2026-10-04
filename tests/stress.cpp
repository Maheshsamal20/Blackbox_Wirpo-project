// stress - hammer /dev/blackbox with many writers and one reader, then verify consistency.
//
//   ./build/stress [-d device] [-w writers] [-n events_per_writer]
//
// Checks:
//   1. the reader never sees a sequence number go backwards
//   2. events from one writer arrive in the order that writer produced them
//   3. the driver's total_written equals writers * events_per_writer
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "BlackBoxDevice.hpp"
#include "EventFormat.hpp"

int main(int argc, char** argv) {
    std::string device = BB_DEVICE_PATH;
    int writers = 4;
    int perWriter = 5000;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "-d") && i + 1 < argc) device = argv[++i];
        else if (!std::strcmp(argv[i], "-w") && i + 1 < argc) writers = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "-n") && i + 1 < argc) perWriter = std::atoi(argv[++i]);
    }

    try {
        bb::BlackBoxDevice ctl(device);
        ctl.clear();

        std::atomic<bool> done{false};
        std::atomic<long> readCount{0}, seqErrors{0}, orderErrors{0};

        std::thread reader([&] {
            bb::BlackBoxDevice dev(device, /*nonblocking=*/true);
            std::map<int, long> lastN;               // per writer: last event number seen
            long long lastSeq = -1;
            std::vector<bb_event> evs;
            while (true) {
                bool wasDone = done.load();
                evs.clear();
                dev.pollReadable(50);
                int n = dev.readEvents(evs, 128);
                for (const auto& e : evs) {
                    ++readCount;
                    if (static_cast<long long>(e.seq) <= lastSeq) ++seqErrors;
                    lastSeq = static_cast<long long>(e.seq);
                    int w = 0; long k = 0;
                    if (std::sscanf(e.msg, "w%d n%ld", &w, &k) == 2) {
                        auto it = lastN.find(w);
                        if (it != lastN.end() && k <= it->second) ++orderErrors;
                        lastN[w] = k;
                    }
                }
                if (n <= 0 && wasDone) break;        // drained after writers finished
            }
        });

        std::vector<std::thread> ws;
        std::atomic<long> failedWrites{0};
        for (int w = 0; w < writers; ++w) {
            ws.emplace_back([&, w] {
                bb::BlackBoxDevice dev(device);
                for (int k = 0; k < perWriter; ++k) {
                    std::string m = "w" + std::to_string(w) + " n" + std::to_string(k);
                    if (!dev.write(m, BB_LVL_DEBUG)) ++failedWrites;
                }
            });
        }
        for (auto& t : ws) t.join();
        done = true;
        reader.join();

        bb_stats st{};
        ctl.stats(st);
        const long expected = static_cast<long>(writers) * perWriter;
        std::printf("writers=%d per_writer=%d expected=%ld\n", writers, perWriter, expected);
        std::printf("driver total_written=%llu  failed_writes=%ld\n",
                    (unsigned long long)st.total_written, failedWrites.load());
        std::printf("reader saw %ld events (overwrites are allowed when the ring is smaller)\n", readCount.load());
        std::printf("sequence errors=%ld  per-writer order errors=%ld\n", seqErrors.load(), orderErrors.load());

        bool ok = seqErrors == 0 && orderErrors == 0 && failedWrites == 0 &&
                  static_cast<long>(st.total_written) == expected;
        std::puts(ok ? "RESULT: PASS" : "RESULT: FAIL");
        return ok ? 0 : 1;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "stress: %s\n", e.what());
        return 2;
    }
}
