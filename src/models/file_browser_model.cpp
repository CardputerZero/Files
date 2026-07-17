#include "models/file_browser_model.hpp"

#include "core/files_config.hpp"
#include "models/filesystem_transfer.hpp"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <system_error>
#include <utility>

namespace files {
namespace fs = std::filesystem;

namespace {

std::string pathString(const fs::path& path)
{
    return path.lexically_normal().string();
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

void FileBrowserModel::refresh(bool preserveSelected)
{
    const FileEntry* selected       = selectedEntry();
    const std::string preferredPath = preserveSelected && selected ? selected->path : "";
    refreshSelecting(preferredPath);
}

void FileBrowserModel::refreshSelecting(const std::string& preferredPath)
{
    std::vector<FileEntry> list;
    std::error_code ec;

    for (const auto& item :
         fs::directory_iterator(_current_directory.get(), fs::directory_options::skip_permission_denied, ec)) {
        list.push_back(makeEntry(item, false));
    }

    if (ec) {
        _status.set("Read failed: " + ec.message());
        spdlog::warn("FileBrowserModel: failed to read {}: {}", _current_directory.get(), ec.message());
    } else {
        _status.set(list.empty() ? "Empty folder" : "Ready");
    }

    std::sort(list.begin(), list.end(), [](const FileEntry& lhs, const FileEntry& rhs) {
        if (lhs.directory != rhs.directory) {
            return lhs.directory && !rhs.directory;
        }
        return lhs.name < rhs.name;
    });
    setEntries(std::move(list), preferredPath);
}

void FileBrowserModel::selectPrevious()
{
    const auto& list = _entries.get();
    if (list.empty()) {
        _selected_index.set(-1);
        return;
    }

    int next = _selected_index.get() - 1;
    if (next < 0) {
        next = static_cast<int>(list.size()) - 1;
    }
    _selected_index.set(next);
}

void FileBrowserModel::selectNext()
{
    const auto& list = _entries.get();
    if (list.empty()) {
        _selected_index.set(-1);
        return;
    }

    int next = _selected_index.get() + 1;
    if (next >= static_cast<int>(list.size())) {
        next = 0;
    }
    _selected_index.set(next);
}

FileOperationResult FileBrowserModel::openSelected(FileEntry* openedFile)
{
    const FileEntry* selected = selectedEntry();
    if (!selected) {
        return errorResult(FileOperationStatus::InvalidSelection, "No file selected");
    }

    if (selected->directory) {
        return goToDirectory(selected->path, true);
    }

    if (openedFile) {
        *openedFile = entryWithMetadata(*selected);
    }
    return FileOperationResult{};
}

FileOperationResult FileBrowserModel::goBack()
{
    if (!_history.empty()) {
        const HistoryEntry previous = std::move(_history.back());
        _history.pop_back();
        return goToDirectory(previous.directory, false, previous.selectedPath);
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
    std::error_code ec;
    const fs::path target        = fs::weakly_canonical(path, ec);
    const std::string targetPath = pathString(ec ? fs::path(path) : target);
    if (!fs::is_directory(targetPath, ec)) {
        return errorResult(FileOperationStatus::NotFound, "Folder not found");
    }

    const std::string previous = _current_directory.get();
    if (pushHistory && previous != targetPath) {
        _history.push_back({previous, targetPath});
    }

    _current_directory.set(targetPath);
    refreshSelecting(preferredPath);
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
        return errorResult(FileOperationStatus::Failed, "Copy failed: " + transfer.detail);
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
        return errorResult(FileOperationStatus::Failed, "Cut failed: " + transfer.detail);
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
    if (fs::exists(destination, ec)) {
        return errorResult(FileOperationStatus::Failed, "Name already exists");
    }

    fs::rename(source, destination, ec);
    if (ec) {
        return errorResult(FileOperationStatus::Failed, "Rename failed: " + ec.message());
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
        return errorResult(FileOperationStatus::Failed, "Delete failed: " + ec.message());
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
            if (entries[i].path == preferredPath) {
                selected = static_cast<int>(i);
                break;
            }
        }
    }

    _selected_index.set(selected);
    _entries.set(std::move(entries));
}

}  // namespace files
