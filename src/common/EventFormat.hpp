#pragma once
// EventFormat - turn bb_event records into readable text and back.
#include <cstdint>
#include <optional>
#include <string>

#include "bb_uapi.h"

namespace bb {

// "DEBUG", "INFO", "WARN", "ERROR" (or "?" for unknown values)
std::string levelName(unsigned level);

// Accepts names (case-insensitive) or digits 0..3.
std::optional<int> parseLevel(const std::string& text);

// "2026-10-04T12:00:00.123456Z"
std::string formatTime(std::uint64_t ts_ns);

// "[time] #seq pid=N LEVEL source: message"
std::string formatEvent(const bb_event& ev);

// Message as a std::string (bounded by len and BB_MSG_MAX).
std::string eventMessage(const bb_event& ev);

// Build an event (used by tests and the mock generator).
bb_event makeEvent(std::uint64_t seq, std::uint32_t pid, unsigned level,
                   const std::string& msg, std::uint64_t ts_ns = 0,
                   unsigned source = BB_SRC_USER);

}  // namespace bb
