#include "SnapshotWriter.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <stdexcept>
#include <system_error>

#include "EventFormat.hpp"

namespace fs = std::filesystem;

namespace bb {

SnapshotWriter::SnapshotWriter(fs::path dir, int keep)
    : dir_(std::move(dir)), keep_(keep < 1 ? 1 : keep) {
    std::error_code ec;
    fs::create_directories(dir_, ec);
    if (ec) throw std::runtime_error("cannot create " + dir_.string() + ": " + ec.message());
}

static std::string timestampName() {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto us = duration_cast<microseconds>(now.time_since_epoch()).count();
    std::time_t secs = static_cast<std::time_t>(us / 1000000);
    unsigned micros = static_cast<unsigned>(us % 1000000);
    std::tm tm{};
    gmtime_r(&secs, &tm);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%04d%02d%02d-%02d%02d%02d-%06u",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec, micros);
    return buf;
}

std::vector<fs::path> SnapshotWriter::list() const {
    std::vector<fs::path> files;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir_, ec)) {
        const std::string name = e.path().filename().string();
        if (name.rfind("blackbox-", 0) == 0 && e.path().extension() == ".snap")
            files.push_back(e.path());
    }
    std::sort(files.begin(), files.end());   // timestamp in name => chronological
    return files;
}

void SnapshotWriter::rotateOld() {
    auto files = list();
    std::error_code ec;
    while (static_cast<int>(files.size()) > keep_) {
        fs::remove(files.front(), ec);
        files.erase(files.begin());
    }
}

fs::path SnapshotWriter::write(const std::vector<bb_event>& events,
                               const std::string& reason, const bb_stats* stats) {
    const std::string name = "blackbox-" + timestampName() + ".snap";
    const fs::path finalPath = dir_ / name;
    const fs::path tmpPath = dir_ / (name + ".tmp");
    {
        std::ofstream out(tmpPath);
        if (!out) throw std::runtime_error("cannot write " + tmpPath.string());
        const auto nowNs = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        out << "# BlackBox snapshot\n";
        out << "# created: " << formatTime(nowNs) << "\n";
        out << "# reason:  " << reason << "\n";
        out << "# events:  " << events.size() << "\n";
        if (stats) {
            out << "# oldest_seq: " << stats->oldest_seq << "  head_seq: " << stats->head_seq
                << "  capacity: " << stats->capacity
                << "  dropped_while_frozen: " << stats->dropped_frozen << "\n";
        }
        out << "# format: [time] #seq pid=PID LEVEL source: message\n\n";
        for (const auto& ev : events) out << formatEvent(ev) << '\n';
        out.flush();
        if (!out) throw std::runtime_error("write failed on " + tmpPath.string());
    }
    std::error_code ec;
    fs::rename(tmpPath, finalPath, ec);
    if (ec) throw std::runtime_error("rename failed: " + ec.message());
    rotateOld();
    return finalPath;
}

}  // namespace bb
