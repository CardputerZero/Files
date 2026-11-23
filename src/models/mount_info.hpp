#pragma once

#include <cstdint>
#include <filesystem>
#include <istream>
#include <vector>

namespace files::internal {

struct MountRecord {
    uint64_t id;
    std::string deviceNumber;
    std::filesystem::path root;
    std::filesystem::path target;
    std::filesystem::path source;
};

std::vector<MountRecord> readMountInfo(std::istream& input);
std::vector<MountRecord> currentMounts();
bool containsMountPoint(const std::filesystem::path& path, std::error_code& error,
                        std::filesystem::path* relativeMount = nullptr);

}  // namespace files::internal
