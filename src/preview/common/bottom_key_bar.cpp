#include "preview/common/bottom_key_bar.hpp"

#include "assets/assets.h"
#include "core/files_language.hpp"
#include "views/ui_style.hpp"

namespace files {
namespace {

const char* keyLabel(const void* icon)
{
    if (icon == &image_icon_fullscreen) return local("Full screen", "Celá obrazovka");
    if (icon == &image_icon_zoom_out) return "−";
    if (icon == &image_icon_zoom_in) return "+";
    if (icon == &image_icon_reset) return local("Rotate", "Otočit");
    if (icon == &image_icon_playback_replay) return "−10 s";
    if (icon == &image_icon_playback_forward) return "+10 s";
    if (icon == &image_icon_playback_play) return local("Play", "Hrát");
    if (icon == &image_icon_playback_pause) return local("Pause", "Pauza");
    return local("Speed", "Tempo");
}

}  // namespace

BottomKeyBar::BottomKeyBar(lv_obj_t* parent)
    : _bar(ui::caption(parent, 150, "")), _label(lv_obj_get_child(_bar, 0))
{
    lv_obj_set_width(_label, 252);
}

BottomKeyBar::~BottomKeyBar() { lv_obj_delete(_bar); }

void BottomKeyBar::setItems(std::initializer_list<Item> items)
{
    for (auto* slot : _slots) {
        if (slot) ui::hidden(slot, true);
    }
    ui::hidden(_label, false);
    std::string text;
    for (const auto& item : items) {
        if (item.key == 0) continue;
        if (!text.empty()) text += keyGap;
        text += static_cast<char>(item.key);
        text += ' ';
        text += item.label ? item.label : keyLabel(item.image);
    }
    const auto keys = keyHints({text, local("Esc Back", "Esc Zpět")}, {}, 252);
    ui::keyLegend(_label, keys);
    show(true);
}

void BottomKeyBar::setSlots(std::initializer_list<Slot> slots)
{
    ui::hidden(_label, true);
    auto slot = slots.begin();
    for (auto*& label : _slots) {
        if (slot == slots.end()) {
            if (label) ui::hidden(label, true);
            continue;
        }
        if (!label) label = ui::label(_bar, slot->x, 2, slot->width, uiFont12());
        lv_obj_set_x(label, slot->x);
        lv_obj_set_width(label, slot->width);
        ui::hidden(label, slot->text.empty());
        ui::keyLegend(label, slot->text);
        ++slot;
    }
    show(true);
}

void BottomKeyBar::show(bool visible)
{
    ui::hidden(_bar, !visible);
}

}  // namespace files