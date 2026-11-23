#include "views/browser_text.hpp"

#include <iomanip>
#include <sstream>

#include "assets/font_assets.hpp"
#include "src/misc/lv_text_private.h"
#include <algorithm>
#include <filesystem>
#include <vector>

namespace files {
namespace {

constexpr char kEllipsis[] = "…";
constexpr char kSeparator[] = " · ";

int textWidth(const std::string& text, const lv_font_t* font = uiFont14())
{
    lv_point_t size{};
    lv_text_get_size(&size, text.c_str(), font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return size.x;
}

}  // namespace

std::string browserRowText(const FileEntry& entry, int width)
{
    std::string name = entry.name.empty() ? "(unnamed)" : entry.name;
    std::replace_if(name.begin(), name.end(), [](char c) { return c == '\r' || c == '\n' || c == '\t'; }, ' ');
    std::string freeSpace;
    if (entry.freeBytes) {
        double available = static_cast<double>(*entry.freeBytes);
        const char* units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
        size_t unit = 0;
        while (available >= 1024 && unit < 4) {
            available /= 1024;
            ++unit;
        }
        std::ostringstream label;
        label << "  |  " << std::fixed << std::setprecision(unit == 0 ? 0 : 1) << available << ' ' << units[unit] << " free";
        freeSpace = label.str();
    }
    const std::string full = name + freeSpace;
    if (textWidth(full) <= width) {
        return full;
    }

    const auto extension = entry.directory ? std::string{} : std::filesystem::path(name).extension().string();
    size_t stemEnd = name.size() - extension.size();
    std::string suffix = extension + freeSpace;
    uint32_t firstLetter = 0;
    lv_text_encoded_next(name.c_str(), &firstLetter);
    if (textWidth(name.substr(0, firstLetter) + kEllipsis + suffix) > width) {
        // For exceptionally long extensions, use both ends of the whole name.
        stemEnd = name.size();
        suffix = freeSpace;
    }

    std::vector<size_t> boundaries{0};
    for (uint32_t offset = 0; offset < stemEnd;) {
        lv_text_encoded_next(name.c_str(), &offset);
        boundaries.push_back(offset);
    }
    const size_t characters = boundaries.size() - 1;
    size_t left = 0;
    size_t right = characters;
    const auto shortened = [&](size_t first, size_t tail) {
        return name.substr(0, boundaries[first]) + kEllipsis +
               name.substr(boundaries[tail], stemEnd - boundaries[tail]) + suffix;
    };

    // Favor the beginning, then fill the remaining pixels from the end.
    const int leftBudget = (width - textWidth(kEllipsis + suffix)) * 2 / 3;
    if (characters > 1 && textWidth(shortened(1, right)) <= width) {
        left = 1;
    }
    while (left + 1 < characters && textWidth(name.substr(0, boundaries[left + 1])) <= leftBudget) {
        ++left;
    }
    while (right > left + 1 && textWidth(shortened(left, right - 1)) <= width) {
        --right;
    }
    while (left + 1 < right && textWidth(shortened(left + 1, right)) <= width) {
        ++left;
    }
    return shortened(left, right);
}

std::string browserKeyHint(bool places, bool eject, bool usb, bool selected, bool canMove, bool menu)
{
    std::string hint;
    size_t moveAt = std::string::npos;
    const auto add = [&hint](const char* part) {
        if (!hint.empty()) {
            hint += kSeparator;
        }
        hint += part;
    };

    if (menu) {
        add("Esc Back");
        if (canMove) {
            moveAt = hint.size();
            add("F↑ X↓");
        }
        add("Enter Select");
    } else if (places && eject) {
        add("Enter Open");
        add("E Eject");
        add("Tab Menu");
    } else {
        add(places ? "Hold Esc Exit" : "Esc Back");
        if (canMove) {
            moveAt = hint.size();
            add("F↑ X↓");
        }
        if (eject) {
            add("E Eject");
        } else if (selected) {
            add("Enter Open");
        }
        add("Tab Menu");
    }
    if (usb) {
        add("U USB");
    }
    if (moveAt != std::string::npos && textWidth(hint, uiFont12()) > 300) {
        hint.erase(moveAt, std::string(" · F↑ X↓").size());
    }
    return hint;
}

}  // namespace files
