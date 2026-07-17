#include "models/filesystem_transfer.hpp"

#include <spdlog/spdlog.h>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <mutex>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {

namespace fs = std::filesystem;
using files::internal::copyPathToDirectory;
using files::internal::FilesystemTransferFailure;
using files::internal::movePathToDirectory;

class TestFailure : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class TestSkipped : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void require(bool condition, const std::string& message)
{
    if (!condition) {
        throw TestFailure(message);
    }
}

std::string uniqueName(const std::string& prefix)
{
    static std::atomic<uint64_t> serial{0};
    const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
#if defined(__unix__) || defined(__APPLE__)
    const auto process = static_cast<unsigned long long>(::getpid());
#else
    const auto process = 0ULL;
#endif
    return prefix + "-" + std::to_string(process) + "-" + std::to_string(tick) + "-" +
           std::to_string(serial.fetch_add(1, std::memory_order_relaxed));
}

class TemporaryDirectory {
public:
    explicit TemporaryDirectory(const fs::path& parent    = fs::temp_directory_path(),
                                const std::string& prefix = "files-transfer-test")
    {
        std::error_code error;
        for (int attempt = 0; attempt < 32; ++attempt) {
            _path = parent / uniqueName(prefix);
            if (fs::create_directory(_path, error)) {
                return;
            }
            if (error != std::errc::file_exists) {
                throw TestSkipped("cannot create temporary directory under " + parent.string() + ": " +
                                  error.message());
            }
            error.clear();
        }
        throw TestSkipped("cannot choose a unique temporary directory under " + parent.string());
    }

    ~TemporaryDirectory()
    {
        std::error_code error;
        fs::permissions(_path, fs::perms::owner_all, fs::perm_options::add, error);
        error.clear();
        fs::remove_all(_path, error);
    }

    const fs::path& path() const
    {
        return _path;
    }

private:
    fs::path _path;
};

void createDirectory(const fs::path& path)
{
    std::error_code error;
    require(fs::create_directories(path, error) || (!error && fs::is_directory(path)),
            "cannot create directory " + path.string() + ": " + error.message());
}

