#include "models/filesystem_transfer.hpp"
#include "models/mount_info.hpp"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <dirent.h>
#include <fcntl.h>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <linux/fs.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace files::internal {
namespace fs = std::filesystem;

namespace {

constexpr int kMaxStageCreationAttempts = 128;
constexpr int kMaxCopyNameAttempts      = 100000;

std::atomic<uint64_t> g_stage_serial{0};
std::atomic<uint64_t> g_hold_serial{0};

struct StructureValidationResult {
    bool valid = false;
    std::error_code error;
    std::string detail;
    uint64_t entries = 0;
};

struct SourceNodeSnapshot {
    fs::path relativePath;
    uint64_t device      = 0;
    uint64_t inode       = 0;
    uint64_t size        = 0;
    uint32_t fileType    = 0;
    int64_t modifiedSec  = 0;
    int64_t modifiedNsec = 0;
    int64_t changedSec   = 0;
    int64_t changedNsec  = 0;
};

struct SourceSnapshot {
    std::vector<SourceNodeSnapshot> nodes;
};

FilesystemTransferResult failureResult(FilesystemTransferFailure failure, const fs::path& destination,
                                       std::error_code error, std::string detail)
{
    spdlog::error("FilesystemTransfer: {} destination='{}' error={} ({}) detail={}",
                  filesystemTransferFailureName(failure), destination.string(), error.value(), error.message(), detail);
    return FilesystemTransferResult{failure, destination, error, std::move(detail)};
}

class FileDescriptor {
public:
    explicit FileDescriptor(int fd) : fd(fd) {}
    ~FileDescriptor() { if (fd >= 0) ::close(fd); }
    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;

