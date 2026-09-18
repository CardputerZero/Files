#include "core/files_app.hpp"

#include "assets/font_assets.hpp"
#include "preview/text/text_preview.hpp"
#include <lvgl.h>
#include <spdlog/spdlog.h>
#if LV_USE_SDL
#include LV_SDL_INCLUDE_PATH
#endif
#include <utility>

namespace files {
namespace {

constexpr uint32_t kEscLongPressMs = 700;

#if LV_USE_SDL
bool sdlKeyHeld(SDL_Scancode scancode)
{
    int key_count      = 0;
    const Uint8* state = SDL_GetKeyboardState(&key_count);
    const int index    = static_cast<int>(scancode);
    return state && index >= 0 && index < key_count && state[index] != 0;
}
#endif

lv_obj_t* focusedTextInput()
{
    lv_indev_t* indev = lv_indev_get_next(nullptr);
    while (indev) {
        lv_group_t* group = lv_indev_get_group(indev);
        if (group) {
            lv_obj_t* focused = lv_group_get_focused(group);
            if (focused && lv_obj_check_type(focused, &lv_textarea_class)) {
                return focused;
            }
        }
        indev = lv_indev_get_next(indev);
    }
    return nullptr;
}

bool textInputFocused()
{
    return focusedTextInput() != nullptr;
}

bool handleFocusedTextInput(uint32_t lv_key, const char* utf8, bool pressed)
{
    lv_obj_t* input = focusedTextInput();
    if (!input) {
        return false;
    }

    if (!pressed) {
        return true;
    }

    switch (lv_key) {
        case LV_KEY_BACKSPACE:
            lv_textarea_delete_char(input);
            return true;
        case LV_KEY_DEL:
            lv_textarea_delete_char_forward(input);
            return true;
        case LV_KEY_LEFT:
            lv_textarea_cursor_left(input);
            return true;
        case LV_KEY_RIGHT:
            lv_textarea_cursor_right(input);
            return true;
        case LV_KEY_HOME:
            lv_textarea_set_cursor_pos(input, 0);
            return true;
        case LV_KEY_END:
            lv_textarea_set_cursor_pos(input, LV_TEXTAREA_CURSOR_LAST);
            return true;
        default:
            break;
    }

    if (utf8 && utf8[0] >= 0x20 && utf8[0] < 0x7f && utf8[1] == '\0') {
        lv_textarea_add_text(input, utf8);
        return true;
    }

    return true;
}

}  // namespace

FilesApp::FilesApp(FilesConfig config)
    : _config(std::move(config)),
      _model(_config.start_directory),
      _browser_vm(_router, _model),
      _preview_vm(_router, _model),
      _browser_view(_browser_vm),
      _preview_view(_preview_vm),
      _view_models{&_browser_vm, &_preview_vm},
      _views{&_browser_view, &_preview_view}
{
}

FilesApp::~FilesApp()
{
#if LV_USE_SDL
    if (_sdl_event_watch_installed) {
        SDL_DelEventWatch(&FilesApp::onSdlEvent, this);
        _sdl_event_watch_installed = false;
        std::lock_guard<std::mutex> lock(_sdl_page_keys_mutex);
        _sdl_page_keys.clear();
    }
#endif
    hideExitHint();
    if (_exit_hint && lv_obj_is_valid(_exit_hint)) {
        lv_obj_delete(_exit_hint);
    }
    _exit_hint = nullptr;
    if (_help_page) {
        _help_page->detach();
        _help_page.reset();
    }
    if (_route_observer_id != 0) {
        _router.currentPage().removeObserver(_route_observer_id);
    }
    if (_input_group) {
        lv_group_del(_input_group);
        _input_group = nullptr;
    }
}

void FilesApp::start()
{
    spdlog::info("FilesApp: startDirectory={}", _config.start_directory);
    initFontAssets();
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, LV_PART_MAIN);
    createExitHint();
    setupInputGroup();
#if LV_USE_SDL
    if (!_sdl_event_watch_installed) {
        {
            std::lock_guard<std::mutex> lock(_sdl_page_keys_mutex);
            _sdl_page_keys.clear();
        }
        SDL_AddEventWatch(&FilesApp::onSdlEvent, this);
        _sdl_event_watch_installed = true;
    }
#endif
    _route_observer_id = _router.currentPage().observe(this, onRouteChanged);
    _help_active       = false;
    setCurrentPage(_router.page());
}

