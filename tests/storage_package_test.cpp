#include "models/removable_volumes.hpp"
#include "models/file_browser_model.hpp"
#include "models/filesystem_transfer.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace files;
using namespace files::internal;

namespace {

void check(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct TempDirectory {
    fs::path path;

    TempDirectory()
    {
        std::string directory = "/tmp/files-storage-test-XXXXXX";
        check(mkdtemp(directory.data()) != nullptr, "cannot create fixture directory");
        path = directory;
    }

    ~TempDirectory()
    {
        std::error_code error;
        fs::remove_all(path, error);
    }
};

void testMountList(const fs::path& temp)
{
    std::istringstream raw(R"(garbage
61 40 8:1 / /media/pi/Žluté\040USB rw,nosuid shared:22 - vfat /dev/sda1 rw
62 40 8:16 / /media/pi/Fixed rw - ext4 /dev/sdb rw
63 40 8:32 / /mnt/LaptopSSD rw - ext4 /dev/sdc rw
64 40 7:0 / /media/pi/Test rw - ext4 /dev/loop0 rw
65 40 179:9 / /run/media/pi/SD\134Cards rw - ext4 /dev/mmcblk0p1 rw
66 40 0:40 / /media/pi/Remote rw - nfs server:/work rw
67 40 8:32 / /media/other/Private rw - ext4 /dev/sdc rw
68 40 8:1 / /media/pi/Incomplete
69 40 8:32 / /media/Elsewhere rw - ext4 /dev/sdc rw
)");
    const auto mounts = readMountInfo(raw);
    check(mounts.size() == 8, "malformed mount rows must be ignored");
    check(mounts.front().target == "/media/pi/Žluté USB", "mount path with spaces was not decoded");
    check(mounts[4].target == "/run/media/pi/SD\\Cards", "literal backslash was not decoded");

    const auto root = temp / "sys";
    fs::create_directories(root / "dev/block");
    const auto block = [&](const char* number, const char* device) {
        const auto path = root / device;
        fs::create_directories(path);
        fs::create_symlink(path, root / "dev/block" / number);
        return path;
    };
    const auto usbPartition = block("8:1", "devices/usb1/1-1/host0/block/sda/sda1");
    std::ofstream(usbPartition / "partition") << "1\n";
    std::ofstream(usbPartition.parent_path() / "removable") << "0\n";
    fs::create_symlink("../../bus/usb", root / "devices/usb1/subsystem");
    std::ofstream(block("8:16", "devices/block/sdb") / "removable") << "0\n";
    std::ofstream(block("8:32", "devices/usb1/1-2/host1/block/sdc") / "removable") << "0\n";
    block("7:0", "devices/virtual/block/loop0");
    const auto sdPartition = block("179:9", "devices/block/mmcblk0/mmcblk0p1");
    std::ofstream(sdPartition / "partition") << "1\n";
    std::ofstream(sdPartition.parent_path() / "removable") << "1\n";

    const auto volumes = removableVolumes(mounts, "/media/pi", root);
    check(volumes.size() == 4, "USB, SD and loop mounts should appear, but not fixed or unrelated mounts");
    const auto& usb = volumes.back();
    check(usb.mount.id == 61 && usb.mount.source == "/dev/sda1" && usb.disk == "/dev/sda" && usb.powerOff,
          "USB partition must map to the correct parent, including disks with RM=0");
    check(volumeContains(usb, "/media/pi/Žluté USB/folder/item"), "child was not recognized");
    check(!volumeContains(usb, "/media/pi/Žluté USB2/file"), "similar prefix matched another drive");
    check(!volumes[1].powerOff && !volumes[2].powerOff, "SD and loop devices should only be unmounted");
}

void selectByName(FileBrowserModel& browser, const std::string& name)
{
    for (size_t i = 0; i < browser.entries().get().size(); ++i) {
        const auto* entry = browser.selectedEntry();
        if (entry && entry->name == name) {
            return;
        }
        browser.selectNext();
    }
    throw std::runtime_error("Missing browser entry: " + name);
}

void testDisconnectDuringCopy(const fs::path& mount, const fs::path& temp)
{
    const auto volumes = removableVolumes();
    const auto volume = std::find_if(volumes.begin(), volumes.end(), [&](const auto& v) {
        return v.mount.target == mount;
    });
    check(volume != volumes.end(), "temporary mount not discovered");
    const TransferDirectory destination(mount, volume->mount.id);
    check(destination.connected(), "failed to pin the test mount");
    const auto source = temp / "keep-source.txt";
    std::ofstream(source) << "detect loss before publishing\n";
    bool copiedIntoStage = false;
    const auto copied = copyPathToDirectory(source, destination.path(), [&] {
        for (const auto& item : fs::directory_iterator(mount)) {
            if (item.path().filename().string().rfind(".files-transfer-", 0) == 0) {
                copiedIntoStage = fs::is_regular_file(item.path() / source.filename());
                break;
            }
        }
        const auto detached = runCommand({"/usr/bin/umount", "--lazy", mount.string()});
        return detached && destination.connected();
    });
    check(copiedIntoStage && copied.failure == FilesystemTransferFailure::DriveDisconnected &&
              copied.destinationState == TransferDestination::PartialRemoved &&
              fs::is_regular_file(source) && !fs::exists(mount / source.filename()) &&
              !fs::exists(destination.path() / source.filename()), "copy was published after disconnection");
    for (const auto& item : fs::directory_iterator(destination.path())) {
        check(item.path().filename().string().rfind(".files-transfer-", 0) != 0, "staged copy was left behind");
    }
    std::cout << "Real mount detached between staging and publication; source kept and no destination residue\n";
}

void testLoop(const fs::path& mount, const fs::path& temp)
{
    FileBrowserModel browser(temp.string(), {});
    std::ofstream(temp / "na disk č. 1.txt") << "written from local storage\n";
    std::ofstream(mount / "z disku č. 2.txt") << "written from mounted image\n";
    browser.refresh();
    selectByName(browser, "na disk č. 1.txt");
    const FileEntry local = *browser.selectedEntry();
    const auto arrival = browser.updateVolumes(removableVolumes());
    check(std::any_of(arrival.arrived.begin(), arrival.arrived.end(), [&](const auto& drive) {
        return drive.mount.target == mount;
    }), "hotplug did not report the loop image");
    browser.showPlaces();
    selectByName(browser, mount.filename().string());
    const FileEntry root = *browser.selectedEntry();
    check(root.mountRoot && root.mountId && root.freeBytes.has_value(), "drive should show a name and free space");
    check(!browser.deleteSelected() && !browser.renameSelectedTo("renamed"), "mounted root can be modified");
    check(static_cast<bool>(browser.openSelected()), "could not open mounted volume");
    auto copied = browser.copyEntryTo(local, mount.string());
    check(static_cast<bool>(copied) && fs::exists(mount / "na disk č. 1.txt"), copied.message.c_str());
    selectByName(browser, "z disku č. 2.txt");
    const FileEntry remote = *browser.selectedEntry();
    check(remote.mountId == root.mountId && !remote.mountRoot, "file should retain removable mount identity");
    check(static_cast<bool>(browser.goBack()) && browser.inPlaces(), "drive Escape must return to Places");
    check(browser.selectedEntry() && browser.selectedEntry()->mountId == root.mountId,
          "returning from a drive should keep that drive selected");
    check(!browser.canGoBack(), "Places must be the navigation root");
    copied = browser.copyEntryTo(remote, temp.string());
    check(static_cast<bool>(copied) && fs::exists(temp / "z disku č. 2.txt"), copied.message.c_str());

    check(static_cast<bool>(browser.openRemovableVolumes()) && browser.currentMountId() == root.mountId,
          "single-drive shortcut could not open the connected drive");
    check(fs::create_directory(mount / "Podadresář"), "could not create test subdirectory");
    browser.refresh();
    selectByName(browser, "Podadresář");
    check(static_cast<bool>(browser.openSelected()), "could not open drive subdirectory");
    check(static_cast<bool>(browser.goBack()) && browser.currentDirectory().get() == mount.string() &&
              static_cast<bool>(browser.goBack()) && browser.inPlaces(), "nested Esc must walk back to Places");

    const auto moveSource = temp / "přesunutý soubor.txt";
    std::ofstream(moveSource) << "move across filesystems\n";
    files::FileEntry toMove;
    toMove.path = moveSource.string();
    toMove.name = moveSource.filename().string();
    const auto moved = browser.moveEntryTo(toMove, mount.string());
    check(static_cast<bool>(moved) && !fs::exists(moveSource) && fs::exists(mount / moveSource.filename()),
          moved.message.c_str());
    check(static_cast<bool>(browser.openVolume(root.mountId)), "could not reopen the drive before removal");

    // The caller owns the temporary loop mount, allowing an actual dismount after Files releases its descriptors.
    const auto detached = runCommand({"/usr/bin/umount", mount.string()});
    check(static_cast<bool>(detached), detached.output.c_str());
    const auto departure = browser.updateVolumes(removableVolumes());
    check(departure.leftVolume && departure.removed.size() == 1 && browser.inPlaces(),
          "unmount must report the removal and return active browsing to Places");
    std::ofstream(mount / "z disku č. 2.txt") << "underlying path; must not be copied\n";
    const auto lost = FileBrowserModel::performTransfer(remote, temp.string(), 0, false);
    check(!lost.operation && lost.operation.message == "Drive disconnected" &&
              lost.disconnectedSource == root.mountId && !lost.disconnectedDestination &&
              lost.destinationState == TransferDestination::NotPublished &&
              !fs::exists(temp / "z disku č. 2 copy.txt"),
          "a disconnected mount must never fall through to the underlying filesystem");
    std::cout << "Loop image visible, copy in both directions, unmount and stale clipboard verified\n";
}

}  // namespace

int main(int argc, char** argv)
{
    try {
        TempDirectory temp;
        if (argc == 3) {
            testDisconnectDuringCopy(argv[1], temp.path);
        } else if (argc == 2) {
            testLoop(argv[1], temp.path);
        } else {
            testMountList(temp.path);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
