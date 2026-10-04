#include "BlackBoxDevice.hpp"

#include <cerrno>
#include <cstring>
#include <system_error>
#include <utility>

#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace bb {

BlackBoxDevice::BlackBoxDevice(const std::string& path, bool nonblocking) {
    int flags = O_RDWR | O_CLOEXEC;
    if (nonblocking) flags |= O_NONBLOCK;
    fd_ = ::open(path.c_str(), flags);
    if (fd_ < 0) {
        // A FIFO used as a mock device may refuse O_RDWR semantics; fall back to read-only.
        if (errno == ENXIO || errno == EACCES) {
            fd_ = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | (nonblocking ? O_NONBLOCK : 0));
        }
    }
    if (fd_ < 0) {
        throw std::system_error(errno, std::generic_category(), "open " + path);
    }
}

BlackBoxDevice::~BlackBoxDevice() {
    if (fd_ >= 0) ::close(fd_);
}

BlackBoxDevice::BlackBoxDevice(BlackBoxDevice&& other) noexcept
    : fd_(other.fd_), unsupported_(other.unsupported_), carry_(std::move(other.carry_)) {
    other.fd_ = -1;
}

BlackBoxDevice& BlackBoxDevice::operator=(BlackBoxDevice&& other) noexcept {
    if (this != &other) {
        if (fd_ >= 0) ::close(fd_);
        fd_ = other.fd_;
        unsupported_ = other.unsupported_;
        carry_ = std::move(other.carry_);
        other.fd_ = -1;
    }
    return *this;
}

bool BlackBoxDevice::pollReadable(int timeoutMs) const {
    pollfd p{};
    p.fd = fd_;
    p.events = POLLIN;
    int rc = ::poll(&p, 1, timeoutMs);
    if (rc <= 0) return false;  // timeout or EINTR
    return (p.revents & (POLLIN | POLLHUP)) != 0;
}

int BlackBoxDevice::readEvents(std::vector<bb_event>& out, std::size_t maxEvents) {
    if (maxEvents == 0) return 0;
    const std::size_t evSize = sizeof(bb_event);
    std::vector<std::uint8_t> buf(maxEvents * evSize);
    std::size_t have = carry_.size();
    std::memcpy(buf.data(), carry_.data(), have);
    carry_.clear();

    ssize_t n = ::read(fd_, buf.data() + have, buf.size() - have);
    if (n < 0) {
        if (have) carry_.assign(buf.begin(), buf.begin() + have);
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        return -1;
    }
    std::size_t total = have + static_cast<std::size_t>(n);
    std::size_t count = total / evSize;
    for (std::size_t i = 0; i < count; ++i) {
        bb_event ev;
        std::memcpy(&ev, buf.data() + i * evSize, evSize);
        out.push_back(ev);
    }
    std::size_t rest = total - count * evSize;
    if (rest) carry_.assign(buf.begin() + count * evSize, buf.begin() + total);
    return static_cast<int>(count);
}

bool BlackBoxDevice::write(const std::string& text, int level) {
    std::string payload = "<" + std::to_string(level) + ">" + text;
    ssize_t n = ::write(fd_, payload.data(), payload.size());
    return n >= 0;
}

bool BlackBoxDevice::doIoctl(unsigned long req, void* arg) {
    unsupported_ = false;
    int rc = arg ? ::ioctl(fd_, req, arg) : ::ioctl(fd_, req);
    if (rc < 0) {
        if (errno == ENOTTY || errno == EINVAL) unsupported_ = (errno == ENOTTY);
        return false;
    }
    return true;
}

bool BlackBoxDevice::freeze()     { return doIoctl(BB_IOC_FREEZE); }
bool BlackBoxDevice::unfreeze()   { return doIoctl(BB_IOC_UNFREEZE); }
bool BlackBoxDevice::clear()      { return doIoctl(BB_IOC_CLEAR); }
bool BlackBoxDevice::seekOldest() { return doIoctl(BB_IOC_SEEK_OLDEST); }
bool BlackBoxDevice::stats(bb_stats& out) {
    std::memset(&out, 0, sizeof(out));
    return doIoctl(BB_IOC_GET_STATS, &out);
}

}  // namespace bb
