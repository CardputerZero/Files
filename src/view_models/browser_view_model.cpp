#include "view_models/browser_view_model.hpp"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <iterator>

namespace files {
namespace {

constexpr BrowserAction kActionsWithoutPaste[] = {
    BrowserAction::Open, BrowserAction::Info,   BrowserAction::Copy,
    BrowserAction::Cut,  BrowserAction::Rename, BrowserAction::Delete,
};

constexpr BrowserAction kActionsWithPaste[] = {
    BrowserAction::Open,  BrowserAction::Info,   BrowserAction::Copy,   BrowserAction::Cut,
    BrowserAction::Paste, BrowserAction::Rename, BrowserAction::Delete,
};

constexpr BrowserAction kPasteOnlyActions[] = {
    BrowserAction::Paste,
};

constexpr uint32_t kHoldRepeatDelayMs = 320;
constexpr uint32_t kHoldRepeatMs      = 90;

}  // namespace

BrowserViewModel::BrowserViewModel(FilesRouter& router, FilesModel& model) : ViewModel(router), _model(model)
{
}

void BrowserViewModel::onEnter()
{
    clearHeldSelection();
    _magic_count = 0;
    spdlog::info("BrowserViewModel enter: {}", _model.browser().currentDirectory().get());
    _model.browser().refresh(true);
}

void BrowserViewModel::onKey(uint32_t key)
{
    if (_pending_rename.get().active) {
        switch (key) {
            case '\x1b':
            case files_key::Left:
                cancelRename();
                break;
            case '\r':
            case '\n':
                confirmRename();
                break;
            default:
                break;
        }
        return;
    }

    if (_pending_delete.get().active) {
        switch (key) {
            case '\x1b':
            case files_key::Left:
                cancelDelete();
                break;
            case '\r':
            case '\n':
                confirmDelete();
                break;
            default:
                break;
        }
        return;
    }

    if (_action_menu_open.get()) {
        switch (key) {
            case files_key::Up:
                selectPreviousAction();
                break;
            case files_key::Down:
                selectNextAction();
                break;
            case '\x1b':
            case files_key::Left:
            case '\t':
                closeActionMenu();
                break;
            case '\r':
            case '\n':
                activateSelectedAction();
                break;
            default:
                break;
        }
        return;
    }

    switch (key) {
        case files_key::Up:
            _model.browser().selectPrevious();
            break;
        case files_key::Down:
            _model.browser().selectNext();
            break;
        case files_key::Left:
        case '\x1b':
            _model.browser().goBack();
            break;
        case '\r':
        case '\n':
            openSelected();
            break;
        case '\t':
            openActionMenu();
            break;
        case ' ':
            if (canGenerateMagic()) {
                ++_magic_count;
                if (_magic_count >= 3) {
                    _magic_count = 0;
                    generateMagic();
                }
            } else {
                _magic_count = 0;
            }
            break;
        default:
            break;
    }
}

void BrowserViewModel::onKeyState(uint32_t key, bool pressed)
{
    if ((key == '\r' || key == '\n') && !_pending_delete.get().active && !_pending_rename.get().active) {
        _enter_pressed.set(pressed);
        return;
    }

    int direction = 0;
    switch (key) {
        case files_key::Up:
            direction = -1;
            break;
        case files_key::Down:
            direction = 1;
            break;
        default:
            return;
    }

    if (pressed) {
        if (_pending_delete.get().active || _pending_rename.get().active) {
            clearHeldSelection();
            return;
        }
        _held_selection_direction = direction;
        _next_selection_at_ms     = 0;
    } else if (_held_selection_direction == direction) {
        clearHeldSelection();
    }
}

void BrowserViewModel::tick(uint32_t nowMs)
{
    if (_pending_delete.get().active || _pending_rename.get().active) {
        clearHeldSelection();
        return;
    }

    if (_held_selection_direction != 0 && _next_selection_at_ms == 0) {
        _next_selection_at_ms = nowMs + kHoldRepeatDelayMs;
        return;
    }

    if (_held_selection_direction != 0 && nowMs >= _next_selection_at_ms) {
        selectByDirection(_held_selection_direction);
        _next_selection_at_ms = nowMs + kHoldRepeatMs;
    }
}

const FileEntry* BrowserViewModel::selectedEntry() const
{
    return _model.browser().selectedEntry();
}

int BrowserViewModel::actionCount() const
{
    if (!selectedEntry()) {
        return _pending_copy.active ? static_cast<int>(std::size(kPasteOnlyActions)) : 0;
    }
    return _pending_copy.active ? static_cast<int>(std::size(kActionsWithPaste))
                                : static_cast<int>(std::size(kActionsWithoutPaste));
}

BrowserAction BrowserViewModel::actionAt(int index) const
{
    if (actionCount() <= 0) {
        return BrowserAction::Open;
    }
    const int clamped = std::clamp(index, 0, actionCount() - 1);
    if (!selectedEntry()) {
        return kPasteOnlyActions[clamped];
    }
    return _pending_copy.active ? kActionsWithPaste[clamped] : kActionsWithoutPaste[clamped];
}

void BrowserViewModel::openSelected()
{
    FileEntry openedFile;
    const FileOperationResult result = _model.browser().openSelected(&openedFile);
    if (!result) {
        _model.browser().status().set(result.message);
        return;
    }

    if (!openedFile.path.empty()) {
        if (_model.preview().open(openedFile)) {
            _router.push(PageId::Preview);
        }
    }
}

void BrowserViewModel::copySelectedToCurrentDirectory()
{
    const auto result = _model.browser().copySelectedTo(_model.browser().currentDirectory().get());
    if (!result) {
        _model.browser().status().set(result.message);
    }
}

void BrowserViewModel::copySelected()
{
    const FileEntry* selected = selectedEntry();
    if (!selected) {
        return;
    }

    _pending_copy = PendingCopyFile{true, *selected};
    _model.browser().status().set("Copied " + selected->name);
}

void BrowserViewModel::cutSelected()
{
    const FileEntry* selected = selectedEntry();
    if (!selected) {
        return;
    }

    _pending_copy = PendingCopyFile{true, *selected, true};
    _model.browser().status().set("Cut " + selected->name);
}

void BrowserViewModel::pasteCopiedToCurrentDirectory()
{
    if (!_pending_copy.active) {
        _model.browser().status().set("Nothing to paste");
        return;
    }

    const auto result =
        _pending_copy.cut ? _model.browser().moveEntryTo(_pending_copy.file, _model.browser().currentDirectory().get())
                          : _model.browser().copyEntryTo(_pending_copy.file, _model.browser().currentDirectory().get());
    if (!result) {
        _model.browser().status().set(result.message);
        return;
    }
    if (_pending_copy.cut) {
        _pending_copy = PendingCopyFile{};
    }
}

void BrowserViewModel::showInfoSelected()
{
    const FileEntry* selected = selectedEntry();
    if (!selected) {
        return;
    }

    const FileEntry detailed = _model.browser().entryWithMetadata(*selected);
    if (_model.preview().openInfo(detailed)) {
        _router.push(PageId::Preview);
    }
}

void BrowserViewModel::deleteSelected()
{
    const auto result = _model.browser().deleteSelected();
    if (!result) {
        _model.browser().status().set(result.message);
    }
}

void BrowserViewModel::openActionMenu()
{
    if (actionCount() <= 0) {
        return;
    }
    _action_menu_selected_index.set(0);
    _action_menu_open.set(true);
}

void BrowserViewModel::closeActionMenu()
{
    _action_menu_open.set(false);
}

void BrowserViewModel::requestDeleteSelected()
{
    const FileEntry* selected = selectedEntry();
    if (!selected) {
        return;
    }

    _pending_delete.set(PendingDeleteFile{true, *selected});
    closeActionMenu();
}

void BrowserViewModel::cancelDelete()
{
    _pending_delete.set(PendingDeleteFile{});
}

void BrowserViewModel::confirmDelete()
{
    if (!_pending_delete.get().active) {
        return;
    }

    _pending_delete.set(PendingDeleteFile{});
    deleteSelected();
}

void BrowserViewModel::requestRenameSelected()
{
    const FileEntry* selected = selectedEntry();
    if (!selected) {
        return;
    }

    _pending_rename.set(PendingRenameFile{true, *selected, selected->name});
    _pending_rename_name.set(selected->name);
    closeActionMenu();
}

void BrowserViewModel::setPendingRenameName(std::string name)
{
    if (!_pending_rename.get().active || _pending_rename_name.get() == name) {
        return;
    }
    _pending_rename_name.set(std::move(name));
}

void BrowserViewModel::cancelRename()
{
    _pending_rename.set(PendingRenameFile{});
    _pending_rename_name.set("");
}

void BrowserViewModel::confirmRename()
{
    PendingRenameFile pending = _pending_rename.get();
    if (!pending.active) {
        return;
    }

    const auto result = _model.browser().renameSelectedTo(_pending_rename_name.get());
    if (!result) {
        _model.browser().status().set(result.message);
        return;
    }
    _pending_rename.set(PendingRenameFile{});
    _pending_rename_name.set("");
}

void BrowserViewModel::selectPreviousAction()
{
    int next = _action_menu_selected_index.get() - 1;
    if (next < 0) {
        next = actionCount() - 1;
    }
    _action_menu_selected_index.set(next);
}

void BrowserViewModel::selectNextAction()
{
    int next = _action_menu_selected_index.get() + 1;
    if (next >= actionCount()) {
        next = 0;
    }
    _action_menu_selected_index.set(next);
}

void BrowserViewModel::activateSelectedAction()
{
    switch (actionAt(_action_menu_selected_index.get())) {
        case BrowserAction::Open:
            closeActionMenu();
            openSelected();
            break;
        case BrowserAction::Copy:
            closeActionMenu();
            copySelected();
            break;
        case BrowserAction::Cut:
            closeActionMenu();
            cutSelected();
            break;
        case BrowserAction::Paste:
            closeActionMenu();
            pasteCopiedToCurrentDirectory();
            break;
        case BrowserAction::Rename:
            requestRenameSelected();
            break;
        case BrowserAction::Info:
            closeActionMenu();
            showInfoSelected();
            break;
        case BrowserAction::Delete:
            requestDeleteSelected();
            break;
    }
}

void BrowserViewModel::selectByDirection(int direction)
{
    if (direction < 0) {
        if (_action_menu_open.get()) {
            selectPreviousAction();
        } else {
            _model.browser().selectPrevious();
        }
    } else if (direction > 0) {
        if (_action_menu_open.get()) {
            selectNextAction();
        } else {
            _model.browser().selectNext();
        }
    }
}

void BrowserViewModel::clearHeldSelection()
{
    _held_selection_direction = 0;
    _next_selection_at_ms     = 0;
}

bool BrowserViewModel::canGenerateMagic() const
{
    return !_pending_delete.get().active && !_pending_rename.get().active && !_action_menu_open.get();
}

void BrowserViewModel::generateMagic()
{
    _magic.set(_magic.get() + 1);
    spdlog::info("BrowserViewModel: magic trigger serial={}", _magic.get());
}

}  // namespace files
