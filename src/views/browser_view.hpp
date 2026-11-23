#pragma once

#include "view_models/browser_view_model.hpp"
#include "views/view.hpp"
#include <memory>

namespace files {

class MagicView;

class BrowserView : public View {
public:
    explicit BrowserView(BrowserViewModel& vm);
    ~BrowserView() override;

    void onEnter(lv_obj_t* parent) override;
    void onExit() override;
    void tick(uint32_t nowMs) override;
    int toastY(int height) const;
    void setExternalToast(int y = 0, int height = 0);

private:
    class Rows;
    class Dialog;

    BrowserViewModel& _vm;
    lv_obj_t* _root = nullptr;
    lv_obj_t* _heading = nullptr;
    lv_obj_t* _path = nullptr;
    lv_obj_t* _free = nullptr;
    lv_obj_t* _trailingKeys = nullptr;
    lv_obj_t* _keys = nullptr;
    lv_obj_t* _menu_error = nullptr;
    lv_obj_t* _menu_error_icon = nullptr;
    lv_obj_t* _menu_exit = nullptr;
    lv_obj_t* _no_usb = nullptr;
    lv_obj_t* _toast = nullptr;
    std::unique_ptr<Rows> _files;
    std::unique_ptr<Rows> _actions;
    std::unique_ptr<Dialog> _dialog;
    std::unique_ptr<MagicView> _magic;
    uint32_t _magic_seen = 0;
    uint32_t _toast_started = 0;
    uint32_t _toast_duration = 0;
    bool _usb = false;
    bool _usb_action = false;
    int _external_y = 0;
    int _external_height = 0;

    void updateChrome();
    void updateMenu(bool open);
    void updateDialog();
    void updateStatus(const std::string& status);
    void placeToast();
    void destroy();
};

}  // namespace files