#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <functional>
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
    DriveDisconnected,
    MountedSource,
    Cancelled,
    SyncFailed,
};

enum class TransferDestination { NotPublished, PartialRemoved, CleanupFailed, CleanupUnverified, Published };

struct FilesystemTransferResult {
    FilesystemTransferFailure failure = FilesystemTransferFailure::None;
    std::filesystem::path destination;
    std::error_code error;
    std::string detail;
    TransferDestination destinationState = TransferDestination::NotPublished;

    explicit operator bool() const
    {
        return failure == FilesystemTransferFailure::None;
    }
};

class TransferControl {
public:
    bool cancel()
    {
        State expected = State::Copying;
        return _state.compare_exchange_strong(expected, State::Cancelled);
    }
    bool cancelled() const { return _state.load() == State::Cancelled; }
    bool beginCommit()
    {
        State expected = State::Copying;
        return _state.compare_exchange_strong(expected, State::Committing);
    }
    void retry() { _state.store(State::Copying); }
    void addBytes(uint64_t bytes) { _bytes.fetch_add(bytes, std::memory_order_relaxed); }
    uint64_t bytes() const { return _bytes.load(std::memory_order_relaxed); }

private:
    enum class State { Copying, Cancelled, Committing };
    std::atomic<State> _state{State::Copying};
    std::atomic<uint64_t> _bytes{0};
};

// Copies into destinationDirectory without replacing or merging an existing entry.
// Copying into the source directory chooses " copy", " copy 2", ... automatically.
FilesystemTransferResult copyPathToDirectory(const std::filesystem::path& source,
                                             const std::filesystem::path& destinationDirectory,
                                             const std::function<bool()>& connected = {},
                                             TransferControl* control = nullptr, std::string_view duplicateWord = "copy");

// Moves into destinationDirectory without replacing an existing entry. Cross-device
// moves sync the staged copy and its final parent before removing the source.
FilesystemTransferResult movePathToDirectory(const std::filesystem::path& source,
                                             const std::filesystem::path& destinationDirectory,
                                             const std::function<bool()>& connected = {},
                                             TransferControl* control = nullptr);

const char* filesystemTransferFailureName(FilesystemTransferFailure failure);

}  // namespace files::internal
