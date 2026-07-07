#pragma once

#include <string>

namespace files {

struct FilesConfig {
    std::string start_directory;
};

FilesConfig defaultFilesConfig();
std::string defaultStartDirectory();
std::string normalizeDirectoryPath(const std::string& path);

}  // namespace files
