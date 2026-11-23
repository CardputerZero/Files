#pragma once

#include "assets/font_assets.hpp"
#include "views/browser_text.hpp"
#include <algorithm>

namespace files::ui {

constexpr uint32_t black = 0x000000;
constexpr uint32_t white = 0xFFFFFF;
constexpr uint32_t secondary = 0xBDBEBD;
constexpr uint32_t accent = 0xFFCF42;
constexpr uint32_t warning = 0xFFAA42;
constexpr uint32_t error = 0xFF8284;
constexpr uint32_t success = 0x8CE38C;
constexpr uint32_t bar = 0x212421;
constexpr uint32_t dialog = 0x102029;
constexpr uint32_t track = 0x636563;
constexpr uint32_t dialogTrack = 0x737573;

inline lv_obj_t* box(lv_obj_t* parent, int x, int y, int w, int h, uint32_t color)
{
    auto* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    return obj;
}

inline lv_obj_t* scrollTrack(lv_obj_t* parent, int x, int y, int w, int h, uint32_t color)
{
    auto* trackBox = box(parent, x, y, w, h, color);
    return trackBox;
}

inline int scrollbarOffset(int travel, int position, int range)
{
    if (position <= 0 || travel <= 0) return 0;
    if (position >= range) return travel;
    if (travel < 2) return 0;
    return std::clamp(static_cast<int>((int64_t{travel} * position + range / 2) / range), 1, travel - 1);
}

inline lv_obj_t* label(lv_obj_t* parent, int x, int y, int width, const lv_font_t* font, uint32_t color = white)
{
    auto* obj = lv_label_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_width(obj, width);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    return obj;
}

inline void hidden(lv_obj_t* obj, bool hide)
{
    if (hide) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

inline lv_obj_t* errorCross(lv_obj_t* parent, int x, int y)
{
    auto* icon = box(parent, x, y, 10, 10, black);
    lv_obj_set_style_bg_opa(icon, LV_OPA_TRANSP, 0);
    static constexpr lv_point_precise_t forward[] = {{1, 1}, {8, 8}};
    static constexpr lv_point_precise_t reverse[] = {{1, 8}, {8, 1}};
    for (const auto* points : {forward, reverse}) {
        auto* stroke = lv_line_create(icon);
        lv_obj_set_size(stroke, 10, 10);
        lv_line_set_points(stroke, points, 2);
        lv_obj_set_style_line_width(stroke, 2, 0);
        lv_obj_set_style_line_color(stroke, lv_color_hex(error), 0);
    }
    return icon;
}

inline void line(lv_obj_t* label, const std::string& text, int width, const lv_font_t* font,
                 size_t limit = std::string::npos, bool extension = false)
{
    const auto fitted = fitText(text, width, font, limit, extension);
    if (fitted != lv_label_get_text(label)) lv_label_set_text(label, fitted.c_str());
}

inline void keyLegend(lv_obj_t* label, std::string_view keys, bool expandGaps = true,
                      std::string_view groupSpacing = "")
{
    constexpr std::string_view extraGap = " ";
    size_t groups = 0;
    for (size_t at = keys.find(keyGap); at != std::string_view::npos; at = keys.find(keyGap, at + keyGap.size())) {
        ++groups;
    }
    const auto* font = lv_obj_get_style_text_font(label, LV_PART_MAIN);
    const bool expand = expandGaps && groups && textLength(keys) + groups <= 44 &&
        textWidth(keys, font) + static_cast<int>(groups) * textWidth(extraGap, font) <= lv_obj_get_style_width(label, LV_PART_MAIN);
    std::string formatted;
    std::string_view previous;
    while (!keys.empty()) {
        const auto end = keys.find(keyGap);
        const auto item = keys.substr(0, end);
        const auto space = item.find(' ');
        if (!formatted.empty()) {
            formatted += keyGap;
            if (previous != "F↑ X↓") formatted += groupSpacing;
            if (expand) formatted += extraGap;
        }
        if (space == std::string_view::npos || item == "F↑ X↓" || item == "Z← C→") {
            formatted += item;
        } else {
            const size_t keyEnd = item.rfind("Z← C→ ", 0) == 0 ? item.find(' ', space + 1) : space;
            formatted += item.substr(0, keyEnd);
            formatted += " #BDBEBD ";
            formatted += item.substr(keyEnd + 1);
            formatted += '#';
        }
        previous = item;
        if (end == std::string_view::npos) break;
        keys.remove_prefix(end + keyGap.size());
    }
    if (formatted == lv_label_get_text(label)) return;
    lv_label_set_recolor(label, true);
    lv_label_set_text(label, formatted.c_str());
}

inline lv_obj_t* caption(lv_obj_t* parent, int y, const std::string& text, const lv_font_t* font = uiFont12())
{
    auto* strip = box(parent, 0, y, 320, 20, bar);
    auto* content = label(strip, 8, std::max<int>(2, (20 - lv_font_get_line_height(font)) / 2), 304, font);
    if (y == 150) keyLegend(content, fitText(text, 304, font, 44));
    else line(content, text, 304, font, std::string::npos, true);
    return strip;
}

inline void toast(lv_obj_t* label, std::string_view text, int y, uint32_t color, bool detailHasKey = false)
{
    const auto split = text.find('\n');
    const bool twoLines = split != std::string_view::npos;
    const std::string symbol = color == success ? "✓ " : color == warning ? "! " : "";
    auto first = fitText(symbol + std::string(text.substr(0, split)), 296, uiFont12(), 36, true);
    auto* detail = lv_obj_get_child(label, 0);
    if (twoLines) {
        if (!detail) {
            detail = ui::label(label, 0, lv_font_get_line_height(uiFont12()), 296, uiFont12());
            lv_obj_set_style_text_align(detail, LV_TEXT_ALIGN_CENTER, 0);
        }
        const auto second = fitText(std::string(text.substr(split + 1)), 296, uiFont12(), 36, true);
        if (detailHasKey) {
            keyLegend(detail, second);
        } else {
            lv_label_set_recolor(detail, false);
            lv_label_set_text(detail, second.c_str());
        }
    }
    if (detail) hidden(detail, !twoLines);
    const int height = twoLines ? 42 : 24;
    lv_obj_set_pos(label, 4, y);
    lv_obj_set_size(label, 312, height);
    lv_obj_set_style_bg_opa(label, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(label, lv_color_hex(dialog), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_hor(label, 8, 0);
    lv_obj_set_style_pad_top(label, (height - (twoLines ? 2 : 1) * lv_font_get_line_height(uiFont12())) / 2, 0);
    lv_label_set_text(label, first.c_str());
}

}  // namespace files::ui