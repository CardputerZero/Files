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
    testPaging();
    testPreviewSelectionRestoration();
    testDirectorySelectionRestoration();
    testRelativeDirectoryAndFailedBack();
    testRefreshFailurePreservesListing();
    return 0;
}
