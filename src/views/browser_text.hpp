#pragma once

#include "core/files_types.hpp"
#include <lvgl.h>
#include <cstddef>
#include <initializer_list>
#include <string>
#include <string_view>

namespace files {

int textWidth(std::string_view text, const lv_font_t* font);
size_t textLength(std::string_view text);
std::string fitText(std::string text, int width, const lv_font_t* font,
                    size_t limit = std::string::npos, bool preserveExtension = false);
std::string fitWordEdges(std::string text, int width, const lv_font_t* font, size_t limit = std::string::npos);
std::string fitPackageVersion(std::string_view metadata, int width);
std::string fitPath(std::string_view path, int width, const lv_font_t* font);
std::string wrapPath(std::string_view path, int width, const lv_font_t* font);
std::string browserRowText(const FileEntry& entry, int width);
inline constexpr std::string_view keyGap = " ";

std::string keyHints(std::initializer_list<std::string_view> primary,
                     std::initializer_list<std::string_view> optional, int width = 304);

struct BrowserKeyHints {
    static constexpr int tabX = 141;
    static constexpr int leadingWidth = tabX - 16;
    static constexpr int trailingWidth = 312 - tabX;
    std::string leading;
    std::string trailing;
};

BrowserKeyHints browserKeyHints(bool places, bool eject, bool usb, bool selected, bool movement, bool menu,
                                bool multipleUsb = false);

}  // namespace files