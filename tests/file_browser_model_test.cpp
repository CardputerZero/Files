#include "models/file_browser_model.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>

namespace {

namespace fs = std::filesystem;

void require(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

struct TemporaryTree {
    TemporaryTree()
    {
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        root             = fs::temp_directory_path() / ("files-browser-model-" + std::to_string(nonce));
        std::error_code error;
        fs::create_directories(root, error);
        require(!error, "failed to create temporary test directory");
    }

    ~TemporaryTree()
    {
        std::error_code error;
        fs::remove_all(root, error);
    }

    fs::path root;
};

void writeFile(const fs::path& path, const std::string& content = "test\n")
{
    std::ofstream output(path, std::ios::binary);
    require(static_cast<bool>(output), "failed to create test file");
    output << content;
}

int indexOf(files::FileBrowserModel& model, const std::string& name)
{
    const auto& entries = model.entries().get();
    for (size_t index = 0; index < entries.size(); ++index) {
        if (entries[index].name == name) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

void selectName(files::FileBrowserModel& model, const std::string& name)
{
    model.refresh(false);
    const int index = indexOf(model, name);
    require(index >= 0, "requested test entry does not exist");
    for (int current = 0; current < index; ++current) {
        model.selectNext();
    }
    require(model.selectedIndex().get() == index, "failed to select requested test entry");
}

void testPlacesAndVolumeChanges()
{
    TemporaryTree tree;
    const auto diskPath = tree.root / "Příručka";
    const auto start = tree.root / "start" / "nested";
    fs::create_directories(diskPath);
    fs::create_directories(start);
    const files::internal::RemovableVolume disk{
        {101, "7:10", "/", diskPath, "/dev/loop10"}, "/dev/loop10", diskPath.filename().string()
    };

    files::FileBrowserModel startedWithDisk(start.string(), {disk});
    require(startedWithDisk.inPlaces() && !startedWithDisk.canGoBack(), "start on Places when a disk is mounted");
    const auto& places = startedWithDisk.entries().get();
    require(places.size() == 3 && places[0].mountRoot && places[0].name == disk.name &&
                places[0].freeBytes.has_value() && places[1].name == "Home" && places[2].name == "File system" &&
                startedWithDisk.selectedIndex().get() == 0,
            "Places must select the drive before Home and File system");
    require(startedWithDisk.hasRemovableVolumes(), "a mounted drive must enable the USB shortcut");
    const auto unchanged = startedWithDisk.updateVolumes({disk});
    require(unchanged.arrived.empty() && unchanged.removed.empty(), "no notification for a disk present at startup");

    files::FileBrowserModel startedWithoutDisk(start.string(), {});
    startedWithoutDisk.refresh();
    require(!startedWithoutDisk.hasRemovableVolumes() && !startedWithoutDisk.openRemovableVolumes(),
            "USB shortcut without a drive must leave navigation alone");
    require(startedWithoutDisk.currentDirectory().get() == start.string(), "retain the start folder without a disk");
    const auto connected = startedWithoutDisk.updateVolumes({disk});
    require(connected.arrived.size() == 1 && connected.arrived.front().mount.id == 101 &&
                startedWithoutDisk.currentDirectory().get() == start.string(),
            "hotplug exposes the new mount without interrupting the current folder");

    while (startedWithoutDisk.canGoBack()) {
        const auto before = fs::path(startedWithoutDisk.currentDirectory().get());
        require(static_cast<bool>(startedWithoutDisk.goBack()), "Escape must reach Places");
        if (!startedWithoutDisk.inPlaces()) {
            require(startedWithoutDisk.currentDirectory().get() == before.parent_path(), "Escape must walk up one level");
        }
    }
    require(startedWithoutDisk.inPlaces() && startedWithoutDisk.selectedEntry()->name == "File system",
            "the file system root must return to Places");
    require(!startedWithoutDisk.goBack(), "Places is the final navigation level");
    require(static_cast<bool>(startedWithoutDisk.openSelected()) &&
                startedWithoutDisk.currentDirectory().get() == "/" &&
                static_cast<bool>(startedWithoutDisk.goBack()) && startedWithoutDisk.inPlaces(),
            "opening the file system from Places must return there");

    auto remounted = disk;
    remounted.mount.id = 102;
    const auto replaced = startedWithoutDisk.updateVolumes({remounted});
    require(replaced.removed.size() == 1 && replaced.removed.front().mount.id == 101 &&
                replaced.arrived.size() == 1 && replaced.arrived.front().mount.id == 102 &&
                !startedWithoutDisk.volume(101) && !startedWithoutDisk.openVolume(101),
            "a new mount at the same path must invalidate the original notification");
    auto second = disk;
    second.mount.id = 103;
    second.mount.target = tree.root / "Druhý disk";
    second.name = second.mount.target.filename().string();
    fs::create_directory(second.mount.target);
    files::FileBrowserModel multiple(start.string(), {disk, second});
    selectName(multiple, "Home");
    require(static_cast<bool>(multiple.openRemovableVolumes()) && multiple.inPlaces() &&
                multiple.selectedEntry()->mountId == disk.mount.id, "multi-drive U should focus a drive in Places");
    multiple.selectNext();
    require(static_cast<bool>(multiple.openRemovableVolumes()) && multiple.selectedEntry()->mountId == second.mount.id,
            "multi-drive U should preserve the selected drive");

    const auto disconnected = startedWithoutDisk.updateVolumes({});
    require(disconnected.removed.size() == 1 && disconnected.arrived.empty() &&
                startedWithoutDisk.inPlaces() && indexOf(startedWithoutDisk, disk.name) < 0,
            "removed drive must disappear from Places");
}

void testPaging()
{
    TemporaryTree tree;
    for (int index = 0; index < 11; ++index) {
        writeFile(tree.root / (std::to_string(index) + ".txt"));
    }

    files::FileBrowserModel model(tree.root.string());
    model.refresh();
    require(model.entries().get().size() == 11, "unexpected entry count");
    require(model.selectedIndex().get() == 0, "refresh did not select the first entry");

    model.selectPageDown(4);
    require(model.selectedIndex().get() == 4, "page down did not keep one row of context");
    model.selectPageDown(4);
    require(model.selectedIndex().get() == 8, "second page down selected the wrong entry");
    model.selectPageDown(4);
    require(model.selectedIndex().get() == 10, "page down did not clamp at the last entry");
    model.selectPageUp(4);
    require(model.selectedIndex().get() == 6, "page up selected the wrong entry");
    model.selectPageUp(99);
    require(model.selectedIndex().get() == 0, "page up did not clamp at the first entry");
    model.selectPageDown(0);
    require(model.selectedIndex().get() == 1, "non-positive page size was not clamped");
    model.selectPageDown(std::numeric_limits<int>::max());
    require(model.selectedIndex().get() == 10, "large page size did not clamp at the last entry");
}

void testPreviewSelectionRestoration()
{
    TemporaryTree tree;
    writeFile(tree.root / "first.txt");
    writeFile(tree.root / "second.txt");

    files::FileBrowserModel model(tree.root.string());
    selectName(model, "second.txt");

    files::FileEntry opened;
    require(static_cast<bool>(model.openSelected(&opened)), "opening a regular file failed");
    require(opened.name == "second.txt", "opened file metadata was not returned");
    model.refresh(true);
    require(model.selectedEntry() && model.selectedEntry()->name == "second.txt",
            "refresh did not restore the selected preview file");
}

void testDirectorySelectionRestoration()
{
    TemporaryTree tree;
    fs::create_directories(tree.root / "alpha");
    fs::create_directories(tree.root / "beta");
    writeFile(tree.root / "alpha" / "inside.txt");

    files::FileBrowserModel model(tree.root.string());
    selectName(model, "beta");
    require(static_cast<bool>(model.openSelected()), "opening a directory failed");
    require(model.currentDirectory().get() == fs::canonical(tree.root / "beta").string(),
            "directory target was not canonicalized");
    require(static_cast<bool>(model.goBack()), "returning from a directory failed");
    require(model.selectedEntry() && model.selectedEntry()->name == "beta",
            "returning from a directory did not restore the opened entry");

#if !defined(_WIN32)
    std::error_code error;
    fs::create_directory_symlink(tree.root / "alpha", tree.root / "linked-alpha", error);
    if (!error) {
        selectName(model, "linked-alpha");
        require(static_cast<bool>(model.openSelected()), "opening a symlinked directory failed");
        require(static_cast<bool>(model.goBack()), "returning from a symlinked directory failed");
        require(model.selectedEntry() && model.selectedEntry()->name == "linked-alpha",
                "returning from a symlinked directory did not restore the link entry");
    }
#endif
}

void testRelativeDirectoryAndFailedBack()
{
    TemporaryTree tree;
    fs::create_directories(tree.root / "child");
    files::FileBrowserModel model(tree.root.string());

    require(static_cast<bool>(model.goToDirectory("child")), "relative directory was not resolved from current path");
    require(model.currentDirectory().get() == fs::canonical(tree.root / "child").string(),
            "relative directory resolved against the process cwd");

    std::error_code error;
    fs::remove_all(tree.root, error);
    require(!error, "failed to remove directory for history failure test");
    const auto failed = model.goBack();
    require(!failed, "goBack unexpectedly succeeded after its target was removed");

    fs::create_directories(tree.root / "child");
    require(static_cast<bool>(model.goBack()), "goBack did not retain history after a failed navigation");
    require(model.currentDirectory().get() == fs::canonical(tree.root).string(),
            "goBack did not return to the original directory after recovery");
}

void testDeleteSelectsNeighbor()
{
    TemporaryTree tree;
    for (const auto* name : {"01.txt", "02.txt", "03.txt"}) {
        writeFile(tree.root / name);
    }
    files::FileBrowserModel model(tree.root.string(), {});
    selectName(model, "02.txt");
    require(static_cast<bool>(model.deleteSelected()), "failed to delete middle entry");
    require(model.selectedEntry() && model.selectedEntry()->name == "03.txt",
            "deleting a middle entry did not select the following neighbor");
    require(static_cast<bool>(model.deleteSelected()), "failed to delete last entry");
    require(model.selectedEntry() && model.selectedEntry()->name == "01.txt",
            "deleting the last entry did not select the preceding neighbor");
}

void testRefreshFailurePreservesListing()
{
    TemporaryTree tree;
    writeFile(tree.root / "first.txt");
    writeFile(tree.root / "second.txt");

    files::FileBrowserModel model(tree.root.string());
    selectName(model, "second.txt");
    const auto before = model.entries().get();

    std::error_code error;
    fs::remove_all(tree.root, error);
    require(!error, "failed to remove directory for refresh failure test");

    model.refresh(true);
    require(model.entries().get().size() == before.size(), "failed refresh cleared the existing listing");
    require(model.selectedEntry() && model.selectedEntry()->name == "second.txt",
            "failed refresh changed the existing selection");
    require(!model.status().get().empty() && model.status().get() != "Ready" && model.status().get() != "Empty folder",
            "failed refresh did not expose an error status");
}

}  // namespace

int main()
{
    testPlacesAndVolumeChanges();
    testPaging();
    testPreviewSelectionRestoration();
    testDirectorySelectionRestoration();
    testRelativeDirectoryAndFailedBack();
    testRefreshFailurePreservesListing();
    testDeleteSelectsNeighbor();
    return 0;
}