    int fd;
};

struct DirectoryCloser {
    void operator()(DIR* directory) const { ::closedir(directory); }
};

std::error_code systemError()
{
    return {errno, std::generic_category()};
}

struct SourceFilesystem {
    dev_t device = 0;
    uint64_t mount = 0;
};

bool identifyFilesystem(int parent, const char* name, int flags, dev_t device, SourceFilesystem& identity)
{
    identity.device = device;
#if defined(__linux__)
    struct statx info{};
    if (::statx(parent, name, flags, STATX_MNT_ID, &info) != 0) {
        return false;
    }
    if (!(info.stx_mask & STATX_MNT_ID)) {
        errno = ENOTSUP;
        return false;
    }
    identity.mount = info.stx_mnt_id;
#else
    (void)parent;
    (void)name;
    (void)flags;
#endif
    return true;
}

std::error_code checkFilesystem(int parent, const char* name, int flags, const struct stat& info,
                                const SourceFilesystem* expected)
{
    if (!expected) {
        return {};
    }
    SourceFilesystem actual;
    if (!identifyFilesystem(parent, name, flags, info.st_dev, actual)) {
        return systemError();
    }
    if (actual.device != expected->device || actual.mount != expected->mount) {
        return std::make_error_code(std::errc::cross_device_link);
    }
    return {};
}

std::error_code checkOpenedSource(int fd, const struct stat& expected, const SourceFilesystem* filesystem)
{
    struct stat info{};
    if (::fstat(fd, &info) != 0) {
        return systemError();
    }
    if (info.st_dev != expected.st_dev || info.st_ino != expected.st_ino ||
        (info.st_mode & S_IFMT) != (expected.st_mode & S_IFMT)) {
        return std::make_error_code(std::errc::resource_unavailable_try_again);
    }
#if defined(__linux__)
    return checkFilesystem(fd, "", AT_EMPTY_PATH, info, filesystem);
#else
    return checkFilesystem(fd, "", 0, info, filesystem);
#endif
}

std::error_code syncDescriptor(int fd)
{
    while (::fsync(fd) != 0) {
        if (errno != EINTR) {
            return systemError();
        }
    }
    return {};
}

std::error_code copyRegularFile(int parent, const char* name, const fs::path& destination,
                                const struct stat& info, const SourceFilesystem* filesystem,
                                TransferControl* control, bool& syncFailure)
{
    const FileDescriptor source(::openat(parent, name, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
    if (source.fd < 0) {
        return systemError();
    }
    if (auto error = checkOpenedSource(source.fd, info, filesystem)) {
        return error;
    }
    FileDescriptor target(::open(destination.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600));
    if (target.fd < 0) {
        return systemError();
    }

    char buffer[128 * 1024];
    for (;;) {
        if (control && control->cancelled()) {
            return std::make_error_code(std::errc::operation_canceled);
        }
        const ssize_t count = ::read(source.fd, buffer, sizeof(buffer));
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            return systemError();
        }
        if (count == 0) {
            break;
        }
        for (ssize_t written = 0; written < count;) {
            const ssize_t part = ::write(target.fd, buffer + written, static_cast<size_t>(count - written));
            if (part < 0 && errno == EINTR) {
                continue;
            }
            if (part <= 0) {
                return part < 0 ? systemError() : std::make_error_code(std::errc::io_error);
            }
            written += part;
            if (control) {
                control->addBytes(static_cast<uint64_t>(part));
            }
        }
    }
    if (::fchmod(target.fd, info.st_mode & 0777) != 0) {
        return systemError();
    }
    if (filesystem) {
        if (auto error = syncDescriptor(target.fd)) {
            syncFailure = true;
            return error;
        }
    }
    const int fd = std::exchange(target.fd, -1);
    return ::close(fd) == 0 ? std::error_code{} : systemError();
}

std::error_code copyNode(int parent, const char* name, const fs::path& destination,
                         const SourceFilesystem* filesystem, TransferControl* control, bool& syncFailure)
{
    if (control && control->cancelled()) {
        return std::make_error_code(std::errc::operation_canceled);
    }
    struct stat info{};
    if (::fstatat(parent, name, &info, AT_SYMLINK_NOFOLLOW) != 0) {
        return systemError();
    }
    if (auto error = checkFilesystem(parent, name, AT_SYMLINK_NOFOLLOW, info, filesystem)) {
        return error;
    }
    if (S_ISREG(info.st_mode)) {
        return copyRegularFile(parent, name, destination, info, filesystem, control, syncFailure);
    }
    if (S_ISLNK(info.st_mode)) {
        std::string target(256, '\0');
        for (;;) {
            const auto length = ::readlinkat(parent, name, target.data(), target.size());
            if (length < 0) {
                return systemError();
            }
            if (static_cast<size_t>(length) < target.size()) {
                target.resize(static_cast<size_t>(length));
                return ::symlink(target.c_str(), destination.c_str()) == 0 ? std::error_code{} : systemError();
            }
            target.resize(target.size() * 2);
        }
    }
    if (!S_ISDIR(info.st_mode)) {
        return std::make_error_code(std::errc::operation_not_supported);
    }

    FileDescriptor source(::openat(parent, name, O_RDONLY | O_NOFOLLOW | O_DIRECTORY | O_CLOEXEC));
    if (source.fd < 0) {
        return systemError();
    }
    if (auto error = checkOpenedSource(source.fd, info, filesystem)) {
        return error;
    }
    if (::mkdir(destination.c_str(), 0700) != 0) {
        return systemError();
    }
    std::unique_ptr<DIR, DirectoryCloser> directory(::fdopendir(source.fd));
    if (!directory) {
        return systemError();
    }
    const int sourceFd = std::exchange(source.fd, -1);
    for (;;) {
        errno = 0;
        const auto* entry = ::readdir(directory.get());
        if (!entry) {
            if (errno) {
                return systemError();
            }
            break;
        }
        const std::string child = entry->d_name;
        if (child != "." && child != "..") {
            if (auto error = copyNode(sourceFd, child.c_str(), destination / child, filesystem, control, syncFailure)) {
                return error;
            }
        }
    }

    const FileDescriptor target(::open(destination.c_str(), O_RDONLY | O_NOFOLLOW | O_DIRECTORY | O_CLOEXEC));
    if (target.fd < 0 || ::fchmod(target.fd, info.st_mode & 0777) != 0) {
        return systemError();
    }
    if (filesystem) {
        if (auto error = syncDescriptor(target.fd)) {
            syncFailure = true;
            return error;
        }
    }
    return {};
}

bool isMissingError(const std::error_code& error)
{
    return error == std::errc::no_such_file_or_directory;
}

bool pathExistsNoFollow(const fs::path& path, bool& exists, std::error_code& error)
{
    error.clear();
    const fs::file_status status = fs::symlink_status(path, error);
    if (isMissingError(error)) {
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

bool supportedSourceType(fs::file_type type)
{
    return type == fs::file_type::regular || type == fs::file_type::directory || type == fs::file_type::symlink;
}

SourceNodeSnapshot snapshotFromStat(const struct stat& info, const fs::path& relativePath)
{
    SourceNodeSnapshot node;
    node.relativePath = relativePath;
    node.device       = static_cast<uint64_t>(info.st_dev);
    node.inode        = static_cast<uint64_t>(info.st_ino);
    node.fileType     = static_cast<uint32_t>(info.st_mode & S_IFMT);
    node.size         = S_ISREG(info.st_mode) || S_ISLNK(info.st_mode) ? static_cast<uint64_t>(info.st_size) : 0;
#if defined(__APPLE__)
    node.modifiedSec  = static_cast<int64_t>(info.st_mtimespec.tv_sec);
    node.modifiedNsec = static_cast<int64_t>(info.st_mtimespec.tv_nsec);
    node.changedSec   = static_cast<int64_t>(info.st_ctimespec.tv_sec);
    node.changedNsec  = static_cast<int64_t>(info.st_ctimespec.tv_nsec);
#else
    node.modifiedSec  = static_cast<int64_t>(info.st_mtim.tv_sec);
    node.modifiedNsec = static_cast<int64_t>(info.st_mtim.tv_nsec);
    node.changedSec   = static_cast<int64_t>(info.st_ctim.tv_sec);
    node.changedNsec  = static_cast<int64_t>(info.st_ctim.tv_nsec);
#endif
    return node;
}

bool snapshotNode(const fs::path& path, const fs::path& relativePath, SourceNodeSnapshot& node, std::error_code& error,
                  std::string& detail)
{
    struct stat info {};
    if (::lstat(path.c_str(), &info) != 0) {
        error  = std::error_code(errno, std::generic_category());
        detail = "cannot inspect source node '" + path.string() + "': " + error.message();
        return false;
    }

    node = snapshotFromStat(info, relativePath);
    error.clear();
    detail.clear();
    return true;
}

bool sameSnapshotNode(const SourceNodeSnapshot& expected, const SourceNodeSnapshot& actual, bool ignoreChangedTime)
{
    return expected.relativePath == actual.relativePath && expected.device == actual.device &&
           expected.inode == actual.inode && expected.size == actual.size && expected.fileType == actual.fileType &&
           expected.modifiedSec == actual.modifiedSec && expected.modifiedNsec == actual.modifiedNsec &&
           (ignoreChangedTime ||
            (expected.changedSec == actual.changedSec && expected.changedNsec == actual.changedNsec));
}

std::string snapshotPathLabel(const fs::path& relativePath)
{
    return relativePath.empty() ? std::string(".") : relativePath.string();
}

bool captureSourceSnapshot(const fs::path& source, SourceSnapshot& snapshot, std::error_code& error,
                           std::string& detail, const SourceFilesystem& filesystem, TransferControl* control = nullptr)
{
    snapshot.nodes.clear();

    SourceNodeSnapshot rootBefore;
    if (!snapshotNode(source, {}, rootBefore, error, detail)) {
        return false;
    }
    struct stat rootInfo{};
    if (::lstat(source.c_str(), &rootInfo) != 0) {
        error = systemError();
        return false;
    }
    error = checkFilesystem(AT_FDCWD, source.c_str(), AT_SYMLINK_NOFOLLOW, rootInfo, &filesystem);
    if (error) {
        detail = "source is on a mounted filesystem: " + source.string();
        return false;
    }
    snapshot.nodes.push_back(rootBefore);

    if (rootBefore.fileType == static_cast<uint32_t>(S_IFDIR)) {
        fs::recursive_directory_iterator iterator(source, fs::directory_options::none, error);
        const fs::recursive_directory_iterator end;
        while (!error && iterator != end) {
            if (control && control->cancelled()) {
                error = std::make_error_code(std::errc::operation_canceled);
                return false;
            }
            struct stat info{};
            if (::lstat(iterator->path().c_str(), &info) != 0) {
                error = systemError();
                return false;
            }
            error = checkFilesystem(AT_FDCWD, iterator->path().c_str(), AT_SYMLINK_NOFOLLOW, info, &filesystem);
            if (error) {
                detail = "folder contains a mounted filesystem: " + iterator->path().string();
                return false;
            }
            SourceNodeSnapshot node;
            const fs::path relativePath = iterator->path().lexically_relative(source);
            if (!snapshotNode(iterator->path(), relativePath, node, error, detail)) {
                return false;
            }
            snapshot.nodes.push_back(std::move(node));
            iterator.increment(error);
        }
        if (error) {
            detail = "cannot enumerate source tree '" + source.string() + "': " + error.message();
            return false;
        }
    }

    SourceNodeSnapshot rootAfter;
    if (!snapshotNode(source, {}, rootAfter, error, detail)) {
        return false;
    }
    if (!sameSnapshotNode(rootBefore, rootAfter, false)) {
        error  = std::make_error_code(std::errc::resource_unavailable_try_again);
        detail = "source root changed while its snapshot was captured: " + source.string();
        return false;
    }

    std::sort(snapshot.nodes.begin(), snapshot.nodes.end(),
              [](const auto& left, const auto& right) { return left.relativePath < right.relativePath; });
    return true;
}

bool compareSourceSnapshots(const SourceSnapshot& expected, const SourceSnapshot& actual, bool ignoreRootChangedTime,
                            std::string& detail)
{
    if (expected.nodes.size() != actual.nodes.size()) {
        detail = "source entry count changed from " + std::to_string(expected.nodes.size()) + " to " +
                 std::to_string(actual.nodes.size());
        return false;
    }

    for (size_t index = 0; index < expected.nodes.size(); ++index) {
        const SourceNodeSnapshot& expectedNode = expected.nodes[index];
        const SourceNodeSnapshot& actualNode   = actual.nodes[index];
        const bool root                        = expectedNode.relativePath.empty();
        if (!sameSnapshotNode(expectedNode, actualNode, root && ignoreRootChangedTime)) {
            detail = "source node changed at '" + snapshotPathLabel(expectedNode.relativePath) + "'";
            return false;
        }
    }
    detail.clear();
    return true;
}

const SourceNodeSnapshot* snapshotRoot(const SourceSnapshot& snapshot)
{
    const auto root = std::find_if(snapshot.nodes.begin(), snapshot.nodes.end(),
                                   [](const SourceNodeSnapshot& node) { return node.relativePath.empty(); });
    return root == snapshot.nodes.end() ? nullptr : &*root;
}

bool sameRootIdentity(const SourceSnapshot& expected, const SourceSnapshot& actual)
{
    const SourceNodeSnapshot* expectedRoot = snapshotRoot(expected);
    const SourceNodeSnapshot* actualRoot   = snapshotRoot(actual);
    return expectedRoot && actualRoot && expectedRoot->device == actualRoot->device &&
           expectedRoot->inode == actualRoot->inode && expectedRoot->fileType == actualRoot->fileType;
}

std::error_code removeSourceNode(int parent, const char* name, const SourceFilesystem& filesystem,
                                 const SourceSnapshot& expected, const fs::path& relative)
{
    struct stat info{};
    if (::fstatat(parent, name, &info, AT_SYMLINK_NOFOLLOW) != 0) {
        return systemError();
    }
    if (auto error = checkFilesystem(parent, name, AT_SYMLINK_NOFOLLOW, info, &filesystem)) {
        return error;
    }
    const auto node = std::lower_bound(expected.nodes.begin(), expected.nodes.end(), relative,
                                       [](const auto& item, const auto& path) { return item.relativePath < path; });
    // Removing an earlier hard link can change ctime on this inode.
    if (node == expected.nodes.end() || !sameSnapshotNode(*node, snapshotFromStat(info, relative), true)) {
        return std::make_error_code(std::errc::resource_unavailable_try_again);
    }
    if (!S_ISDIR(info.st_mode)) {
        return ::unlinkat(parent, name, 0) == 0 ? std::error_code{} : systemError();
    }

    FileDescriptor fd(::openat(parent, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if (fd.fd < 0) {
        return systemError();
    }
    if (auto error = checkOpenedSource(fd.fd, info, &filesystem)) {
        return error;
    }
    std::unique_ptr<DIR, DirectoryCloser> directory(::fdopendir(fd.fd));
    if (!directory) {
        return systemError();
    }
    const int directoryFd = std::exchange(fd.fd, -1);
    for (;;) {
        errno = 0;
        const auto* entry = ::readdir(directory.get());
        if (!entry) {
            if (errno) {
                return systemError();
            }
            break;
        }
        const std::string child = entry->d_name;
        if (child != "." && child != "..") {
            if (auto error = removeSourceNode(directoryFd, child.c_str(), filesystem, expected, relative / child)) {
                return error;
            }
        }
    }
    return ::unlinkat(parent, name, AT_REMOVEDIR) == 0 ? std::error_code{} : systemError();
}

bool sameOrDescendant(const fs::path& candidate, const fs::path& directory)
{
    auto candidatePart = candidate.begin();
    auto directoryPart = directory.begin();
    for (; directoryPart != directory.end(); ++directoryPart, ++candidatePart) {
        if (candidatePart == candidate.end() || *candidatePart != *directoryPart) {
            return false;
        }
    }
    return true;
}

FilesystemTransferResult validateEndpoints(const fs::path& source, const fs::path& destination,
                                           fs::file_status& sourceStatus)
{
    std::error_code error;
    sourceStatus = fs::symlink_status(source, error);
    if (isMissingError(error) || sourceStatus.type() == fs::file_type::not_found) {
        return failureResult(FilesystemTransferFailure::SourceNotFound, destination,
                             std::make_error_code(std::errc::no_such_file_or_directory),
                             "source does not exist: " + source.string());
    }
    if (error) {
        return failureResult(FilesystemTransferFailure::SourceNotFound, destination, error,
                             "cannot inspect source: " + source.string());
    }
    if (!supportedSourceType(sourceStatus.type())) {
        return failureResult(FilesystemTransferFailure::UnsupportedSourceType, destination,
                             std::make_error_code(std::errc::operation_not_supported),
                             "source is not a regular file, directory, or symbolic link");
    }

    const fs::path destinationParent = destination.parent_path();
    if (destinationParent.empty() || !fs::is_directory(destinationParent, error)) {
        const std::error_code resultError = error ? error : std::make_error_code(std::errc::not_a_directory);
        return failureResult(FilesystemTransferFailure::InvalidDestination, destination, resultError,
                             "destination parent is not an accessible directory");
    }

    const fs::path absoluteSource     = fs::absolute(source, error).lexically_normal();
    const std::error_code sourceError = error;
    error.clear();
    const fs::path absoluteDestination = fs::absolute(destination, error).lexically_normal();
    if (!sourceError && !error && absoluteSource == absoluteDestination) {
        return failureResult(FilesystemTransferFailure::SamePath, destination,
                             std::make_error_code(std::errc::invalid_argument),
                             "source and destination are the same path");
    }

    bool destinationExists = false;
    if (!pathExistsNoFollow(destination, destinationExists, error)) {
        return failureResult(FilesystemTransferFailure::InvalidDestination, destination, error,
                             "cannot inspect destination");
    }
    if (destinationExists) {
        return failureResult(FilesystemTransferFailure::DestinationExists, destination,
                             std::make_error_code(std::errc::file_exists), "destination already exists");
    }

    if (sourceStatus.type() == fs::file_type::directory) {
        const fs::path canonicalSource = fs::canonical(source, error);
        if (error) {
            return failureResult(FilesystemTransferFailure::SourceNotFound, destination, error,
                                 "cannot resolve source directory");
        }
        const fs::path canonicalDestination = fs::weakly_canonical(destination, error);
        if (error) {
            return failureResult(FilesystemTransferFailure::InvalidDestination, destination, error,
                                 "cannot resolve destination path");
        }
        if (sameOrDescendant(canonicalDestination, canonicalSource)) {
            return failureResult(FilesystemTransferFailure::DestinationInsideSource, destination,
                                 std::make_error_code(std::errc::invalid_argument),
                                 "destination resolves inside the source directory");
        }
    }

    return FilesystemTransferResult{};
}

std::error_code renameNoReplace(const fs::path& source, const fs::path& destination)
{
#if defined(__linux__) && defined(SYS_renameat2)
    if (::syscall(SYS_renameat2, AT_FDCWD, source.c_str(), AT_FDCWD, destination.c_str(), RENAME_NOREPLACE) == 0) {
        return {};
    }
    return std::error_code(errno, std::generic_category());
#elif defined(__APPLE__)
    if (::renamex_np(source.c_str(), destination.c_str(), RENAME_EXCL) == 0) {
        return {};
    }
    return std::error_code(errno, std::generic_category());
#else
    (void)source;
    (void)destination;
    return std::make_error_code(std::errc::operation_not_supported);
#endif
}

fs::path quarantineSource(const fs::path& source, std::error_code& error)
{
    const fs::path sourceParent = source.parent_path().empty() ? fs::path(".") : source.parent_path();
    const uint64_t timestamp    = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    for (int attempt = 0; attempt < kMaxStageCreationAttempts; ++attempt) {
        const uint64_t serial = g_hold_serial.fetch_add(1, std::memory_order_relaxed);
        const fs::path hold = sourceParent / (".files-move-hold-" + std::to_string(static_cast<long long>(::getpid())) +
                                              "-" + std::to_string(timestamp) + "-" + std::to_string(serial));
        error               = renameNoReplace(source, hold);
        if (!error) {
            return hold;
        }
        if (error != std::errc::file_exists) {
            return {};
        }
    }

    error = std::make_error_code(std::errc::file_exists);
    return {};
}

fs::path createStageDirectory(const fs::path& destinationParent, std::error_code& error)
{
    const uint64_t timestamp = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    for (int attempt = 0; attempt < kMaxStageCreationAttempts; ++attempt) {
        const uint64_t serial = g_stage_serial.fetch_add(1, std::memory_order_relaxed);
        const fs::path candidate =
            destinationParent / (".files-transfer-v2-" + std::to_string(static_cast<long long>(::getpid())) + "-" +
                                 std::to_string(timestamp) + "-" + std::to_string(serial));
        if (::mkdir(candidate.c_str(), S_IRWXU) == 0) {
            error.clear();
            return candidate;
        }
        error = std::error_code(errno, std::generic_category());
        if (error != std::errc::file_exists) {
            return {};
        }
    }

    error = std::make_error_code(std::errc::file_exists);
    return {};
}

bool makeStagedDirectoriesOwnerAccessible(const fs::path& path, std::error_code& error, std::string& detail)
{
    error.clear();
    const fs::file_status status = fs::symlink_status(path, error);
    if (error == std::errc::no_such_file_or_directory || status.type() == fs::file_type::not_found) {
        error.clear();
        return true;
    }
    if (error) {
        detail = "cannot inspect staged node '" + path.string() + "': " + error.message();
        return false;
    }
    if (status.type() != fs::file_type::directory) {
        return true;
    }

    fs::permissions(path, fs::perms::owner_all, fs::perm_options::add | fs::perm_options::nofollow, error);
    if (error) {
        detail = "cannot make staged directory removable '" + path.string() + "': " + error.message();
        return false;
    }

    fs::directory_iterator iterator(path, fs::directory_options::none, error);
    const fs::directory_iterator end;
    while (!error && iterator != end) {
        if (!makeStagedDirectoriesOwnerAccessible(iterator->path(), error, detail)) {
            return false;
        }
        iterator.increment(error);
    }
    if (error) {
        detail = "cannot enumerate staged directory '" + path.string() + "': " + error.message();
        return false;
    }
    return true;
}

bool cleanupStage(const fs::path& stageDirectory)
{
    std::error_code cleanupError;
    std::string cleanupDetail;
    if (!makeStagedDirectoriesOwnerAccessible(stageDirectory, cleanupError, cleanupDetail)) {
        spdlog::warn("FilesystemTransfer: stage permission repair failed '{}' error={} ({}) detail={}",
                     stageDirectory.string(), cleanupError.value(), cleanupError.message(), cleanupDetail);
    }

    cleanupError.clear();
    fs::remove_all(stageDirectory, cleanupError);
    if (cleanupError) {
        spdlog::warn("FilesystemTransfer: failed to clean stage '{}' error={} ({})", stageDirectory.string(),
                     cleanupError.value(), cleanupError.message());
    }
    return !cleanupError;
}

FilesystemTransferResult restoreQuarantinedSource(const fs::path& source, const fs::path& hold,
                                                  const fs::path& destination, std::error_code causeError,
                                                  const std::string& causeDetail)
{
    std::error_code restoreError = renameNoReplace(hold, source);
    if (!restoreError) {
        return failureResult(FilesystemTransferFailure::SourceCleanupFailed, destination, causeError,
                             causeDetail + "; source restored to '" + source.string() + "'");
    }

    return failureResult(
        FilesystemTransferFailure::SourceCleanupFailed, destination, restoreError,
        causeDetail + "; restore failed: " + restoreError.message() + "; source preserved at '" + hold.string() + "'");
}

StructureValidationResult compareNode(const fs::path& source, const fs::path& destination)
{
    std::error_code error;
    const fs::file_status sourceStatus = fs::symlink_status(source, error);
    if (error) {
        return {false, error, "cannot inspect source node: " + source.string(), 0};
    }
    const fs::file_status destinationStatus = fs::symlink_status(destination, error);
    if (error) {
        return {false, error, "cannot inspect staged node: " + destination.string(), 0};
    }
    if (sourceStatus.type() != destinationStatus.type()) {
        return {false, std::make_error_code(std::errc::invalid_argument), "node type differs at: " + source.string(),
                0};
    }

    if (sourceStatus.type() == fs::file_type::regular) {
        const uintmax_t sourceSize = fs::file_size(source, error);
        if (error) {
            return {false, error, "cannot read source size: " + source.string(), 0};
        }
        const uintmax_t destinationSize = fs::file_size(destination, error);
        if (error) {
            return {false, error, "cannot read staged size: " + destination.string(), 0};
        }
        if (sourceSize != destinationSize) {
            return {false, std::make_error_code(std::errc::io_error), "file size differs at: " + source.string(), 0};
        }
    } else if (sourceStatus.type() == fs::file_type::symlink) {
        const fs::path sourceTarget = fs::read_symlink(source, error);
        if (error) {
            return {false, error, "cannot read source symbolic link: " + source.string(), 0};
        }
        const fs::path destinationTarget = fs::read_symlink(destination, error);
        if (error) {
            return {false, error, "cannot read staged symbolic link: " + destination.string(), 0};
        }
        if (sourceTarget != destinationTarget) {
            return {false, std::make_error_code(std::errc::io_error),
                    "symbolic link target differs at: " + source.string(), 0};
        }
    } else if (sourceStatus.type() != fs::file_type::directory) {
        return {false, std::make_error_code(std::errc::operation_not_supported),
                "unsupported node in source tree: " + source.string(), 0};
    }

    return {true, {}, {}, 1};
}

StructureValidationResult validateStructure(const fs::path& source, const fs::path& destination,
                                            const TransferControl* control, const SourceFilesystem* filesystem)
{
    StructureValidationResult root = compareNode(source, destination);
    if (!root.valid) {
        return root;
    }

    std::error_code error;
    const fs::file_status sourceStatus = fs::symlink_status(source, error);
    if (error) {
        return {false, error, "cannot re-inspect source root during validation", root.entries};
    }
    if (sourceStatus.type() != fs::file_type::directory) {
        return root;
    }

    uint64_t sourceEntries = 0;
    fs::recursive_directory_iterator sourceIterator(source, fs::directory_options::none, error);
    const fs::recursive_directory_iterator end;
    while (!error && sourceIterator != end) {
        if (control && control->cancelled()) {
            return {false, std::make_error_code(std::errc::operation_canceled), {}, sourceEntries};
        }
        if (filesystem) {
            struct stat info{};
            if (::lstat(sourceIterator->path().c_str(), &info) != 0) {
                return {false, systemError(), sourceIterator->path().string(), sourceEntries};
            }
            error = checkFilesystem(AT_FDCWD, sourceIterator->path().c_str(), AT_SYMLINK_NOFOLLOW, info, filesystem);
            if (error) {
                return {false, error, "folder contains a mounted filesystem", sourceEntries};
            }
        }
        const fs::path relative = sourceIterator->path().lexically_relative(source);
        StructureValidationResult node = compareNode(sourceIterator->path(), destination / relative);
        if (!node.valid) {
            return node;
        }
        ++sourceEntries;
        sourceIterator.increment(error);
    }
    if (error) {
        return {false, error, "cannot enumerate source during validation", sourceEntries};
    }

    uint64_t destinationEntries = 0;
    fs::recursive_directory_iterator destinationIterator(destination, fs::directory_options::none, error);
    while (!error && destinationIterator != end) {
        if (control && control->cancelled()) {
            return {false, std::make_error_code(std::errc::operation_canceled), {}, destinationEntries};
        }
        ++destinationEntries;
        destinationIterator.increment(error);
    }
    if (error) {
        return {false, error, "cannot enumerate staged copy during validation", destinationEntries};
    }
    if (sourceEntries != destinationEntries) {
        return {false, std::make_error_code(std::errc::io_error), "staged tree entry count differs", sourceEntries};
    }

    root.entries += sourceEntries;
    return root;
}

FilesystemTransferResult driveDisconnected(const fs::path& destination)
{
    return failureResult(FilesystemTransferFailure::DriveDisconnected, destination,
                         std::make_error_code(std::errc::no_such_device), "drive disconnected");
}

FilesystemTransferResult cancelledTransfer(const fs::path& destination)
{
    return failureResult(FilesystemTransferFailure::Cancelled, destination,
                         std::make_error_code(std::errc::operation_canceled), "Transfer cancelled");
}

FilesystemTransferResult mountedSource(const fs::path& destination)
{
    return failureResult(FilesystemTransferFailure::MountedSource, destination,
                         std::make_error_code(std::errc::cross_device_link), "Folder contains a mounted filesystem");
}

FilesystemTransferResult stagedCopyNoReplace(const fs::path& source, const fs::path& destination,
                                              const std::function<bool()>& connected, TransferControl* control,
                                              const SourceFilesystem* filesystem = nullptr)
{
    fs::file_status sourceStatus;
    FilesystemTransferResult endpointResult = validateEndpoints(source, destination, sourceStatus);
    if (!endpointResult) {
        return endpointResult;
    }

    std::error_code error;
    const fs::path stageDirectory = createStageDirectory(destination.parent_path(), error);
    if (stageDirectory.empty()) {
        return failureResult(FilesystemTransferFailure::StageCreationFailed, destination, error,
                             "cannot create a private staging directory");
    }

    const fs::path stagedPayload = stageDirectory / source.filename();
    const auto discardStage = [&](FilesystemTransferResult result) {
        std::error_code error;
        const bool hadPayload = fs::exists(fs::symlink_status(stagedPayload, error));
        const bool removed = cleanupStage(stageDirectory);
        if (error && error != std::errc::no_such_file_or_directory && removed) {
            result.destinationState = TransferDestination::CleanupUnverified;
        } else if (!removed) {
            result.destinationState = TransferDestination::CleanupFailed;
        } else if (hadPayload) {
            result.destinationState = TransferDestination::PartialRemoved;
        }
        return result;
    };
    spdlog::info("FilesystemTransfer: staging copy source='{}' stage='{}' destination='{}'", source.string(),
                 stagedPayload.string(), destination.string());

    bool syncFailure = false;
    error = copyNode(AT_FDCWD, source.c_str(), stagedPayload, filesystem, control, syncFailure);
    if (error) {
        if (error == std::errc::operation_canceled) {
            return discardStage(cancelledTransfer(destination));
        }
        if (error == std::errc::cross_device_link) {
            return discardStage(mountedSource(destination));
        }
        const auto failure = syncFailure ? FilesystemTransferFailure::SyncFailed : FilesystemTransferFailure::CopyFailed;
        const char* detail = syncFailure ? "cannot sync the staged copy" : "failed while copying into the staging directory";
        return discardStage(failureResult(failure, destination, error, detail));
    }

    const StructureValidationResult validation = validateStructure(source, stagedPayload, control, filesystem);
    if (!validation.valid) {
        if (validation.error == std::errc::operation_canceled) {
            return discardStage(cancelledTransfer(destination));
        }
        if (validation.error == std::errc::cross_device_link) {
            return discardStage(mountedSource(destination));
        }
        return discardStage(failureResult(FilesystemTransferFailure::ValidationFailed, destination, validation.error,
                                          validation.detail));
    }
    spdlog::info("FilesystemTransfer: validated staged copy source='{}' entries={}", source.string(),
                 validation.entries);

    if (connected && !connected()) {
        return discardStage(driveDisconnected(destination));
    }
    const FileDescriptor parent(filesystem ? ::open(destination.parent_path().c_str(),
                                                   O_RDONLY | O_DIRECTORY | O_CLOEXEC) : -1);
    const FileDescriptor stage(filesystem ? ::open(stageDirectory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC) : -1);
    if (filesystem && (parent.fd < 0 || stage.fd < 0)) {
        return discardStage(failureResult(FilesystemTransferFailure::SyncFailed, destination, systemError(),
                                          "cannot open the destination directories for syncing"));
    }
    if (control && !control->beginCommit()) {
        return discardStage(cancelledTransfer(destination));
    }
    error = renameNoReplace(stagedPayload, destination);
    if (error) {
        if (control) {
            control->retry();
        }
        const FilesystemTransferFailure failure = error == std::errc::file_exists
                                                      ? FilesystemTransferFailure::DestinationExists
                                                      : FilesystemTransferFailure::PublishFailed;
        return discardStage(failureResult(failure, destination, error,
                                          "cannot publish the staged copy without replacement"));
    }

    const auto stageSyncError = filesystem ? syncDescriptor(stage.fd) : std::error_code{};
    cleanupStage(stageDirectory);
    if (filesystem) {
        const auto parentSyncError = syncDescriptor(parent.fd);
        if (const auto syncError = stageSyncError ? stageSyncError : parentSyncError) {
            auto result = failureResult(FilesystemTransferFailure::SyncFailed, destination, syncError,
                                        "cannot sync the published destination; source kept");
            result.destinationState = TransferDestination::Published;
            return result;
        }
    }
    spdlog::info("FilesystemTransfer: published staged copy destination='{}'", destination.string());
    return FilesystemTransferResult{FilesystemTransferFailure::None, destination, {}, {}};
}

FilesystemTransferResult movePathNoReplace(const fs::path& source, const fs::path& destination,
                                             const std::function<bool()>& connected, TransferControl* control)
{
    fs::file_status sourceStatus;
    FilesystemTransferResult endpointResult = validateEndpoints(source, destination, sourceStatus);
    if (!endpointResult) {
        return endpointResult;
    }

    std::error_code error;
    if (containsMountPoint(source, error)) {
        return mountedSource(destination);
    }
    if (error) {
        return failureResult(FilesystemTransferFailure::ValidationFailed, destination, error,
                             "cannot check the source for mounted filesystems");
    }
    if (connected && !connected()) {
        return driveDisconnected(destination);
    }
    spdlog::info("FilesystemTransfer: moving source='{}' destination='{}'", source.string(), destination.string());
    if (control && !control->beginCommit()) {
        return cancelledTransfer(destination);
    }
    error = renameNoReplace(source, destination);
    if (error && control) {
        control->retry();
    }
    if (!error) {
        spdlog::info("FilesystemTransfer: same-filesystem move complete destination='{}'", destination.string());
        return FilesystemTransferResult{FilesystemTransferFailure::None, destination, {}, {}};
    }
    if (error != std::errc::cross_device_link) {
        const FilesystemTransferFailure failure = error == std::errc::file_exists
                                                      ? FilesystemTransferFailure::DestinationExists
                                                      : FilesystemTransferFailure::PublishFailed;
        return failureResult(failure, destination, error, "same-filesystem no-replace rename failed");
    }

    spdlog::info("FilesystemTransfer: cross-device move detected, switching to staged copy source='{}'",
                 source.string());
    struct stat sourceInfo{};
    SourceFilesystem filesystem;
    if (::lstat(source.c_str(), &sourceInfo) != 0 ||
        !identifyFilesystem(AT_FDCWD, source.c_str(), AT_SYMLINK_NOFOLLOW, sourceInfo.st_dev, filesystem)) {
        return failureResult(FilesystemTransferFailure::ValidationFailed, destination, systemError(),
                             "cannot identify the source filesystem");
    }
    SourceSnapshot sourceBeforeCopy;
    std::string snapshotDetail;
    if (!captureSourceSnapshot(source, sourceBeforeCopy, error, snapshotDetail, filesystem, control)) {
        if (error == std::errc::operation_canceled) {
            return cancelledTransfer(destination);
        }
        if (error == std::errc::cross_device_link) {
            return mountedSource(destination);
        }
        return failureResult(FilesystemTransferFailure::ValidationFailed, destination, error,
                             "cannot capture source before cross-device copy: " + snapshotDetail);
    }
    spdlog::info("FilesystemTransfer: captured source snapshot source='{}' entries={}", source.string(),
                 sourceBeforeCopy.nodes.size());

    FilesystemTransferResult copyResult = stagedCopyNoReplace(source, destination, connected, control, &filesystem);
    if (!copyResult) {
        return copyResult;
    }

    SourceSnapshot sourceAfterCopy;
    if (!captureSourceSnapshot(source, sourceAfterCopy, error, snapshotDetail, filesystem)) {
        return failureResult(FilesystemTransferFailure::SourceCleanupFailed, destination, error,
                             "destination is complete; source preserved at '" + source.string() +
                                 "' because post-copy verification failed: " + snapshotDetail);
    }
    if (!compareSourceSnapshots(sourceBeforeCopy, sourceAfterCopy, false, snapshotDetail)) {
        return failureResult(FilesystemTransferFailure::SourceCleanupFailed, destination,
                             std::make_error_code(std::errc::resource_unavailable_try_again),
                             "destination is complete; source preserved at '" + source.string() +
                                 "' because it changed during copy: " + snapshotDetail);
    }

    if (connected && !connected()) {
        copyResult = driveDisconnected(destination);
        copyResult.destinationState = TransferDestination::Published;
        return copyResult;
    }
    const fs::path hold = quarantineSource(source, error);
    if (hold.empty()) {
        return failureResult(FilesystemTransferFailure::SourceCleanupFailed, destination, error,
                             "destination is complete; source preserved at '" + source.string() +
                                 "' because it could not be moved to a private hold path");
    }
    spdlog::info("FilesystemTransfer: quarantined verified source source='{}' hold='{}'", source.string(),
                 hold.string());

    SourceSnapshot quarantinedSource;
    if (!captureSourceSnapshot(hold, quarantinedSource, error, snapshotDetail, filesystem)) {
        return restoreQuarantinedSource(source, hold, destination, error,
                                        "destination is complete; quarantined source verification failed at '" +
                                            hold.string() + "': " + snapshotDetail);
    }
    if (!sameRootIdentity(sourceAfterCopy, quarantinedSource)) {
        return restoreQuarantinedSource(
            source, hold, destination, std::make_error_code(std::errc::resource_unavailable_try_again),
            "destination is complete; quarantined root identity differs at '" + hold.string() + "'");
    }
    if (!compareSourceSnapshots(sourceAfterCopy, quarantinedSource, true, snapshotDetail)) {
        return restoreQuarantinedSource(source, hold, destination,
                                        std::make_error_code(std::errc::resource_unavailable_try_again),
                                        "destination is complete; source changed while entering quarantine at '" +
                                            hold.string() + "': " + snapshotDetail);
    }

    if (containsMountPoint(hold, error) || error) {
        return restoreQuarantinedSource(source, hold, destination,
                                        error ? error : std::make_error_code(std::errc::cross_device_link),
                                        "source cleanup stopped while checking for mounted filesystems");
    }
    if (connected && !connected()) {
        return restoreQuarantinedSource(source, hold, destination,
                                        std::make_error_code(std::errc::no_such_device), "drive disconnected");
    }
    error = removeSourceNode(AT_FDCWD, hold.c_str(), filesystem, quarantinedSource, {});
    if (error) {
        return failureResult(FilesystemTransferFailure::SourceCleanupFailed, destination, error,
                             "destination is complete; quarantined source may be partially retained at '" +
                                 hold.string() + "': " + error.message());
    }

    spdlog::info("FilesystemTransfer: cross-device move complete source='{}' hold='{}' destination='{}'",
                 source.string(), hold.string(), destination.string());
    return FilesystemTransferResult{FilesystemTransferFailure::None, destination, {}, {}};
}

bool sameDirectory(const fs::path& source, const fs::path& destinationDirectory, std::error_code& error)
{
    error.clear();
    const fs::path sourceParent = source.parent_path().empty() ? fs::path(".") : source.parent_path();
    const bool equivalent       = fs::equivalent(sourceParent, destinationDirectory, error);
    return !error && equivalent;
}

fs::path copyName(const fs::path& source, int index, std::string_view duplicateWord, std::error_code& error)
{
    error.clear();
    const fs::file_status status = fs::symlink_status(source, error);
    if (error) {
        return {};
    }

    bool directory = status.type() == fs::file_type::directory;
    if (status.type() == fs::file_type::symlink) {
        error.clear();
        directory = fs::is_directory(source, error);
        if (error) {
            error.clear();
            directory = false;
        }
    }

    const fs::path filename  = source.filename();
    const std::string suffix = " " + std::string(duplicateWord) + (index == 1 ? "" : " " + std::to_string(index));
    if (directory) {
        return filename.string() + suffix;
    }
    return filename.stem().string() + suffix + filename.extension().string();
}

}  // namespace

FilesystemTransferResult copyPathToDirectory(const fs::path& source, const fs::path& destinationDirectory,
                                            const std::function<bool()>& connected, TransferControl* control,
                                            std::string_view duplicateWord)
{
    spdlog::info("FilesystemTransfer: copy requested source='{}' directory='{}'", source.string(),
                 destinationDirectory.string());

    std::error_code error;
    if (!sameDirectory(source, destinationDirectory, error)) {
        if (error) {
            return failureResult(FilesystemTransferFailure::InvalidDestination,
                                 destinationDirectory / source.filename(), error,
                                 "cannot compare source and destination directories");
        }
        return stagedCopyNoReplace(source, destinationDirectory / source.filename(), connected, control);
    }

    for (int index = 1; index <= kMaxCopyNameAttempts; ++index) {
        const fs::path name = copyName(source, index, duplicateWord, error);
        if (error) {
            return failureResult(FilesystemTransferFailure::SourceNotFound, destinationDirectory, error,
                                 "cannot choose a name for the copy");
        }
        FilesystemTransferResult result = stagedCopyNoReplace(source, destinationDirectory / name, connected, control);
        if (result.failure != FilesystemTransferFailure::DestinationExists) {
            if (result) {
                spdlog::info("FilesystemTransfer: same-directory copy selected name='{}'", name.string());
            }
            return result;
        }
    }

    return failureResult(FilesystemTransferFailure::DestinationExists, destinationDirectory,
                         std::make_error_code(std::errc::file_exists), "no available automatic copy name");
}

FilesystemTransferResult movePathToDirectory(const fs::path& source, const fs::path& destinationDirectory,
                                            const std::function<bool()>& connected, TransferControl* control)
{
    return movePathNoReplace(source, destinationDirectory / source.filename(), connected, control);
}

const char* filesystemTransferFailureName(FilesystemTransferFailure failure)
{
    switch (failure) {
        case FilesystemTransferFailure::None:
            return "none";
        case FilesystemTransferFailure::SourceNotFound:
            return "source-not-found";
        case FilesystemTransferFailure::UnsupportedSourceType:
            return "unsupported-source-type";
        case FilesystemTransferFailure::InvalidDestination:
            return "invalid-destination";
        case FilesystemTransferFailure::SamePath:
            return "same-path";
        case FilesystemTransferFailure::DestinationExists:
            return "destination-exists";
        case FilesystemTransferFailure::DestinationInsideSource:
            return "destination-inside-source";
        case FilesystemTransferFailure::StageCreationFailed:
            return "stage-creation-failed";
        case FilesystemTransferFailure::CopyFailed:
            return "copy-failed";
        case FilesystemTransferFailure::ValidationFailed:
            return "validation-failed";
        case FilesystemTransferFailure::PublishFailed:
            return "publish-failed";
        case FilesystemTransferFailure::DriveDisconnected:
            return "drive-disconnected";
        case FilesystemTransferFailure::Cancelled:
            return "cancelled";
        case FilesystemTransferFailure::MountedSource:
            return "mounted-source";
        case FilesystemTransferFailure::SyncFailed:
            return "sync-failed";
        case FilesystemTransferFailure::SourceCleanupFailed:
            return "source-cleanup-failed";
        default:
            return "unknown";
    }
}

}  // namespace files::internal
