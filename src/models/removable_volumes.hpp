#pragma once

#include "core/files_types.hpp"
#include "models/command.hpp"
#include <filesystem>
#include "models/mount_info.hpp"

namespace files::internal {

struct RemovableVolume {
    MountRecord mount;
    std::filesystem::path disk;
    std::string name;
    bool powerOff = false;
    unsigned displayNumber = 0;

    std::string displayName() const
    {
        return displayNumber ? name + " #" + std::to_string(displayNumber) : name;
    }
};

std::vector<RemovableVolume> removableVolumes(const std::vector<MountRecord>& mounts,
                                             const std::filesystem::path& mediaDirectory,
                                             const std::filesystem::path& sysfs);
std::vector<RemovableVolume> removableVolumes();
bool volumeContains(const RemovableVolume& volume, const std::filesystem::path& path);
bool volumePresent(uint64_t id);

// Keep transfer paths on the same mount if the mount point is removed or reused.
class TransferDirectory {
public:
    TransferDirectory(const std::filesystem::path& directory, uint64_t mountId);
    ~TransferDirectory();
    TransferDirectory(const TransferDirectory&) = delete;
    TransferDirectory& operator=(const TransferDirectory&) = delete;
    bool connected() const;
    const std::filesystem::path& path() const { return _path; }

private:
    int _fd = -1;
    uint64_t _mountId;
    std::filesystem::path _path;
};
FileOperationResult ejectVolume(const RemovableVolume& volume, const CommandOutput& progress = {});

}  // namespace files::internal
