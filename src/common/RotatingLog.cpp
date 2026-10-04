#include "RotatingLog.hpp"

#include <stdexcept>
#include <system_error>

namespace fs = std::filesystem;

namespace bb {

RotatingLog::RotatingLog(fs::path path, std::size_t maxBytes, int keep)
    : path_(std::move(path)), maxBytes_(maxBytes), keep_(keep < 1 ? 1 : keep) {
    if (path_.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(path_.parent_path(), ec);
    }
    openFile();
}

void RotatingLog::openFile() {
    std::error_code ec;
    size_ = fs::exists(path_, ec) ? static_cast<std::size_t>(fs::file_size(path_, ec)) : 0;
    out_.open(path_, std::ios::app);
    if (!out_) throw std::runtime_error("cannot open log file " + path_.string());
}

void RotatingLog::rotate() {
    out_.close();
    std::error_code ec;
    // log.(keep-1) is removed, the others shift up by one.
    fs::remove(fs::path(path_.string() + "." + std::to_string(keep_ - 1)), ec);
    for (int i = keep_ - 2; i >= 1; --i) {
        fs::path from = path_.string() + "." + std::to_string(i);
        fs::path to = path_.string() + "." + std::to_string(i + 1);
        if (fs::exists(from, ec)) fs::rename(from, to, ec);
    }
    if (keep_ > 1) {
        fs::rename(path_, fs::path(path_.string() + ".1"), ec);
    } else {
        fs::remove(path_, ec);
    }
    openFile();
}

void RotatingLog::append(const std::string& line) {
    if (size_ > 0 && size_ + line.size() + 1 > maxBytes_) rotate();
    out_ << line << '\n';
    out_.flush();
    if (!out_) throw std::runtime_error("write failed on " + path_.string());
    size_ += line.size() + 1;
}

}  // namespace bb
