#include "assets/assets.h"

#include <lvgl.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace {

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

void requireGlyph(const lv_font_t* font, std::uint32_t codepoint, const char* context)
{
    lv_font_glyph_dsc_t glyph{};
    const bool found = lv_font_get_glyph_dsc(font, &glyph, codepoint, 0);
    if (found && !glyph.is_placeholder && glyph.resolved_font) {
        return;
    }

    std::cerr << "FAIL: missing U+" << std::hex << std::uppercase << codepoint << " in " << context << '\n';
    std::exit(1);
}

void requireUiCoverage(const lv_font_t* font, const char* context)
{
    for (const std::uint32_t codepoint : kRequiredCodepoints) {
        requireGlyph(font, codepoint, context);
    }
}

void requireTextCoverage(const lv_font_t& mono_source, const lv_font_t& sc_source, const char* context)
{
    lv_font_t sc = sc_source;
    sc.fallback  = nullptr;

    lv_font_t mono = mono_source;
    mono.fallback  = &sc;

    for (const std::uint32_t codepoint : kRequiredCodepoints) {
        requireGlyph(&mono, codepoint, context);
    }
}

}  // namespace

int main()
{
    requireUiCoverage(&font_noto_sans_sc_semibold_10, "10px UI font");
    requireUiCoverage(&font_noto_sans_sc_semibold_12, "12px UI font");
    requireUiCoverage(&font_noto_sans_sc_semibold_14, "14px UI font");
    requireTextCoverage(font_noto_sans_mono_semibold_12, font_noto_sans_sc_semibold_12, "12px mono fallback");
    requireTextCoverage(font_noto_sans_mono_semibold_14, font_noto_sans_sc_semibold_14, "text preview font");
    return 0;
}
