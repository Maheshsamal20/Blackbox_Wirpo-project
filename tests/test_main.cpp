// Minimal unit-test runner (no external framework needed).
#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "BlackBoxDevice.hpp"
#include "BoundedQueue.hpp"
#include "EventFormat.hpp"
#include "RotatingLog.hpp"
#include "SnapshotWriter.hpp"

namespace fs = std::filesystem;

static std::atomic<int> g_failed{0}, g_checks{0};
#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            ++g_failed;                                                          \
            std::printf("    FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
        }                                                                        \
    } while (0)

static fs::path tempDir(const std::string& name) {
    fs::path p = fs::temp_directory_path() / ("bbtest-" + name + "-" + std::to_string(getpid()));
    fs::remove_all(p);
    fs::create_directories(p);
    return p;
}

static std::string slurp(const fs::path& p) {
    std::ifstream in(p);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// ---------------------------------------------------------------------------

static void test_abi_layout() {
    CHECK(sizeof(bb_event) == 128);
    CHECK(sizeof(bb_stats) == 48);
}

static void test_levels() {
    CHECK(bb::levelName(0) == "DEBUG");
    CHECK(bb::levelName(3) == "ERROR");
    CHECK(bb::levelName(9) == "?");
    CHECK(bb::parseLevel("warn").value() == 2);
    CHECK(bb::parseLevel("ERROR").value() == 3);
    CHECK(bb::parseLevel("1").value() == 1);
    CHECK(!bb::parseLevel("loud").has_value());
}

static void test_format() {
    bb_event e = bb::makeEvent(7, 1234, BB_LVL_ERROR, "disk failure", 1700000000123456000ULL);
    std::string line = bb::formatEvent(e);
    CHECK(line.find("#7") != std::string::npos);
    CHECK(line.find("pid=1234") != std::string::npos);
    CHECK(line.find("ERROR") != std::string::npos);
    CHECK(line.find("disk failure") != std::string::npos);
    CHECK(bb::formatTime(1700000000123456000ULL) == "2023-11-14T22:13:20.123456Z");
}

static void test_message_truncation() {
    std::string longMsg(500, 'x');
    bb_event e = bb::makeEvent(0, 1, BB_LVL_INFO, longMsg);
    CHECK(bb::eventMessage(e).size() == BB_MSG_MAX - 1);
    bb_event bad = bb::makeEvent(0, 1, BB_LVL_INFO, "abc");
    bad.len = 9999;                                  // a lying length must not overflow
    CHECK(bb::eventMessage(bad) == "abc");
}

static void test_queue_order_and_close() {
    bb::BoundedQueue<int> q(4);
    std::atomic<long> sum{0};
    std::thread consumer([&] {
        int v;
        int expect = 0;
        while (q.pop(v)) { CHECK(v == expect++); sum += v; }
    });
    for (int i = 0; i < 1000; ++i) CHECK(q.push(i));   // blocks when full (cap 4)
    q.close();
    consumer.join();
    CHECK(sum == 999L * 1000 / 2);
    CHECK(!q.push(1));                                 // closed queue rejects
}

static void test_queue_bounded() {
    bb::BoundedQueue<int> q(2);
    q.push(1); q.push(2);
    CHECK(q.size() == 2);
    std::atomic<bool> pushed{false};
    std::thread t([&] { q.push(3); pushed = true; });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(!pushed);                                    // still blocked: queue full
    int v;
    q.pop(v);
    t.join();
    CHECK(pushed);
}

static void test_rotating_log() {
    fs::path d = tempDir("log");
    bb::RotatingLog log(d / "t.log", 100, 3);
    for (int i = 0; i < 30; ++i) log.append("line number " + std::to_string(i));  // ~15 B each
    CHECK(fs::exists(d / "t.log"));
    CHECK(fs::exists(d / "t.log.1"));
    CHECK(fs::exists(d / "t.log.2"));
    CHECK(!fs::exists(d / "t.log.3"));                 // keep = 3 files total
    CHECK(fs::file_size(d / "t.log") <= 100);
    CHECK(slurp(d / "t.log").find("line number 29") != std::string::npos);
    fs::remove_all(d);
}

static void test_snapshot_writer() {
    fs::path d = tempDir("snap");
    bb::SnapshotWriter w(d, 3);
    std::vector<bb_event> evs;
    for (int i = 0; i < 5; ++i) evs.push_back(bb::makeEvent(i, 100 + i, BB_LVL_INFO, "event " + std::to_string(i), 1700000000000000000ULL + i));
    bb_stats st{};
    st.oldest_seq = 0; st.head_seq = 5; st.capacity = 1024;
    fs::path p = w.write(evs, "unit test", &st);
    std::string text = slurp(p);
    CHECK(text.find("# reason:  unit test") != std::string::npos);
    CHECK(text.find("# events:  5") != std::string::npos);
    CHECK(text.find("event 0") != std::string::npos);
    CHECK(text.find("event 4") != std::string::npos);
    CHECK(text.find("capacity: 1024") != std::string::npos);
    for (int i = 0; i < 6; ++i) { w.write(evs, "rotate " + std::to_string(i), nullptr); }
    CHECK(w.list().size() == 3);                       // rotated down to keep = 3
    for (const auto& e : fs::directory_iterator(d)) CHECK(e.path().extension() != ".tmp");
    fs::remove_all(d);
}

static void test_device_partial_reads() {
    fs::path d = tempDir("dev");
    fs::path fifo = d / "fifo";
    CHECK(mkfifo(fifo.c_str(), 0600) == 0);
    bb::BlackBoxDevice dev(fifo.string(), /*nonblocking=*/true);
    int wfd = ::open(fifo.c_str(), O_WRONLY);
    CHECK(wfd >= 0);

    bb_event a = bb::makeEvent(1, 1, BB_LVL_INFO, "first");
    bb_event b = bb::makeEvent(2, 2, BB_LVL_WARN, "second");
    std::vector<bb_event> got;

    // one whole event plus half of the next
    CHECK(::write(wfd, &a, sizeof(a)) == (ssize_t)sizeof(a));
    CHECK(::write(wfd, &b, 64) == 64);
    CHECK(dev.readEvents(got, 8) == 1);
    CHECK(got.size() == 1 && got[0].seq == 1);
    // nothing complete yet
    CHECK(dev.readEvents(got, 8) == 0);
    // rest of the second event
    CHECK(::write(wfd, reinterpret_cast<char*>(&b) + 64, sizeof(b) - 64) == (ssize_t)(sizeof(b) - 64));
    CHECK(dev.readEvents(got, 8) == 1);
    CHECK(got.size() == 2 && got[1].seq == 2 && bb::eventMessage(got[1]) == "second");

    ::close(wfd);
    // ioctl on a FIFO must report "unsupported", not crash
    CHECK(!dev.freeze());
    CHECK(dev.lastIoctlUnsupported());
    fs::remove_all(d);
}

static void test_device_open_missing() {
    bool threw = false;
    try { bb::BlackBoxDevice dev("/nonexistent/blackbox"); } catch (const std::system_error&) { threw = true; }
    CHECK(threw);
}

int main() {
    struct T { const char* name; std::function<void()> fn; };
    std::vector<T> tests = {
        {"abi_layout", test_abi_layout},
        {"levels", test_levels},
        {"format", test_format},
        {"message_truncation", test_message_truncation},
        {"queue_order_and_close", test_queue_order_and_close},
        {"queue_bounded", test_queue_bounded},
        {"rotating_log", test_rotating_log},
        {"snapshot_writer", test_snapshot_writer},
        {"device_partial_reads", test_device_partial_reads},
        {"device_open_missing", test_device_open_missing},
    };
    int failedTests = 0;
    for (auto& t : tests) {
        int before = g_failed;
        t.fn();
        bool ok = g_failed == before;
        std::printf("[%s] %s\n", ok ? " OK " : "FAIL", t.name);
        if (!ok) ++failedTests;
    }
    std::printf("\n%zu tests, %d checks, %d failed tests\n", tests.size(), g_checks.load(), failedTests);
    return failedTests ? 1 : 0;
}
