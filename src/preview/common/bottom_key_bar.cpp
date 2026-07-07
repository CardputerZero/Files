#include "preview/common/bottom_key_bar.hpp"

#include <core/animation/animate_value/animate_value.hpp>
#include <core/easing/ease.hpp>
#include <lvgl/lvgl_cpp/image.hpp>
#include <lvgl/lvgl_cpp/obj.hpp>
#include <algorithm>
#include <array>
#include <cmath>

namespace files {
namespace {

constexpr int32_t kIndicatorY      = 82;
constexpr int32_t kIndicatorWidth  = 2;
constexpr int32_t kIndicatorHeight = 6;
constexpr int32_t kIconY           = 64;
constexpr uint32_t kIndicatorColor = 0x656565;
constexpr float kFadeDuration      = 0.16f;

lv_opa_t toOpa(float value)
{
    return static_cast<lv_opa_t>(std::clamp(static_cast<int>(std::round(value)), 0, 255));
}

void setupFadeAnimation(smooth_ui_toolkit::AnimateValue& value)
{
    value.easingOptions().duration       = kFadeDuration;
    value.easingOptions().easingFunction = smooth_ui_toolkit::ease::ease_out_quad;
}

}  // namespace

class BottomKeyBar::Indicator {
public:
    Indicator(lv_obj_t* parent, int32_t x) : _bar(std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(parent))
    {
        setupFadeAnimation(_opa);

        _bar->setSize(kIndicatorWidth, kIndicatorHeight);
        _bar->align(LV_ALIGN_CENTER, x, kIndicatorY);
        _bar->setBgColor(lv_color_hex(kIndicatorColor));
        _bar->setBgOpa(LV_OPA_COVER);
        _bar->setBorderWidth(0);
        _bar->setShadowWidth(0);
        _bar->setPaddingAll(0);
        _bar->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
        _bar->addFlag(LV_OBJ_FLAG_HIDDEN);
    }

    void setVisible(bool visible)
    {
        if (visible == _visible) {
            return;
        }

        _opa.update();
        _visible = visible;
        if (_visible) {
            _bar->removeFlag(LV_OBJ_FLAG_HIDDEN);
            _opa.move(255);
        } else {
            _opa.move(0);
        }
        applyOpacity();
    }

    void tick()
    {
        _opa.update();
        applyOpacity();

        if (!_visible && _opa.done() && _opa.directValue() <= 0.0f) {
            _bar->addFlag(LV_OBJ_FLAG_HIDDEN);
        }
    }

private:
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _bar;
    smooth_ui_toolkit::AnimateValue _opa{0};
    bool _visible = false;

    void applyOpacity()
    {
        _bar->setOpa(toOpa(_opa.directValue()));
    }
};

class BottomKeyBar::KeyIcon {
public:
    KeyIcon(lv_obj_t* parent, int32_t x)
        : _current(std::make_unique<smooth_ui_toolkit::lvgl_cpp::Image>(parent)),
          _previous(std::make_unique<smooth_ui_toolkit::lvgl_cpp::Image>(parent)),
          _current_opa(0),
          _previous_opa(0)
    {
        setupFadeAnimation(_current_opa);
        setupFadeAnimation(_previous_opa);

        _current->align(LV_ALIGN_CENTER, x, kIconY);
        _previous->align(LV_ALIGN_CENTER, x, kIconY);
        _current->addFlag(LV_OBJ_FLAG_HIDDEN);
        _previous->addFlag(LV_OBJ_FLAG_HIDDEN);
    }

    void setImage(const void* image)
    {
        if (image == _image) {
            return;
        }

        _current_opa.update();
        _previous_opa.update();
        applyOpacity();

        const void* old_image = _image;
        _image                = image;

        if (old_image) {
            _previous->setSrc(old_image);
            _previous->removeFlag(LV_OBJ_FLAG_HIDDEN);
            _previous_opa.teleport(_current_opa.directValue());
            _previous_opa.move(0);
        }

        if (_image) {
            _current->setSrc(_image);
            _current->removeFlag(LV_OBJ_FLAG_HIDDEN);
            _current_opa.teleport(0);
            _current_opa.move(255);
        } else {
            _current->addFlag(LV_OBJ_FLAG_HIDDEN);
            _current_opa.teleport(0);
        }

        applyOpacity();
    }

    bool hasImage() const
    {
        return _image != nullptr;
    }

    void tick()
    {
        _current_opa.update();
        _previous_opa.update();
        applyOpacity();

        if (!_image) {
            _current->addFlag(LV_OBJ_FLAG_HIDDEN);
        }
        if (_previous_opa.done() && _previous_opa.directValue() <= 0.0f) {
            _previous->addFlag(LV_OBJ_FLAG_HIDDEN);
        }
    }

private:
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Image> _current;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Image> _previous;
    smooth_ui_toolkit::AnimateValue _current_opa;
    smooth_ui_toolkit::AnimateValue _previous_opa;
    const void* _image = nullptr;

    void applyOpacity()
    {
        _current->setOpa(toOpa(_current_opa.directValue()));
        _previous->setOpa(toOpa(_previous_opa.directValue()));
    }
};

BottomKeyBar::BottomKeyBar(lv_obj_t* parent)
    : _slots{KeySlot{'4', -112, nullptr, nullptr}, KeySlot{'5', -56, nullptr, nullptr},
             KeySlot{'6', 0, nullptr, nullptr}, KeySlot{'7', 56, nullptr, nullptr}, KeySlot{'8', 112, nullptr, nullptr}}
{
    for (auto& slot : _slots) {
        slot.icon      = std::make_unique<KeyIcon>(parent, slot.x);
        slot.indicator = std::make_unique<Indicator>(parent, slot.x);
    }
}

BottomKeyBar::~BottomKeyBar() = default;

void BottomKeyBar::setItems(std::initializer_list<Item> items)
{
    std::array<const void*, 5> images{};

    for (const auto& item : items) {
        auto* slot = slotFor(item.key);
        if (!slot) {
            continue;
        }

        const auto index = static_cast<size_t>(slot - _slots.data());
        images[index]    = item.image;
    }

    for (size_t i = 0; i < _slots.size(); ++i) {
        _slots[i].icon->setImage(images[i]);
        _slots[i].indicator->setVisible(_slots[i].icon->hasImage());
    }
}

void BottomKeyBar::tick()
{
    for (auto& slot : _slots) {
        slot.icon->tick();
        slot.indicator->tick();
    }
}

BottomKeyBar::KeySlot* BottomKeyBar::slotFor(uint32_t key)
{
    for (auto& slot : _slots) {
        if (slot.key == key) {
            return &slot;
        }
    }
    return nullptr;
}

}  // namespace files
