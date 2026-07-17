#pragma once

#include "core/files_types.hpp"
#include "models/file_type_registry.hpp"
#include <filesystem>
#include <tools/observable/single_observable.hpp>
#include <string>
#include <vector>

namespace files {

class FileBrowserModel {
public:
    explicit FileBrowserModel(std::string start_directory = {});

    smooth_ui_toolkit::SingleObservable<std::string>& currentDirectory()
    {
        return _current_directory;
    }

    smooth_ui_toolkit::SingleObservable<std::vector<FileEntry>>& entries()
    {
        return _entries;
    }

    smooth_ui_toolkit::SingleObservable<int>& selectedIndex()
    {
        return _selected_index;
    }

    smooth_ui_toolkit::SingleObservable<std::string>& status()
    {
        return _status;
    }

    const FileEntry* selectedEntry() const;
    FileEntry entryWithMetadata(const FileEntry& entry) const;
    void refresh(bool preserveSelected = true);
    void selectPrevious();
    void selectNext();
    FileOperationResult openSelected(FileEntry* openedFile = nullptr);
    FileOperationResult goBack();
    FileOperationResult goToDirectory(const std::string& path, bool pushHistory = true);
    FileOperationResult copyEntryTo(const FileEntry& entry, const std::string& destinationDirectory);
    FileOperationResult copySelectedTo(const std::string& destinationDirectory);
    FileOperationResult moveEntryTo(const FileEntry& entry, const std::string& destinationDirectory);
    FileOperationResult renameSelectedTo(const std::string& name);
    FileOperationResult deleteSelected();

private:
    struct HistoryEntry {
        std::string directory;
        std::string selectedPath;
    };

    FileTypeRegistry _file_types;
    smooth_ui_toolkit::SingleObservable<std::string> _current_directory;
    smooth_ui_toolkit::SingleObservable<std::vector<FileEntry>> _entries{std::vector<FileEntry>{}};
    smooth_ui_toolkit::SingleObservable<int> _selected_index{-1};
    smooth_ui_toolkit::SingleObservable<std::string> _status{""};
    std::vector<HistoryEntry> _history;

    FileEntry makeEntry(const std::filesystem::directory_entry& item, bool includeMetadata) const;
    void setEntries(std::vector<FileEntry> entries, const std::string& preferredPath);
    void refreshSelecting(const std::string& preferredPath);
    FileOperationResult goToDirectory(const std::string& path, bool pushHistory, const std::string& preferredPath);
};

}  // namespace files
