#include "models/mount_info.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace files::internal {
namespace fs = std::filesystem;
namespace {

std::string unescapeMountField(const std::string& text)
{
    std::string value;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\\' && i + 3 < text.size() &&
            text.find_first_not_of("01234567", i + 1) >= i + 4) {
            value += static_cast<char>((text[i+1] - '0') * 64 + (text[i+2] - '0') * 8 + text[i+3] - '0');
            i += 3;
        } else {
            value += text[i];
        }
    }
    return value;
}

}  // namespace

std::vector<MountRecord> readMountInfo(std::istream& input)
{
    std::vector<MountRecord> mounts;
    for (std::string line; std::getline(input, line);) {
        std::istringstream fields(line);
        uint64_t parent;
        MountRecord mount{};
        std::string root, target, token, source;
        if (!(fields >> mount.id >> parent >> mount.deviceNumber >> root >> target)) {
            continue;
        }
        if (mount.deviceNumber.find(':') == std::string::npos ||
            mount.deviceNumber.find_first_not_of("0123456789:") != std::string::npos) {
            continue;
        }
        while (fields >> token && token != "-") {}
        if (token != "-" || !(fields >> token >> source)) {
            continue;
        }
        mount.root = unescapeMountField(root);
        mount.target = unescapeMountField(target);
        mount.source = unescapeMountField(source);
        mounts.push_back(std::move(mount));
    }
    return mounts;
}

std::vector<MountRecord> currentMounts()
{
    std::ifstream stream("/proc/self/mountinfo");
    return readMountInfo(stream);
}

bool containsMountPoint(const fs::path& path, std::error_code& error, fs::path* relativeMount)
{
    error.clear();
#if defined(__linux__)
    if (fs::is_symlink(fs::symlink_status(path, error)) || error) {
        return false;
    }
    const auto resolved = fs::canonical(path, error);
    if (error) {
        return false;
    }
    std::ifstream input("/proc/self/mountinfo");
    const auto mounts = readMountInfo(input);
    if (mounts.empty() || input.bad()) {
        error = std::make_error_code(std::errc::io_error);
        return false;
    }
    return std::any_of(mounts.begin(), mounts.end(), [&](const auto& mount) {
        const auto relative = mount.target.lexically_relative(resolved);
        if (relative.empty() || *relative.begin() == "..") return false;
        if (relativeMount) *relativeMount = relative;
        return true;
    });
#else
    (void)path;
    (void)relativeMount;
    return false;
#endif
}

}  // namespace files::internal