void FilesApp::onKey(uint32_t key)
{
    if (key == files_key::Help) {
        if (_help_active) {
            closeHelpPage();
        } else if (!textInputFocused()) {
            showHelpPage();
        }
        return;
    }

    if (_help_active) {
        if (key == files_key::Left || key == '\x1b') {
            closeHelpPage();
        } else if (_help_page) {
            _help_page->onKey(key, _router);
        }
        return;
    }

    if (key == '\x1b') {
        // A short Esc from a preview returns to the browser. On the browser
        // page, keep walking up the directory tree before reserving Esc for
        // the app-exit gesture. Menus and dialogs always consume it first.
        if (_router.page() != PageId::Browser || _browser_vm.canGoBack() || browserEscapeHandledByView()) {
            if (_current_vm) {
                _current_vm->onKey(key);
            }
        }
        return;
    }

    if (_current_vm) {
        _current_vm->onKey(key);
    }
}

void FilesApp::onLvglKey(uint32_t lv_key, const char* utf8)
{
    onLvglKeyState(lv_key, utf8, true);
}

bool FilesApp::onLvglKeyState(uint32_t lv_key, const char* utf8, bool pressed)
{
    bool desktop_help = false;
#if LV_USE_SDL
    desktop_help = (lv_key == 'h' || lv_key == 'H') && !textInputFocused();
#endif
    if (lv_key == files_key::Help || desktop_help) {
        if (pressed) {
            onKey(files_key::Help);
        }
        return true;
    }

    if (_help_active) {
        if (lv_key == LV_KEY_ESC || lv_key == LV_KEY_LEFT || lv_key == '\x1b') {
            if (pressed) {
                closeHelpPage();
            }
            return true;
        }

#if LV_USE_SDL
        // Consume the raw marker for both LV_KEY_PREV and LV_KEY_NEXT while
        // help is open.  PageDown and Tab share LV_KEY_NEXT in LVGL's SDL
        // driver; leaving a Tab marker behind would misclassify the next key.
        SdlPageKey sdl_page_key = SdlPageKey::None;
        if (pressed && (lv_key == LV_KEY_PREV || lv_key == LV_KEY_NEXT)) {
            sdl_page_key = takeSdlPageKey(lv_key);
        }
#endif

        uint32_t help_key = 0;
        if (lv_key == files_key::PageUp) {
            help_key = files_key::PageUp;
        } else if (lv_key == files_key::PageDown) {
            help_key = files_key::PageDown;
        } else if (lv_key == LV_KEY_PREV) {
#if LV_USE_SDL
            help_key = sdl_page_key == SdlPageKey::PageUp ? files_key::PageUp : files_key::Up;
#else
            help_key = files_key::PageUp;
#endif
        } else if (lv_key == LV_KEY_UP || lv_key == files_key::Up) {
            help_key = files_key::Up;
        } else if (lv_key == LV_KEY_DOWN || lv_key == files_key::Down
#if LV_USE_SDL
                   || (lv_key == LV_KEY_NEXT && sdl_page_key == SdlPageKey::PageDown)
#endif
        ) {
            help_key = (lv_key == LV_KEY_NEXT) ? files_key::PageDown : files_key::Down;
        } else if (utf8 && (utf8[0] == 'f' || utf8[0] == 'F')) {
            help_key = files_key::Up;
        } else if (utf8 && (utf8[0] == 'x' || utf8[0] == 'X')) {
            help_key = files_key::Down;
        }

        if (help_key != 0 && _help_page) {
            _help_page->onKeyState(help_key, pressed, _router);
            if (pressed) {
                _help_page->onKey(help_key, _router);
            }
        }
        return true;
    }

    if (lv_key == LV_KEY_ESC) {
        if (pressed && !_esc_pressed) {
            _esc_pressed       = true;
            _esc_pressed_at    = lv_tick_get();
            _esc_long_consumed = false;
            _esc_exit_armed =
                _router.page() == PageId::Browser && !_browser_vm.canGoBack() && !browserEscapeHandledByView();
            if (_esc_exit_armed) {
                showExitHint();
            }
        } else if (!pressed && _esc_pressed) {
#if LV_USE_SDL
            // LVGL's SDL driver may synthesize a release while the physical
            // key is still held. Keep the long-press edge armed in that case.
            if (sdlKeyHeld(SDL_SCANCODE_ESCAPE)) {
                return true;
            }
#endif
            releaseEscPress();
        }
        return true;
    }

    if (lv_key == LV_KEY_ENTER) {
        if (_current_vm) {
            _current_vm->onKeyState('\r', pressed);
        }
        if (!pressed && _enter_pressed) {
            onKey('\r');
        }
        _enter_pressed = pressed;
        return true;
    }

    // LVGL's SDL driver maps Tab and PageDown to the same LV_KEY_NEXT value.
    // The raw SDL event watcher records which key generated the event so the
    // two actions remain distinct without changing the vendored LVGL driver.
#if LV_USE_SDL
    SdlPageKey sdl_page_key = SdlPageKey::Tab;
    if (pressed && (lv_key == LV_KEY_NEXT || lv_key == LV_KEY_PREV)) {
        sdl_page_key = takeSdlPageKey(lv_key);
    }
#endif

    if (lv_key == LV_KEY_PREV) {
        if (pressed) {
            onKey(files_key::PageUp);
        }
        return true;
    }

    if (lv_key == LV_KEY_NEXT) {
        if (pressed) {
#if LV_USE_SDL
            if (sdl_page_key == SdlPageKey::PageDown) {
                onKey(files_key::PageDown);
                return true;
            }
#endif
            onKey('\t');
        }
        return true;
    }

#if !LV_USE_SDL
    if (handleFocusedTextInput(lv_key, utf8, pressed)) {
        return true;
    }
#else
    if (textInputFocused()) {
        return true;
    }
#endif

    switch (lv_key) {
        case files_key::PageUp:
        case files_key::PageDown:
            if (pressed) {
                onKey(lv_key);
            }
            return true;
        case LV_KEY_UP:
            if (_current_vm) {
                _current_vm->onKeyState(files_key::Up, pressed);
            }
            if (pressed) {
                onKey(files_key::Up);
            }
            return true;
        case LV_KEY_DOWN:
            if (_current_vm) {
                _current_vm->onKeyState(files_key::Down, pressed);
            }
            if (pressed) {
                onKey(files_key::Down);
            }
            return true;
        case LV_KEY_LEFT:
            if (_current_vm) {
                _current_vm->onKeyState(files_key::Left, pressed);
            }
            if (pressed) {
                onKey(files_key::Left);
            }
            return true;
        case LV_KEY_RIGHT:
            if (_current_vm) {
                _current_vm->onKeyState(files_key::Right, pressed);
            }
            if (pressed) {
                onKey(files_key::Right);
            }
            return true;
        default:
            break;
    }

    if (utf8 && utf8[0] != '\0') {
        uint32_t mapped_key = 0;
        switch (utf8[0]) {
            case 'f':
            case 'F':
                mapped_key = files_key::Up;
                break;
            case 'x':
            case 'X':
                mapped_key = files_key::Down;
                break;
            case 'z':
            case 'Z':
                mapped_key = files_key::Left;
                break;
            case 'c':
            case 'C':
                mapped_key = files_key::Right;
                break;
            default:
                break;
        }
        if (mapped_key != 0) {
            if (_current_vm) {
                _current_vm->onKeyState(mapped_key, pressed);
            }
            if (pressed) {
                onKey(mapped_key);
            }
            return true;
        }
    }

    if (!pressed) {
        return true;
    }

    if (utf8 && (utf8[0] == ' ' || (utf8[0] >= '0' && utf8[0] <= '9'))) {
        onKey(static_cast<uint32_t>(utf8[0]));
        return true;
    }

    if (utf8 && utf8[0] == '\t') {
        onKey('\t');
        return true;
    }

    return true;
}

