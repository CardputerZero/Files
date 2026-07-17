#include "views/browser_view.hpp"

#include "assets/assets.h"
#include "assets/font_assets.hpp"
#include "views/magic_view.hpp"
#include <lvgl/lvgl_cpp/image.hpp>
#include <lvgl/lvgl_cpp/text_area.hpp>
#include <widget/select_menu/smooth_selector.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iterator>

namespace files {
namespace {

constexpr int32_t kScreenWidth        = 320;
constexpr int32_t kScreenHeight       = 170;
constexpr int32_t kPathBarHeight      = 21;
constexpr int32_t kPathTextX          = 10;
constexpr int32_t kPathTextWidth      = 300;
constexpr int32_t kListX              = 0;
constexpr int32_t kListY              = 29;
constexpr int32_t kListWidth          = 320;
constexpr int32_t kListHeight         = 132;
constexpr int32_t kIconX              = 10;
constexpr int32_t kTextX              = 38;
constexpr int32_t kCursorSize         = 25;
constexpr int32_t kCursorPaddingX     = 9;
constexpr int32_t kCursorOffsetY      = 2;
constexpr int32_t kMenuItemHeight     = 20;
constexpr int32_t kMenuItemPitch      = 27;
constexpr int32_t kMenuItemMinWidth   = 72;
constexpr int32_t kMenuItemMaxWidth   = 280;
constexpr int32_t kMenuPaddingRight   = 11;
constexpr int32_t kMenuCameraPaddingY = 10;
constexpr int32_t kFileRowOverscan    = 2;
constexpr size_t kFileRowPoolSize =
    static_cast<size_t>((kListHeight + kMenuItemPitch - 1) / kMenuItemPitch + kFileRowOverscan * 2 + 1);
constexpr int32_t kActionMenuY              = 56;
constexpr int32_t kActionMenuHiddenY        = 171;
constexpr int32_t kActionMenuHeight         = kScreenHeight - kActionMenuY;
constexpr int32_t kActionMenuSideBleed      = 2;
constexpr int32_t kActionMenuBottomBleed    = 12;
constexpr int32_t kActionMenuPaddingTop     = 10;
constexpr int32_t kActionMenuPaddingBottom  = 10;
constexpr int32_t kActionMenuPaddingLeft    = 0;
constexpr int32_t kActionMenuPaddingRight   = 0;
constexpr int32_t kActionMenuCameraPaddingY = 8;
constexpr int32_t kScrollBarX               = 312;
constexpr int32_t kScrollBarY               = 0;
constexpr int32_t kScrollBarWidth           = 3;
constexpr int32_t kScrollBarHeight          = 128;
constexpr int32_t kScrollThumbMinH          = 17;
constexpr int32_t kDeleteDialogWidth        = 258;
constexpr int32_t kDeleteDialogHeight       = 116;
constexpr int32_t kDeleteDialogY            = 0;
constexpr int32_t kDeleteDialogRadius       = 14;
constexpr int32_t kDeleteNameAreaWidth      = 238;
constexpr int32_t kDeleteButtonHeight       = 23;
constexpr int32_t kDeleteButtonRadius       = 5;
constexpr int32_t kDeletePromptCenterY      = -40;
constexpr int32_t kDeleteNameAreaCenterY    = -6;
constexpr int32_t kDeleteButtonCenterY      = 34;
constexpr int32_t kDeleteOpenOriginX        = 0;
constexpr int32_t kDeleteOpenOriginY        = -150;
constexpr int32_t kDeleteOpenOriginWidth    = 180;
constexpr int32_t kDeleteOpenOriginHeight   = 120;
constexpr int32_t kHudShownY                = 137;
constexpr int32_t kHudHiddenY               = 171;
constexpr uint32_t kPathBarColor            = 0x313131;
constexpr uint32_t kTextColor               = 0xFFFFFF;
constexpr uint32_t kMutedTextColor          = 0x9A9A9A;
constexpr uint32_t kActionMenuBgColor       = 0x313131;
constexpr uint32_t kScrollBarColor          = 0x2B2B2B;
constexpr uint32_t kScrollThumbColor        = 0x848484;
constexpr float kHudCloseDuration           = 0.34f;
constexpr float kSelectorMoveDuration       = 0.32f;
constexpr float kSelectorMoveBounce         = 0.30f;
constexpr float kSelectorShapeDuration      = 0.30f;
constexpr float kSelectorShapeBounce        = 0.18f;
constexpr float kCameraDuration             = 0.44f;
constexpr float kActionMenuDuration         = 0.36f;
constexpr uint32_t kMenuRenderInterval      = 16;
constexpr uint32_t kHudHoldMs               = 10000;

std::string compactPath(const std::string& path)
{
    std::filesystem::path fsPath(path);
    const char* home = std::getenv("HOME");
    if (home && home[0] != '\0') {
        std::string homePath   = std::filesystem::path(home).lexically_normal().string();
        std::string normalized = fsPath.lexically_normal().string();
        if (normalized == homePath) {
            return "~";
        }
        if (normalized.rfind(homePath + "/", 0) == 0) {
            return "~" + normalized.substr(homePath.size());
        }
    }
    return fsPath.lexically_normal().string();
}

std::string rowText(const FileEntry& entry)
{
    if (entry.name.empty()) {
        return "(unnamed)";
    }
    return entry.name;
}

const lv_image_dsc_t* imageForEntry(const FileEntry& entry)
{
    if (entry.icon == "folder") {
        return &image_icon_folder;
    }
    if (entry.icon == "audio") {
        return &image_icon_audio;
    }
    if (entry.icon == "video") {
        return &image_icon_video;
    }
    if (entry.icon == "image") {
        return &image_icon_image;
    }
    if (entry.icon == "text") {
        return &image_icon_text;
    }
    if (entry.icon == "scripts") {
        return &image_icon_scripts;
    }
    return &image_icon_default;
}

struct MenuAction {
    const char* text           = "";
    const lv_image_dsc_t* icon = nullptr;
};

MenuAction menuActionFor(BrowserAction action, const FileEntry* selectedEntry = nullptr)
{
    switch (action) {
        case BrowserAction::Open:
            return {"Open", selectedEntry ? imageForEntry(*selectedEntry) : &image_icon_default};
        case BrowserAction::Copy:
            return {"Copy", &image_icon_copy};
        case BrowserAction::Cut:
            return {"Cut", &image_icon_cut};
        case BrowserAction::Paste:
            return {"Paste", &image_icon_paste};
        case BrowserAction::Rename:
            return {"Rename", &image_icon_rename};
        case BrowserAction::Info:
            return {"Info", &image_icon_info};
        case BrowserAction::Delete:
            return {"Delete", &image_icon_delete};
    }
    return {"Open", &image_icon_default};
}

int32_t textWidth(const std::string& text)
{
    lv_point_t size{};
    lv_text_get_size(&size, text.c_str(), uiFont14(), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return size.x;
}

int32_t optionWidth(const std::string& text)
{
    return std::clamp(kTextX + textWidth(text) + kMenuPaddingRight, kMenuItemMinWidth, kMenuItemMaxWidth);
}

lv_group_t* keyboardGroup()
{
    lv_indev_t* indev = lv_indev_get_next(nullptr);
    while (indev) {
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_KEYPAD) {
            lv_group_t* group = lv_indev_get_group(indev);
            if (group) {
                return group;
            }
        }
        indev = lv_indev_get_next(indev);
    }
    return nullptr;
}

}  // namespace

class BrowserView::FilesMenu : public smooth_ui_toolkit::SmoothSelectorMenu {
public:
    explicit FilesMenu(lv_obj_t* parent)
        : _panel(std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(parent)),
          _empty_label(std::make_unique<smooth_ui_toolkit::lvgl_cpp::Label>(_panel->raw_ptr())),
          _scroll_bar(std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(_panel->raw_ptr())),
          _scroll_thumb(std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(_panel->raw_ptr()))
    {
        _panel->setSize(kListWidth, kListHeight);
        _panel->setPos(kListX, kListY);
        _panel->setBgOpa(LV_OPA_TRANSP);
        _panel->setBorderWidth(0);
        _panel->setShadowWidth(0);
        _panel->setPaddingAll(0);
        _panel->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
        _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

        _empty_label->setText("No files");
        _empty_label->setTextFont(uiFont14());
        _empty_label->setTextColor(lv_color_hex(kMutedTextColor));
        _empty_label->setTextAlign(LV_TEXT_ALIGN_CENTER);
        _empty_label->setSize(kListWidth, LV_SIZE_CONTENT);
        _empty_label->align(LV_ALIGN_CENTER, 0, -4);
        _empty_label->addFlag(LV_OBJ_FLAG_HIDDEN);

        setupScrollBar();
        setupAnimation();
        setConfig().moveInLoop        = true;
        setConfig().renderInterval    = kMenuRenderInterval;
        setConfig().readInputInterval = 0;
        setCameraSize(kListWidth, kListHeight);
    }

