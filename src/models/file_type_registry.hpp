#pragma once

#include "core/files_types.hpp"
#include <string>
#include <vector>

namespace files {

struct FileTypeMapping {
    std::string extension;
    FileKind kind = FileKind::Unknown;
    std::string icon;
};

class FileTypeRegistry {
public:
    FileTypeRegistry();

    FileKind kindForExtension(const std::string& extension) const;
    std::string iconFor(const FileEntry& entry) const;
    void addMapping(FileTypeMapping mapping);

private:
    std::vector<FileTypeMapping> _mappings;
};

std::string normalizedExtension(const std::string& extension);

}  // namespace files
