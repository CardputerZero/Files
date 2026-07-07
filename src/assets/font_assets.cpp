#include "assets/font_assets.hpp"

#include "assets/assets.h"

namespace files {
namespace {

bool g_initialized = false;
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
    g_ui_jp_12.fallback = nullptr;
    g_ui_sc_14.fallback = &g_ui_jp_14;
    g_ui_jp_14.fallback = nullptr;

    g_mono_12.fallback = &g_ui_sc_12;
    g_mono_14.fallback = &g_ui_sc_14;
    g_initialized      = true;
}

const lv_font_t* uiFont10()
{
    initFontAssets();
    return &g_ui_sc_10;
}

const lv_font_t* uiFont12()
{
    initFontAssets();
    return &g_ui_sc_12;
}

const lv_font_t* uiFont14()
{
    initFontAssets();
    return &g_ui_sc_14;
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
