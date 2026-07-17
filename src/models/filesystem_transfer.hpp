#pragma once

#include <filesystem>
#include <string>
#include <system_error>

namespace files::internal {

enum class FilesystemTransferFailure {
    None,
    SourceNotFound,
    UnsupportedSourceType,
    InvalidDestination,
    SamePath,
    DestinationExists,
    DestinationInsideSource,
    StageCreationFailed,
    CopyFailed,
    ValidationFailed,
    PublishFailed,
    SourceCleanupFailed,
};

struct FilesystemTransferResult {
    FilesystemTransferFailure failure = FilesystemTransferFailure::None;
    std::filesystem::path destination;
    std::error_code error;
    std::string detail;

    explicit operator bool() const
    {
        return failure == FilesystemTransferFailure::None;
    }
};

// Copies into destinationDirectory without replacing or merging an existing entry.
// Copying into the source directory chooses " copy", " copy 2", ... automatically.
FilesystemTransferResult copyPathToDirectory(const std::filesystem::path& source,
                                             const std::filesystem::path& destinationDirectory);

// Moves into destinationDirectory without replacing an existing entry. Cross-device
// moves use a validated staged copy and remove the source only after publication.
FilesystemTransferResult movePathToDirectory(const std::filesystem::path& source,
                                             const std::filesystem::path& destinationDirectory);

const char* filesystemTransferFailureName(FilesystemTransferFailure failure);

}  // namespace files::internal