void FilesApp::tick(uint32_t nowMs)
{
    if (_esc_exit_armed && (_router.page() != PageId::Browser || _help_active || _browser_vm.canGoBack() ||
                            browserEscapeHandledByView())) {
        _esc_exit_armed = false;
        hideExitHint();
    }
    if (_esc_pressed && !_esc_long_consumed && _esc_exit_armed && nowMs - _esc_pressed_at >= kEscLongPressMs) {
        _esc_long_consumed = true;
        hideExitHint();
        spdlog::info("FilesApp: quit requested after holding Esc for {} ms", kEscLongPressMs);
        _quit_requested = true;
    }
#if LV_USE_SDL
    if (_esc_pressed && !sdlKeyHeld(SDL_SCANCODE_ESCAPE)) {
        releaseEscPress();
    }
#endif
    if (_help_page) {
        _help_page->tick(nowMs);
    }
    if (_current_vm) {
        _current_vm->tick(nowMs);
    }
    if (_current_view) {
        _current_view->tick(nowMs);
    }
}

bool FilesApp::hostRenderingSuspended() const
{
    if (_help_active) {
        return false;
    }
    return _current_vm && _current_vm->suspendsHostRendering();
}

void FilesApp::showHelpPage()
{
    if (_help_active || textInputFocused()) {
        return;
    }

    _help_page = createTextPreviewPage("Help",
                                       "Browse and edit file directories, and preview supported file formats.\n\n"
                                       "F / X / OK / ESC: navigation\n"
                                       "TAB: menu\n"
                                       "PgUp / PgDn: page");
    if (!_help_page) {
        spdlog::error("FilesApp: failed to create help page");
        return;
    }

    _esc_exit_armed = false;
    hideExitHint();
    _help_page->attach(lv_screen_active());
    _help_active = true;
}

