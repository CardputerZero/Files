#pragma once

#include <lvgl.h>

namespace files {

void initFontAssets();
const lv_font_t* uiFont10();
const lv_font_t* uiFont12();
const lv_font_t* uiFont14();
const lv_font_t* uiMonoFont12();
const lv_font_t* uiMonoFont14();
const lv_font_t* textPreviewFont();

}  // namespace files
