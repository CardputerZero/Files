#include "models/file_browser_model.hpp"

#include "core/files_config.hpp"
#include "models/filesystem_transfer.hpp"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <unistd.h>
#include <pwd.h>
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

FileOperationResult disconnected()
{
    return errorResult(FileOperationStatus::NotFound, "Drive disconnected");
}

std::optional<uint64_t> freeSpace(const fs::path& path)
{
    std::error_code error;
    const auto space = fs::space(path, error);
    if (error) {
        return std::nullopt;
    }
    return space.available;
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
    if (error == std::errc::no_space_on_device) return errorResult(FileOperationStatus::Failed, "Disk is full");
    if (error == std::errc::read_only_file_system) return errorResult(FileOperationStatus::Failed, "Disk is read-only");
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
    if (transfer.error == std::errc::no_space_on_device || transfer.error == std::errc::read_only_file_system) {
        return filesystemError(operation, transfer.error);
    }
    if (transfer.failure == internal::FilesystemTransferFailure::Cancelled ||
        transfer.failure == internal::FilesystemTransferFailure::MountedSource) {
        return errorResult(FileOperationStatus::Failed, transfer.detail);
    }
    const auto reason = transfer.error ? transfer.error.message() : std::string{};
    const auto detail = transfer.detail.empty() ? reason : transfer.detail + (reason.empty() ? "" : "\n" + reason);
    return errorResult(FileOperationStatus::Failed, std::string(operation) + " failed: " + detail);
}

bool sameVolume(const internal::RemovableVolume& a, const internal::RemovableVolume& b)
{
    return a.mount.id == b.mount.id && a.mount.target == b.mount.target && a.mount.deviceNumber == b.mount.deviceNumber;
}

void numberDuplicateVolumes(std::vector<internal::RemovableVolume>& volumes,
                            const std::vector<internal::RemovableVolume>& previous)
{
    for (auto& drive : volumes) {
        const auto old = std::find_if(previous.begin(), previous.end(), [&](const auto& old) {
            return sameVolume(old, drive) && old.name == drive.name;
        });
        drive.displayNumber = old == previous.end() ? 0 : old->displayNumber;
    }
    for (auto& drive : volumes) {
        if (drive.displayNumber) continue;
        const auto matches = [&](const auto& other) { return other.name == drive.name; };
        if (std::count_if(volumes.begin(), volumes.end(), matches) <= 1) continue;
        unsigned number = 1;
        while (std::any_of(volumes.begin(), volumes.end(), [&](const auto& other) {
            return matches(other) && other.displayNumber == number;
        })) ++number;
        drive.displayNumber = number;
    }
}

}  // namespace

FileBrowserModel::FileBrowserModel(std::string start_directory, std::vector<internal::RemovableVolume> volumes)
    : _current_directory(normalizeDirectoryPath(start_directory.empty() ? defaultStartDirectory() : start_directory)),
      _volumes(std::move(volumes))
{
    if (!_volumes.empty()) {
        numberDuplicateVolumes(_volumes, {});
        showPlaces();
    }
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
    return !inPlaces();
}

void FileBrowserModel::refresh(bool preserveSelected)
{
    const auto* selected = selectedEntry();
    const std::string preferredPath = preserveSelected && selected ? selected->path : "";
    if (inPlaces()) {
        refreshPlaces(preferredPath);
    } else {
        (void)refreshSelecting(preferredPath);
    }
}

void FileBrowserModel::showPlaces(const std::string& preferredPath)
{
    _history.clear();
    _current_mount_id = 0;
    _current_directory.set("");
    _status.set("Ready");
    refreshPlaces(preferredPath);
}

const internal::RemovableVolume* FileBrowserModel::volume(uint64_t mountId) const
{
    const auto it = std::find_if(_volumes.begin(), _volumes.end(), [mountId](const auto& item) {
        return item.mount.id == mountId;
    });
    return it == _volumes.end() ? nullptr : &*it;
}

