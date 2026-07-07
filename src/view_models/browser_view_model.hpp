#pragma once

#include "models/files_model.hpp"
#include "view_models/view_model.hpp"
#include <tools/observable/single_observable.hpp>

namespace files {

enum class BrowserAction {
    Open,
    Copy,
    Cut,
    Paste,
    Rename,
    Info,
    Delete,
};

class BrowserViewModel : public ViewModel {
public:
    BrowserViewModel(FilesRouter& router, FilesModel& model);

    PageId pageId() const override
    {
        return PageId::Browser;
    }

    void onEnter() override;
    void onKey(uint32_t key) override;
    void onKeyState(uint32_t key, bool pressed) override;
    void tick(uint32_t nowMs) override;

    smooth_ui_toolkit::SingleObservable<std::string>& currentDirectory()
    {
        return _model.browser().currentDirectory();
    }

    smooth_ui_toolkit::SingleObservable<std::vector<FileEntry>>& entries()
    {
        return _model.browser().entries();
    }

    smooth_ui_toolkit::SingleObservable<int>& selectedIndex()
    {
        return _model.browser().selectedIndex();
    }

    smooth_ui_toolkit::SingleObservable<std::string>& status()
    {
        return _model.browser().status();
    }

    smooth_ui_toolkit::SingleObservable<bool>& enterPressed()
    {
        return _enter_pressed;
    }

    smooth_ui_toolkit::SingleObservable<bool>& actionMenuOpen()
    {
        return _action_menu_open;
    }

    smooth_ui_toolkit::SingleObservable<int>& actionMenuSelectedIndex()
    {
        return _action_menu_selected_index;
    }

    smooth_ui_toolkit::SingleObservable<PendingDeleteFile>& pendingDelete()
    {
        return _pending_delete;
    }

    smooth_ui_toolkit::SingleObservable<PendingRenameFile>& pendingRename()
    {
        return _pending_rename;
    }

    smooth_ui_toolkit::SingleObservable<std::string>& pendingRenameName()
    {
        return _pending_rename_name;
    }

    smooth_ui_toolkit::SingleObservable<uint32_t>& magic()
    {
        return _magic;
    }

    const FileEntry* selectedEntry() const;
    int actionCount() const;
    BrowserAction actionAt(int index) const;
    void openSelected();
    void copySelectedToCurrentDirectory();
    void copySelected();
    void cutSelected();
    void pasteCopiedToCurrentDirectory();
    void showInfoSelected();
    void deleteSelected();
    void openActionMenu();
    void closeActionMenu();
    void requestDeleteSelected();
    void cancelDelete();
    void confirmDelete();
    void requestRenameSelected();
    void setPendingRenameName(std::string name);
    void cancelRename();
    void confirmRename();

private:
    FilesModel& _model;
    smooth_ui_toolkit::SingleObservable<bool> _enter_pressed{false};
    smooth_ui_toolkit::SingleObservable<bool> _action_menu_open{false};
    smooth_ui_toolkit::SingleObservable<int> _action_menu_selected_index{0};
    smooth_ui_toolkit::SingleObservable<PendingDeleteFile> _pending_delete{PendingDeleteFile{}};
    smooth_ui_toolkit::SingleObservable<PendingRenameFile> _pending_rename{PendingRenameFile{}};
    smooth_ui_toolkit::SingleObservable<std::string> _pending_rename_name{""};
    PendingCopyFile _pending_copy;
    smooth_ui_toolkit::SingleObservable<uint32_t> _magic{0};
    int _held_selection_direction  = 0;
    uint32_t _next_selection_at_ms = 0;
    uint32_t _magic_count          = 0;

    void selectPreviousAction();
    void selectNextAction();
    void activateSelectedAction();
    void selectByDirection(int direction);
    void clearHeldSelection();
    bool canGenerateMagic() const;
    void generateMagic();
};

}  // namespace files
