#include "assets/font_assets.hpp"

#include "assets/assets.h"

namespace files {
namespace {

constexpr uint8_t keyBitmap[] = {
    0x00, 0x00, 0x00, 0xF0,
    0x00, 0x00, 0x00, 0xF0,
    0x00, 0xF0, 0x00, 0xF0,
    0x0F, 0x00, 0x00, 0xF0,
    0xFF, 0xFF, 0xFF, 0xF0,
    0x0F, 0x00, 0x00, 0x00,
    0x00, 0xF0, 0x00, 0x00,
    // Check mark
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x0F,
    0x00, 0x00, 0x00, 0x00, 0xFF,
    0x00, 0x00, 0x00, 0x0F, 0xF0,
    0x00, 0x00, 0x00, 0xFF, 0x00,
    0xFF, 0x00, 0x0F, 0xF0, 0x00,
    0x0F, 0xF0, 0xFF, 0x00, 0x00,
    0x00, 0xFF, 0xF0, 0x00, 0x00,
    0x00, 0x0F, 0x00, 0x00, 0x00,
    // Pause
    0xFF, 0x00, 0xFF,
    0xFF, 0x00, 0xFF,
    0xFF, 0x00, 0xFF,
    0xFF, 0x00, 0xFF,
    0xFF, 0x00, 0xFF,
    0xFF, 0x00, 0xFF,
    0xFF, 0x00, 0xFF,
    0xFF, 0x00, 0xFF,
};
constexpr lv_font_fmt_txt_glyph_dsc_t keyGlyphs[] = {
    {0, 0, 0, 0, 0, 0},
    {0, 144, 8, 7, 0, 1},
    {28, 176, 10, 9, 0, 0},
    {73, 112, 6, 8, 0, 0},
};
constexpr lv_font_fmt_txt_cmap_t keyMap[] = {
    {0x2161, 1, 3, nullptr, nullptr, 0, LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY},
    {0x21B5, 1, 1, nullptr, nullptr, 0, LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY},
    {0x2713, 1, 2, nullptr, nullptr, 0, LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY},
};
const lv_font_fmt_txt_dsc_t keyDescriptor = [] {
    lv_font_fmt_txt_dsc_t descriptor{};
    descriptor.glyph_bitmap = keyBitmap;
    descriptor.glyph_dsc = keyGlyphs;
    descriptor.cmaps = keyMap;
    descriptor.cmap_num = 3;
    descriptor.bpp = 4;
    return descriptor;
}();
const lv_font_t keyFont = [] {
    lv_font_t font{};
    font.get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt;
    font.get_glyph_bitmap = lv_font_get_bitmap_fmt_txt;
    font.line_height = 16;
    font.base_line = 3;
    font.dsc = &keyDescriptor;
    return font;
}();

bool g_initialized = false;
lv_font_t g_ui_latin_12;
lv_font_t g_ui_latin_14;
lv_font_t g_ui_sc_10;
lv_font_t g_ui_jp_10;
lv_font_t g_ui_sc_12;
lv_font_t g_ui_jp_12;
lv_font_t g_ui_sc_14;
lv_font_t g_ui_jp_14;
lv_font_t g_mono_12;
lv_font_t g_mono_14;

}  // namespace

void initFontAssets()
{
    if (g_initialized) {
        return;
    }

    g_ui_latin_12 = font_noto_sans_semibold_12;
    g_ui_latin_14 = font_noto_sans_semibold_14;
    // Use the space required by panel typography, leaving room for accents and descenders.
    g_ui_latin_12.line_height = 16;
    g_ui_latin_12.base_line = 3;
    g_ui_latin_14.line_height = 18;
    g_ui_latin_14.base_line = 4;
    g_ui_latin_12.fallback = &g_ui_sc_12;
    g_ui_latin_14.fallback = &g_ui_sc_14;

    g_ui_sc_10 = font_noto_sans_sc_semibold_10;
    g_ui_jp_10 = font_noto_sans_jp_semibold_10;
    g_ui_sc_12 = font_noto_sans_sc_semibold_12;
    g_ui_jp_12 = font_noto_sans_jp_semibold_12;
    g_ui_sc_14 = font_noto_sans_sc_semibold_14;
    g_ui_jp_14 = font_noto_sans_jp_semibold_14;
    g_mono_12  = font_noto_sans_mono_semibold_12;
    g_mono_14  = font_noto_sans_mono_semibold_14;

    g_ui_sc_10.fallback = &g_ui_jp_10;
    g_ui_jp_10.fallback = nullptr;
    g_ui_sc_12.fallback = &g_ui_jp_12;
    g_ui_jp_12.fallback = &keyFont;
    g_ui_sc_14.fallback = &g_ui_jp_14;
    g_ui_jp_14.fallback = &keyFont;

    g_mono_12.line_height = 16;
    g_mono_12.base_line = 3;
    g_mono_12.fallback = &g_ui_latin_12;
    g_mono_14.fallback = &g_ui_latin_14;
    g_initialized      = true;
}

const lv_font_t* uiKeyBitmapFont() { return &keyFont; }

const lv_font_t* uiFont10()
{
    initFontAssets();
    return &g_ui_sc_10;
}

const lv_font_t* uiFont12()
{
    initFontAssets();
    return &g_ui_latin_12;
}

const lv_font_t* uiFont14()
{
    initFontAssets();
    return &g_ui_latin_14;
}

const lv_font_t* uiMonoFont12()
{
    initFontAssets();
    return &g_mono_12;
}

const lv_font_t* uiMonoFont14()
{
    initFontAssets();
    return &g_mono_14;
}

const lv_font_t* textPreviewFont()
{
    initFontAssets();
    return &g_mono_14;
}

}  // namespace files
