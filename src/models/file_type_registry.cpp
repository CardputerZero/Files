#include "models/file_type_registry.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

namespace files {
namespace {

std::vector<FileTypeMapping> defaultMappings()
{
    return {
        {".png", FileKind::Image, "image"},    {".jpg", FileKind::Image, "image"},
        {".jpeg", FileKind::Image, "image"},   {".bmp", FileKind::Image, "image"},
        {".gif", FileKind::Image, "image"},    {".webp", FileKind::Image, "image"},
        {".mp3", FileKind::Audio, "audio"},    {".wav", FileKind::Audio, "audio"},
        {".flac", FileKind::Audio, "audio"},   {".mp4", FileKind::Video, "video"},
        {".mov", FileKind::Video, "video"},    {".mkv", FileKind::Video, "video"},
        {".avi", FileKind::Video, "video"},    {".webm", FileKind::Video, "video"},
        {".txt", FileKind::Text, "text"},      {".md", FileKind::Text, "text"},
        {".json", FileKind::Text, "text"},     {".csv", FileKind::Text, "text"},
        {".log", FileKind::Text, "text"},      {".sh", FileKind::Binary, "scripts"},
        {".py", FileKind::Binary, "scripts"},  {".js", FileKind::Binary, "scripts"},
        {".ts", FileKind::Binary, "scripts"},  {".c", FileKind::Binary, "scripts"},
        {".cpp", FileKind::Binary, "scripts"}, {".h", FileKind::Binary, "scripts"},
        {".hpp", FileKind::Binary, "scripts"}, {".pdf", FileKind::Document, "text"},
        {".zip", FileKind::Archive, "file"},   {".tar", FileKind::Archive, "file"},
        {".gz", FileKind::Archive, "file"},    {".xz", FileKind::Archive, "file"},
    };
}

}  // namespace

FileTypeRegistry::FileTypeRegistry() : _mappings(defaultMappings())
{
}

FileKind FileTypeRegistry::kindForExtension(const std::string& extension) const
{
    const std::string normalized = normalizedExtension(extension);
    for (const auto& mapping : _mappings) {
        if (mapping.extension == normalized) {
            return mapping.kind;
        }
    }
    return FileKind::Unknown;
}

std::string FileTypeRegistry::iconFor(const FileEntry& entry) const
{
    if (entry.directory) {
        return "folder";
    }

    const std::string normalized = normalizedExtension(entry.extension);
    for (const auto& mapping : _mappings) {
        if (mapping.extension == normalized) {
            return mapping.icon;
        }
    }
    return "file";
}

void FileTypeRegistry::addMapping(FileTypeMapping mapping)
{
    mapping.extension = normalizedExtension(mapping.extension);
    _mappings.push_back(std::move(mapping));
}

std::string normalizedExtension(const std::string& extension)
{
    std::string out = extension;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (!out.empty() && out.front() != '.') {
        out.insert(out.begin(), '.');
    }
    return out;
}

}  // namespace files
