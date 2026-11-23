#include "models/command.hpp"
#include "models/file_browser_model.hpp"
#include "models/filesystem_transfer.hpp"

#include <cerrno>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace files;
using namespace files::internal;

static std::string syncFault;
static unsigned syncCalls;
extern "C" int __real_fsync(int);
extern "C" int __wrap_fsync(int fd)
{
    ++syncCalls;
    const auto path = fs::read_symlink("/proc/self/fd/" + std::to_string(fd));
    const bool fault = syncFault == "parent" ? path.filename() == "Cíl bez jmenovky"
                     : syncFault == "file" ? fs::is_regular_file(path)
                     : syncFault == "directory" && path.filename() == "Jiný svazek";
    if (fault) {
        errno = EIO;
        return -1;
    }
    return __real_fsync(fd);
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        return 2;
    }
    const std::string mode = argv[1];
    if (mode == "pipe") {
        const auto start = std::chrono::steady_clock::now();
        const auto command = runCommand({"/bin/sh", "-c", "sleep 3 & printf complete"});
        const auto elapsed = std::chrono::steady_clock::now() - start;
        std::cout << "elapsed(ms)=" << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() << '\n';
        return command && command.output == "complete" && elapsed < std::chrono::seconds(1) ? 0 : 1;
    }
    const fs::path source = argv[2];
    const fs::path destination = argv[3];
    if (mode == "space" || mode == "readonly" || mode == "permission") {
        FileBrowserModel browser(source.parent_path().string());
        FileEntry entry;
        entry.path = source.string();
        entry.name = source.filename().string();
        const auto copied = browser.copyEntryTo(entry, destination.string());
        std::cout << copied.message << '\n';
        const char* expected = mode == "space" ? "Disk is full"
                             : mode == "readonly" ? "Disk is read-only" : "Permission denied";
        return !copied && copied.message.find(expected) != std::string::npos ? 0 : 1;
    }
    if (mode == "sync-file" || mode == "sync-directory" || mode == "sync-parent") {
        syncFault = mode.substr(5);
    }
    const auto moved = movePathToDirectory(source, destination);
    std::cout << moved.detail << " fsync calls=" << syncCalls << '\n';
    if (mode == "move") {
        return moved && !fs::exists(source) && fs::exists(destination / source.filename()) ? 0 : 1;
    }
    if (mode == "mount" || mode == "bind") {
        return !moved && moved.detail.find("mounted") != std::string::npos &&
               fs::exists(source / "Jiný svazek/chráněný.txt") && !fs::exists(destination / source.filename()) ? 0 : 1;
    }
    return !moved && syncCalls > 0 && fs::exists(source / "Příliš žluťoučký.txt") ? 0 : 1;
}
