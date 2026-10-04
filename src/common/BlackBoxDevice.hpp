#pragma once
// BlackBoxDevice - RAII wrapper around the /dev/blackbox file descriptor.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "bb_uapi.h"

namespace bb {

class BlackBoxDevice {
public:
    // Opens the device. Throws std::system_error on failure.
    explicit BlackBoxDevice(const std::string& path = BB_DEVICE_PATH,
                            bool nonblocking = false);
    ~BlackBoxDevice();

    BlackBoxDevice(const BlackBoxDevice&) = delete;
    BlackBoxDevice& operator=(const BlackBoxDevice&) = delete;
    BlackBoxDevice(BlackBoxDevice&& other) noexcept;
    BlackBoxDevice& operator=(BlackBoxDevice&& other) noexcept;

    int fd() const { return fd_; }

    // Wait until events are readable. Returns true if readable, false on timeout.
    bool pollReadable(int timeoutMs) const;

    // Read up to maxEvents whole events, appended to `out`.
    // Returns the number of events read, 0 if nothing was available
    // (non-blocking or EOF), or -1 on error (errno set).
    int readEvents(std::vector<bb_event>& out, std::size_t maxEvents = 64);

    // Write a text message with the given level. Returns false on failure.
    bool write(const std::string& text, int level = BB_LVL_INFO);

    // ioctl helpers - return false (errno set) on failure.
    bool freeze();
    bool unfreeze();
    bool clear();
    bool seekOldest();
    bool stats(bb_stats& out);

    // True if the last ioctl failed because the device does not support it
    // (e.g. a FIFO used as a mock device).
    bool lastIoctlUnsupported() const { return unsupported_; }

private:
    bool doIoctl(unsigned long req, void* arg = nullptr);

    int fd_ = -1;
    bool unsupported_ = false;
    std::vector<std::uint8_t> carry_;  // bytes of a partially read event
};

}  // namespace bb
