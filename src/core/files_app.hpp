#pragma once

#include "core/files_config.hpp"
#include "core/files_router.hpp"
#include "models/files_model.hpp"
#include "preview/preview_support.hpp"
#include "view_models/browser_view_model.hpp"
#include "view_models/preview_view_model.hpp"
#include "views/browser_view.hpp"
#include "views/preview_view.hpp"
#include "views/view.hpp"
#include <array>
#include <deque>
#include <lvgl.h>
#include <memory>
#if LV_USE_SDL
#include <mutex>
#endif
#include <utility>

#if LV_USE_SDL
#include LV_SDL_INCLUDE_PATH
#endif

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
    std::unique_ptr<PreviewPage> _help_page;
    ViewModel* _current_vm    = nullptr;
    View* _current_view       = nullptr;
    lv_group_t* _input_group  = nullptr;
    size_t _route_observer_id = 0;
    bool _quit_requested      = false;
    bool _enter_pressed       = false;
    bool _esc_pressed         = false;
    bool _esc_long_consumed   = false;
    uint32_t _esc_pressed_at  = 0;
    bool _help_active         = false;

#if LV_USE_SDL
    enum class SdlPageKey : uint8_t {
        None,
        Tab,
        PageUp,
        PageDown,
    };

    std::deque<SdlPageKey> _sdl_page_keys;
    std::mutex _sdl_page_keys_mutex;
    bool _sdl_event_watch_installed = false;
#endif

    std::array<ViewModel*, 2> _view_models;
    std::array<View*, 2> _views;

    ViewModel* viewModelFor(PageId page);
    View* viewFor(PageId page);
    void setupInputGroup();
    void setCurrentPage(PageId page);
    void showHelpPage();
    void closeHelpPage();
    static void onRouteChanged(void* context, const PageId& page);
    static void onKeyboardEvent(lv_event_t* event);
#if LV_USE_SDL
    static int onSdlEvent(void* userdata, SDL_Event* event);
    SdlPageKey takeSdlPageKey(uint32_t lvKey);
#endif
};

}  // namespace files