void FilesApp::closeHelpPage()
{
    if (!_help_active) {
        return;
    }
    if (_help_page) {
        _help_page->detach();
        _help_page.reset();
    }
    _help_active = false;
}

void FilesApp::createExitHint()
{
    if (_exit_hint) {
        return;
    }

    _exit_hint = lv_label_create(lv_layer_top());
    if (!_exit_hint) {
        return;
    }

    lv_label_set_text(_exit_hint, "Hold ESC to exit");
    lv_obj_set_size(_exit_hint, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_text_font(_exit_hint, uiMonoFont12(), LV_PART_MAIN);
    lv_obj_set_style_text_color(_exit_hint, lv_color_hex(0xFED40D), LV_PART_MAIN);
    lv_obj_set_style_bg_color(_exit_hint, lv_color_hex(0x474747), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_exit_hint, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(_exit_hint, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(_exit_hint, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_left(_exit_hint, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_right(_exit_hint, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_top(_exit_hint, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(_exit_hint, 5, LV_PART_MAIN);
    lv_obj_set_style_text_align(_exit_hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(_exit_hint, LV_ALIGN_BOTTOM_MID, 0, -38);
    lv_obj_clear_flag(_exit_hint, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(_exit_hint, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(_exit_hint, LV_OBJ_FLAG_HIDDEN);
}

void FilesApp::showExitHint()
{
    if (!_exit_hint || !lv_obj_is_valid(_exit_hint)) {
        return;
    }
    lv_obj_remove_flag(_exit_hint, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(_exit_hint);
}

void FilesApp::hideExitHint()
{
    if (_exit_hint && lv_obj_is_valid(_exit_hint)) {
        lv_obj_add_flag(_exit_hint, LV_OBJ_FLAG_HIDDEN);
    }
}

void FilesApp::releaseEscPress()
{
    if (!_esc_pressed) {
        return;
    }

    if (!_esc_long_consumed && (!_esc_exit_armed || _help_active || browserEscapeHandledByView())) {
        onKey('\x1b');
    }
    hideExitHint();
    _esc_pressed       = false;
    _esc_long_consumed = false;
    _esc_exit_armed    = false;
    _esc_pressed_at    = 0;
}

bool FilesApp::browserEscapeHandledByView()
{
    return _browser_vm.actionMenuOpen().get() || _browser_vm.pendingDelete().get().active ||
           _browser_vm.pendingRename().get().active;
}

ViewModel* FilesApp::viewModelFor(PageId page)
{
    for (auto* vm : _view_models) {
        if (vm && vm->pageId() == page) {
            return vm;
        }
    }
    return nullptr;
}

View* FilesApp::viewFor(PageId page)
{
    const auto index = static_cast<size_t>(page);
    if (index >= _views.size()) {
        return nullptr;
    }
    return _views[index];
}

void FilesApp::setupInputGroup()
{
    if (_input_group) {
        return;
    }

    _input_group = lv_group_create();

    lv_indev_t* indev = lv_indev_get_next(nullptr);
    while (indev) {
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_KEYPAD) {
            lv_indev_set_group(indev, _input_group);
#if LV_USE_SDL
            lv_indev_add_event_cb(indev, onKeyboardEvent, LV_EVENT_KEY, this);
            lv_indev_add_event_cb(indev, onKeyboardEvent, LV_EVENT_RELEASED, this);
#endif
        }
        indev = lv_indev_get_next(indev);
    }
}

void FilesApp::setCurrentPage(PageId page)
{
    ViewModel* next = viewModelFor(page);
    View* next_view = viewFor(page);
    if (!next || (next == _current_vm && next_view == _current_view)) {
        return;
    }

    if (page != PageId::Browser && _esc_exit_armed) {
        _esc_exit_armed = false;
        hideExitHint();
    }

    if (_current_view) {
        _current_view->onExit();
    }
    if (_current_vm) {
        _current_vm->onKeyState('\r', false);
        _current_vm->onExit();
    }
    _current_vm   = next;
    _current_view = next_view;
    spdlog::info("Files route -> {}", pageIdName(page));
    _current_vm->onEnter();
    if (_current_view) {
        _current_view->onEnter(lv_screen_active());
    }
}

void FilesApp::onRouteChanged(void* context, const PageId& page)
{
    auto* self = static_cast<FilesApp*>(context);
    if (self) {
        self->setCurrentPage(page);
    }
}

void FilesApp::onKeyboardEvent(lv_event_t* event)
{
    auto* self  = static_cast<FilesApp*>(lv_event_get_user_data(event));
    auto* indev = static_cast<lv_indev_t*>(lv_event_get_target(event));
    if (!self || !indev) {
        return;
    }

    const uint32_t key = lv_indev_get_key(indev);
    char utf8[2]       = {0, 0};
    if (key >= 0x20 && key < 0x7f) {
        utf8[0] = static_cast<char>(key);
    }
    const bool pressed =
        lv_event_get_code(event) == LV_EVENT_KEY && lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED;
    self->onLvglKeyState(key, utf8, pressed);
}

#if LV_USE_SDL
int FilesApp::onSdlEvent(void* userdata, SDL_Event* event)
{
    auto* self = static_cast<FilesApp*>(userdata);
    if (!self || !event || event->type != SDL_KEYDOWN) {
        return 0;
    }

    SdlPageKey page_key = SdlPageKey::None;
    if (event->key.keysym.sym == SDLK_TAB) {
        page_key = SdlPageKey::Tab;
    } else if (event->key.keysym.sym == SDLK_PAGEUP) {
        page_key = SdlPageKey::PageUp;
    } else if (event->key.keysym.sym == SDLK_PAGEDOWN) {
        page_key = SdlPageKey::PageDown;
    }

    std::lock_guard<std::mutex> lock(self->_sdl_page_keys_mutex);
    if (page_key == SdlPageKey::None) {
        // Keep navigation markers queued across ordinary key events.  SDL
        // invokes event watchers when events are pushed, while LVGL consumes
        // them later from its timer; clearing here would lose a valid page
        // key whenever a letter or arrow was queued before that timer ran.
        return 0;
    }

    // The SDL LVGL driver normally queues and dispatches one key immediately,
    // but retain a short FIFO for repeated key presses in one event pass.
    constexpr size_t kMaxPendingPageKeys = 32;
    if (self->_sdl_page_keys.size() >= kMaxPendingPageKeys) {
        self->_sdl_page_keys.pop_front();
    }
    self->_sdl_page_keys.push_back(page_key);
    return 0;
}

FilesApp::SdlPageKey FilesApp::takeSdlPageKey(uint32_t lvKey)
{
    std::lock_guard<std::mutex> lock(_sdl_page_keys_mutex);
    if (_sdl_page_keys.empty()) {
        return SdlPageKey::None;
    }

    const SdlPageKey key = _sdl_page_keys.front();
    const bool matches   = (lvKey == LV_KEY_PREV && key == SdlPageKey::PageUp) ||
                         (lvKey == LV_KEY_NEXT && (key == SdlPageKey::Tab || key == SdlPageKey::PageDown));
    if (!matches) {
        // A marker can only become out of sync if LVGL dropped an input event
        // or another input device delivered a control key. Discard it rather
        // than allowing a later Tab/PageDown to trigger the wrong action.
        _sdl_page_keys.clear();
        return SdlPageKey::None;
    }

    _sdl_page_keys.pop_front();
    return key;
}
#endif

}  // namespace files