VolumeChanges FileBrowserModel::updateVolumes(std::vector<internal::RemovableVolume> volumes)
{
    const auto contains = [](const auto& list, const auto& item) {
        return std::any_of(list.begin(), list.end(), [&item](const auto& other) {
            return sameVolume(item, other);
        });
    };

    numberDuplicateVolumes(volumes, _volumes);
    VolumeChanges changes;
    for (const auto& item : volumes) {
        if (!contains(_volumes, item)) {
            changes.arrived.push_back(item);
        }
    }
    for (const auto& item : _volumes) {
        if (!contains(volumes, item)) {
            changes.removed.push_back(item);
            changes.leftVolume |= item.mount.id == _current_mount_id;
        }
    }
    _volumes = std::move(volumes);

    // Keep the closest remaining drive selected if a selected volume disappears.
    std::string neighbor;
    const auto* selection = selectedEntry();
    const auto oldId = _current_mount_id ? _current_mount_id : (selection && selection->mountRoot ? selection->mountId : 0);
    if (oldId && !volume(oldId)) {
        const auto old = std::find_if(changes.removed.begin(), changes.removed.end(), [oldId](const auto& item) {
            return item.mount.id == oldId;
        });
        const auto next = old == changes.removed.end() ? _volumes.begin() :
            std::lower_bound(_volumes.begin(), _volumes.end(), *old, [](const auto& a, const auto& b) {
                return a.name == b.name ? a.mount.target < b.mount.target : a.name < b.name;
            });
        if (!_volumes.empty()) neighbor = (next == _volumes.end() ? _volumes.back() : *next).mount.target.string();
    }
    // Navigation can reach a recently mounted drive before the next poll.
    if (changes.leftVolume || (_current_mount_id && !volume(_current_mount_id))) {
        changes.leftVolume = true;
        showPlaces(neighbor);
    } else if (inPlaces()) {
        if (neighbor.empty()) refresh();
        else refreshPlaces(neighbor);
    }
    return changes;
}

FileOperationResult FileBrowserModel::openRemovableVolumes()
{
    if (_volumes.empty()) {
        return disconnected();
    }
    if (_volumes.size() == 1) {
        return openVolume(_volumes.front().mount.id);
    }
    const auto* selected = selectedEntry();
    const auto* current = volume(_current_mount_id);
    const auto* preferred = inPlaces() && selected && selected->mountRoot ? volume(selected->mountId) : current;
    showPlaces((preferred ? preferred : &_volumes.front())->mount.target.string());
    return {};
}

FileOperationResult FileBrowserModel::openVolume(uint64_t mountId, bool rememberLocation)
{
    const auto* drive = volume(mountId);
    if (!drive || !internal::volumePresent(mountId)) {
        return disconnected();
    }
    if (rememberLocation) return goToDirectory(drive->mount.target.string(), true);
    auto result = goToDirectory(drive->mount.target.string(), false);
    if (result) {
        _history.clear();
    }
    return result;
}

std::string FileBrowserModel::volumeDisplayName(uint64_t mountId) const
{
    const auto* drive = volume(mountId);
    return drive ? drive->displayName() : std::string{};
}

void FileBrowserModel::refreshPlaces(const std::string& preferredPath)
{
    std::vector<FileEntry> list;
    const auto addLocation = [&list](const fs::path& path, std::string name) -> FileEntry& {
        FileEntry entry;
        entry.path = path.string();
        entry.name = std::move(name);
        entry.kind = FileKind::Directory;
        entry.directory = true;
        entry.icon = "folder";
        list.push_back(std::move(entry));
        return list.back();
    };
    for (const auto& volume : _volumes) {
        auto& entry = addLocation(volume.mount.target, volumeDisplayName(volume.mount.id));
        entry.mountId = volume.mount.id;
        entry.mountRoot = true;
        entry.freeBytes = freeSpace(volume.mount.target);
    }
    if (const auto* user = getpwuid(getuid()); user && user->pw_dir) {
        addLocation(user->pw_dir, "Home");
    }
    addLocation("/", "File system");
    const auto& previous = _entries.get();
    if (previous.size() != list.size() ||
        !std::equal(list.begin(), list.end(), previous.begin(), [](const auto& a, const auto& b) {
            return a.path == b.path && a.name == b.name && a.mountId == b.mountId && a.freeBytes == b.freeBytes;
        })) {
        setEntries(std::move(list), preferredPath);
    } else if (!preferredPath.empty()) {
        const auto it = std::find_if(previous.begin(), previous.end(), [&preferredPath](const auto& entry) {
            return entry.path == preferredPath;
        });
        if (it != previous.end()) {
            _selected_index.set(static_cast<int>(it - previous.begin()));
        }
    }
}

