#pragma once
// SnapshotWriter - writes human-readable snapshot files and rotates old ones.
#include <filesystem>
#include <string>
#include <vector>

#include "bb_uapi.h"

namespace bb {

class SnapshotWriter {
public:
    SnapshotWriter(std::filesystem::path dir, int keep);

    // Writes <dir>/blackbox-YYYYmmdd-HHMMSS-uuuuuu.snap atomically
    // (temp file, then rename). `stats` may be null. Returns the final path.
    // Throws std::runtime_error on failure.
    std::filesystem::path write(const std::vector<bb_event>& events,
                                const std::string& reason,
                                const bb_stats* stats);

    // Snapshot files in the directory, oldest first.
    std::vector<std::filesystem::path> list() const;

private:
    void rotateOld();

    std::filesystem::path dir_;
    int keep_;
};

}  // namespace bb
