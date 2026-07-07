#include "core/files_config.hpp"

#include <spdlog/spdlog.h>
#include <cstdlib>
#include <string>
#include <unistd.h>

namespace files {
namespace {

std::string envOrEmpty(const char* name)
{
    const char* value = std::getenv(name);
    return value && value[0] != '\0' ? value : "";
}

std::string currentDirectory()
{
    char cwd[512] = {};
    if (getcwd(cwd, sizeof(cwd)) == nullptr) {
        spdlog::warn("FilesConfig: getcwd failed, fallback to current directory");
        return ".";
    }
    return cwd;
}

std::string homeDirectory(const char* fallback)
{
    const char* home = std::getenv("HOME");
    return home && home[0] != '\0' ? home : fallback;
}

}  // namespace

FilesConfig defaultFilesConfig()
{
    return FilesConfig{defaultStartDirectory()};
}

std::string defaultStartDirectory()
{
    const std::string explicit_dir = envOrEmpty("FILES_START_DIR");
    if (!explicit_dir.empty()) {
        return normalizeDirectoryPath(explicit_dir);
    }

#if FILES_USE_SDL
    return normalizeDirectoryPath(homeDirectory(currentDirectory().c_str()));
#else
    return normalizeDirectoryPath(homeDirectory("/"));
#endif
}

std::string normalizeDirectoryPath(const std::string& path)
{
    std::string normalized = path.empty() ? currentDirectory() : path;
    if (!normalized.empty() && normalized.front() != '/') {
        normalized = currentDirectory() + "/" + normalized;
    }

    while (normalized.size() > 1 && normalized.back() == '/') {
        normalized.pop_back();
    }
    return normalized;
}

}  // namespace files