    void setEntries(const std::vector<FileEntry>& entries, int selectedIndex)
    {
        ensureRowPool();
        for (auto& row : _rows) {
            row->unbind();
        }

        _row_data.clear();
        _data.option_list.clear();
        _data.selected_option_index = 0;

        if (entries.empty()) {
            _empty_label->removeFlag(LV_OBJ_FLAG_HIDDEN);
            _scroll_bar->addFlag(LV_OBJ_FLAG_HIDDEN);
            _scroll_thumb->addFlag(LV_OBJ_FLAG_HIDDEN);
            return;
        }

        _empty_label->addFlag(LV_OBJ_FLAG_HIDDEN);

        _row_data.reserve(entries.size());
        _data.option_list.reserve(entries.size());
        for (size_t i = 0; i < entries.size(); ++i) {
            std::string text    = rowText(entries[i]);
            const int32_t width = optionWidth(text);
            addOption({{0.0f, static_cast<float>(static_cast<int32_t>(i) * kMenuItemPitch), static_cast<float>(width),
                        static_cast<float>(kMenuItemHeight)},
                       nullptr});
            _row_data.push_back(RowData{std::move(text), imageForEntry(entries[i]), width});
        }

        jumpToInstant(clampIndex(selectedIndex));
        render();
    }

    void setSelectedIndex(int index)
    {
        if (!_data.option_list.empty()) {
            moveToWithCamera(clampIndex(index));
        }
    }

    void setPinnedToSelected(bool pinned)
    {
        if (_pinned_to_selected == pinned) {
            return;
        }
        _pinned_to_selected = pinned;
        if (!_data.option_list.empty()) {
            _update_camera_keyframe();
        }
    }

    void update(uint32_t nowMs) override
    {
        smooth_ui_toolkit::SmoothSelectorMenu::update(nowMs);
        if (!_data.option_list.empty()) {
            render();
        }
    }

    bool hasSelection() const
    {
        return !_data.option_list.empty();
    }

    lv_point_t cursorTarget()
    {
        if (_data.option_list.empty()) {
            return {0, 0};
        }

        const auto selector           = getSelectorCurrentFrame();
        const int32_t camera_x_offset = -static_cast<int32_t>(std::round(getCameraOffset().x));
        const int32_t camera_y_offset = -static_cast<int32_t>(std::round(getCameraOffset().y));
        const int32_t x = kListX + static_cast<int32_t>(std::round(selector.x + selector.width)) + camera_x_offset +
                          kCursorPaddingX - kCursorSize / 2;
        const int32_t y = kListY + static_cast<int32_t>(std::round(selector.y + selector.height / 2.0f)) +
                          camera_y_offset + kCursorOffsetY - kCursorSize / 2;
        return {x, y};
    }

private:
    struct RowData {
        std::string text;
        const lv_image_dsc_t* image = nullptr;
        int32_t width               = 0;
    };

    struct Row {
        explicit Row(lv_obj_t* parent)
            : container(std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(parent)),
              icon(std::make_unique<smooth_ui_toolkit::lvgl_cpp::Image>(container->raw_ptr())),
              label(std::make_unique<smooth_ui_toolkit::lvgl_cpp::Label>(container->raw_ptr()))
        {
            container->setSize(kMenuItemMinWidth, kMenuItemHeight);
            container->setBgOpa(LV_OPA_TRANSP);
            container->setBorderWidth(0);
            container->setShadowWidth(0);
            container->setPaddingAll(0);
            container->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
            container->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

            icon->setSize(20, 20);
            icon->setPos(kIconX, 0);

            label->setTextFont(uiFont14());
            label->setTextColor(lv_color_hex(kMutedTextColor));
            label->setLongMode(LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
            label->align(LV_ALIGN_LEFT_MID, kTextX, 0);
            container->addFlag(LV_OBJ_FLAG_HIDDEN);
        }

        void bind(size_t index, const RowData& data)
        {
            if (entryIndex == index) {
                return;
            }

            entryIndex = index;
            itemWidth  = data.width;
            text       = data.text;
            icon->setSrc(data.image);
            label->setText(text);
            container->setSize(itemWidth, kMenuItemHeight);
            label->setSize(itemWidth - kTextX - kMenuPaddingRight, LV_SIZE_CONTENT);
            container->removeFlag(LV_OBJ_FLAG_HIDDEN);
        }

        void unbind()
        {
            if (entryIndex == kUnboundIndex) {
                return;
            }
            entryIndex = kUnboundIndex;
            container->addFlag(LV_OBJ_FLAG_HIDDEN);
        }

        void setSelected(bool selected)
        {
            if (selected == isSelected) {
                return;
            }
            isSelected = selected;
            label->setTextColor(lv_color_hex(selected ? kTextColor : kMutedTextColor));
        }

        std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> container;
        std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Image> icon;
        std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> label;
        int32_t itemWidth = 0;
        std::string text;
        size_t entryIndex = kUnboundIndex;
        bool isSelected   = false;

        static constexpr size_t kUnboundIndex = static_cast<size_t>(-1);
    };

    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _panel;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _empty_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _scroll_bar;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _scroll_thumb;
    std::vector<RowData> _row_data;
    std::vector<std::unique_ptr<Row>> _rows;
    bool _pinned_to_selected = false;

    void ensureRowPool()
    {
        if (!_rows.empty()) {
            return;
        }

        _rows.reserve(kFileRowPoolSize);
        for (size_t index = 0; index < kFileRowPoolSize; ++index) {
            _rows.push_back(std::make_unique<Row>(_panel->raw_ptr()));
        }
    }

