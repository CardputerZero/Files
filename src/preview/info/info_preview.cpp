#include "preview/info/info_preview.hpp"

#include "core/files_language.hpp"
#include "views/ui_style.hpp"
#include "src/misc/lv_text_private.h"
#include <vector>

#include <array>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace files {
namespace {

std::string modifiedText(int64_t unixSec)
{
    const auto time = static_cast<std::time_t>(unixSec);
    std::tm value{};
    if (unixSec <= 0 || !localtime_r(&time, &value)) return "–";
    std::ostringstream text;
    text << std::put_time(&value, "%Y-%m-%d %H:%M");
    return text.str();
}

const char* kindText(FileKind kind)
{
    switch (kind) {
        case FileKind::Directory: return local("Folder", "Složka");
        case FileKind::Image: return local("Image", "Obrázek");
        case FileKind::Audio: return local("Audio", "Zvuk");
        case FileKind::Video: return "Video";
        case FileKind::Text: return "Text";
        case FileKind::Document: return local("Document", "Dokument");
        case FileKind::Archive: return local("Archive", "Archiv");
        case FileKind::Binary: return local("Binary", "Binární");
        default: return local("Unknown", "Neznámý");
    }
}

class InfoPreviewPage : public PreviewPage {
public:
    explicit InfoPreviewPage(FileEntry file) : _file(std::move(file)), _title(local("Details", "Podrobnosti")) {}
    const std::string& title() const override { return _title; }

    void attach(lv_obj_t* parent) override
    {
        _root = ui::box(parent, 0, 0, 320, 170, ui::black);
        ui::caption(_root, 0, _title, uiFont14());
        _body = ui::box(_root, 0, 20, 320, 130, ui::black);
        lv_obj_add_flag(_body, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(_body, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(_body, LV_SCROLLBAR_MODE_OFF);

        const int nameHeight = addWrapped(_file.name, 8, 2, 300, uiFont14(), ui::white, 19);
        const auto kinds = std::string(kindText(_file.kind));
        const auto size = _file.directory ? "–" : sizeInUnits(_file.size);
        const std::array<std::pair<const char*, std::string>, 4> details{{
            {local("Kind", "Typ"), kinds},
            {local("Size", "Velikost"), size},
            {local("Modified", "Změněno"), modifiedText(_file.modifiedUnixSec)},
            {local("Hidden", "Skrytý"), _file.hidden ? local("Yes", "Ano") : local("No", "Ne")},
        }};
        constexpr int step = 17;
        int y = nameHeight;
        for (const auto& [field, value] : details) {
            addLine(field, 8, y, 66, uiFont12(), ui::secondary);
            addLine(value, 76, y, 232, uiMonoFont12(), ui::white);
            y += step;
        }
        addLine(local("Path", "Cesta"), 8, y, 66, uiFont12(), ui::secondary);
        const auto path = wrapPath(_file.path, 232, uiMonoFont12());
        const int bottom = addWrapped(path, 76, y, 232, uiMonoFont12(), ui::secondary, step);
        auto* extent = ui::box(_body, 0, 130 + (std::max(0, bottom - 130) + 16) / 17 * 17 - 1, 1, 1, ui::black);
        lv_obj_set_style_bg_opa(extent, LV_OPA_TRANSP, 0);
        lv_obj_update_layout(_body);
        _track = ui::scrollTrack(_root, 315, 20, 3, 130, ui::track);
        _thumb = ui::box(_root, 315, 20, 3, 12, ui::secondary);
        auto* footer = ui::caption(_root, 150, "");
        _keys = lv_obj_get_child(footer, 0);
        updateThumb();
    }

    void tick(uint32_t) override { updateThumb(); }

    void detach() override
    {
        if (_root) lv_obj_delete(_root);
        _root = _body = _thumb = nullptr;
        _lines.clear();
    }

    void onKey(uint32_t key, FilesRouter& router) override
    {
        switch (key) {
            case files_key::Left:
            case '\x1b': router.back(); break;
            case files_key::Up: lv_obj_scroll_by_bounded(_body, 0, 17, LV_ANIM_OFF); break;
            case files_key::Down: lv_obj_scroll_by_bounded(_body, 0, -17, LV_ANIM_OFF); break;
            case files_key::PageUp: lv_obj_scroll_by_bounded(_body, 0, 119, LV_ANIM_OFF); break;
            case files_key::PageDown: lv_obj_scroll_by_bounded(_body, 0, -119, LV_ANIM_OFF); break;
            default: break;
        }
    }

private:
    FileEntry _file;
    std::string _title;
    lv_obj_t* _root = nullptr;
    lv_obj_t* _body = nullptr;
    lv_obj_t* _track = nullptr;
    lv_obj_t* _thumb = nullptr;
    lv_obj_t* _keys = nullptr;
    struct PositionedLine {
        lv_obj_t* label;
        int start;
        int end;
    };
    std::vector<PositionedLine> _lines;

    void addLine(const std::string& text, int x, int y, int width, const lv_font_t* font, uint32_t color)
    {
        auto* line = ui::label(_body, x, y, width, font, color);
        lv_label_set_long_mode(line, LV_LABEL_LONG_MODE_CLIP);
        lv_label_set_text(line, text.c_str());
        _lines.push_back({line, y, y + lv_font_get_line_height(font)});
    }

    int addWrapped(const std::string& text, int x, int y, int width,
                   const lv_font_t* font, uint32_t color, int step)
    {
        lv_text_attributes_t wrapping{0, 0, width, LV_TEXT_FLAG_NONE};
        for (size_t start = 0; start < text.size();) {
            const auto length = lv_text_get_next_line(text.c_str() + start, text.size() - start,
                                                       font, nullptr, &wrapping);
            if (!length) break;
            auto line = text.substr(start, length);
            if (!line.empty() && line.back() == '\n') line.pop_back();
            addLine(line, x, y, width, font, color);
            y += step;
            start += length;
        }
        return y;
    }

    void updateThumb()
    {
        const int top = lv_obj_get_scroll_top(_body);
        const int range = top + lv_obj_get_scroll_bottom(_body);
        for (const auto& line : _lines) {
            ui::hidden(line.label, line.start < top || line.end > top + 130);
        }
        ui::hidden(_thumb, range <= 0);
        ui::hidden(_track, range <= 0);
        ui::keyLegend(_keys, keyHints({top > 0 ? "F↑" : "", range > top ? "X↓" : "",
                                      local("Esc Back", "Esc Zpět")}, {}));
        if (range <= 0) return;
        constexpr int track = 130;
        const int height = std::max(12, track * track / (track + range));
        lv_obj_set_height(_thumb, height);
        lv_obj_set_y(_thumb, 20 + ui::scrollbarOffset(track - height, top, range));
    }
};

class InfoPreviewSupport : public PreviewSupport {
public:
    const char* id() const override { return "info"; }
    bool supports(const FileEntry&) const override { return true; }
    std::unique_ptr<PreviewPage> open(const FileEntry& file) const override
    {
        return std::make_unique<InfoPreviewPage>(file);
    }
};

}  // namespace

std::unique_ptr<PreviewSupport> createInfoPreviewSupport()
{
    return std::make_unique<InfoPreviewSupport>();
}

}  // namespace files