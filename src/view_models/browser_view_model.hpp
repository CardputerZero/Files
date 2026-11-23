#pragma once

#include "models/files_model.hpp"
#include "models/debian_package.hpp"
#include "view_models/view_model.hpp"
#include <tools/observable/single_observable.hpp>
#include <atomic>
#include <deque>
#include <future>
#include <mutex>

namespace files {

enum class BrowserAction {
    Open,
    Copy,
    Cut,
    Paste,
    Rename,
    Info,
    Delete,
    Places,
    Eject,
    NewFolder,
    Language,
    SystemLanguage,
    English,
    Czech,
};

enum class PackageDialogMode { Closed, Reading, Review, Password, Installing, ConfirmTransfer, Transferring, ConfirmEject, Ejecting, Result };

enum class DialogTone { Normal, Warning, Error, Success };

struct PackageDialog {
    PackageDialogMode mode = PackageDialogMode::Closed;
    std::string title;
    std::string text;
    bool allowInstall = false;
    DialogTone tone = DialogTone::Normal;
    std::string identity;
    std::string metadata;
    bool rootScripts = false;
    bool move = false;
    std::string fixedStatus;
    std::string fixedInstruction;
    std::string diagnosticLabel;
    std::string maskedPassword;
    std::optional<double> progress;
    bool cancelling = false;
    std::string metadataStatus;
    std::string indentedText;
    std::string afterIndentedText;
    std::string technicalPath;
    std::string diagnosticHeading;
    std::string diagnosticText;
    std::string fullVersion;
};

struct VolumeNotice {
    std::string text;
    uint64_t connectedMountId = 0;
    DialogTone tone = DialogTone::Normal;
    unsigned duration() const { return text.find('\n') == std::string::npos ? 3000 : 5000; }
};

class BrowserViewModel : public ViewModel {
public:
    BrowserViewModel(FilesRouter& router, FilesModel& model);
    ~BrowserViewModel() override;

    PageId pageId() const override
    {
        return PageId::Browser;
    }

    void onEnter() override;
    void onKey(uint32_t key) override;
    void onKeyState(uint32_t key, bool pressed) override;
    void tick(uint32_t nowMs) override;
    void pollVolumes(uint32_t nowMs);
    bool openUsb();
    bool canOpenUsb() const;
    bool ejectUsb();
    bool hasRemovableVolumes() const { return _model.browser().hasRemovableVolumes(); }
    auto& volumeNotice() { return _volume_notice; }
    bool modalInputActive() const
    {
        return _action_menu_open.get() || _pending_delete.get().active ||
               _pending_rename.get().active || packageDialogOpen() || _magic.get();
    }
    bool canEject() const;
    bool languageMenu() const { return _language_menu; }
    std::string directoryTitle();
    std::string directoryCaption();
    std::string menuContext() const;
    bool clipboardMoves() const { return hasClipboard() && _pending_copy.cut; }
    bool hasClipboard() const { return _pending_copy.active; }
    bool hasMultipleRemovableVolumes() const { return _model.browser().hasMultipleRemovableVolumes(); }
    bool volumeActionShown() const { return _volume_notice.get().connectedMountId != 0; }
    void clearStatus() { status().set(""); }
    std::vector<BrowserAction> availableActions() const;
    bool inPlaces() const { return _model.browser().inPlaces(); }

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

    auto& packageDialog() { return _package_dialog; }
    bool packageDialogOpen() const { return _package_dialog.get().mode != PackageDialogMode::Closed; }
    bool packagePasswordActive() const { return _package_dialog.get().mode == PackageDialogMode::Password; }
    auto& packageScroll() { return _package_scroll; }
    std::string passwordAttempt() const;
    void editPackagePassword(uint32_t key, const char* utf8);
    const FileEntry* selectedEntry() const;
    bool canGoBack() const
    {
        return _model.browser().canGoBack();
    }
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
    void requestNewFolder();
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
    smooth_ui_toolkit::SingleObservable<PackageDialog> _package_dialog{PackageDialog{}};
    smooth_ui_toolkit::SingleObservable<int> _package_scroll{0};
    std::unique_ptr<internal::PreparedDeb> _prepared_deb;
    std::string _package_password;
    int _password_attempts = 0;
    uint64_t _package_source_mount_id = 0;
    std::atomic<internal::DebInstallPhase> _package_phase{internal::DebInstallPhase::Preparing};
    const char* packageInstallStatus() const;
    // The workers finish before the state they use is destroyed.
    std::future<internal::DebInspection> _package_read;
    std::future<internal::DebInstallResult> _package_install;
    std::shared_ptr<internal::TransferControl> _transfer_control;
    std::mutex _eject_mutex;
    std::string _eject_progress;
    std::future<FileTransferResult> _storage_operation;
    uint64_t _transfer_destination_mount_id = 0;
    std::string _transfer_source_name;
    std::string _transfer_destination_name;
    std::string _transfer_destination_caption;
    std::string _transfer_return_path;
    std::optional<VolumeNotice> _return_notice;
    std::string _package_source_name;
    std::string _package_file_name;
    std::string transferProgress(uint64_t bytes) const;
    void markCancelling(PackageDialog& dialog) const;
    uint64_t _eject_mount_id = 0;
    std::string _eject_name;
    smooth_ui_toolkit::SingleObservable<VolumeNotice> _volume_notice{VolumeNotice{}};
    std::deque<VolumeNotice> _queued_notices;
    uint32_t _last_volume_check = 0;
    uint32_t _last_tick = 0;
    uint32_t _notice_started_at = 0;
    uint32_t _last_package_update = 0;
    smooth_ui_toolkit::SingleObservable<uint32_t> _magic{0};
    int _held_selection_direction  = 0;
    uint32_t _next_selection_at_ms = 0;
    uint32_t _magic_count          = 0;
    uint32_t _magic_serial         = 0;

    bool _language_menu = false;
    void startTransfer();
    void handlePackageKey(uint32_t key);
    void startPackageRead(const FileEntry& entry);
    void startPackageInstall();
    void tickPackage(uint32_t nowMs);
    void requestEject();
    void startEject();
    void showVolumeNotice(VolumeNotice notice);
    void advanceVolumeNotice();
    void tickStorage(uint32_t nowMs);
    void selectPreviousAction();
    void selectNextAction();
    void activateSelectedAction();
    void selectByDirection(int direction);
    void clearHeldSelection();
    bool canGenerateMagic() const;
    void generateMagic();
};

}  // namespace files
