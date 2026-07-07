#include "core/files_app.hpp"

#include "assets/font_assets.hpp"
#include <lvgl.h>
#include <spdlog/spdlog.h>
#include <utility>

namespace files {
namespace {

constexpr uint32_t kEscLongPressMs = 900;

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
    setupInputGroup();
    _route_observer_id = _router.currentPage().observe(this, onRouteChanged);
    setCurrentPage(_router.page());
}

void FilesApp::onKey(uint32_t key)
{
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
    if (lv_key == LV_KEY_ESC) {
        if (pressed && !_esc_pressed) {
            _esc_pressed       = true;
            _esc_pressed_at    = lv_tick_get();
            _esc_long_consumed = false;
        } else if (!pressed) {
            if (_esc_pressed && !_esc_long_consumed) {
                onKey('\x1b');
            }
            _esc_pressed       = false;
            _esc_long_consumed = false;
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

    if (lv_key == LV_KEY_NEXT || lv_key == LV_KEY_PREV) {
        if (pressed) {
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
    if (_esc_pressed && !_esc_long_consumed && nowMs - _esc_pressed_at >= kEscLongPressMs) {
        _esc_long_consumed = true;
        _quit_requested    = true;
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
    return _current_vm && _current_vm->suspendsHostRendering();
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

}  // namespace files
