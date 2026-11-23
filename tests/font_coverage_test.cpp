#include "assets/assets.h"
#include "assets/font_assets.hpp"
#include "views/browser_text.hpp"
#include "src/misc/lv_text_private.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace {

constexpr char kCzechFilename[] = u8"Příliš žluťoučký kůň úpěl ďábelské ódy.txt";

constexpr std::array<std::uint32_t, 8> kRequiredCodepoints{
    0x0100,  // Latin capital A with macron
    0x03A9,  // Greek capital omega
    0x0416,  // Cyrillic capital Zhe
    0x2026,  // Horizontal ellipsis
    0x20AC,  // Euro sign
    0x2116,  // Numero sign
    0x221E,  // Infinity
    0xFFE5,  // Fullwidth yen sign
};

struct Font {
    const lv_font_t* font;
    const char* name;
};

void requireGlyph(Font face, std::uint32_t codepoint, bool requirePrimary = false)
{
    lv_font_glyph_dsc_t glyph{};
    const bool found = lv_font_get_glyph_dsc(face.font, &glyph, codepoint, 0);
    if (found && !glyph.is_placeholder && glyph.resolved_font &&
        (!requirePrimary || glyph.resolved_font == face.font)) {
        return;
    }

    std::cerr << "FAIL: missing U+" << std::hex << std::uppercase << codepoint << " in " << face.name << '\n';
    std::exit(1);
}

void requireUiCoverage(Font face)
{
    for (const std::uint32_t codepoint : kRequiredCodepoints) {
        requireGlyph(face, codepoint);
    }
}

void check(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void requireBrowserText()
{
    files::FileEntry entry;
    entry.name = kCzechFilename;
    const auto shortened = files::browserRowText(entry, 231);
    const auto ellipsis = shortened.find("…");
    check(ellipsis != std::string::npos && shortened.rfind("Příliš žluťoučký", 0) == 0 &&
          shortened.substr(shortened.size() - 4) == ".txt", "middle ellipsis must keep the Czech prefix and extension");
    const auto suffix = shortened.substr(ellipsis + std::string("…").size());
    check(entry.name.compare(entry.name.size() - suffix.size(), suffix.size(), suffix) == 0,
          "shortened filename lost its ending");
    for (std::uint32_t offset = 0; offset < shortened.size();) {
        requireGlyph({files::uiFont14(), "shortened filename"}, lv_text_encoded_next(shortened.c_str(), &offset));
    }
    check(files::browserRowText(entry, 600) == entry.name, "a fitting filename must remain intact");

    for (unsigned state = 0; state < 64; ++state) {
        const bool usb = state & 4;
        const bool menu = state & 32;
        const auto hints = files::browserKeyHints(state & 1, state & 2, usb, state & 8, state & 16, menu);
        const auto* font = files::uiFont12();
        check(files::textWidth(hints.leading, font) <= (hints.trailing.empty() ? 304 : files::BrowserKeyHints::leadingWidth),
              hints.leading.c_str());
        check(files::textWidth(hints.trailing, font) <= files::BrowserKeyHints::trailingWidth,
              hints.trailing.c_str());
        const auto hint = hints.leading + (hints.leading.empty() || hints.trailing.empty() ? "" : " ") + hints.trailing;
        check(files::textLength(hint) <= 44, hint.c_str());
        check(usb || hint.find("U ") == std::string::npos, "USB shortcut needs a connected drive");
        if ((state & 16) != 0) check(hint.find("F↑") == 0, "movement must be the first shortcut");
        if (!menu && !hints.trailing.empty()) check(hints.trailing.find("Tab") == 0, "Tab must start the second group");
        if (!menu && !(state & 1)) check(hint.find("Esc Back") != std::string::npos, "missing Back action");
    }
    const auto drive = files::browserKeyHints(true, true, false, true, true, false);
    check(drive.trailing.find("E Eject") != std::string::npos && drive.leading.find("Enter") != std::string::npos,
          "selected drive should offer eject and open shortcuts");
    requireGlyph({files::uiFont12(), "footer arrows"}, 0x2191);
    requireGlyph({files::uiFont12(), "footer arrows"}, 0x2193);
    requireGlyph({files::uiFont12(), "footer return"}, 0x21B5);
    requireGlyph({files::uiFont12(), "footer group spacing"}, 0x2002);
    requireGlyph({files::uiFont12(), "toast check mark"}, 0x2713);
    requireGlyph({files::uiFont12(), "video pause"}, 0x2161);
}

void requireCzechFilename(Font face)
{
    std::uint32_t offset = 0;
    while (kCzechFilename[offset] != '\0') {
        requireGlyph(face, lv_text_encoded_next(kCzechFilename, &offset), true);
    }
}

}  // namespace

int main()
{
    const std::array<Font, 9> faces{{
        {files::uiFont10(), "Noto Sans SC 10px"},
        {files::uiFont12(), "Noto Sans 12px"},
        {files::uiFont14(), "Noto Sans 14px"},
        {&font_noto_sans_jp_semibold_10, "Noto Sans JP 10px"},
        {&font_noto_sans_jp_semibold_12, "Noto Sans JP 12px"},
        {&font_noto_sans_jp_semibold_14, "Noto Sans JP 14px"},
        {files::uiMonoFont12(), "Noto Sans Mono 12px"},
        {files::uiMonoFont14(), "Noto Sans Mono 14px"},
        {LV_FONT_DEFAULT, "LVGL default Montserrat 14px"},
    }};
    for (const Font face : faces) {
        requireCzechFilename(face);
        for (std::uint32_t codepoint = 0x00A0; codepoint <= 0x017F; ++codepoint) {
            requireGlyph(face, codepoint, true);
        }
    }

    requireUiCoverage({&font_noto_sans_sc_semibold_10, "Noto Sans SC 10px"});
    requireUiCoverage({&font_noto_sans_sc_semibold_12, "Noto Sans SC 12px"});
    requireUiCoverage({&font_noto_sans_sc_semibold_14, "Noto Sans SC 14px"});
    requireUiCoverage({files::uiMonoFont12(), "12px mono fallback"});
    requireUiCoverage({files::textPreviewFont(), "text preview"});
    requireCzechFilename({files::textPreviewFont(), "text preview"});

    const Font defaultFont{LV_FONT_DEFAULT, "LVGL default symbols"};
    for (const char* symbol : {LV_SYMBOL_OK, LV_SYMBOL_FILE, LV_SYMBOL_DIRECTORY, LV_SYMBOL_WIFI, LV_SYMBOL_EJECT}) {
        std::uint32_t offset = 0;
        requireGlyph(defaultFont, lv_text_encoded_next(symbol, &offset), true);
    }
    requireBrowserText();
    return 0;
}
