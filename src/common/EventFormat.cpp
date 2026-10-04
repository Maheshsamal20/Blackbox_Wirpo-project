#include "EventFormat.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace bb {

std::string levelName(unsigned level) {
    switch (level) {
        case BB_LVL_DEBUG: return "DEBUG";
        case BB_LVL_INFO:  return "INFO";
        case BB_LVL_WARN:  return "WARN";
        case BB_LVL_ERROR: return "ERROR";
        default:           return "?";
    }
}

std::optional<int> parseLevel(const std::string& text) {
    std::string t;
    for (char c : text) t.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    if (t == "DEBUG" || t == "0") return BB_LVL_DEBUG;
    if (t == "INFO" || t == "1") return BB_LVL_INFO;
    if (t == "WARN" || t == "WARNING" || t == "2") return BB_LVL_WARN;
    if (t == "ERROR" || t == "ERR" || t == "3") return BB_LVL_ERROR;
    return std::nullopt;
}

std::string formatTime(std::uint64_t ts_ns) {
    std::time_t secs = static_cast<std::time_t>(ts_ns / 1000000000ULL);
    unsigned micros = static_cast<unsigned>((ts_ns % 1000000000ULL) / 1000ULL);
    std::tm tm{};
    gmtime_r(&secs, &tm);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%06uZ",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec, micros);
    return buf;
}

std::string eventMessage(const bb_event& ev) {
    std::size_t n = std::min<std::size_t>(ev.len, BB_MSG_MAX - 1);
    std::size_t real = 0;
    while (real < n && ev.msg[real] != '\0') ++real;  // stop at NUL if len lies
    return std::string(ev.msg, real);
}

static const char* sourceName(unsigned s) {
    switch (s) {
        case BB_SRC_USER:      return "user";
        case BB_SRC_HEARTBEAT: return "heartbeat";
        case BB_SRC_DRIVER:    return "driver";
        default:               return "?";
    }
}

std::string formatEvent(const bb_event& ev) {
    char head[160];
    std::snprintf(head, sizeof(head), "[%s] #%llu pid=%u %-5s %s: ",
                  formatTime(ev.ts_ns).c_str(),
                  static_cast<unsigned long long>(ev.seq), ev.pid,
                  levelName(ev.level).c_str(), sourceName(ev.source));
    return std::string(head) + eventMessage(ev);
}

bb_event makeEvent(std::uint64_t seq, std::uint32_t pid, unsigned level,
                   const std::string& msg, std::uint64_t ts_ns, unsigned source) {
    bb_event ev;
    std::memset(&ev, 0, sizeof(ev));
    ev.seq = seq;
    ev.pid = pid;
    ev.level = static_cast<__u8>(level);
    ev.source = static_cast<__u8>(source);
    ev.ts_ns = ts_ns;
    std::size_t n = std::min<std::size_t>(msg.size(), BB_MSG_MAX - 1);
    std::memcpy(ev.msg, msg.data(), n);
    ev.len = static_cast<__u16>(n);
    return ev;
}

}  // namespace bb
