#pragma once
// RotatingLog - append-only text log that rotates by size: log, log.1, log.2 ...
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>

namespace bb {

class RotatingLog {
public:
    RotatingLog(std::filesystem::path path, std::size_t maxBytes, int keep);

    // Appends one line (newline added). Throws std::runtime_error if the file cannot be written.
    void append(const std::string& line);

    const std::filesystem::path& path() const { return path_; }

private:
    void openFile();
    void rotate();

    std::filesystem::path path_;
    std::size_t maxBytes_;
    int keep_;
    std::size_t size_ = 0;
    std::ofstream out_;
};

}  // namespace bb