    int clampIndex(int index) const
    {
        if (_data.option_list.empty()) {
            return 0;
        }
        return std::clamp(index, 0, static_cast<int>(_data.option_list.size()) - 1);
    }

    void jumpToInstant(int index)
    {
        if (_data.option_list.empty()) {
            return;
        }

        _data.selected_option_index = clampIndex(index);
        const auto& keyframe        = _data.option_list[_data.selected_option_index].keyframe;
        getSelectorPostion().teleport(keyframe.x, keyframe.y);
        getSelectorShape().teleport(keyframe.width, keyframe.height);
        getCamera().teleport(0, cameraYFor(keyframe));
    }

    void moveToWithCamera(int index)
    {
        _data.selected_option_index = clampIndex(index);
        _update_selector_keyframe();
        _update_camera_keyframe();
    }

    void _update_camera_keyframe() override
    {
        if (_data.option_list.empty()) {
            return;
        }

        const auto& keyframe = getSelectedKeyframe();
        getCamera().move(0, _pinned_to_selected ? pinnedCameraYFor(keyframe) : cameraYFor(keyframe));
    }

    int32_t cameraYFor(const smooth_ui_toolkit::Vector4& keyframe)
    {
        int32_t offset           = static_cast<int32_t>(std::round(getCameraOffset().y));
        const int32_t top        = static_cast<int32_t>(std::round(keyframe.y));
        const int32_t bottom     = top + static_cast<int32_t>(std::round(keyframe.height));
        const int32_t max_offset = maxCameraY();

        if (top - offset < kMenuCameraPaddingY) {
            offset = top - kMenuCameraPaddingY;
        } else if (bottom - offset > kListHeight - kMenuCameraPaddingY) {
            offset = bottom - kListHeight + kMenuCameraPaddingY;
        }

        return std::clamp(offset, 0, max_offset);
    }

    int32_t maxCameraY() const
    {
        if (_data.option_list.empty()) {
            return 0;
        }

        const auto& keyframe         = _data.option_list.back().keyframe;
        const int32_t content_bottom = static_cast<int32_t>(std::round(keyframe.y + keyframe.height));
        return std::max(0, content_bottom + kMenuCameraPaddingY - kListHeight);
    }

    int32_t pinnedCameraYFor(const smooth_ui_toolkit::Vector4& keyframe) const
    {
        return std::max(0, static_cast<int32_t>(std::round(keyframe.y)));
    }

    void setupScrollBar()
    {
        _scroll_bar->setSize(kScrollBarWidth, kScrollBarHeight);
        _scroll_bar->setPos(kScrollBarX, kScrollBarY);
        _scroll_bar->setBgColor(lv_color_hex(kScrollBarColor));
        _scroll_bar->setBgOpa(LV_OPA_COVER);
        _scroll_bar->setRadius(1);
        _scroll_bar->setBorderWidth(0);
        _scroll_bar->setShadowWidth(0);
        _scroll_bar->setPaddingAll(0);
        _scroll_bar->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
        _scroll_bar->addFlag(LV_OBJ_FLAG_HIDDEN);

        _scroll_thumb->setSize(kScrollBarWidth, kScrollThumbMinH);
        _scroll_thumb->setPos(kScrollBarX, kScrollBarY);
        _scroll_thumb->setBgColor(lv_color_hex(kScrollThumbColor));
        _scroll_thumb->setBgOpa(LV_OPA_COVER);
        _scroll_thumb->setRadius(1);
        _scroll_thumb->setBorderWidth(0);
        _scroll_thumb->setShadowWidth(0);
        _scroll_thumb->setPaddingAll(0);
        _scroll_thumb->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
        _scroll_thumb->addFlag(LV_OBJ_FLAG_HIDDEN);
    }

    void setupAnimation()
    {
        auto& selector_position_options          = getSelectorPostion().x.springOptions();
        selector_position_options.visualDuration = kSelectorMoveDuration;
        selector_position_options.bounce         = kSelectorMoveBounce;
        getSelectorPostion().y.springOptions()   = selector_position_options;

        auto& selector_shape_options          = getSelectorShape().x.springOptions();
        selector_shape_options.visualDuration = kSelectorShapeDuration;
        selector_shape_options.bounce         = kSelectorShapeBounce;
        getSelectorShape().y.springOptions()  = selector_shape_options;

        auto& camera_options          = getCamera().y.springOptions();
        camera_options.visualDuration = kCameraDuration;
        camera_options.bounce         = 0.0f;
        getCamera().x.springOptions() = camera_options;
    }

    void render()
    {
        const int32_t camera_x_offset = -static_cast<int32_t>(std::round(getCameraOffset().x));
        const int32_t camera_y_offset = -static_cast<int32_t>(std::round(getCameraOffset().y));
        const int32_t selected_index  = clampIndex(_data.selected_option_index);

        const int32_t camera_y = static_cast<int32_t>(std::floor(getCameraOffset().y));
        const int32_t first    = std::max(0, camera_y / kMenuItemPitch - kFileRowOverscan);
        const int32_t last =
            std::min(static_cast<int32_t>(_row_data.size()),
                     (camera_y + kListHeight + kMenuItemPitch - 1) / kMenuItemPitch + kFileRowOverscan);

        size_t row_slot = 0;
        for (int32_t index = first; index < last && row_slot < _rows.size(); ++index, ++row_slot) {
            const auto& keyframe = _data.option_list[static_cast<size_t>(index)].keyframe;
            auto& row            = *_rows[row_slot];
            row.bind(static_cast<size_t>(index), _row_data[static_cast<size_t>(index)]);
            row.container->setPos(static_cast<int32_t>(std::round(keyframe.x)) + camera_x_offset,
                                  static_cast<int32_t>(std::round(keyframe.y)) + camera_y_offset);
            row.setSelected(index == selected_index);
        }
        for (; row_slot < _rows.size(); ++row_slot) {
            _rows[row_slot]->unbind();
        }

        renderScrollBar();
    }

    void renderScrollBar()
    {
        const int32_t max_offset = maxCameraY();
        if (max_offset <= 0) {
            _scroll_bar->addFlag(LV_OBJ_FLAG_HIDDEN);
            _scroll_thumb->addFlag(LV_OBJ_FLAG_HIDDEN);
            return;
        }

        const float offset         = std::clamp(getCameraOffset().y, 0.0f, static_cast<float>(max_offset));
        const float progress       = offset / static_cast<float>(max_offset);
        const int32_t thumb_height = std::max(
            kScrollThumbMinH,
            static_cast<int32_t>((static_cast<float>(kListHeight) / static_cast<float>(max_offset + kListHeight)) *
                                 static_cast<float>(kScrollBarHeight)));
        const int32_t thumb_travel = kScrollBarHeight - thumb_height;
        const int32_t thumb_y      = kScrollBarY + static_cast<int32_t>(std::round(progress * thumb_travel));

        _scroll_bar->removeFlag(LV_OBJ_FLAG_HIDDEN);
        _scroll_thumb->removeFlag(LV_OBJ_FLAG_HIDDEN);
        _scroll_thumb->setSize(kScrollBarWidth, thumb_height);
        _scroll_thumb->setPos(kScrollBarX, thumb_y);
    }
};

class BrowserView::MenuCursor {
public:
    explicit MenuCursor(lv_obj_t* parent) : _image(std::make_unique<smooth_ui_toolkit::lvgl_cpp::Image>(parent))
    {
        _image->setSrc(&image_cursor_hover);
        _image->setSize(kCursorSize, kCursorSize);
        _image->addFlag(LV_OBJ_FLAG_HIDDEN);
    }

