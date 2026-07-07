#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace files {

namespace files_key {

constexpr uint32_t Up    = 0x10001;
constexpr uint32_t Down  = 0x10002;
constexpr uint32_t Left  = 0x10003;
constexpr uint32_t Right = 0x10004;

}  // namespace files_key

enum class PageId {
    Browser = 0,
    Preview,
};

enum class FileKind {
    Directory,
    Image,
    Audio,
    Video,
    Text,
    Document,
    Archive,
    Binary,
    Unknown,
};

enum class FileOperationStatus {
    Ok,
    NotFound,
    NotSupported,
    PermissionDenied,
    InvalidSelection,
    Failed,
};

struct FileEntry {
    std::string path;
    std::string name;
    std::string extension;
    std::string icon;
    FileKind kind           = FileKind::Unknown;
    uint64_t size           = 0;
    int64_t modifiedUnixSec = 0;
    bool directory          = false;
    bool hidden             = false;
    bool readable           = true;
    bool writable           = true;
};

struct FileOperationResult {
    FileOperationStatus status = FileOperationStatus::Ok;
    std::string message;

    explicit operator bool() const
    {
        return status == FileOperationStatus::Ok;
    }
};

struct PendingDeleteFile {
    bool active = false;
    FileEntry file;
};

struct PendingCopyFile {
    bool active = false;
    FileEntry file;
    bool cut = false;
};

struct PendingRenameFile {
    bool active = false;
    FileEntry file;
    std::string name;
};

const char* pageIdName(PageId page);
const char* fileKindName(FileKind kind);
const char* fileOperationStatusName(FileOperationStatus status);

}  // namespace files
