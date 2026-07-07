#pragma once

#include "core/files_config.hpp"
#include "core/files_router.hpp"
#include "models/files_model.hpp"
#include "view_models/browser_view_model.hpp"
#include "view_models/preview_view_model.hpp"
#include "views/browser_view.hpp"
#include "views/preview_view.hpp"
#include "views/view.hpp"
#include <array>
#include <lvgl.h>
#include <utility>

namespace files {

class FilesApp {
public:
    explicit FilesApp(FilesConfig config = defaultFilesConfig());
    ~FilesApp();

    FilesApp(const FilesApp&)            = delete;
    FilesApp& operator=(const FilesApp&) = delete;

    void start();
    void onKey(uint32_t key);
    void onLvglKey(uint32_t lv_key, const char* utf8);
    bool onLvglKeyState(uint32_t lv_key, const char* utf8, bool pressed);
    void tick(uint32_t nowMs);
    bool hostRenderingSuspended() const;

    bool quitRequested() const
    {
        return _quit_requested;
    }

private:
    FilesConfig _config;
    FilesRouter _router;
    FilesModel _model;
    BrowserViewModel _browser_vm;
    PreviewViewModel _preview_vm;
    BrowserView _browser_view;
    PreviewView _preview_view;
    ViewModel* _current_vm    = nullptr;
    View* _current_view       = nullptr;
    lv_group_t* _input_group  = nullptr;
    size_t _route_observer_id = 0;
    bool _quit_requested      = false;
    bool _enter_pressed       = false;
    bool _esc_pressed         = false;
    bool _esc_long_consumed   = false;
    uint32_t _esc_pressed_at  = 0;

    std::array<ViewModel*, 2> _view_models;
    std::array<View*, 2> _views;

    ViewModel* viewModelFor(PageId page);
    View* viewFor(PageId page);
    void setupInputGroup();
    void setCurrentPage(PageId page);
    static void onRouteChanged(void* context, const PageId& page);
    static void onKeyboardEvent(lv_event_t* event);
};

}  // namespace files