    void setPressed(bool pressed)
    {
        if (_pressed == pressed) {
            return;
        }
        _pressed = pressed;
        _image->setSrc(_pressed ? &image_cursor_pressed : &image_cursor_hover);
    }

    void setTarget(bool visible, const lv_point_t& target)
    {
        if (!visible) {
            _image->addFlag(LV_OBJ_FLAG_HIDDEN);
            return;
        }

        _image->removeFlag(LV_OBJ_FLAG_HIDDEN);
        _image->setPos(target.x, target.y);
        lv_obj_move_foreground(_image->raw_ptr());
    }

private:
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Image> _image;
    bool _pressed = false;
};

class BrowserView::ActionMenu : public smooth_ui_toolkit::SmoothSelectorMenu {
public:
    explicit ActionMenu(lv_obj_t* parent)
        : _panel(std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(parent)), _y(kActionMenuHiddenY)
    {
        _panel->setSize(kListWidth + kActionMenuSideBleed * 2, kActionMenuHeight + kActionMenuBottomBleed);
        _panel->setPos(kListX - kActionMenuSideBleed, kActionMenuHiddenY);
        _panel->setBgColor(lv_color_hex(kActionMenuBgColor));
        _panel->setBorderWidth(0);
        _panel->setShadowWidth(0);
        _panel->setPadding(kActionMenuPaddingTop, kActionMenuPaddingBottom, kActionMenuPaddingLeft,
                           kActionMenuPaddingRight);
        _panel->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
        _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
        _panel->addFlag(LV_OBJ_FLAG_HIDDEN);
        _panel->setRadius(10);
        _panel->setBorderColor(lv_color_hex(0x595959));
        _panel->setBorderWidth(2);

        setupAnimation();
        setConfig().moveInLoop        = true;
        setConfig().renderInterval    = kMenuRenderInterval;
        setConfig().readInputInterval = 0;
        setCameraSize(kListWidth, kActionMenuHeight);
    }

    void setActions(std::vector<BrowserAction> actions, const FileEntry* selectedEntry)
    {
        _rows.clear();
        _data.option_list.clear();
        _data.selected_option_index = 0;

        _rows.reserve(actions.size());
        for (size_t i = 0; i < actions.size(); ++i) {
            const MenuAction action = menuActionFor(actions[i], selectedEntry);
            const int32_t width     = optionWidth(action.text);
            addOption({{0.0f, static_cast<float>(static_cast<int32_t>(i) * kMenuItemPitch), static_cast<float>(width),
                        static_cast<float>(kMenuItemHeight)},
                       nullptr});
            _rows.push_back(std::make_unique<Row>(_panel->raw_ptr(), action, width));
        }

        if (!_data.option_list.empty()) {
            jumpToInstant(0);
        }
    }