void writeFile(const fs::path& path, const std::string& content)
{
    createDirectory(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    require(static_cast<bool>(stream), "cannot open file for writing: " + path.string());
    stream.write(content.data(), static_cast<std::streamsize>(content.size()));
    require(static_cast<bool>(stream), "cannot write file: " + path.string());
}

std::string readFile(const fs::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    require(static_cast<bool>(stream), "cannot open file for reading: " + path.string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

bool existsNoFollow(const fs::path& path)
{
    std::error_code error;
    const fs::file_status status = fs::symlink_status(path, error);
    return !error && status.type() != fs::file_type::not_found;
}

void requireNoEntriesWithPrefix(const fs::path& directory, const std::string& prefix, const char* description)
{
    std::error_code error;
    for (fs::directory_iterator iterator(directory, error), end; !error && iterator != end; iterator.increment(error)) {
        const std::string name = iterator->path().filename().string();
        require(name.rfind(prefix, 0) != 0, std::string(description) + " was not cleaned: " + name);
    }
    require(!error, "cannot inspect directory for temporary transfer entries: " + error.message());
}

void requireNoStageDirectories(const fs::path& directory)
{
    requireNoEntriesWithPrefix(directory, ".files-transfer-", "staging directory");
}

void requireNoMoveHoldEntries(const fs::path& directory)
{
    requireNoEntriesWithPrefix(directory, ".files-move-hold-", "move hold entry");
}

void testCopyRegularFile()
{
    TemporaryDirectory root;
    const fs::path source      = root.path() / "source" / "message.txt";
    const fs::path destination = root.path() / "destination";
    writeFile(source, "hello\nworld\n");
    createDirectory(destination);

    const auto result = copyPathToDirectory(source, destination);
    require(static_cast<bool>(result), "regular file copy failed: " + result.detail);
    require(result.destination == destination / source.filename(), "copy returned the wrong destination");
    require(readFile(source) == "hello\nworld\n", "copy modified the source file");
    require(readFile(result.destination) == "hello\nworld\n", "copied file content differs");
    requireNoStageDirectories(destination);
}

void testCopyDirectoryTree()
{
    TemporaryDirectory root;
    const fs::path source      = root.path() / "source" / "tree";
    const fs::path destination = root.path() / "destination";
    writeFile(source / "nested" / "payload.bin", std::string("a\0b\0c", 5));
    writeFile(source / "root.txt", "root");
    createDirectory(destination);

    const auto result = copyPathToDirectory(source, destination);
    require(static_cast<bool>(result), "directory copy failed: " + result.detail);
    require(readFile(result.destination / "nested" / "payload.bin") == std::string("a\0b\0c", 5),
            "nested file content differs");
    require(readFile(result.destination / "root.txt") == "root", "root file content differs");
    require(readFile(source / "root.txt") == "root", "directory copy removed or changed the source");
    requireNoStageDirectories(destination);
}

void testCopyEmptyFileAndDirectory()
{
    TemporaryDirectory root;
    const fs::path destination = root.path() / "destination";
    const fs::path emptyFile   = root.path() / "source" / "empty.txt";
    const fs::path emptyDir    = root.path() / "source" / "empty-dir";
    writeFile(emptyFile, "");
    createDirectory(emptyDir);
    createDirectory(destination);

    const auto fileResult = copyPathToDirectory(emptyFile, destination);
    require(static_cast<bool>(fileResult), "empty file copy failed: " + fileResult.detail);
    require(fs::file_size(fileResult.destination) == 0, "empty file copy is not empty");

    const auto directoryResult = copyPathToDirectory(emptyDir, destination);
    require(static_cast<bool>(directoryResult), "empty directory copy failed: " + directoryResult.detail);
    require(fs::is_directory(directoryResult.destination), "empty directory copy is not a directory");
    require(fs::is_empty(directoryResult.destination), "empty directory copy contains entries");
    requireNoStageDirectories(destination);
}

void testCopySymbolicLink()
{
#if defined(__unix__) || defined(__APPLE__)
    TemporaryDirectory root;
    const fs::path sourceDirectory = root.path() / "source";
    const fs::path destination     = root.path() / "destination";
    writeFile(sourceDirectory / "target.txt", "target");
    createDirectory(destination);

    std::error_code error;
    const fs::path source = sourceDirectory / "link.txt";
    fs::create_symlink("target.txt", source, error);
    if (error) {
        throw TestSkipped("cannot create symbolic link: " + error.message());
    }

    const auto result = copyPathToDirectory(source, destination);
    require(static_cast<bool>(result), "symbolic link copy failed: " + result.detail);
    require(fs::is_symlink(fs::symlink_status(result.destination)), "copied node is not a symbolic link");
    require(fs::read_symlink(result.destination) == fs::path("target.txt"), "symbolic link target changed");
    require(existsNoFollow(source), "symbolic link source was removed");
    requireNoStageDirectories(destination);
#else
    throw TestSkipped("symbolic links are not covered on this platform");
#endif
}

void testSameDirectoryCopyChoosesNames()
{
    TemporaryDirectory root;
    const fs::path source = root.path() / "archive.tar.gz";
    writeFile(source, "archive");

    const auto first = copyPathToDirectory(source, root.path());
    require(static_cast<bool>(first), "first same-directory copy failed: " + first.detail);
    require(first.destination.filename() == "archive.tar copy.gz", "first automatic copy name is wrong");

    const auto second = copyPathToDirectory(source, root.path());
    require(static_cast<bool>(second), "second same-directory copy failed: " + second.detail);
    require(second.destination.filename() == "archive.tar copy 2.gz", "second automatic copy name is wrong");
    require(readFile(source) == "archive", "same-directory copy changed the source");
    require(readFile(first.destination) == "archive", "first same-directory copy content differs");
    require(readFile(second.destination) == "archive", "second same-directory copy content differs");
    requireNoStageDirectories(root.path());
}

void testCollisionLeavesBothSidesUnchanged()
{
    TemporaryDirectory root;
    const fs::path sourceDirectory = root.path() / "source";
    const fs::path destination     = root.path() / "destination";
    const fs::path source          = sourceDirectory / "same.txt";
    const fs::path existing        = destination / "same.txt";
    writeFile(source, "source-content");
    writeFile(existing, "destination-content");

    const auto copyResult = copyPathToDirectory(source, destination);
    require(copyResult.failure == FilesystemTransferFailure::DestinationExists,
            "copy collision did not report DestinationExists");
    require(readFile(source) == "source-content", "copy collision changed the source");
    require(readFile(existing) == "destination-content", "copy collision changed the destination");

    const auto moveResult = movePathToDirectory(source, destination);
    require(moveResult.failure == FilesystemTransferFailure::DestinationExists,
            "move collision did not report DestinationExists");
    require(readFile(source) == "source-content", "move collision changed the source");
    require(readFile(existing) == "destination-content", "move collision changed the destination");
    requireNoStageDirectories(destination);
}

void testDirectoryCollisionDoesNotMerge()
{
    TemporaryDirectory root;
    const fs::path source      = root.path() / "source" / "tree";
    const fs::path destination = root.path() / "destination";
    const fs::path existing    = destination / source.filename();
    writeFile(source / "new.txt", "new-content");
    writeFile(existing / "existing.txt", "existing-content");

    const auto copyResult = copyPathToDirectory(source, destination);
    require(copyResult.failure == FilesystemTransferFailure::DestinationExists,
            "directory copy collision did not report DestinationExists");
    require(readFile(source / "new.txt") == "new-content", "directory copy collision changed the source");
    require(readFile(existing / "existing.txt") == "existing-content",
            "directory copy collision changed existing destination content");
    require(!existsNoFollow(existing / "new.txt"), "directory copy collision merged source content");

    const auto moveResult = movePathToDirectory(source, destination);
    require(moveResult.failure == FilesystemTransferFailure::DestinationExists,
            "directory move collision did not report DestinationExists");
    require(readFile(source / "new.txt") == "new-content", "directory move collision changed the source");
    require(readFile(existing / "existing.txt") == "existing-content",
            "directory move collision changed existing destination content");
    require(!existsNoFollow(existing / "new.txt"), "directory move collision merged source content");
    requireNoStageDirectories(destination);
}

class StartGate {
public:
    explicit StartGate(int participants) : _participants(participants)
    {
    }

    void arriveAndWait()
    {
        std::unique_lock<std::mutex> lock(_mutex);
        ++_arrived;
        if (_arrived == _participants) {
            _open = true;
            _condition.notify_all();
            return;
        }
        _condition.wait(lock, [this]() { return _open; });
    }

private:
    std::mutex _mutex;
    std::condition_variable _condition;
    int _participants = 0;
    int _arrived      = 0;
    bool _open        = false;
};

void testConcurrentPublishHasOneWinner()
{
    TemporaryDirectory root;
    const fs::path firstSource  = root.path() / "first" / "shared.txt";
    const fs::path secondSource = root.path() / "second" / "shared.txt";
    writeFile(firstSource, "first-content");
    writeFile(secondSource, "second-content");

    constexpr int kAttempts = 12;
    for (int attempt = 0; attempt < kAttempts; ++attempt) {
        const fs::path destination = root.path() / ("destination-" + std::to_string(attempt));
        createDirectory(destination);

        StartGate gate(2);
        std::array<files::internal::FilesystemTransferResult, 2> results;
        std::thread first([&]() {
            gate.arriveAndWait();
            results[0] = copyPathToDirectory(firstSource, destination);
        });
        std::thread second([&]() {
            gate.arriveAndWait();
            results[1] = copyPathToDirectory(secondSource, destination);
        });
        first.join();
        second.join();

        const int successCount =
            static_cast<int>(static_cast<bool>(results[0])) + static_cast<int>(static_cast<bool>(results[1]));
        require(successCount == 1, "concurrent publication did not produce exactly one winner");
        const size_t winner = results[0] ? 0U : 1U;
        const size_t loser  = winner == 0U ? 1U : 0U;
        require(results[loser].failure == FilesystemTransferFailure::DestinationExists,
                "concurrent publication loser did not report DestinationExists");

        const std::string expected = winner == 0U ? "first-content" : "second-content";
        require(readFile(destination / "shared.txt") == expected,
                "published content does not match the concurrent winner");
        require(readFile(firstSource) == "first-content", "concurrent copy changed the first source");
        require(readFile(secondSource) == "second-content", "concurrent copy changed the second source");
        requireNoStageDirectories(destination);
    }
}

void testUnsupportedNodeAbortsStagedCopy()
{
#if defined(__unix__) || defined(__APPLE__)
    TemporaryDirectory root;
    const fs::path source      = root.path() / "source" / "tree";
    const fs::path destination = root.path() / "destination";
    writeFile(source / "before.txt", "source-content");
    createDirectory(destination);

    const fs::path fifo = source / "unsupported.fifo";
    if (::mkfifo(fifo.c_str(), 0600) != 0) {
        throw TestSkipped("cannot create FIFO: " + std::string(std::strerror(errno)));
    }

    const auto result = copyPathToDirectory(source, destination);
    require(result.failure == FilesystemTransferFailure::CopyFailed,
            "FIFO copy interruption did not report CopyFailed");
    require(readFile(source / "before.txt") == "source-content", "failed staged copy changed the source file");
    require(fs::is_fifo(fs::symlink_status(fifo)), "failed staged copy changed or removed the source FIFO");
    require(!existsNoFollow(destination / source.filename()), "failed staged copy published a final destination");
    requireNoStageDirectories(destination);
#else
    throw TestSkipped("FIFO copy interruption is not covered on this platform");
#endif
}

void testUnreadableNestedDirectoryCleansStage()
{
#if defined(__unix__) || defined(__APPLE__)
    if (::geteuid() == 0) {
        throw TestSkipped("mode-000 directory access failure is not reliable as root");
    }

    TemporaryDirectory root;
    const fs::path source      = root.path() / "source" / "tree";
    const fs::path locked      = source / "locked";
    const fs::path destination = root.path() / "destination";
    writeFile(locked / "secret.txt", "secret-content");
    writeFile(source / "visible.txt", "visible-content");
    createDirectory(destination);

    std::error_code error;
    fs::permissions(locked, fs::perms::none, fs::perm_options::replace, error);
    require(!error, "cannot set nested directory mode to 000: " + error.message());
    if (::access(locked.c_str(), R_OK | X_OK) == 0) {
        fs::permissions(locked, fs::perms::owner_all, fs::perm_options::add, error);
        throw TestSkipped("mode-000 nested directory remains accessible in this environment");
    }

    const auto result = copyPathToDirectory(source, destination);
    fs::permissions(locked, fs::perms::owner_all, fs::perm_options::add, error);
    require(!error, "cannot restore source directory permissions: " + error.message());

    require(result.failure == FilesystemTransferFailure::CopyFailed ||
                result.failure == FilesystemTransferFailure::ValidationFailed,
            "unreadable nested directory did not fail copy or validation");
    require(readFile(source / "visible.txt") == "visible-content", "failed copy changed the visible source file");
    require(readFile(locked / "secret.txt") == "secret-content", "failed copy changed the locked source file");
    require(!existsNoFollow(destination / source.filename()), "failed copy published a final destination");
    requireNoStageDirectories(destination);
#else
    throw TestSkipped("mode-000 directory failure is not covered on this platform");
#endif
}

void testSelfAndDescendantTransfersAreRejected()
{
    TemporaryDirectory root;
    const fs::path source = root.path() / "tree";
    writeFile(source / "child" / "payload.txt", "payload");

    const auto selfMove = movePathToDirectory(source, root.path());
    require(selfMove.failure == FilesystemTransferFailure::SamePath, "self move did not report SamePath");
    require(readFile(source / "child" / "payload.txt") == "payload", "self move changed the source");

    const auto descendantCopy = copyPathToDirectory(source, source / "child");
    require(descendantCopy.failure == FilesystemTransferFailure::DestinationInsideSource,
            "descendant copy did not report DestinationInsideSource");
    require(!existsNoFollow(source / "child" / source.filename()), "descendant copy left a destination behind");

#if defined(__unix__) || defined(__APPLE__)
    const fs::path alias = root.path() / "source-alias";
    std::error_code error;
    fs::create_directory_symlink(source / "child", alias, error);
    if (error) {
        throw TestSkipped("direct descendant checks passed; cannot create symlink alias: " + error.message());
    }

    const auto aliasMove = movePathToDirectory(source, alias);
    require(aliasMove.failure == FilesystemTransferFailure::DestinationInsideSource,
            "symlink-alias descendant move did not report DestinationInsideSource");
    require(readFile(source / "child" / "payload.txt") == "payload", "alias move changed the source");
#endif
}

void testSameFilesystemMove()
{
    TemporaryDirectory root;
    const fs::path source      = root.path() / "source" / "move.txt";
    const fs::path destination = root.path() / "destination";
    writeFile(source, "move-content");
    createDirectory(destination);

    const auto result = movePathToDirectory(source, destination);
    require(static_cast<bool>(result), "same-filesystem move failed: " + result.detail);
    require(!existsNoFollow(source), "same-filesystem move left the source behind");
    require(readFile(result.destination) == "move-content", "same-filesystem move content differs");
    requireNoStageDirectories(destination);
}

void testInvalidDestinationPreservesSource()
{
    TemporaryDirectory root;
    const fs::path source        = root.path() / "source.txt";
    const fs::path notADirectory = root.path() / "not-a-directory";
    writeFile(source, "source-content");
    writeFile(notADirectory, "blocker");

    const auto result = movePathToDirectory(source, notADirectory);
    require(result.failure == FilesystemTransferFailure::InvalidDestination,
            "invalid destination did not report InvalidDestination");
    require(readFile(source) == "source-content", "invalid destination failure changed the source");
    require(readFile(notADirectory) == "blocker", "invalid destination failure changed the blocker");
}

void testNonExdevRenameFailurePreservesSource()
{
#if defined(__unix__) || defined(__APPLE__)
    if (::geteuid() == 0) {
        throw TestSkipped("permission-based rename failure is not reliable as root");
    }

    TemporaryDirectory root;
    const fs::path source      = root.path() / "source" / "protected.txt";
    const fs::path destination = root.path() / "locked";
    writeFile(source, "protected-content");
    createDirectory(destination);

    std::error_code error;
    fs::permissions(destination, fs::perms::owner_read | fs::perms::owner_exec, fs::perm_options::replace, error);
    require(!error, "cannot make destination read-only: " + error.message());

    const auto result = movePathToDirectory(source, destination);
    fs::permissions(destination, fs::perms::owner_all, fs::perm_options::add, error);

    require(result.failure == FilesystemTransferFailure::PublishFailed,
            "non-EXDEV rename failure did not report PublishFailed");
    require(result.error != std::errc::cross_device_link, "failure unexpectedly reported EXDEV");
    require(readFile(source) == "protected-content", "non-EXDEV failure changed the source");
    require(!existsNoFollow(destination / source.filename()), "non-EXDEV failure published a destination");
#else
    throw TestSkipped("permission-based rename failure is not covered on this platform");
#endif
}

#if defined(__linux__)
dev_t deviceFor(const fs::path& path)
{
    struct stat info {};
    if (::stat(path.c_str(), &info) != 0) {
        throw TestSkipped("cannot stat filesystem for " + path.string());
    }
    return info.st_dev;
}
#endif

void testRealCrossDeviceMove()
{
#if defined(__linux__)
    TemporaryDirectory sourceRoot;
    TemporaryDirectory destinationRoot("/dev/shm", "files-transfer-exdev-test");
    if (deviceFor(sourceRoot.path()) == deviceFor(destinationRoot.path())) {
        throw TestSkipped("temporary directory and /dev/shm are on the same filesystem");
    }

    const fs::path source = sourceRoot.path() / "cross-device.txt";
    writeFile(source, "cross-device-content");

    const auto result = movePathToDirectory(source, destinationRoot.path());
    require(static_cast<bool>(result), "cross-device move failed: " + result.detail);
    require(!existsNoFollow(source), "cross-device move left the source behind");
    require(readFile(result.destination) == "cross-device-content", "cross-device move content differs");
    requireNoStageDirectories(destinationRoot.path());
    requireNoMoveHoldEntries(sourceRoot.path());
#else
    throw TestSkipped("/dev/shm EXDEV test is Linux-specific");
#endif
}

void testRealCrossDeviceDirectoryMovePreservesLinks()
{
#if defined(__linux__)
    TemporaryDirectory sourceRoot;
    TemporaryDirectory destinationRoot("/dev/shm", "files-transfer-exdev-tree-test");
    if (deviceFor(sourceRoot.path()) == deviceFor(destinationRoot.path())) {
        throw TestSkipped("temporary directory and /dev/shm are on the same filesystem");
    }

    const fs::path source = sourceRoot.path() / "tree";
    writeFile(source / "nested" / "payload.txt", "nested-content");

    std::error_code error;
    fs::create_symlink("nested/payload.txt", source / "relative-link", error);
    if (error) {
        throw TestSkipped("cannot create relative symbolic link: " + error.message());
    }
    fs::create_directory_symlink(".", source / "cycle", error);
    if (error) {
        throw TestSkipped("cannot create circular symbolic link: " + error.message());
    }

    const auto result = movePathToDirectory(source, destinationRoot.path());
    require(static_cast<bool>(result), "cross-device directory move failed: " + result.detail);
    require(!existsNoFollow(source), "cross-device directory move left the source behind");
    require(readFile(result.destination / "nested" / "payload.txt") == "nested-content",
            "cross-device nested file content differs");
    require(fs::is_symlink(fs::symlink_status(result.destination / "relative-link")),
            "relative link was not preserved as a symbolic link");
    require(fs::read_symlink(result.destination / "relative-link") == fs::path("nested/payload.txt"),
            "relative link target changed");
    require(fs::is_symlink(fs::symlink_status(result.destination / "cycle")),
            "circular link was not preserved as a symbolic link");
    require(fs::read_symlink(result.destination / "cycle") == fs::path("."), "circular link target changed");
    requireNoStageDirectories(destinationRoot.path());
    requireNoMoveHoldEntries(sourceRoot.path());
#else
    throw TestSkipped("/dev/shm EXDEV directory test is Linux-specific");
#endif
}

void testCrossDeviceQuarantineFailurePreservesSource()
{
#if defined(__linux__)
    if (::geteuid() == 0) {
        throw TestSkipped("permission-based quarantine failure is not reliable as root");
    }

    TemporaryDirectory sourceRoot;
    TemporaryDirectory destinationRoot("/dev/shm", "files-transfer-exdev-cleanup-test");
    if (deviceFor(sourceRoot.path()) == deviceFor(destinationRoot.path())) {
        throw TestSkipped("temporary directory and /dev/shm are on the same filesystem");
    }

    const fs::path sourceParent = sourceRoot.path() / "locked-parent";
    const fs::path source       = sourceParent / "preserved.txt";
    writeFile(source, "preserved-content");

    std::error_code error;
    fs::permissions(sourceParent, fs::perms::owner_read | fs::perms::owner_exec, fs::perm_options::replace, error);
    require(!error, "cannot make source parent read-only: " + error.message());
    if (::access(sourceParent.c_str(), W_OK) == 0) {
        fs::permissions(sourceParent, fs::perms::owner_all, fs::perm_options::add, error);
        throw TestSkipped("read-only source parent remains writable in this environment");
    }

    const auto result = movePathToDirectory(source, destinationRoot.path());
    fs::permissions(sourceParent, fs::perms::owner_all, fs::perm_options::add, error);
    require(!error, "cannot restore source parent permissions: " + error.message());

    require(result.failure == FilesystemTransferFailure::SourceCleanupFailed,
            "quarantine failure did not report SourceCleanupFailed");
    require(readFile(source) == "preserved-content", "quarantine failure did not preserve the source");
    require(readFile(result.destination) == "preserved-content",
            "quarantine failure did not leave a complete destination");
    requireNoStageDirectories(destinationRoot.path());
    requireNoMoveHoldEntries(sourceParent);
#else
    throw TestSkipped("cross-device quarantine failure is Linux-specific");
#endif
}

class TestRunner {
public:
    void run(const char* name, const std::function<void()>& test)
    {
        try {
            test();
            ++_passed;
            std::cout << "[PASS] " << name << '\n';
        } catch (const TestSkipped& error) {
            ++_skipped;
            std::cout << "[SKIP] " << name << ": " << error.what() << '\n';
        } catch (const std::exception& error) {
            ++_failed;
            std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
        }
    }

    int finish() const
    {
        std::cout << "Summary: " << _passed << " passed, " << _skipped << " skipped, " << _failed << " failed\n";
        return _failed == 0 ? 0 : 1;
    }

private:
    int _passed  = 0;
    int _skipped = 0;
    int _failed  = 0;
};

}  // namespace

int main()
{
    spdlog::set_level(spdlog::level::off);

    TestRunner runner;
    runner.run("copy regular file", testCopyRegularFile);
    runner.run("copy directory tree", testCopyDirectoryTree);
    runner.run("copy empty file and directory", testCopyEmptyFileAndDirectory);
    runner.run("copy symbolic link", testCopySymbolicLink);
    runner.run("same-directory copy chooses names", testSameDirectoryCopyChoosesNames);
    runner.run("collision leaves both sides unchanged", testCollisionLeavesBothSidesUnchanged);
    runner.run("directory collision does not merge", testDirectoryCollisionDoesNotMerge);
    runner.run("concurrent publish has one winner", testConcurrentPublishHasOneWinner);
    runner.run("unsupported node aborts staged copy", testUnsupportedNodeAbortsStagedCopy);
    runner.run("unreadable nested directory cleans stage", testUnreadableNestedDirectoryCleansStage);
    runner.run("self and descendant transfers are rejected", testSelfAndDescendantTransfersAreRejected);
    runner.run("same-filesystem move", testSameFilesystemMove);
    runner.run("invalid destination preserves source", testInvalidDestinationPreservesSource);
    runner.run("non-EXDEV rename failure preserves source", testNonExdevRenameFailurePreservesSource);
    runner.run("real cross-device move", testRealCrossDeviceMove);
    runner.run("real cross-device directory move preserves links", testRealCrossDeviceDirectoryMovePreservesLinks);
    runner.run("cross-device quarantine failure preserves source", testCrossDeviceQuarantineFailurePreservesSource);
    return runner.finish();
}
