#include "views/browser_text.hpp"

#include "assets/font_assets.hpp"
#include "core/files_language.hpp"
#include "src/misc/lv_text_private.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <vector>

namespace files {
namespace {

std::vector<size_t> characterEnds(const std::string& text)
{
    std::vector<size_t> ends{0};
    for (uint32_t offset = 0; offset < text.size();) {
        const auto codepoint = lv_text_encoded_next(text.c_str(), &offset);
        const bool combiningMark = (codepoint >= 0x0300 && codepoint <= 0x036f) ||
                                   (codepoint >= 0x1ab0 && codepoint <= 0x1aff) ||
                                   (codepoint >= 0x1dc0 && codepoint <= 0x1dff);
        if (combiningMark && ends.size() > 1) ends.back() = offset;
        else ends.push_back(offset);
    }
    return ends;
}

bool fits(std::string_view text, int width, size_t limit, const lv_font_t* font)
{
    return textLength(text) <= limit && textWidth(text, font) <= width;
}

}  // namespace

int textWidth(std::string_view text, const lv_font_t* font)
{
    lv_point_t size{};
    const std::string value(text);
    lv_text_get_size(&size, value.c_str(), font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return size.x;
}

size_t textLength(std::string_view text)
{
    return std::count_if(text.begin(), text.end(), [](unsigned char byte) { return (byte & 0xC0) != 0x80; });
}

std::string fitText(std::string text, int width, const lv_font_t* font, size_t limit, bool preserveExtension)
{
    std::replace_if(text.begin(), text.end(), [](unsigned char c) { return c < 0x20 || c == 0x7F; }, ' ');
    if (fits(text, width, limit, font)) return text;
    const std::string ellipsis = "…";
    if (!fits(ellipsis, width, limit, font)) return {};
    const auto ends = characterEnds(text);
    size_t left = 0;
    size_t right = ends.size() - 1;

    const auto build = [&](size_t first, size_t last) {
        return text.substr(0, ends[first]) + ellipsis + text.substr(ends[last]);
    };
    if (preserveExtension) {
        const auto suffix = std::filesystem::path(text).extension().string();
        const auto stem = text.size() - suffix.size();
        const auto boundary = std::lower_bound(ends.begin(), ends.end(), stem);
        const auto candidate = static_cast<size_t>(boundary - ends.begin());
        if (candidate > 1 && fits(build(1, candidate), width, limit, font)) right = candidate;
    }
    if (preserveExtension && right < ends.size() - 1) {
        // Keep the final version token and, when space permits, its preceding word.
        auto boundary = text.rfind(' ', ends[right]);
        if (boundary != std::string::npos && boundary + 2 < ends[right] &&
            text[boundary + 1] == 'v' && std::isdigit(static_cast<unsigned char>(text[boundary + 2]))) {
            const auto prior = boundary > 0 ? text.rfind(' ', boundary - 1) : std::string::npos;
            if (prior != std::string::npos) {
                const auto word = static_cast<size_t>(std::lower_bound(ends.begin(), ends.end(), prior + 1) - ends.begin());
                if (fits(build(1, word), width, limit, font)) right = word;
            }
        }
    }
    const int leftBudget = std::max(0, (width - textWidth(build(0, right), font)) * 2 / 3);
    // Reserve suffix room in both pixels and characters, for example in a 36-character toast.
    const auto leftLimit = limit == std::string::npos ? limit : std::max<size_t>(1, (limit - 1) * 2 / 3);
    while (left < leftLimit && left + 1 < right && fits(build(left + 1, right), width, limit, font) &&
           (left == 0 || textWidth(text.substr(0, ends[left + 1]), font) <= leftBudget)) ++left;
    while (right > left + 1 && fits(build(left, right - 1), width, limit, font)) --right;
    while (left + 1 < right && fits(build(left + 1, right), width, limit, font)) ++left;
    if (preserveExtension && ends[left] > 0 && text[ends[left]] != ' ' && text[ends[left] - 1] != ' ') {
        const auto space = text.rfind(' ', ends[left] - 1);
        if (space != std::string::npos && ends[left] - space <= 5 && space > 0) {
            left = static_cast<size_t>(std::lower_bound(ends.begin(), ends.end(), space) - ends.begin());
        }
    }
    if (preserveExtension && right < ends.size() - 1 && text[ends[right]] != ' ' && text[ends[right] - 1] != ' ') {
        const auto space = text.find(' ', ends[right]);
        if (space != std::string::npos && space - ends[right] <= 5) {
            right = static_cast<size_t>(std::lower_bound(ends.begin(), ends.end(), space + 1) - ends.begin());
        }
    }
    if (preserveExtension) {
        // If word trimming freed room, spend it on more of the distinctive prefix.
        size_t longest = left;
        for (size_t end = left + 1; end + 1 < right && fits(build(end, right), width, limit, font); ++end) {
            if (text[ends[end] - 1] == ' ') continue;
            longest = end;
            if (text[ends[end]] == ' ') left = end;
        }
        if (longest > left && textWidth(build(left, right), font) + 14 < width) left = longest;
    }
    return build(left, right);
}

std::string fitWordEdges(std::string text, int width, const lv_font_t* font, size_t limit)
{
    std::replace_if(text.begin(), text.end(), [](unsigned char c) { return c < 0x20 || c == 0x7F; }, ' ');
    const auto fallback = fitText(text, width, font, limit, true);
    if (fallback == text) return fallback;
    // Prefer the final version and the word before it, where the name has them.
    const auto lastSpace = text.rfind(' ');
    if (lastSpace == std::string::npos || lastSpace == 0) return fallback;
    const auto isVersion = lastSpace + 2 < text.size() && text[lastSpace + 1] == 'v' &&
        std::isdigit(static_cast<unsigned char>(text[lastSpace + 2]));
    const auto previousSpace = isVersion ? text.rfind(' ', lastSpace - 1) : std::string::npos;
    const auto suffixStart = previousSpace == std::string::npos ? lastSpace : previousSpace;
    for (auto prefixEnd = text.rfind(' ', suffixStart - 1);
         prefixEnd != std::string::npos && prefixEnd > 0;
         prefixEnd = text.rfind(' ', prefixEnd - 1)) {
        const auto prefix = text.substr(0, prefixEnd);
        const auto suffix = text.substr(suffixStart + 1);
        if (limit == std::string::npos) {
            auto spaced = prefix + " … " + suffix;
            if (fits(spaced, width, limit, font)) return spaced;
        } else if (prefix.size() >= 2 && prefix.compare(prefix.size() - 2, 2, " a") == 0) {
            // Keep short leading articles with the part of the name being elided.
            continue;
        }
        auto compact = prefix + "…" + suffix;
        if (fits(compact, width, limit, font)) return compact;
    }
    return fallback;
}

std::string fitPackageVersion(std::string_view metadata, int width)
{
    const auto* font = uiFont12();
    if (textWidth(metadata, font) <= width) return std::string(metadata);
    const auto arch = metadata.rfind(" · ");
    const auto revision = arch == std::string_view::npos ? std::string_view::npos : metadata.rfind('-', arch);
    if (revision != std::string_view::npos) {
        const auto tail = "…" + std::string(metadata.substr(revision));
        auto prefix = std::string(metadata.substr(0, revision));
        while (!prefix.empty() && textWidth(prefix + tail, font) > width) prefix.pop_back();
        if (!prefix.empty()) return prefix + tail;
    }
    return fitText(std::string(metadata), width, font);
}

std::string fitPath(std::string_view path, int width, const lv_font_t* font)
{
    const auto leaf = path.rfind('/');
    const auto parent = leaf == std::string_view::npos || leaf == 0 ? std::string_view::npos :
                        path.rfind('/', leaf - 1);
    const auto measured = textWidth(path, font);
    const bool deep = parent != std::string_view::npos && parent > 0 &&
                      path.find('/', 1) < parent && measured > width * 3 / 4;
    if (!deep && measured <= width) return std::string(path);
    if (path.front() == '/' && parent != std::string_view::npos) {
        const auto first = path.find('/', 1);
        if (first < parent) {
            for (auto slash = path.find('/', first + 1); slash <= parent; slash = path.find('/', slash + 1)) {
                auto compact = std::string(path.substr(0, first + 1)) + "…" + std::string(path.substr(slash));
                if (textWidth(compact, font) <= width) return compact;
            }
        }
    }
    for (auto slash : {parent, leaf}) {
        if (slash == std::string_view::npos) continue;
        const auto tail = "…" + std::string(path.substr(slash));
        if (textWidth(tail, font) <= width) return tail;
    }
    return fitText(std::string(leaf == std::string_view::npos ? path : path.substr(leaf + 1)), width, font);
}

std::string wrapPath(std::string_view path, int width, const lv_font_t* font)
{
    std::string result;
    std::string line;
    while (!path.empty()) {
        const auto slash = path.find('/');
        const auto component = path.substr(0, slash == std::string_view::npos ? path.size() : slash + 1);
        if (!line.empty() && textWidth(line + std::string(component), font) > width) {
            result += line + "\n";
            line.clear();
        }
        line += component;
        path.remove_prefix(component.size());
    }
    return result + line;
}

std::string browserRowText(const FileEntry& entry, int width)
{
    const auto name = entry.name.empty() ? local("(unnamed)", "(bez názvu)") : entry.name;
    return fitText(name, width, uiFont14(), std::string::npos, !entry.directory);
}

std::string keyHints(std::initializer_list<std::string_view> primary,
                     std::initializer_list<std::string_view> optional, int width)
{
    std::string text;
    const auto add = [&](std::string_view part) {
        if (part.empty()) return;
        auto candidate = text;
        if (!candidate.empty()) candidate += keyGap;
        candidate += part;
        if (fits(candidate, width, 44, uiFont12())) text = std::move(candidate);
    };
    for (const auto part : primary) add(part);
    for (const auto part : optional) add(part);
    return text;
}

BrowserKeyHints browserKeyHints(bool places, bool eject, bool usb, bool selected, bool movement, bool menu,
                                bool multipleUsb)
{
    const auto* movementHint = movement ? "F↑ X↓" : "";
    const auto* open = selected ? local("Enter Open", "Enter Otevřít") : "";
    const auto* back = local("Esc Back", "Esc Zpět");
    const auto* tab = local("Tab Menu", "Tab Nabídka");
    const auto* ejectHint = eject ? local("E Eject", "E Vysunout") : "";
    const auto* usbHint = !usb ? "" : multipleUsb ?
        local("U Choose drive", "U Vybrat disk") : local("U Open drive", "U Otevřít disk");
    if (menu) return {keyHints({movementHint, local("Enter Select", "Enter Vybrat"), back, usbHint}, {}), {}};

    const auto* compactOpen = selected ? local("↵ Open", "↵ Otevřít") : "";
    if (!places && eject) {
        auto hints = keyHints({movementHint, compactOpen, ejectHint, tab, back}, {});
        if (hints.find(back) == std::string::npos) {
            hints = keyHints({movementHint, selected ? "↵" : "", ejectHint, tab, back}, {});
        }
        return {std::move(hints), {}};
    }
    if (!places && usb) {
        const auto hints = keyHints({movementHint, compactOpen, tab, back, usbHint}, {});
        if (hints.find(usbHint) != std::string::npos) return {hints, {}};
        return {keyHints({movement ? "F↑X↓" : "", compactOpen, usbHint, "Tab", back}, {}), {}};
    }

    // Keep Tab aligned between Places and the ordinary file list.
    const int leadWidth = BrowserKeyHints::leadingWidth;
    auto leading = keyHints({movementHint, open}, {}, leadWidth);
    if (selected && leading.find(open) == std::string::npos) {
        leading = keyHints({movementHint, compactOpen}, {}, leadWidth);
    }
    const auto* placeCommand = !places ? "" : eject ? ejectHint : usbHint;
    auto trailing = keyHints({tab, places ? "" : back}, {placeCommand}, BrowserKeyHints::trailingWidth);
    if (places && usb && !eject && trailing.find(usbHint) == std::string::npos) {
        trailing = keyHints({tab, local("U Drives", "U Disky")}, {}, BrowserKeyHints::trailingWidth);
    }
    if (textLength(leading) + textLength(trailing) + (leading.empty() ? 0 : 1) > 44) {
        leading = keyHints({open}, {}, leadWidth);
    }
    return {std::move(leading), std::move(trailing)};
}

}  // namespace files
