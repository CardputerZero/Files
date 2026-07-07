#include "core/files_types.hpp"

namespace files {

const char* pageIdName(PageId page)
{
    switch (page) {
        case PageId::Browser:
            return "Browser";
        case PageId::Preview:
            return "Preview";
        default:
            return "Unknown";
    }
}

const char* fileKindName(FileKind kind)
{
    switch (kind) {
        case FileKind::Directory:
            return "Directory";
        case FileKind::Image:
            return "Image";
        case FileKind::Audio:
            return "Audio";
        case FileKind::Video:
            return "Video";
        case FileKind::Text:
            return "Text";
        case FileKind::Document:
            return "Document";
        case FileKind::Archive:
            return "Archive";
        case FileKind::Binary:
            return "Binary";
        default:
            return "Unknown";
    }
}

const char* fileOperationStatusName(FileOperationStatus status)
{
    switch (status) {
        case FileOperationStatus::Ok:
            return "Ok";
        case FileOperationStatus::NotFound:
            return "NotFound";
        case FileOperationStatus::NotSupported:
            return "NotSupported";
        case FileOperationStatus::PermissionDenied:
            return "PermissionDenied";
        case FileOperationStatus::InvalidSelection:
            return "InvalidSelection";
        case FileOperationStatus::Failed:
            return "Failed";
        default:
            return "Unknown";
    }
}

}  // namespace files