    void setOpen(bool open)
    {
        _open = open;
        if (_open) {
            _panel->removeFlag(LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(_panel->raw_ptr());
            _y.delay = 0.12;
            _y.move(kActionMenuY);
        } else {
            _y.delay = 0.0;
            _y.move(kActionMenuHiddenY);
        }
    }

    void setSelectedIndex(int index)
    {
        if (_data.option_list.empty()) {
            return;
        }
        moveToWithCamera(clampIndex(index));
    }

    void update(uint32_t nowMs) override
    {
        (void)nowMs;
        smooth_ui_toolkit::SmoothSelectorMenu::update(nowMs);
        _panel->setY(static_cast<int32_t>(std::round(_y.value())));
        if (!_open && _y.done()) {
            _panel->addFlag(LV_OBJ_FLAG_HIDDEN);
        }
        render();
    }

    lv_point_t cursorTarget()
    {
        if (_data.option_list.empty()) {
            return {0, 0};
        }

        const auto selector           = getSelectorCurrentFrame();
        const int32_t camera_y_offset = -static_cast<int32_t>(std::round(getCameraOffset().y));
        const int32_t x               = kListX + kActionMenuPaddingLeft +
                          static_cast<int32_t>(std::round(selector.x + selector.width)) + kCursorPaddingX -
                          kCursorSize / 2;
        const int32_t y = static_cast<int32_t>(std::round(_y.directValue())) + kActionMenuPaddingTop +
                          static_cast<int32_t>(std::round(selector.y + selector.height / 2.0f)) + kCursorOffsetY -
                          kCursorSize / 2 + camera_y_offset + 2;
        return {x, y};
    }

private:
    struct Row {
        Row(lv_obj_t* parent, const MenuAction& action, int32_t width)
            : container(std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(parent)),
              icon(std::make_unique<smooth_ui_toolkit::lvgl_cpp::Image>(container->raw_ptr())),
              label(std::make_unique<smooth_ui_toolkit::lvgl_cpp::Label>(container->raw_ptr())),
              itemWidth(width)
        {
            container->setSize(itemWidth, kMenuItemHeight);
            container->setBgOpa(LV_OPA_TRANSP);
            container->setBorderWidth(0);
            container->setShadowWidth(0);
            container->setPaddingAll(0);
            container->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
            container->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

            icon->setSrc(action.icon);
            icon->setSize(20, 20);
            icon->setPos(kIconX, 0);

            label->setText(action.text);
            label->setTextFont(uiFont14());
            label->setTextColor(lv_color_hex(kTextColor));
            label->setLongMode(LV_LABEL_LONG_DOT);
            label->setSize(itemWidth - kTextX - kMenuPaddingRight, LV_SIZE_CONTENT);
            label->align(LV_ALIGN_LEFT_MID, kTextX, 0);
        }

        std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> container;
        std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Image> icon;
        std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> label;
        int32_t itemWidth = 0;
    };

    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _panel;
    std::vector<std::unique_ptr<Row>> _rows;
    smooth_ui_toolkit::AnimateValue _y;
    bool _open = false;

    int clampIndex(int index) const
    {
        if (_data.option_list.empty()) {
            return 0;
        }
        return std::clamp(index, 0, static_cast<int>(_data.option_list.size()) - 1);
    }

    void jumpToInstant(int index)
    {
        _data.selected_option_index = clampIndex(index);
        const auto& keyframe        = _data.option_list[_data.selected_option_index].keyframe;
        getSelectorPostion().teleport(keyframe.x, keyframe.y);
        getSelectorShape().teleport(keyframe.width, keyframe.height);
        getCamera().teleport(0, cameraYFor(keyframe));
    }

    void moveToWithCamera(int index)
    {
        _data.selected_option_index = clampIndex(index);
        _update_selector_keyframe();
        _update_camera_keyframe();
    }

    void _update_camera_keyframe() override
    {
        if (_data.option_list.empty()) {
            return;
        }

        getCamera().move(0, cameraYFor(getSelectedKeyframe()));
    }

    void setupAnimation()
    {
        _y.springOptions().visualDuration = kActionMenuDuration;
        _y.springOptions().bounce         = 0.28f;

        auto& selector_position_options          = getSelectorPostion().x.springOptions();
        selector_position_options.visualDuration = kSelectorMoveDuration;
        selector_position_options.bounce         = kSelectorMoveBounce;
        getSelectorPostion().y.springOptions()   = selector_position_options;

        auto& selector_shape_options          = getSelectorShape().x.springOptions();
        selector_shape_options.visualDuration = kSelectorShapeDuration;
        selector_shape_options.bounce         = kSelectorShapeBounce;
        getSelectorShape().y.springOptions()  = selector_shape_options;

        _y.begin();
    }

    void render()
    {
        const int32_t camera_y_offset = -static_cast<int32_t>(std::round(getCameraOffset().y));
        const int32_t selected_index  = clampIndex(_data.selected_option_index);
        for (size_t i = 0; i < _rows.size() && i < _data.option_list.size(); ++i) {
            const auto& keyframe = _data.option_list[i].keyframe;
            auto& row            = *_rows[i];
            row.container->setSize(row.itemWidth, kMenuItemHeight);
            row.container->setPos(static_cast<int32_t>(std::round(keyframe.x)),
                                  static_cast<int32_t>(std::round(keyframe.y)) + camera_y_offset);
            row.label->setTextColor(
                lv_color_hex(static_cast<int32_t>(i) == selected_index ? kTextColor : kMutedTextColor));
        }
    }

    int32_t cameraYFor(const smooth_ui_toolkit::Vector4& keyframe)
    {
        int32_t offset           = static_cast<int32_t>(std::round(getCameraOffset().y));
        const int32_t top        = kActionMenuPaddingTop + static_cast<int32_t>(std::round(keyframe.y));
        const int32_t bottom     = top + static_cast<int32_t>(std::round(keyframe.height));
        const int32_t max_offset = maxCameraY();

        if (top - offset < kActionMenuCameraPaddingY) {
            offset = top - kActionMenuCameraPaddingY;
        } else if (bottom - offset > kActionMenuHeight - kActionMenuCameraPaddingY) {
            offset = bottom - kActionMenuHeight + kActionMenuCameraPaddingY;
        }

        return std::clamp(offset, 0, max_offset);
    }

    int32_t maxCameraY() const
    {
        if (_data.option_list.empty()) {
            return 0;
        }

        const auto& keyframe         = _data.option_list.back().keyframe;
        const int32_t content_bottom = kActionMenuPaddingTop +
                                       static_cast<int32_t>(std::round(keyframe.y + keyframe.height)) +
                                       kActionMenuPaddingBottom;
        return std::max(0, content_bottom - kActionMenuHeight);
    }
};

class BrowserView::DeleteConfirmDialog {
public:
    explicit DeleteConfirmDialog(lv_obj_t* parent)
        : _panel(std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(parent)),
          _prompt_label(std::make_unique<smooth_ui_toolkit::lvgl_cpp::Label>(_panel->raw_ptr())),
          _name_label(std::make_unique<smooth_ui_toolkit::lvgl_cpp::Label>(_panel->raw_ptr())),
          _cancel_button(std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(_panel->raw_ptr())),
          _confirm_button(std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(_panel->raw_ptr())),
          _cancel_label(std::make_unique<smooth_ui_toolkit::lvgl_cpp::Label>(_cancel_button->raw_ptr())),
          _confirm_label(std::make_unique<smooth_ui_toolkit::lvgl_cpp::Label>(_confirm_button->raw_ptr())),
          _x(kDeleteOpenOriginX),
          _y(kDeleteOpenOriginY),
          _width(kDeleteOpenOriginWidth),
          _height(kDeleteOpenOriginHeight)
    {
        configureAnimation();
        applyAnimatedValue();
        _panel->setBgColor(lv_color_hex(0x474747));
        _panel->setBgOpa(LV_OPA_COVER);
        _panel->setRadius(kDeleteDialogRadius);
        _panel->setBorderWidth(0);
        _panel->setShadowWidth(0);
        _panel->setPaddingAll(0);
        _panel->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
        _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
        _panel->addFlag(LV_OBJ_FLAG_HIDDEN);

        setupPrompt();
        setupNameArea();
        setupButton(*_cancel_button, *_cancel_label, -46, 102, "ESC: Cancel", lv_color_hex(0x6D6D6D),
                    lv_color_hex(0xF3F3F3));
        setupButton(*_confirm_button, *_confirm_label, 67, 106, "Enter: Delete", lv_color_hex(0xC33630),
                    lv_color_hex(0xFFECEC));
    }

