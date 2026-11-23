#pragma once

#include "view_models/browser_view_model.hpp"
#include "views/view.hpp"
#include <core/animation/animate_value/animate_value.hpp>
#include <lvgl/lvgl_cpp/label.hpp>
#include <lvgl/lvgl_cpp/obj.hpp>
#include <memory>
#include <vector>

namespace files {

class MagicView;

class BrowserView : public View {
public:
    explicit BrowserView(BrowserViewModel& vm);
    ~BrowserView() override;

    void onEnter(lv_obj_t* parent) override;
    void onExit() override;
    void tick(uint32_t nowMs) override;

private:
    class FilesMenu;
    class ActionMenu;
    class MenuCursor;
    class DeleteConfirmDialog;
    class RenameConfirmDialog;
    class PackagePanel;
    class StatusHud;

    BrowserViewModel& _vm;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _root;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _path_bar;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _path_label;
    std::unique_ptr<FilesMenu> _file_list;
    std::unique_ptr<ActionMenu> _action_menu;
    std::unique_ptr<MenuCursor> _cursor;
    std::unique_ptr<DeleteConfirmDialog> _delete_confirm_dialog;
    std::unique_ptr<RenameConfirmDialog> _rename_confirm_dialog;
    std::unique_ptr<PackagePanel> _package_panel;
    std::unique_ptr<StatusHud> _status_hud;
    bool _usb_hint_visible = false;
    lv_obj_t* _key_bar = nullptr;
    std::unique_ptr<MagicView> _magic_view;
    uint32_t _magic_serial_seen = 0;

    void destroy();
    void renderKeyBar();
    void renderDirectory(const std::string& directory);
    void renderEntries(const std::vector<FileEntry>& entries);
    void renderSelectedIndex(int index);
    void renderStatus(const std::string& status);
    void renderEnterPressed(bool pressed);
    void renderActionMenuOpen(bool open);
    void renderActionMenuSelectedIndex(int index);
    void renderPendingDelete(const PendingDeleteFile& pending);
    void renderPendingRename(const PendingRenameFile& pending);
    void renderPendingRenameName(const std::string& name);
    void renderMagic(uint32_t magicSerial);
    void updateCursorTarget();
    static void onDirectoryChanged(void* context, const std::string& directory);
    static void onEntriesChanged(void* context, const std::vector<FileEntry>& entries);
    static void onSelectedIndexChanged(void* context, const int& index);
    static void onStatusChanged(void* context, const std::string& status);
    static void onEnterPressedChanged(void* context, const bool& pressed);
    static void onActionMenuOpenChanged(void* context, const bool& open);
    static void onActionMenuSelectedIndexChanged(void* context, const int& index);
    static void onPendingDeleteChanged(void* context, const PendingDeleteFile& pending);
    static void onPendingRenameChanged(void* context, const PendingRenameFile& pending);
    static void onPendingRenameNameChanged(void* context, const std::string& name);
    static void onPackageDialogChanged(void* context, const PackageDialog& dialog);
    static void onPackageScroll(void* context, const int& offset);
    static void onMagicChanged(void* context, const uint32_t& magicSerial);
};

}  // namespace files
