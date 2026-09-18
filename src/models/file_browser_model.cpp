#include "models/file_browser_model.hpp"

#include "core/files_config.hpp"
#include "models/filesystem_transfer.hpp"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <limits>
#include <system_error>
#include <utility>

namespace files {
namespace fs = std::filesystem;

namespace {

std::string pathString(const fs::path& path)
{
    return path.lexically_normal().string();
}

bool isPreferredEntry(const FileEntry& entry, const std::string& preferredPath)
{
    if (preferredPath.empty()) {
        return false;
    }

    const fs::path entryPath = fs::path(entry.path).lexically_normal();
    const fs::path preferred = fs::path(preferredPath).lexically_normal();
    if (entryPath == preferred) {
        return true;
    }

    // Directory entries can be symlinks.  The path used while descending may
    // be canonicalized through the link, while the entry itself keeps the
    // link's spelling.  Names are unique within one directory, so a basename
    // fallback is sufficient, but only when both paths belong to that same
    // parent.  This avoids selecting an unrelated entry with the same name.
    if (entryPath.parent_path() != preferred.parent_path()) {
        return false;
    }
    const std::string preferredName = preferred.filename().string();
    return !preferredName.empty() && entry.name == preferredName;
}

int64_t modifiedUnixSec(const fs::directory_entry& entry)
{
    std::error_code ec;
    const auto file_time = entry.last_write_time(ec);
    if (ec) {
        return 0;
    }

    const auto system_time = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        file_time - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    return std::chrono::system_clock::to_time_t(system_time);
}

FileOperationResult errorResult(FileOperationStatus status, std::string message)
{
    return FileOperationResult{status, std::move(message)};
}

bool isPermissionError(const std::error_code& error)
{
    return error == std::errc::permission_denied || error == std::errc::operation_not_permitted;
}

bool pathExistsNoFollow(const fs::path& path, bool& exists, std::error_code& error)
{
    error.clear();
    const fs::file_status status = fs::symlink_status(path, error);
    if (error == std::errc::no_such_file_or_directory) {
        error.clear();
        exists = false;
        return true;
    }
    if (error) {
        exists = false;
        return false;
    }

    exists = status.type() != fs::file_type::not_found;
    return true;
}

FileOperationResult filesystemError(const char* operation, const std::error_code& error)
{
    if (isPermissionError(error)) {
        return errorResult(FileOperationStatus::PermissionDenied, "Permission denied");
    }
    return errorResult(FileOperationStatus::Failed, std::string(operation) + " failed: " + error.message());
}

FileOperationResult transferError(const char* operation, const internal::FilesystemTransferResult& transfer)
{
    if (transfer.failure == internal::FilesystemTransferFailure::DestinationExists) {
        return errorResult(FileOperationStatus::Failed, "Name already exists");
    }
    if (isPermissionError(transfer.error)) {
        return errorResult(FileOperationStatus::PermissionDenied, "Permission denied");
    }
    return errorResult(FileOperationStatus::Failed, std::string(operation) + " failed: " + transfer.detail);
}

}  // namespace

FileBrowserModel::FileBrowserModel(std::string start_directory)
    : _current_directory(normalizeDirectoryPath(start_directory.empty() ? defaultStartDirectory() : start_directory))
{
}

const FileEntry* FileBrowserModel::selectedEntry() const
{
    const auto& list = _entries.get();
    const int index  = _selected_index.get();
    if (index < 0 || static_cast<size_t>(index) >= list.size()) {
        return nullptr;
    }
    return &list[static_cast<size_t>(index)];
}

FileEntry FileBrowserModel::entryWithMetadata(const FileEntry& entry) const
{
    std::error_code ec;
    const fs::directory_entry item(entry.path, ec);
    return ec ? entry : makeEntry(item, true);
}

bool FileBrowserModel::canGoBack() const
{
    if (!_history.empty()) {
        return true;
    }

    const fs::path current = fs::path(_current_directory.get());
    const fs::path parent  = current.parent_path();
    return !parent.empty() && parent != current;
}

void FileBrowserModel::refresh(bool preserveSelected)
{
    const FileEntry* selected       = selectedEntry();
    const std::string preferredPath = preserveSelected && selected ? selected->path : "";
    (void)refreshSelecting(preferredPath);
}

FileOperationResult FileBrowserModel::readDirectoryEntries(const std::string& directory,
                                                           std::vector<FileEntry>& entries) const
{
    entries.clear();
    std::error_code ec;

    // libstdc++ may silently turn an unreadable directory into an empty
    // iterator when skip_permission_denied is requested. Probe the directory
    // itself without that option first so callers can report the real error;
    // the second iterator still tolerates an inaccessible child entry.
    {
        const fs::directory_iterator probe(directory, fs::directory_options::none, ec);
        if (ec) {
            const FileOperationResult result = filesystemError("Read", ec);
            spdlog::warn("FileBrowserModel: failed to open directory {}: {}", directory, ec.message());
            return result;
        }
        (void)probe;
    }
    ec.clear();

    for (const auto& item : fs::directory_iterator(directory, fs::directory_options::skip_permission_denied, ec)) {
        entries.push_back(makeEntry(item, false));
    }

    if (ec) {
        const FileOperationResult result = filesystemError("Read", ec);
        spdlog::warn("FileBrowserModel: failed to read {}: {}", directory, ec.message());
        std::sort(entries.begin(), entries.end(), [](const FileEntry& lhs, const FileEntry& rhs) {
            if (lhs.directory != rhs.directory) {
                return lhs.directory && !rhs.directory;
            }
            return lhs.name < rhs.name;
        });
        return result;
    }

    std::sort(entries.begin(), entries.end(), [](const FileEntry& lhs, const FileEntry& rhs) {
        if (lhs.directory != rhs.directory) {
            return lhs.directory && !rhs.directory;
        }
        return lhs.name < rhs.name;
    });
    return FileOperationResult{};
}

FileOperationResult FileBrowserModel::refreshSelecting(const std::string& preferredPath)
{
    std::vector<FileEntry> list;
    const FileOperationResult result = readDirectoryEntries(_current_directory.get(), list);
    if (!result) {
        _status.set(result.message);
        // Keep the last known listing and selection available for retry. A
        // transient permission or filesystem error should not blank the UI.
        return result;
    }

    _status.set(list.empty() ? "Empty folder" : "Ready");
    setEntries(std::move(list), preferredPath);
    return result;
}

void FileBrowserModel::selectPrevious()
{
    const auto& list = _entries.get();
    if (list.empty()) {
        _selected_index.set(-1);
        return;
    }

    const size_t last    = std::min(list.size() - 1, static_cast<size_t>(std::numeric_limits<int>::max()));
    const size_t current = _selected_index.get() < 0 ? 0 : std::min(static_cast<size_t>(_selected_index.get()), last);
    const size_t next    = current == 0 ? last : current - 1;
    _selected_index.set(static_cast<int>(next));
}

void FileBrowserModel::selectNext()
{
    const auto& list = _entries.get();
    if (list.empty()) {
        _selected_index.set(-1);
        return;
    }

    const size_t last    = std::min(list.size() - 1, static_cast<size_t>(std::numeric_limits<int>::max()));
    const size_t current = _selected_index.get() < 0 ? 0 : std::min(static_cast<size_t>(_selected_index.get()), last);
    const size_t next    = current >= last ? 0 : current + 1;
    _selected_index.set(static_cast<int>(next));
}

void FileBrowserModel::selectPageUp(int pageSize)
{
    const auto& list = _entries.get();
    if (list.empty()) {
        _selected_index.set(-1);
        return;
    }

    const size_t last    = std::min(list.size() - 1, static_cast<size_t>(std::numeric_limits<int>::max()));
    const size_t current = _selected_index.get() < 0 ? 0 : std::min(static_cast<size_t>(_selected_index.get()), last);
    const size_t step    = pageSize > 0 ? static_cast<size_t>(pageSize) : 1U;
    _selected_index.set(static_cast<int>(step >= current ? 0 : current - step));
}

void FileBrowserModel::selectPageDown(int pageSize)
{
    const auto& list = _entries.get();
    if (list.empty()) {
        _selected_index.set(-1);
        return;
    }

    const size_t last      = std::min(list.size() - 1, static_cast<size_t>(std::numeric_limits<int>::max()));
    const size_t current   = _selected_index.get() < 0 ? 0 : std::min(static_cast<size_t>(_selected_index.get()), last);
    const size_t step      = pageSize > 0 ? static_cast<size_t>(pageSize) : 1U;
    const size_t remaining = last - current;
    _selected_index.set(static_cast<int>(step >= remaining ? last : current + step));
}

FileOperationResult FileBrowserModel::openSelected(FileEntry* openedFile)
{
    const FileEntry* selected = selectedEntry();
    if (!selected) {
        return errorResult(FileOperationStatus::InvalidSelection, "No file selected");
    }

    if (selected->directory) {
        // Keep the spelling used by the directory entry in history.  A
        // symlinked directory is canonicalized for browsing, but returning
        // should still highlight that exact entry in the parent directory.
        return goToDirectory(selected->path, true, selected->path);
    }

    if (openedFile) {
        *openedFile = entryWithMetadata(*selected);
    }
    return FileOperationResult{};
}

FileOperationResult FileBrowserModel::goBack()
{
    if (!_history.empty()) {
        const HistoryEntry& previous     = _history.back();
        const FileOperationResult result = goToDirectory(previous.directory, false, previous.selectedPath);
        if (result) {
            _history.pop_back();
        }
        return result;
    }

    fs::path current(_current_directory.get());
    const fs::path parent = current.parent_path();
    if (parent.empty() || parent == current) {
        return errorResult(FileOperationStatus::NotSupported, "Already at root");
    }
    return goToDirectory(pathString(parent), false, pathString(current));
}

FileOperationResult FileBrowserModel::goToDirectory(const std::string& path, bool pushHistory)
{
    return goToDirectory(path, pushHistory, "");
}

FileOperationResult FileBrowserModel::goToDirectory(const std::string& path, bool pushHistory,
                                                    const std::string& preferredPath)
{
    fs::path requestedPath(path);
    if (requestedPath.is_relative()) {
        requestedPath = fs::path(_current_directory.get()) / requestedPath;
    }

    std::error_code ec;
    const fs::path target        = fs::weakly_canonical(requestedPath, ec);
    const std::string targetPath = pathString(ec ? requestedPath : target);
    ec.clear();
    if (!fs::is_directory(targetPath, ec)) {
        if (ec) {
            return filesystemError("Open", ec);
        }
        return errorResult(FileOperationStatus::NotFound, "Folder not found");
    }

    std::vector<FileEntry> targetEntries;
    const FileOperationResult readResult = readDirectoryEntries(targetPath, targetEntries);
    if (!readResult) {
        _status.set(readResult.message);
        return readResult;
    }

    const std::string previous = _current_directory.get();
    if (pushHistory && previous != targetPath) {
        _history.push_back({previous, pathString(requestedPath)});
    }

    _current_directory.set(targetPath);
    _status.set(targetEntries.empty() ? "Empty folder" : "Ready");
    setEntries(std::move(targetEntries), preferredPath);
    return FileOperationResult{};
}

FileOperationResult FileBrowserModel::copyEntryTo(const FileEntry& entry, const std::string& destinationDirectory)
{
    const fs::path source(entry.path);
    const std::string sourceName = entry.name;
    const internal::FilesystemTransferResult transfer =
        internal::copyPathToDirectory(source, fs::path(destinationDirectory));
    if (!transfer) {
        spdlog::warn("FileBrowserModel: copy failed source='{}' directory='{}' reason={} detail={}", source.string(),
                     destinationDirectory, internal::filesystemTransferFailureName(transfer.failure), transfer.detail);
        return transferError("Copy", transfer);
    }

    refresh(false);
    _status.set("Copied " + sourceName);
    spdlog::info("FileBrowserModel: copied source='{}' destination='{}'", source.string(),
                 transfer.destination.string());
    return FileOperationResult{};
}

FileOperationResult FileBrowserModel::copySelectedTo(const std::string& destinationDirectory)
{
    const FileEntry* selected = selectedEntry();
    if (!selected) {
        return errorResult(FileOperationStatus::InvalidSelection, "No file selected");
    }

    return copyEntryTo(*selected, destinationDirectory);
}

FileOperationResult FileBrowserModel::moveEntryTo(const FileEntry& entry, const std::string& destinationDirectory)
{
    const fs::path source(entry.path);
    const std::string sourceName = entry.name;
    const internal::FilesystemTransferResult transfer =
        internal::movePathToDirectory(source, fs::path(destinationDirectory));
    if (transfer.failure == internal::FilesystemTransferFailure::SamePath) {
        return errorResult(FileOperationStatus::InvalidSelection, "Already here");
    }
    if (!transfer) {
        spdlog::warn("FileBrowserModel: move failed source='{}' directory='{}' reason={} detail={}", source.string(),
                     destinationDirectory, internal::filesystemTransferFailureName(transfer.failure), transfer.detail);
        return transferError("Cut", transfer);
    }

    refresh(false);
    _status.set("Moved " + sourceName);
    spdlog::info("FileBrowserModel: moved source='{}' destination='{}'", source.string(),
                 transfer.destination.string());
    return FileOperationResult{};
}

FileOperationResult FileBrowserModel::renameSelectedTo(const std::string& name)
{
    const FileEntry* selected = selectedEntry();
    if (!selected) {
        return errorResult(FileOperationStatus::InvalidSelection, "No file selected");
    }
    const std::string oldName = selected->name;
    if (name.empty() || name == "." || name == ".." || name.find('/') != std::string::npos) {
        return errorResult(FileOperationStatus::InvalidSelection, "Invalid name");
    }

    std::error_code ec;
    const fs::path source(selected->path);
    const fs::path destination = source.parent_path() / name;
    if (source == destination) {
        return FileOperationResult{};
    }
    bool destinationExists = false;
    if (!pathExistsNoFollow(destination, destinationExists, ec)) {
        return filesystemError("Rename", ec);
    }
    if (destinationExists) {
        return errorResult(FileOperationStatus::Failed, "Name already exists");
    }

    fs::rename(source, destination, ec);
    if (ec) {
        return filesystemError("Rename", ec);
    }

    refreshSelecting(pathString(destination));
    _status.set("Renamed " + oldName);
    return FileOperationResult{};
}

FileOperationResult FileBrowserModel::deleteSelected()
{
    const FileEntry* selected = selectedEntry();
    if (!selected) {
        return errorResult(FileOperationStatus::InvalidSelection, "No file selected");
    }

    const std::string deletedPath = selected->path;
    const std::string deletedName = selected->name;
    std::error_code ec;
    fs::remove_all(deletedPath, ec);
    if (ec) {
        return filesystemError("Delete", ec);
    }

    refresh(false);
    _status.set("Deleted " + deletedName);
    return FileOperationResult{};
}

FileEntry FileBrowserModel::makeEntry(const fs::directory_entry& item, bool includeMetadata) const
{
    std::error_code ec;
    const fs::path fsPath       = item.path();
    const bool directory        = item.is_directory(ec);
    const std::string extension = directory ? "" : normalizedExtension(fsPath.extension().string());

    uint64_t size = 0;
    if (includeMetadata && !directory) {
        ec.clear();
        size = static_cast<uint64_t>(item.file_size(ec));
        if (ec) {
            size = 0;
        }
    }

    FileEntry entry;
    entry.path            = pathString(fsPath);
    entry.name            = fsPath.filename().string();
    entry.extension       = extension;
    entry.directory       = directory;
    entry.kind            = directory ? FileKind::Directory : _file_types.kindForExtension(extension);
    entry.icon            = _file_types.iconFor(entry);
    entry.size            = size;
    entry.modifiedUnixSec = includeMetadata ? modifiedUnixSec(item) : 0;
    entry.hidden          = !entry.name.empty() && entry.name.front() == '.';
    entry.readable        = true;
    entry.writable        = true;
    return entry;
}

void FileBrowserModel::setEntries(std::vector<FileEntry> entries, const std::string& preferredPath)
{
    int selected = entries.empty() ? -1 : 0;
    if (!preferredPath.empty()) {
        for (size_t i = 0; i < entries.size(); ++i) {
            if (isPreferredEntry(entries[i], preferredPath)) {
                selected = static_cast<int>(i);
                break;
            }
        }
    }

    _selected_index.set(selected);
    _entries.set(std::move(entries));
}

}  // namespace files