FileOperationResult FileBrowserModel::ejectSelected()
{
    const auto* entry = selectedEntry();
    if (!entry || !entry->mountRoot) {
        return errorResult(FileOperationStatus::InvalidSelection, "Select a removable drive");
    }
    const auto* drive = volume(entry->mountId);
    const auto result = drive ? ejectDrive(entry->mountId, {}, drive->displayNumber) : disconnected();
    updateVolumes(internal::removableVolumes());
    refresh(true);
    return result;
}

FileOperationResult FileBrowserModel::ejectDrive(uint64_t mountId, const internal::CommandOutput& progress,
                                                 unsigned displayNumber)
{
    for (auto& volume : internal::removableVolumes()) {
        if (volume.mount.id == mountId) {
            volume.displayNumber = displayNumber;
            return internal::ejectVolume(volume, progress);
        }
    }
    return disconnected();
}

FileOperationResult FileBrowserModel::readDirectoryEntries(const std::string& directory,
                                                           std::vector<FileEntry>& entries) const
{
    entries.clear();
    std::error_code ec;

    const auto volumes = internal::removableVolumes();
    const fs::directory_iterator end;
    fs::directory_iterator item(directory, ec);
    while (!ec && item != end) {
        auto entry = makeEntry(*item, false);
        std::error_code typeError;
        const auto type = item->status(typeError);
        entry.special = !typeError && type.type() != fs::file_type::regular &&
                        type.type() != fs::file_type::directory;
        constexpr std::string_view stagePrefix = ".files-transfer-v2-";
        if (entry.directory && entry.name.compare(0, stagePrefix.size(), stagePrefix) == 0 &&
            !fs::is_symlink(item->symlink_status(typeError))) {
            const std::string_view suffix = std::string_view(entry.name).substr(stagePrefix.size());
            size_t fields = 0;
            size_t digits = 0;
            for (char character : suffix) {
                if (character >= '0' && character <= '9') {
                    ++digits;
                } else if (character == '-' && digits) {
                    ++fields;
                    digits = 0;
                } else {
                    fields = 3;
                    break;
                }
            }
            entry.incompleteCopy = fields == 2 && digits != 0;
        }
        for (const auto& volume : volumes) {
            if (internal::volumeContains(volume, entry.path)) {
                entry.mountId = volume.mount.id;
                entry.mountRoot = entry.path == volume.mount.target;
            }
        }
        entries.push_back(std::move(entry));
        item.increment(ec);
    }
    if (ec) {
        spdlog::warn("FileBrowserModel: failed to read {}: {}", directory, ec.message());
        return filesystemError("Read", ec);
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
    if (_current_mount_id == 0) {
        for (const auto& volume : internal::removableVolumes()) {
            if (internal::volumeContains(volume, _current_directory.get())) {
                _current_mount_id = volume.mount.id;
                break;
            }
        }
    }
    if (!internal::volumePresent(_current_mount_id)) {
        showPlaces();
        _status.set("Drive disconnected");
        return disconnected();
    }
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

    if (!internal::volumePresent(selected->mountId)) {
        refresh(true);
        return disconnected();
    }
    if (selected->directory) {
        // Keep the spelling used by the directory entry in history.  A
        // symlinked directory is canonicalized for browsing, but returning
        // should still highlight that exact entry in the parent directory.
        const auto path = selected->path;
        auto result = goToDirectory(path, true, path);
        if (!result) {
            std::error_code error;
            if (fs::exists(path, error) && !fs::is_directory(path, error) && !error) {
                result.message = "Item is no longer a folder";
            }
            refreshSelecting(path);
        }
        return result;
    }

    if (openedFile) {
        *openedFile = entryWithMetadata(*selected);
        openedFile->mountId = selected->mountId;
    }
    return FileOperationResult{};
}

FileOperationResult FileBrowserModel::goBack()
{
    if (inPlaces()) {
        return errorResult(FileOperationStatus::NotSupported, "Already at Places");
    }
    if (!internal::volumePresent(_current_mount_id)) {
        showPlaces();
        return disconnected();
    }

    const fs::path current(_current_directory.get());
    const auto* drive = volume(_current_mount_id);
    if (((drive && current == drive->mount.target) || current == current.root_path()) && _history.empty()) {
        showPlaces(current.string());
        return {};
    }
    while (!_history.empty() && !internal::volumePresent(_history.back().mountId)) {
        _history.pop_back();
    }
    if (!_history.empty()) {
        const HistoryEntry previous = _history.back();
        if (previous.directory.empty()) {
            showPlaces(previous.selectedPath);
            return {};
        }
        const FileOperationResult result = goToDirectory(previous.directory, false, previous.selectedPath);
        if (result) {
            _history.pop_back();
        }
        return result;
    }
    return goToDirectory(pathString(current.parent_path()), false, pathString(current));
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
        if (ec && ec != std::errc::no_such_file_or_directory) {
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
        const auto* entry = selectedEntry();
        _history.push_back({previous, entry ? entry->path : pathString(requestedPath), _current_mount_id});
    }
    _current_mount_id = 0;
    for (const auto& volume : internal::removableVolumes()) {
        if (internal::volumeContains(volume, targetPath)) {
            _current_mount_id = volume.mount.id;
            break;
        }
    }

    _current_directory.set(targetPath);
    _status.set(targetEntries.empty() ? "Empty folder" : "Ready");
    setEntries(std::move(targetEntries), preferredPath);
    return FileOperationResult{};
}

FileOperationResult FileBrowserModel::copyEntryTo(const FileEntry& entry, const std::string& destinationDirectory)
{
    return transferEntry(entry, destinationDirectory, false);
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
    return transferEntry(entry, destinationDirectory, true);
}

FileOperationResult FileBrowserModel::transferEntry(const FileEntry& entry, const std::string& destinationDirectory,
                                                    bool move)
{
    const auto destinationMountId = destinationDirectory == _current_directory.get() ? _current_mount_id : 0;
    const auto transfer = performTransfer(entry, destinationDirectory, destinationMountId, move);
    if (transfer.operation) {
        refreshSelecting(transfer.destinationPath);
        _status.set(transfer.operation.message);
    }
    return transfer.operation;
}

FileTransferResult FileBrowserModel::performTransfer(const FileEntry& entry, const std::string& destinationDirectory,
                                                     uint64_t destinationMountId, bool move,
                                                     internal::TransferControl* control, std::string_view duplicateWord)
{
    if (entry.mountRoot || destinationDirectory.empty()) {
        return {errorResult(FileOperationStatus::InvalidSelection, "Open a folder first")};
    }
    const fs::path source(entry.path);
    if (!destinationMountId) {
        for (const auto& volume : internal::removableVolumes()) {
            if (internal::volumeContains(volume, destinationDirectory)) {
                destinationMountId = volume.mount.id;
                break;
            }
        }
    }
    const auto sourceParent = source.parent_path().empty() ? fs::path(".") : source.parent_path();
    const internal::TransferDirectory sourceDirectory(sourceParent, entry.mountId);
    const internal::TransferDirectory destination(destinationDirectory, destinationMountId);
    FileTransferResult result;
    const auto connected = [&] {
        if (!sourceDirectory.connected()) {
            result.disconnectedSource = entry.mountId;
        }
        if (!destination.connected()) {
            result.disconnectedDestination = destinationMountId;
        }
        return !result.disconnectedSource && !result.disconnectedDestination;
    };
    if (!connected()) {
        result.operation = disconnected();
        return result;
    }
    const auto pinnedSource = sourceDirectory.path() / source.filename();
    const auto transfer = move ? internal::movePathToDirectory(pinnedSource, destination.path(), connected, control)
                               : internal::copyPathToDirectory(pinnedSource, destination.path(), connected, control,
                                                               duplicateWord);
    result.destinationState = transfer.destinationState;
    if (!transfer) {
        if (!transfer.destination.empty()) {
            result.destinationPath = pathString(fs::path(destinationDirectory) / transfer.destination.filename());
        }
        std::error_code sourceError;
        result.sourceStillPresent = sourceDirectory.connected() &&
            fs::exists(fs::symlink_status(pinnedSource, sourceError));
    }
    if (!transfer && !connected()) {
        result.operation = disconnected();
        result.destinationState = transfer.failure == internal::FilesystemTransferFailure::SourceCleanupFailed
                                      ? internal::TransferDestination::Published : transfer.destinationState;
    } else if (transfer.failure == internal::FilesystemTransferFailure::SamePath) {
        result.operation = errorResult(FileOperationStatus::InvalidSelection, "Already here");
    } else if (!transfer) {
        result.operation = transferError(move ? "Cut" : "Copy", transfer);
    } else {
        const auto name = transfer.destination.filename();
        result.destinationPath = pathString(fs::path(destinationDirectory) / name);
        const auto* verb = move ? "Moved " : name != source.filename() ? "Created copy " : "Copied ";
        result.operation = {FileOperationStatus::Ok, std::string(verb) + name.string()};
        result.destinationState = internal::TransferDestination::Published;
    }
    return result;
}

FileOperationResult FileBrowserModel::createDirectory(const std::string& name)
{
    if (inPlaces()) return errorResult(FileOperationStatus::InvalidSelection, "Open a folder first");
    if (name.empty() || name == "." || name == ".." || name.find('/') != std::string::npos ||
        name.find('\0') != std::string::npos) {
        return errorResult(FileOperationStatus::InvalidSelection, "Invalid name");
    }
    const internal::TransferDirectory directory(_current_directory.get(), _current_mount_id);
    if (!directory.connected()) return disconnected();
    std::error_code error;
    if (!fs::create_directory(directory.path() / name, error)) {
        if (!error || error == std::errc::file_exists) {
            return errorResult(FileOperationStatus::Failed, "Name already exists");
        }
        return filesystemError("Create folder", error);
    }
    refreshSelecting(pathString(fs::path(_current_directory.get()) / name));
    _status.set("Created " + name);
    return {};
}

FileOperationResult FileBrowserModel::renameSelectedTo(const std::string& name)
{
    const FileEntry* selected = selectedEntry();
    if (!selected) {
        return errorResult(FileOperationStatus::InvalidSelection, "No file selected");
    }
    if (inPlaces() || selected->mountRoot) {
        return errorResult(FileOperationStatus::NotSupported, "Cannot rename a location");
    }
    if (!internal::volumePresent(selected->mountId)) {
        return disconnected();
    }
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

    const internal::TransferDirectory directory(source.parent_path(), selected->mountId);
    if (!directory.connected()) {
        return disconnected();
    }
    fs::rename(directory.path() / source.filename(), directory.path() / destination.filename(), ec);
    if (ec) {
        return filesystemError("Rename", ec);
    }

    refreshSelecting(pathString(destination));
    _status.set("Renamed to " + name);
    return FileOperationResult{};
}

FileOperationResult FileBrowserModel::deleteSelected()
{
    const FileEntry* selected = selectedEntry();
    if (!selected) {
        return errorResult(FileOperationStatus::InvalidSelection, "No file selected");
    }

    if (inPlaces() || selected->mountRoot) {
        return errorResult(FileOperationStatus::NotSupported, "Cannot delete a location");
    }
    if (!internal::volumePresent(selected->mountId)) {
        return disconnected();
    }
    const fs::path deletedPath = selected->path;
    const std::string deletedName = selected->name;
    const internal::TransferDirectory directory(deletedPath.parent_path(), selected->mountId);
    if (!directory.connected()) {
        return disconnected();
    }
    std::error_code ec;
    fs::path mounted;
    if (internal::containsMountPoint(directory.path() / deletedPath.filename(), ec, &mounted)) {
        return errorResult(FileOperationStatus::NotSupported,
                           "Folder contains a mounted filesystem\n" + mounted.string());
    }
    if (ec) {
        return filesystemError("Cannot check for mounted filesystems", ec);
    }
    fs::remove_all(directory.path() / deletedPath.filename(), ec);
    if (!directory.connected()) {
        return errorResult(FileOperationStatus::Failed, "Drive disconnected during deletion");
    }
    if (ec) {
        return filesystemError("Delete", ec);
    }

    const auto& before = _entries.get();
    const size_t index = static_cast<size_t>(_selected_index.get());
    std::string neighbor;
    if (index + 1 < before.size()) neighbor = before[index + 1].path;
    else if (index > 0) neighbor = before[index - 1].path;
    refreshSelecting(neighbor);
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
    if (!directory && item.is_regular_file(ec)) {
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