    void setPending(const PendingDeleteFile& pending)
    {
        if (pending.active) {
            _name_label->setText(pending.file.name.c_str());
            _panel->removeFlag(LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(_panel->raw_ptr());
            _visible = true;
            configureAnimation();
            _x.teleport(kDeleteOpenOriginX);
            _y.teleport(kDeleteOpenOriginY);
            _width.teleport(kDeleteOpenOriginWidth);
            _height.teleport(kDeleteOpenOriginHeight);
            applyAnimatedValue();
            _x.move(0);
            _y.move(kDeleteDialogY);
            _width.move(kDeleteDialogWidth);
            _height.move(kDeleteDialogHeight);
        } else {
            _visible = false;
            closeToOpenOrigin();
        }
    }

    void tick()
    {
        _x.update();
        _y.update();
        _width.update();
        _height.update();
        applyAnimatedValue();

        if (!_visible && _x.done() && _y.done() && _width.done() && _height.done()) {
            _panel->addFlag(LV_OBJ_FLAG_HIDDEN);
        }
    }

private:
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _panel;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _prompt_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _name_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _cancel_button;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _confirm_button;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _cancel_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _confirm_label;
    smooth_ui_toolkit::AnimateValue _x;
    smooth_ui_toolkit::AnimateValue _y;
    smooth_ui_toolkit::AnimateValue _width;
    smooth_ui_toolkit::AnimateValue _height;
    bool _visible = false;

    static void setupAnimation(smooth_ui_toolkit::AnimateValue& value, float duration, float bounce)
    {
        value.springOptions().visualDuration = duration;
        value.springOptions().bounce         = bounce;
    }

    void configureAnimation()
    {
        setupAnimation(_x, 0.35f, 0.4f);
        setupAnimation(_y, 0.35f, 0.3f);
        setupAnimation(_width, 0.35f, 0.2f);
        setupAnimation(_height, 0.35f, 0.2f);
        _y.delay = 0.0;
    }

    void closeToOpenOrigin()
    {
        configureAnimation();
        _x.move(kDeleteOpenOriginX);
        _y.move(kDeleteOpenOriginY);
        _width.move(kDeleteOpenOriginWidth);
        _height.move(kDeleteOpenOriginHeight);
    }

    void applyAnimatedValue()
    {
        _panel->setSize(static_cast<int32_t>(std::round(_width.directValue())),
                        static_cast<int32_t>(std::round(_height.directValue())));
        _panel->align(LV_ALIGN_CENTER, static_cast<int32_t>(std::round(_x.directValue())),
                      static_cast<int32_t>(std::round(_y.directValue())));
    }

    void setupPrompt()
    {
        _prompt_label->setText("Delete file?");
        _prompt_label->setTextFont(uiFont14());
        _prompt_label->setTextColor(lv_color_hex(0xBCBCBC));
        _prompt_label->setTextAlign(LV_TEXT_ALIGN_LEFT);
        _prompt_label->setSize(220, LV_SIZE_CONTENT);
        _prompt_label->align(LV_ALIGN_CENTER, 0, kDeletePromptCenterY);
    }

    void setupNameArea()
    {
        _name_label->setText("");
        _name_label->setTextFont(uiFont14());
        _name_label->setTextColor(lv_color_hex(0xFFFFFF));
        _name_label->setLongMode(LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
        _name_label->setSize(kDeleteNameAreaWidth - 18, LV_SIZE_CONTENT);
        _name_label->align(LV_ALIGN_CENTER, 0, kDeleteNameAreaCenterY);
    }

    void setupButton(smooth_ui_toolkit::lvgl_cpp::Container& button, smooth_ui_toolkit::lvgl_cpp::Label& label,
                     int32_t x, int32_t width, const char* text, lv_color_t bgColor, lv_color_t labelColor)
    {
        button.setSize(width, kDeleteButtonHeight);
        button.align(LV_ALIGN_CENTER, x, kDeleteButtonCenterY);
        button.setBgColor(bgColor);
        button.setBgOpa(LV_OPA_COVER);
        button.setRadius(kDeleteButtonRadius);
        button.setBorderWidth(0);
        button.setShadowWidth(0);
        button.setPaddingAll(0);
        button.removeFlag(LV_OBJ_FLAG_SCROLLABLE);

        label.setText(text);
        label.setTextFont(uiMonoFont12());
        label.setTextColor(labelColor);
        label.setTextAlign(LV_TEXT_ALIGN_CENTER);
        label.setSize(width, LV_SIZE_CONTENT);
        label.align(LV_ALIGN_CENTER, 0, 0);
    }
};

class BrowserView::RenameConfirmDialog {
public:
    RenameConfirmDialog(lv_obj_t* parent, BrowserViewModel& viewModel)
        : _view_model(viewModel),
          _panel(std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(parent)),
          _prompt_label(std::make_unique<smooth_ui_toolkit::lvgl_cpp::Label>(_panel->raw_ptr())),
          _input(std::make_unique<smooth_ui_toolkit::lvgl_cpp::TextArea>(_panel->raw_ptr())),
          _cancel_button(std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(_panel->raw_ptr())),
          _confirm_button(std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(_panel->raw_ptr())),
          _cancel_label(std::make_unique<smooth_ui_toolkit::lvgl_cpp::Label>(_cancel_button->raw_ptr())),
          _confirm_label(std::make_unique<smooth_ui_toolkit::lvgl_cpp::Label>(_confirm_button->raw_ptr())),
          _x(kDeleteOpenOriginX),
          _y(kDeleteOpenOriginY),
          _width(kDeleteOpenOriginWidth),
          _height(kDeleteOpenOriginHeight)
    {
        configureAnimation();
        applyAnimatedValue();
        _panel->setBgColor(lv_color_hex(0x474747));
        _panel->setBgOpa(LV_OPA_COVER);
        _panel->setRadius(kDeleteDialogRadius);
        _panel->setBorderWidth(0);
        _panel->setShadowWidth(0);
        _panel->setPaddingAll(0);
        _panel->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
        _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
        _panel->addFlag(LV_OBJ_FLAG_HIDDEN);

        setupPrompt();
        setupInput();
        setupButton(*_cancel_button, *_cancel_label, -46, 102, "ESC: Cancel", lv_color_hex(0x6D6D6D),
                    lv_color_hex(0xF3F3F3), onCancelClicked);
        setupButton(*_confirm_button, *_confirm_label, 67, 87, "Enter: OK", lv_color_hex(0xFED40D),
                    lv_color_hex(0x5E4D00), onConfirmClicked);
    }

    ~RenameConfirmDialog()
    {
        removeInputFromGroup();
    }

    void setPending(const PendingRenameFile& pending)
    {
        if (pending.active) {
            _panel->removeFlag(LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(_panel->raw_ptr());
            _visible = true;
            configureAnimation();
            _x.teleport(kDeleteOpenOriginX);
            _y.teleport(kDeleteOpenOriginY);
            _width.teleport(kDeleteOpenOriginWidth);
            _height.teleport(kDeleteOpenOriginHeight);
            applyAnimatedValue();
            _x.move(0);
            _y.move(kDeleteDialogY);
            _width.move(kDeleteDialogWidth);
            _height.move(kDeleteDialogHeight);
            focusInput();
        } else {
            removeInputFromGroup();
            _visible = false;
            closeToOpenOrigin();
        }
    }

    void setName(const std::string& name)
    {
        const char* current = lv_textarea_get_text(_input->raw_ptr());
        if (current && name == current) {
            return;
        }

        _updating_text = true;
        _input->setText(name);
        _input->setCursorPos(static_cast<int32_t>(name.size()));
        _updating_text = false;
    }

    void tick()
    {
        _x.update();
        _y.update();
        _width.update();
        _height.update();
        applyAnimatedValue();

        if (!_visible && _x.done() && _y.done() && _width.done() && _height.done()) {
            _panel->addFlag(LV_OBJ_FLAG_HIDDEN);
        }
    }

private:
    BrowserViewModel& _view_model;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _panel;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _prompt_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::TextArea> _input;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _cancel_button;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _confirm_button;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _cancel_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _confirm_label;
    smooth_ui_toolkit::AnimateValue _x;
    smooth_ui_toolkit::AnimateValue _y;
    smooth_ui_toolkit::AnimateValue _width;
    smooth_ui_toolkit::AnimateValue _height;
    bool _input_in_group = false;
    bool _updating_text  = false;
    bool _visible        = false;

    static void setupAnimation(smooth_ui_toolkit::AnimateValue& value, float duration, float bounce)
    {
        value.springOptions().visualDuration = duration;
        value.springOptions().bounce         = bounce;
    }

    void configureAnimation()
    {
        setupAnimation(_x, 0.35f, 0.4f);
        setupAnimation(_y, 0.35f, 0.3f);
        setupAnimation(_width, 0.35f, 0.2f);
        setupAnimation(_height, 0.35f, 0.2f);
        _y.delay = 0.0;
    }

    void closeToOpenOrigin()
    {
        configureAnimation();
        _x.move(kDeleteOpenOriginX);
        _y.move(kDeleteOpenOriginY);
        _width.move(kDeleteOpenOriginWidth);
        _height.move(kDeleteOpenOriginHeight);
    }

    void applyAnimatedValue()
    {
        _panel->setSize(static_cast<int32_t>(std::round(_width.directValue())),
                        static_cast<int32_t>(std::round(_height.directValue())));
        _panel->align(LV_ALIGN_CENTER, static_cast<int32_t>(std::round(_x.directValue())),
                      static_cast<int32_t>(std::round(_y.directValue())));
    }

    void setupPrompt()
    {
        _prompt_label->setText("Rename file");
        _prompt_label->setTextFont(uiFont14());
        _prompt_label->setTextColor(lv_color_hex(0xA1A1A1));
        _prompt_label->setTextAlign(LV_TEXT_ALIGN_LEFT);
        _prompt_label->setSize(230, LV_SIZE_CONTENT);
        _prompt_label->align(LV_ALIGN_CENTER, 0, kDeletePromptCenterY);
    }

    void setupInput()
    {
        _input->setSize(kDeleteNameAreaWidth, 34);
        _input->align(LV_ALIGN_CENTER, 0, kDeleteNameAreaCenterY);
        _input->setBgColor(lv_color_hex(0x676767));
        _input->setBgOpa(LV_OPA_COVER);
        _input->setRadius(5);
        _input->setBorderWidth(0);
        _input->setShadowWidth(0);
        _input->setPadding(4, 4, 9, 9);
        _input->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
        _input->setTextFont(uiFont14());
        _input->setTextColor(lv_color_hex(0xFFFFFF));
        _input->setOneLine(true);
        _input->setMaxLength(128);
        _input->addEventCb(onInputValueChanged, LV_EVENT_VALUE_CHANGED, this);
        _input->setOutlineWidth(0, LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY);
    }

    void setupButton(smooth_ui_toolkit::lvgl_cpp::Container& button, smooth_ui_toolkit::lvgl_cpp::Label& label,
                     int32_t x, int32_t width, const char* text, lv_color_t bgColor, lv_color_t labelColor,
                     lv_event_cb_t callback)
    {
        button.setSize(width, kDeleteButtonHeight);
        button.align(LV_ALIGN_CENTER, x, kDeleteButtonCenterY);
        button.setBgColor(bgColor);
        button.setBgOpa(LV_OPA_COVER);
        button.setRadius(kDeleteButtonRadius);
        button.setBorderWidth(0);
        button.setShadowWidth(0);
        button.setPaddingAll(0);
        button.removeFlag(LV_OBJ_FLAG_SCROLLABLE);
        button.addFlag(LV_OBJ_FLAG_CLICKABLE);
        button.addEventCb(callback, LV_EVENT_CLICKED, this);

        label.setText(text);
        label.setTextFont(uiFont14());
        label.setTextColor(labelColor);
        label.setTextAlign(LV_TEXT_ALIGN_CENTER);
        label.center();
    }

    void focusInput()
    {
        lv_group_t* group = keyboardGroup();
        if (!group) {
            return;
        }

        if (!_input_in_group) {
            lv_group_add_obj(group, _input->raw_ptr());
            _input_in_group = true;
        }
        lv_group_focus_obj(_input->raw_ptr());
    }

    void removeInputFromGroup()
    {
        if (!_input_in_group) {
            return;
        }

        lv_group_remove_obj(_input->raw_ptr());
        _input_in_group = false;
    }

    static void onInputValueChanged(lv_event_t* event)
    {
        auto* self = static_cast<RenameConfirmDialog*>(lv_event_get_user_data(event));
        if (!self || self->_updating_text) {
            return;
        }

        const char* text = lv_textarea_get_text(self->_input->raw_ptr());
        self->_view_model.setPendingRenameName(text ? text : "");
    }

    static void onCancelClicked(lv_event_t* event)
    {
        auto* self = static_cast<RenameConfirmDialog*>(lv_event_get_user_data(event));
        if (self) {
            self->_view_model.cancelRename();
        }
    }

    static void onConfirmClicked(lv_event_t* event)
    {
        auto* self = static_cast<RenameConfirmDialog*>(lv_event_get_user_data(event));
        if (self) {
            self->_view_model.confirmRename();
        }
    }
};

class BrowserView::TipsHud {
public:
    explicit TipsHud(lv_obj_t* parent)
        : _image(std::make_unique<smooth_ui_toolkit::lvgl_cpp::Image>(parent)), _y(kHudHiddenY)
    {
        _image->setSrc(&image_hud);
        _image->setPos(0, kHudHiddenY);

        _y.springOptions().visualDuration = 0.4;
        _y.springOptions().bounce         = 0.22f;

        _y.begin();
        _y.move(kHudShownY);
    }

    void tick(uint32_t nowMs)
    {
        if (_shown_at_ms == 0) {
            _shown_at_ms = nowMs;
        }
        if (!_closing && nowMs >= _shown_at_ms + kHudHoldMs) {
            _closing                          = true;
            _y.springOptions().visualDuration = kHudCloseDuration;
            _y.springOptions().bounce         = 0.0f;
            _y.move(kHudHiddenY);
        }

        _image->setY(static_cast<int32_t>(std::round(_y.value())));
    }

    void dismiss()
    {
        if (_closing) {
            return;
        }
        _closing                          = true;
        _y.springOptions().visualDuration = kHudCloseDuration;
        _y.springOptions().bounce         = 0.0f;
        _y.move(kHudHiddenY);
    }

private:
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Image> _image;
    smooth_ui_toolkit::AnimateValue _y;
    uint32_t _shown_at_ms = 0;
    bool _closing         = false;
};

BrowserView::BrowserView(BrowserViewModel& vm) : _vm(vm)
{
}

BrowserView::~BrowserView()
{
    destroy();
}

void BrowserView::onEnter(lv_obj_t* parent)
{
    _render_ready = false;

    _root = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(parent);
    _root->setSize(kScreenWidth, kScreenHeight);
    _root->setBgColor(lv_color_hex(0x000000));
    _root->setBgOpa(LV_OPA_COVER);
    _root->setBorderWidth(0);
    _root->setPaddingAll(0);
    _root->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
    _root->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _path_bar = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(_root->raw_ptr());
    _path_bar->setSize(kScreenWidth, kPathBarHeight);
    _path_bar->setPos(0, 0);
    _path_bar->setBgColor(lv_color_hex(kPathBarColor));
    _path_bar->setBgOpa(LV_OPA_COVER);
    _path_bar->setBorderWidth(0);
    _path_bar->setRadius(0);
    _path_bar->setPaddingAll(0);
    _path_bar->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _path_label = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Label>(_path_bar->raw_ptr());
    _path_label->setTextFont(uiFont10());
    _path_label->setTextColor(lv_color_hex(0xAAAAAA));
    _path_label->setLongMode(LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    const int32_t path_text_height = lv_font_get_line_height(uiFont10());
    _path_label->setSize(kPathTextWidth, path_text_height);
    _path_label->setPos(kPathTextX, std::max<int32_t>(0, (kPathBarHeight - path_text_height) / 2));

    _file_list             = std::make_unique<FilesMenu>(_root->raw_ptr());
    _action_menu           = std::make_unique<ActionMenu>(_root->raw_ptr());
    _cursor                = std::make_unique<MenuCursor>(_root->raw_ptr());
    _delete_confirm_dialog = std::make_unique<DeleteConfirmDialog>(_root->raw_ptr());
    _rename_confirm_dialog = std::make_unique<RenameConfirmDialog>(_root->raw_ptr(), _vm);
    _magic_view            = std::make_unique<MagicView>(_root->raw_ptr());
    if (!_tips_hud_shown) {
        _tips_hud       = std::make_unique<TipsHud>(_root->raw_ptr());
        _tips_hud_shown = true;
    }

    _vm.currentDirectory().observe(this, onDirectoryChanged);
    _vm.entries().observe(this, onEntriesChanged);
    _vm.selectedIndex().observe(this, onSelectedIndexChanged);
    _vm.status().observe(this, onStatusChanged);
    _vm.enterPressed().observe(this, onEnterPressedChanged);
    _vm.actionMenuOpen().observe(this, onActionMenuOpenChanged);
    _vm.actionMenuSelectedIndex().observe(this, onActionMenuSelectedIndexChanged);
    _vm.pendingDelete().observe(this, onPendingDeleteChanged);
    _vm.pendingRename().observe(this, onPendingRenameChanged);
    _vm.pendingRenameName().observe(this, onPendingRenameNameChanged);
    _magic_serial_seen = _vm.magic().get();
    _vm.magic().observe(this, onMagicChanged);

    _render_ready = true;
}

void BrowserView::onExit()
{
    destroy();
}

void BrowserView::tick(uint32_t nowMs)
{
    if (_file_list) {
        _file_list->update(nowMs);
    }
    if (_action_menu) {
        _action_menu->update(nowMs);
    }
    if (_delete_confirm_dialog) {
        _delete_confirm_dialog->tick();
    }
    if (_rename_confirm_dialog) {
        _rename_confirm_dialog->tick();
    }
    updateCursorTarget();
    if (_tips_hud) {
        _tips_hud->tick(nowMs);
    }
    if (_magic_view) {
        _magic_view->tick(nowMs);
    }
}

void BrowserView::destroy()
{
    _render_ready = false;
    _vm.magic().removeObserver();
    _vm.pendingRenameName().removeObserver();
    _vm.pendingRename().removeObserver();
    _vm.pendingDelete().removeObserver();
    _vm.actionMenuSelectedIndex().removeObserver();
    _vm.actionMenuOpen().removeObserver();
    _vm.enterPressed().removeObserver();
    _vm.status().removeObserver();
    _vm.selectedIndex().removeObserver();
    _vm.entries().removeObserver();
    _vm.currentDirectory().removeObserver();
    _magic_view.reset();
    _tips_hud.reset();
    _rename_confirm_dialog.reset();
    _delete_confirm_dialog.reset();
    _cursor.reset();
    _action_menu.reset();
    _file_list.reset();
    _path_label.reset();
    _path_bar.reset();
    _root.reset();
}

void BrowserView::dismissTipsHud()
{
    if (_render_ready && _tips_hud) {
        _tips_hud->dismiss();
    }
}

void BrowserView::renderDirectory(const std::string& directory)
{
    dismissTipsHud();
    if (_path_label) {
        _path_label->setText(compactPath(directory).c_str());
    }
}

void BrowserView::renderEntries(const std::vector<FileEntry>& entries)
{
    if (_file_list) {
        _file_list->setEntries(entries, _vm.selectedIndex().get());
    }
    updateCursorTarget();
}

void BrowserView::renderSelectedIndex(int index)
{
    dismissTipsHud();
    if (_file_list) {
        _file_list->setSelectedIndex(index);
    }
    updateCursorTarget();
}

void BrowserView::renderStatus(const std::string& status)
{
    (void)status;
}

void BrowserView::renderEnterPressed(bool pressed)
{
    if (pressed) {
        dismissTipsHud();
    }
    if (_cursor) {
        _cursor->setPressed(pressed);
    }
}

void BrowserView::renderActionMenuOpen(bool open)
{
    if (open) {
        dismissTipsHud();
    }
    if (_file_list) {
        _file_list->setPinnedToSelected(open);
    }
    if (_action_menu) {
        if (open) {
            std::vector<BrowserAction> actions;
            actions.reserve(static_cast<size_t>(_vm.actionCount()));
            for (int i = 0; i < _vm.actionCount(); ++i) {
                actions.push_back(_vm.actionAt(i));
            }
            _action_menu->setActions(std::move(actions), _vm.selectedEntry());
        }
        _action_menu->setOpen(open);
    }
    updateCursorTarget();
}

void BrowserView::renderActionMenuSelectedIndex(int index)
{
    dismissTipsHud();
    if (_action_menu) {
        _action_menu->setSelectedIndex(index);
    }
    updateCursorTarget();
}

void BrowserView::renderPendingDelete(const PendingDeleteFile& pending)
{
    if (pending.active) {
        dismissTipsHud();
    }
    if (_delete_confirm_dialog) {
        _delete_confirm_dialog->setPending(pending);
    }
    updateCursorTarget();
}

void BrowserView::renderPendingRename(const PendingRenameFile& pending)
{
    if (pending.active) {
        dismissTipsHud();
    }
    if (_rename_confirm_dialog) {
        _rename_confirm_dialog->setPending(pending);
    }
    updateCursorTarget();
}

void BrowserView::renderPendingRenameName(const std::string& name)
{
    if (_rename_confirm_dialog) {
        _rename_confirm_dialog->setName(name);
    }
}

void BrowserView::renderMagic(uint32_t magicSerial)
{
    if (magicSerial == 0 || magicSerial == _magic_serial_seen) {
        return;
    }

    _magic_serial_seen = magicSerial;
    if (_magic_view) {
        _magic_view->generate(magicSerial);
    }
}

void BrowserView::updateCursorTarget()
{
    if (!_cursor) {
        return;
    }

    if (_vm.pendingDelete().get().active || _vm.pendingRename().get().active) {
        _cursor->setTarget(false, {0, 0});
        return;
    }

    if (_action_menu && _vm.actionMenuOpen().get()) {
        _cursor->setTarget(true, _action_menu->cursorTarget());
        return;
    }

    if (_file_list) {
        _cursor->setTarget(_file_list->hasSelection(), _file_list->cursorTarget());
        return;
    }

    _cursor->setTarget(false, {0, 0});
}

void BrowserView::onDirectoryChanged(void* context, const std::string& directory)
{
    static_cast<BrowserView*>(context)->renderDirectory(directory);
}

void BrowserView::onEntriesChanged(void* context, const std::vector<FileEntry>& entries)
{
    static_cast<BrowserView*>(context)->renderEntries(entries);
}

void BrowserView::onSelectedIndexChanged(void* context, const int& index)
{
    static_cast<BrowserView*>(context)->renderSelectedIndex(index);
}

void BrowserView::onStatusChanged(void* context, const std::string& status)
{
    static_cast<BrowserView*>(context)->renderStatus(status);
}

void BrowserView::onEnterPressedChanged(void* context, const bool& pressed)
{
    static_cast<BrowserView*>(context)->renderEnterPressed(pressed);
}

void BrowserView::onActionMenuOpenChanged(void* context, const bool& open)
{
    static_cast<BrowserView*>(context)->renderActionMenuOpen(open);
}

void BrowserView::onActionMenuSelectedIndexChanged(void* context, const int& index)
{
    static_cast<BrowserView*>(context)->renderActionMenuSelectedIndex(index);
}

void BrowserView::onPendingDeleteChanged(void* context, const PendingDeleteFile& pending)
{
    static_cast<BrowserView*>(context)->renderPendingDelete(pending);
}

void BrowserView::onPendingRenameChanged(void* context, const PendingRenameFile& pending)
{
    static_cast<BrowserView*>(context)->renderPendingRename(pending);
}

void BrowserView::onPendingRenameNameChanged(void* context, const std::string& name)
{
    static_cast<BrowserView*>(context)->renderPendingRenameName(name);
}

void BrowserView::onMagicChanged(void* context, const uint32_t& magicSerial)
{
    static_cast<BrowserView*>(context)->renderMagic(magicSerial);
}

}  // namespace files
