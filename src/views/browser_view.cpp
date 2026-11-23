#include "views/browser_view.hpp"

#include "assets/assets.h"
#include "core/files_language.hpp"
#include "views/magic_view.hpp"
#include "views/ui_style.hpp"

#include <algorithm>
#include <cmath>
#include <array>
#include <filesystem>

namespace files {
namespace {

constexpr int rowHeight = 26;
constexpr int visibleRows = 5;
constexpr int rowWidth = 312;
constexpr int metadataWidth = 84;

const lv_image_dsc_t* entryIcon(const FileEntry& entry)
{
    if (entry.icon == "folder") return &image_icon_folder;
    if (entry.icon == "audio") return &image_icon_audio;
    if (entry.icon == "video") return &image_icon_video;
    if (entry.icon == "image") return &image_icon_image;
    if (entry.icon == "text") return &image_icon_text;
    if (entry.icon == "scripts") return &image_icon_scripts;
    return &image_icon_default;
}

struct RowData {
    std::string name;
    std::string metadata;
    const lv_image_dsc_t* icon = nullptr;
    bool directory = false;
    bool checked = false;
    bool special = false;
    const char* shortcut = nullptr;
};

RowData actionRow(BrowserAction action, const FileEntry* target)
{
    switch (action) {
        case BrowserAction::Open: return {local("Open", "Otevřít"), {}, target && !target->directory ? &image_icon_open_file : &image_icon_folder};
        case BrowserAction::Copy: return {local("Copy", "Kopírovat"), {}, &image_icon_copy};
        case BrowserAction::Cut: return {local("Cut", "Vyjmout"), {}, &image_icon_cut};
        case BrowserAction::Paste: return {local("Paste", "Vložit"), {}, &image_icon_paste};
        case BrowserAction::Rename: return {local("Rename", "Přejmenovat"), {}, &image_icon_rename};
        case BrowserAction::Info: return {local("Details", "Podrobnosti"), {}, &image_icon_info};
        case BrowserAction::Delete: return {local("Delete", "Smazat"), {}, &image_icon_delete};
        case BrowserAction::Places: return {local("Places", "Umístění"), {}, &image_icon_folder};
        case BrowserAction::Eject: return {local("Eject", "Vysunout"), {}, &image_icon_eject, false, false, false, "[E]"};
        case BrowserAction::NewFolder: return {local("New folder", "Nová složka"), {}, &image_icon_new_folder};
        case BrowserAction::Language: return {local("Language", "Jazyk"), {}, &image_icon_language};
        case BrowserAction::SystemLanguage: return {local("System language", "Systémový jazyk"), {}, &image_icon_language, false, languagePreference() == Language::System};
        case BrowserAction::English: return {"English", {}, nullptr, false, languagePreference() == Language::English};
        case BrowserAction::Czech: return {"Čeština", {}, nullptr, false, languagePreference() == Language::Czech};
    }
    return {};
}

lv_group_t* keyboardGroup()
{
    for (auto* indev = lv_indev_get_next(nullptr); indev; indev = lv_indev_get_next(indev)) {
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_KEYPAD && lv_indev_get_group(indev)) {
            return lv_indev_get_group(indev);
        }
    }
    return nullptr;
}

uint32_t toneColor(DialogTone tone)
{
    switch (tone) {
        case DialogTone::Warning: return ui::warning;
        case DialogTone::Error: return ui::error;
        case DialogTone::Success: return ui::success;
        default: return ui::white;
    }
}

lv_obj_t* inputChevron(lv_obj_t* parent, int x, bool right)
{
    constexpr std::array<int, 7> offsets{0, 1, 2, 3, 2, 1, 0};
    auto* arrow = ui::box(parent, x, 8, 5, 7, ui::black);
    for (size_t y = 0; y < offsets.size(); ++y) {
        ui::box(arrow, right ? offsets[y] : 3 - offsets[y], y, 2, 1, ui::white);
    }
    return arrow;
}

const char* toneSymbol(DialogTone tone)
{
    switch (tone) {
        case DialogTone::Warning: return "!";
        case DialogTone::Error: return LV_SYMBOL_CLOSE;
        case DialogTone::Success: return LV_SYMBOL_OK;
        default: return "";
    }
}

}  // namespace

class BrowserView::Rows {
public:
    Rows(lv_obj_t* parent, uint32_t background)
        : _background(background), _panel(ui::box(parent, 0, 20, 320, 130, background)),
          _track(ui::scrollTrack(_panel, 315, 0, 3, 130, background == ui::dialog ? ui::dialogTrack : ui::track)),
          _thumb(ui::box(_panel, 315, 0, 3, 12, ui::white)),
          _empty(ui::label(_panel, 8, 54, 304, uiFont14())),
          _pasteHint(ui::label(_panel, 8, 74, 304, uiFont12(), ui::secondary))
    {
        lv_obj_set_style_text_align(_empty, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_align(_pasteHint, LV_TEXT_ALIGN_CENTER, 0);
        ui::hidden(_pasteHint, true);
        for (int i = 0; i < visibleRows; ++i) {
            auto& row = _rows[i];
            row.panel = ui::box(_panel, 0, i * rowHeight, rowWidth, rowHeight, background);
            row.icon = lv_image_create(row.panel);
            row.specialStem = ui::box(row.panel, 15, 10, 2, 5, background);
            row.specialDot = ui::box(row.panel, 15, 17, 2, 2, background);
            row.name = ui::label(row.panel, 32, 4, 276, uiFont14());
            row.shortcut = ui::label(row.panel, 32, 5, 24, uiFont12());
            const int baseline = lv_font_get_line_height(uiFont14()) - uiFont14()->base_line;
            const int metadataY = 4 + baseline - (lv_font_get_line_height(uiFont12()) - uiFont12()->base_line);
            row.metadata = ui::label(row.panel, 224, metadataY, metadataWidth, uiFont12(), ui::secondary);
            lv_obj_set_style_text_align(row.metadata, LV_TEXT_ALIGN_RIGHT, 0);
            row.context = ui::label(row.panel, 146, metadataY, 72, uiFont12(), ui::black);
            row.check = ui::label(row.panel, 289, 4, 18, LV_FONT_DEFAULT);
            lv_label_set_text(row.check, LV_SYMBOL_OK);
        }
    }

    ~Rows() { lv_obj_delete(_panel); }

    void setFiles(const std::vector<FileEntry>& entries, int selection, bool places, bool reset)
    {
        if (reset) {
            _first = 0;
            if (_unobstructedFirst) *_unobstructedFirst = 0;
        }
        _files = &entries;
        _places = places;
        select(selection, true);
    }

    void setActions(std::vector<RowData> actions, int selected)
    {
        _actions = std::move(actions);
        _first = 0;
        select(selected, true);
    }

    void show(bool visible) { ui::hidden(_panel, !visible); }
    void showScrollbar(bool visible)
    {
        _showScrollbar = visible;
        select(_selected);
    }
    void showPasteHint(bool visible, bool moving)
    {
        ui::hidden(_pasteHint, !visible);
        _hasPasteHint = visible;
        alignEmpty(130 - _reservedHeight);
        if (visible) ui::line(_pasteHint, moving ? local("Tab → Move here", "Tab → Přesunout sem") :
                                                 local("Tab → Paste", "Tab → Vložit"), 304, uiFont12());
    }
    void showEjectContext(bool visible)
    {
        if (_showEjectContext == visible) return;
        _showEjectContext = visible;
        select(_selected);
    }
    int selectedY() const
    {
        return _selected < 0 ? -1 : 20 + (_selected - (_unobstructedFirst.value_or(_first))) * rowHeight;
    }

    void reserve(int y, int height)
    {
        if (_reservedY == y && _reservedHeight == height) return;
        if (height && !_reservedHeight) _unobstructedFirst = _first;
        if (!height && _unobstructedFirst) {
            _first = *_unobstructedFirst;
            _unobstructedFirst.reset();
        }
        _reservedY = y;
        _reservedHeight = height;
        const int panelHeight = 130 - height;
        _capacity = panelHeight / rowHeight;
        lv_obj_set_y(_panel, 20 + (y == 20 ? height : 0));
        lv_obj_set_height(_panel, panelHeight);
        alignEmpty(panelHeight);
        select(_selected, true);
    }

    void select(int selection, bool rebind = false)
    {
        const int count = _files ? static_cast<int>(_files->size()) : static_cast<int>(_actions.size());
        _selected = count ? std::clamp(selection, 0, count - 1) : -1;
        const auto firstFor = [&](int first, int capacity) {
            return std::clamp(first, std::max(0, _selected - capacity + 1),
                              std::max(0, std::min(_selected, count - capacity)));
        };
        if (_unobstructedFirst) *_unobstructedFirst = firstFor(*_unobstructedFirst, visibleRows);
        const int first = firstFor(_first, _capacity);
        if (first != _first) rebind = true;
        _first = first;
        ui::hidden(_empty, count != 0);
        if (!count) lv_label_set_text(_empty, local("Empty folder", "Prázdná složka"));
        ui::hidden(_track, !_showScrollbar || count <= _capacity);
        ui::hidden(_thumb, !_showScrollbar || count <= _capacity);
        const int track = _capacity * rowHeight;
        const int offset = _reservedY == 20 ? 130 - _reservedHeight - track : 0;
        if (count > _capacity) {
            const int height = std::max(12, (track * _capacity + count / 2) / count);
            lv_obj_set_y(_track, offset);
            lv_obj_set_height(_track, track);
            lv_obj_set_height(_thumb, height);
            lv_obj_set_y(_thumb, offset + ui::scrollbarOffset(track - height, _first, count - _capacity));
        }
        for (int i = 0; i < visibleRows; ++i) {
            auto& row = _rows[i];
            lv_obj_set_y(row.panel, offset + i * rowHeight);
            const int index = _first + i;
            ui::hidden(row.panel, index >= count || i >= _capacity);
            if (index >= count || i >= _capacity) continue;
            if (rebind) {
                if (_files) {
                    const auto& entry = (*_files)[index];
                    const auto name = entry.incompleteCopy ? local("Incomplete copy", "Nedokončená kopie") :
                        _places && !entry.mountRoot ? localizedMessage(entry.name) : entry.name;
                    const auto metadata = entry.mountRoot ? (entry.freeBytes ? sizeInUnits(*entry.freeBytes) : "–") :
                                          entry.directory ? "" : sizeInUnits(entry.size);
                    bind(row, {name, metadata, entry.mountRoot ? &image_icon_usb : entryIcon(entry),
                               entry.directory, false, entry.special});
                } else {
                    bind(row, _actions[index]);
                }
            }
            const bool active = index == _selected;
            const auto foreground = active ? ui::black : ui::white;
            const bool contextualEject = active && _showEjectContext;
            ui::hidden(row.context, !contextualEject);
            if (_places && _files && (*_files)[index].mountRoot) {
                const int width = contextualEject ? 108 : 184;
                lv_obj_set_width(row.name, width);
                ui::line(row.name, (*_files)[index].name, width, uiFont14());
            }
            if (contextualEject) lv_label_set_text(row.context, local("E Eject", "E Vysunout"));
            lv_obj_set_style_bg_color(row.panel, lv_color_hex(active ? ui::accent : _background), 0);
            lv_obj_set_style_text_color(row.name, lv_color_hex(foreground), 0);
            lv_obj_set_style_text_color(row.shortcut, lv_color_hex(active ? ui::black : ui::secondary), 0);
            for (auto* piece : {row.specialStem, row.specialDot}) {
                lv_obj_set_style_bg_color(piece, lv_color_hex(active ? ui::accent : _background), 0);
            }
            lv_obj_set_style_text_color(row.check, lv_color_hex(foreground), 0);
            lv_obj_set_style_text_color(row.metadata, lv_color_hex(active ? ui::black : ui::secondary), 0);
            lv_obj_set_style_image_recolor(row.icon, lv_color_hex(ui::black), 0);
            lv_obj_set_style_image_recolor_opa(row.icon, active ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        }
    }

private:
    struct Row {
        lv_obj_t* panel;
        lv_obj_t* name;
        lv_obj_t* shortcut;
        lv_obj_t* metadata;
        lv_obj_t* context;
        lv_obj_t* icon;
        lv_obj_t* check;
        lv_obj_t* specialStem;
        lv_obj_t* specialDot;
    };

    void alignEmpty(int panelHeight)
    {
        const int center = 65 - (_reservedY == 20 ? _reservedHeight : 0);
        lv_obj_set_y(_empty, std::clamp(center - (_hasPasteHint ? 18 : 9), 0, panelHeight - 20));
        lv_obj_set_y(_pasteHint, std::clamp(center + 2, 20, panelHeight - 16));
    }

    void bind(const Row& row, const RowData& data)
    {
        const int sizeWidth = std::min(metadataWidth, textWidth(data.metadata, uiFont12()) + 1);
        const int used = data.metadata.empty() ? (data.checked ? 24 : 0) : sizeWidth + 8;
        const int inset = data.shortcut ? textWidth(data.shortcut, uiFont12()) + 4 : 0;
        const int width = 276 - used - inset;
        ui::hidden(row.check, !data.checked);
        ui::hidden(row.shortcut, !data.shortcut);
        if (data.shortcut) lv_label_set_text(row.shortcut, data.shortcut);
        lv_obj_set_x(row.name, 32 + inset);
        lv_obj_set_width(row.name, width);
        ui::line(row.name, data.name, width, uiFont14(), std::string::npos, !data.directory);
        if (!data.metadata.empty()) {
            lv_obj_set_x(row.metadata, 308 - sizeWidth);
            lv_obj_set_width(row.metadata, sizeWidth);
            ui::line(row.metadata, data.metadata, sizeWidth, uiFont12());
        }
        ui::hidden(row.metadata, data.metadata.empty());
        ui::hidden(row.icon, !data.icon);
        ui::hidden(row.specialStem, !data.special);
        ui::hidden(row.specialDot, !data.special);
        if (data.special) {
            lv_obj_move_foreground(row.specialStem);
            lv_obj_move_foreground(row.specialDot);
        }
        if (data.icon) {
            lv_image_set_src(row.icon, data.icon);
            const int w = data.icon->header.w;
            const int h = data.icon->header.h;
            const int scale = 16 * LV_SCALE_NONE / std::max(w, h);
            lv_image_set_scale(row.icon, scale);
            // LVGL centers scaled images over the source object's intrinsic area.
            lv_obj_set_pos(row.icon, 8 + (16 - w) / 2 - (data.icon == &image_icon_open_file), (rowHeight - h) / 2 + (data.icon == &image_icon_paste ? 1 : 0));
            lv_obj_set_style_transform_pivot_x(row.icon, w / 2, 0);
            lv_obj_set_style_transform_pivot_y(row.icon, h / 2, 0);
        }
    }

    uint32_t _background;
    lv_obj_t* _panel;
    lv_obj_t* _track;
    lv_obj_t* _thumb;
    lv_obj_t* _empty;
    lv_obj_t* _pasteHint;
    std::array<Row, visibleRows> _rows{};
    const std::vector<FileEntry>* _files = nullptr;
    std::vector<RowData> _actions;
    bool _places = false;
    bool _showEjectContext = false;
    bool _showScrollbar = true;
    bool _hasPasteHint = false;
    int _first = 0;
    int _selected = -1;
    int _capacity = visibleRows;
    int _reservedY = 0;
    int _reservedHeight = 0;
    std::optional<int> _unobstructedFirst;
};

class BrowserView::Dialog {
public:
    Dialog(lv_obj_t* parent, BrowserViewModel& vm)
        : _vm(vm), _box(ui::box(parent, 8, 8, 304, 154, ui::dialog)),
          _symbol(ui::label(_box, 8, 8, 16, uiFont14())),
          _title(ui::label(_box, 8, 8, 288, uiFont14())),
          _identity(ui::label(_box, 8, 29, 288, uiFont14())),
          _meta(ui::label(_box, 8, 48, 288, uiFont12(), ui::secondary)),
          _warningSymbol(ui::label(_box, 8, 65, 12, uiFont14(), ui::warning)),
          _warning(ui::label(_box, 24, 65, 272, uiFont14(), ui::warning)),
          _metaStatus(ui::label(_box, 8, 48, 288, uiFont12(), ui::secondary)),
          _fixed(ui::label(_box, 8, 65, 288, uiFont14())),
          _fixedCross(ui::errorCross(_box, 8, 69)),
          _fixedWarning(ui::label(_box, 8, 65, 10, uiFont14(), ui::warning)),
          _instruction(ui::label(_box, 8, 69, 288, uiFont14())),
          _body(ui::box(_box, 8, 30, 288, 98, ui::dialog)),
          _text(ui::label(_body, 0, 0, 282, uiFont14())),
          _version(ui::label(_body, 0, 0, 282, uiFont14())),
          _indented(ui::label(_body, 4, 0, 278, uiFont14())),
          _continuation(ui::label(_body, 0, 0, 282, uiFont14())),
          _technical(ui::label(_body, 0, 0, 282, uiFont12(), ui::secondary)),
          _diagnosticHeading(ui::label(_body, 0, 0, 282, uiFont12(), ui::secondary)),
          _diagnostics(ui::label(_body, 0, 0, 282, uiMonoFont12(), ui::secondary)),
          _scrollTrack(ui::scrollTrack(_box, 294, 0, 2, 12, ui::dialogTrack)),
          _thumb(ui::box(_box, 293, 0, 3, 12, ui::white)),
          _passwordLine(ui::box(_box, 8, 84, 288, 1, ui::secondary)),
          _passwordCursor(ui::box(_box, 8, 70, 2, 13, ui::white)),
          _masked(ui::label(_box, 8, 67, 288, uiFont14())),
          _progressTrack(ui::box(_box, 8, 72, 288, 6, ui::bar)),
          _progressFill(ui::box(_progressTrack, 0, 0, 0, 4, ui::accent)),
          _hint(ui::label(_box, 8, 130, 288, uiFont12())),
          _editSave(ui::label(_box, 8, 130, 100, uiFont12())),
          _scrollUp(ui::label(_box, 8, 130, 18, uiFont12())),
          _scrollDown(ui::label(_box, 26, 130, 18, uiFont12())),
          _inputFrame(ui::box(_box, 8, 54, 288, 24, ui::black)),
          _input(lv_textarea_create(_inputFrame)),
          _inputText(ui::label(_inputFrame, 11, 2, 268, uiFont14())),
          _inputLeft(inputChevron(_inputFrame, 3, false)),
          _inputRight(inputChevron(_inputFrame, 278, true)),
          _inputCursor(ui::box(_inputFrame, 8, 5, 2, 13, ui::accent))
    {
        lv_obj_set_style_border_width(_progressTrack, 1, 0);
        lv_obj_set_style_border_color(_progressTrack, lv_color_hex(ui::secondary), 0);
        lv_obj_add_flag(_body, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(_body, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(_body, LV_SCROLLBAR_MODE_OFF);
        lv_obj_add_event_cb(_body, [](lv_event_t* event) {
            static_cast<Dialog*>(lv_event_get_user_data(event))->updateThumb();
        }, LV_EVENT_SCROLL, this);
        lv_label_set_text(_warningSymbol, "!");
        lv_label_set_text(_fixedWarning, "!");
        lv_obj_set_style_text_line_space(_text, 19 - lv_font_get_line_height(uiFont14()), 0);
        lv_obj_set_style_text_line_space(_diagnostics, 19 - lv_font_get_line_height(uiMonoFont12()), 0);
        lv_obj_set_style_text_align(_hint, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(_scrollUp, "F↑");
        lv_label_set_text(_scrollDown, "X↓");
        lv_obj_set_style_border_width(_inputFrame, 1, 0);
        lv_obj_set_style_border_color(_inputFrame, lv_color_hex(ui::accent), 0);
        lv_obj_remove_style_all(_input);
        lv_obj_set_size(_input, 278, 22);
        lv_obj_set_pos(_input, 4, 0);
        lv_obj_set_style_bg_color(_input, lv_color_hex(ui::black), 0);
        lv_obj_set_style_bg_opa(_input, LV_OPA_COVER, 0);
        lv_obj_set_style_text_font(_input, uiFont14(), 0);
        lv_obj_set_style_text_opa(_input, LV_OPA_TRANSP, LV_PART_CURSOR);
        lv_obj_set_scrollbar_mode(_input, LV_SCROLLBAR_MODE_OFF);
        lv_label_set_long_mode(_inputText, LV_LABEL_LONG_MODE_CLIP);
        lv_obj_add_event_cb(_input, [](lv_event_t* event) {
            if (auto* animation = static_cast<lv_anim_t*>(lv_event_get_param(event))) {
                lv_anim_set_duration(animation, 1);
            }
        }, LV_EVENT_SCROLL_BEGIN, nullptr);
        lv_textarea_set_one_line(_input, true);
        lv_textarea_set_max_length(_input, 255);
        lv_obj_set_style_text_opa(lv_textarea_get_label(_input), LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_add_event_cb(_input, [](lv_event_t* event) {
            auto* self = static_cast<Dialog*>(lv_event_get_user_data(event));
            if (!self->_setting) self->_vm.setPendingRenameName(lv_textarea_get_text(self->_input));
        }, LV_EVENT_VALUE_CHANGED, this);
        ui::hidden(_inputFrame, true);
        ui::hidden(_box, true);
    }

    ~Dialog()
    {
        edit(false);
        lv_obj_delete(_box);
    }

    void set(const PackageDialog& package, const PendingDeleteFile& deletion, const PendingRenameFile& rename)
    {
        const bool visible = package.mode != PackageDialogMode::Closed || deletion.active || rename.active;
        ui::hidden(_box, !visible);
        edit(rename.active);
        if (!visible) {
            _mode = PackageDialogMode::Closed;
            return;
        }
        lv_obj_move_foreground(_box);
        PackageDialog dialog = package;
        if (deletion.active) {
            dialog = {PackageDialogMode::Result,
                      deletion.file.directory ? local("Permanently delete folder?", "Trvale smazat složku?") :
                                                local("Permanently delete file?", "Trvale smazat soubor?"),
                      deletion.file.name + (deletion.file.directory ?
                          (deletion.emptyFolder ? local("\nThis folder is empty.", "\nSložka je prázdná.") :
                              local("\nAll contents will also be deleted.", "\nSložka bude smazána včetně obsahu.")) : ""),
                      false, DialogTone::Warning};
        } else if (rename.active) {
            dialog = {PackageDialogMode::Result,
                      rename.creating ? local("New folder", "Nová složka") : local("Rename", "Přejmenovat"),
                      {}, false};
        }
        const bool previousMode = dialog.mode == _mode;
        const bool previousTitle = dialog.title == _last_title;
        _mode = dialog.mode;
        _last_title = dialog.title;

        const bool packageTitle = dialog.title == local("Debian package", "Balíček Debian");
        const bool password = _mode == PackageDialogMode::Password;
        const bool symbol = !password && !packageTitle && dialog.tone != DialogTone::Normal;
        ui::hidden(_symbol, !symbol);
        lv_obj_set_style_text_font(_symbol, dialog.tone == DialogTone::Warning ? uiFont14() : LV_FONT_DEFAULT, 0);
        lv_label_set_text(_symbol, toneSymbol(dialog.tone));
        for (auto* label : {_symbol, _title}) lv_obj_set_style_text_color(label, lv_color_hex(toneColor(dialog.tone)), 0);
        if (packageTitle || password) lv_obj_set_style_text_color(_title, lv_color_hex(ui::white), 0);
        const int titleX = !symbol ? 8 : dialog.tone == DialogTone::Warning ? 19 : 28;
        const int titleWidth = 296 - titleX;
        lv_obj_set_x(_title, titleX);
        lv_obj_set_width(_title, titleWidth);
        ui::line(_title, dialog.title, titleWidth, uiFont14());

        ui::hidden(_identity, dialog.identity.empty());
        ui::hidden(_meta, dialog.metadata.empty());
        ui::hidden(_warning, !dialog.rootScripts);
        ui::hidden(_warningSymbol, !dialog.rootScripts);
        _identityText = dialog.identity;
        lv_obj_set_width(_identity, 288);
        ui::line(_identity, dialog.identity, 288, uiFont14(), std::string::npos, true);
        lv_obj_set_y(_metaStatus, 48);
        const int statusWidth = dialog.metadataStatus.empty() ? 0 : textWidth(dialog.metadataStatus, uiFont12()) + 1;
        const int metadataWidth = 288 - (statusWidth ? statusWidth + 8 : 0);
        lv_obj_set_width(_meta, metadataWidth);
        const std::string versionPrefix = !dialog.fullVersion.empty() ? local("Version: ", "Verze: ") : "";
        ui::line(_meta, versionPrefix + fitPackageVersion(dialog.metadata,
                 metadataWidth - textWidth(versionPrefix, uiFont12())), metadataWidth, uiFont12());
        ui::hidden(_metaStatus, !statusWidth);
        if (statusWidth) {
            lv_obj_set_x(_metaStatus, 296 - statusWidth);
            lv_obj_set_width(_metaStatus, statusWidth);
            ui::line(_metaStatus, dialog.metadataStatus, statusWidth, uiFont12());
        }
        lv_label_set_text(_warning, local("Install scripts will run as root", "Při instalaci poběží skripty jako root"));
        const bool installing = _mode == PackageDialogMode::Installing;
        const int fixedY = dialog.rootScripts ? 82 : !dialog.metadata.empty() ? 66 : 50;
        ui::hidden(_fixed, dialog.fixedStatus.empty());
        lv_obj_set_y(_fixed, fixedY);
        const bool fixedError = packageTitle && dialog.tone == DialogTone::Error && !dialog.fixedStatus.empty();
        const bool fixedWarning = packageTitle && dialog.tone == DialogTone::Warning && !dialog.fixedStatus.empty();
        ui::hidden(_fixedCross, !fixedError);
        ui::hidden(_fixedWarning, !fixedWarning);
        lv_obj_set_y(_fixedCross, fixedY + 4);
        lv_obj_set_y(_fixedWarning, fixedY);
        const int fixedInset = fixedError || fixedWarning ? 14 : 0;
        lv_obj_set_x(_fixed, 8 + fixedInset);
        lv_obj_set_width(_fixed, 288 - fixedInset);
        lv_obj_set_style_text_color(_fixed, lv_color_hex(fixedError ? ui::error : fixedWarning ? ui::warning : ui::white), 0);
        ui::line(_fixed, dialog.fixedStatus, 288 - fixedInset, uiFont14());
        const auto& instruction = dialog.fixedInstruction;
        ui::hidden(_instruction, instruction.empty());
        lv_obj_set_y(_instruction, fixedY + 19);
        ui::line(_instruction, instruction, 288, uiFont14());
        ui::hidden(_passwordLine, !password);
        ui::hidden(_passwordCursor, !password);
        lv_obj_set_x(_passwordCursor, 9 + textWidth(dialog.maskedPassword, uiFont14()));
        ui::hidden(_masked, !password || dialog.maskedPassword.empty());
        ui::line(_masked, dialog.maskedPassword, 288, uiFont14());
        const bool transferring = _mode == PackageDialogMode::Transferring;
        _indeterminate = installing || (transferring && !dialog.progress);
        ui::hidden(_progressTrack, !transferring && !installing);
        lv_obj_set_y(_progressTrack, installing ? 80 : 72);
        if (dialog.progress) {
            lv_obj_set_x(_progressFill, 0);
            const int fill = static_cast<int>(std::lround(286 * *dialog.progress));
            lv_obj_set_width(_progressFill, fill);
            ui::hidden(_progressFill, fill == 0);
        } else if (_indeterminate) {
            lv_obj_set_width(_progressFill, 52);
            ui::hidden(_progressFill, false);
        }
        if (rename.active) _inputPlaceholder = rename.creating ? local("Folder name", "Název složky") : "";
        const int top = rename.active ? 85 : password ? 91 : installing ? 61 :
                        !instruction.empty() ? fixedY + 38 : !dialog.fixedStatus.empty() ? fixedY + 19 :
                        dialog.rootScripts ? 83 : !dialog.metadata.empty() ? 66 : !dialog.identity.empty() ? 50 : 31;
        lv_obj_set_y(_body, top);
        const int lineHeight = 19;
        const auto* bodyFont = transferring && dialog.progress ? uiFont12() : uiFont14();
        lv_obj_set_style_text_font(_text, bodyFont, 0);
        lv_obj_set_style_text_line_space(_text, lineHeight - lv_font_get_line_height(bodyFont), 0);
        const int height = std::max(lineHeight, ((130 - top) / lineHeight) * lineHeight);
        lv_obj_set_height(_body, installing ? lineHeight : height);
        auto body = dialog.text;
        if (const auto first = body.find_first_not_of("\n"); first != std::string::npos) body.erase(0, first);
        const std::string marker = std::string(local("Details", "Podrobnosti")) + "\n";
        auto diagnostics = dialog.diagnosticText;
        if (diagnostics.empty() && dialog.tone == DialogTone::Error) {
            const auto begin = body.rfind(marker, 0) == 0 ? 0 : body.find("\n" + marker);
            if (begin != std::string::npos) {
                const auto end = begin + (begin == 0 ? 0 : 1) + marker.size();
                diagnostics = body.substr(end);
                if (end == marker.size()) body.clear();
                else body.resize(end - 1);
            }
        }
        while (!body.empty() && body.back() == '\n') body.pop_back();
        while (!diagnostics.empty() && diagnostics.back() == '\n') diagnostics.pop_back();
        if (password && dialog.tone == DialogTone::Warning) {
            const auto period = body.find('.');
            if (period != std::string::npos) {
                if (body.rfind("! ", 0) != 0) {
                    body.insert(0, "! ");
                }
                const auto warningEnd = body.find('.') + 1;
                body.insert(warningEnd, "#");
                body.insert(0, "#FFAA42 ");
            }
        }
        lv_label_set_recolor(_text, password);
        lv_label_set_text(_text, body.c_str());
        // Lay out whole lines in each block so wrapped text keeps its own inset.
        int bodyBottom = 0;
        const auto addBlock = [&](lv_obj_t* label, const std::string& content, const lv_font_t* font) {
            ui::hidden(label, content.empty());
            if (content.empty()) return;
            lv_obj_set_y(label, (bodyBottom + lineHeight - 1) / lineHeight * lineHeight);
            lv_label_set_text(label, content.c_str());
            lv_obj_set_style_text_line_space(label, lineHeight - lv_font_get_line_height(font), 0);
            lv_obj_update_layout(_box);
            bodyBottom = lv_obj_get_y(label) + lv_obj_get_height(label);
        };
        const bool previousDialog = previousMode && previousTitle;
        if (!previousDialog) lv_obj_scroll_to_y(_body, 0, LV_ANIM_OFF);
        lv_obj_set_style_pad_bottom(_body, 0, 0);
        ui::hidden(_text, body.empty() && !rename.active);
        lv_obj_set_style_text_color(_text, lv_color_hex(ui::white), 0);
        lv_obj_update_layout(_box);
        if (!body.empty()) bodyBottom = lv_obj_get_height(_text);
        addBlock(_version, dialog.fullVersion.empty() ? "" :
                 local("Version\n", "Verze\n") + dialog.fullVersion, uiFont14());
        addBlock(_technical, dialog.technicalPath, uiFont12());
        addBlock(_indented, dialog.indentedText, uiFont14());
        addBlock(_continuation, dialog.afterIndentedText, uiFont14());
        addBlock(_diagnosticHeading, dialog.diagnosticHeading, uiFont12());
        addBlock(_diagnostics, diagnostics, uiMonoFont12());
        if (!rename.active && (_mode == PackageDialogMode::Review || _mode == PackageDialogMode::Result)) {
            const int remainder = bodyBottom > height ? (bodyBottom - height) % lineHeight : 0;
            lv_obj_set_style_pad_bottom(_body, remainder ? lineHeight - remainder : 0, 0);
            lv_obj_update_layout(_box);
        }
        updateThumb();
        const bool canScroll = !rename.active &&
                               lv_obj_get_scroll_top(_body) + lv_obj_get_scroll_bottom(_body) > 0 &&
                               (_mode == PackageDialogMode::Review || _mode == PackageDialogMode::Result);
        const auto cancel = local("Esc Cancel", "Esc Zrušit");
        const auto back = local("Esc Back", "Esc Zpět");
        std::string hint;
        if (rename.active || deletion.active) {
            hint = keyHints({rename.active ? (rename.creating ? local("Enter Create", "Enter Vytvořit") :
                                                               local("Enter Save", "Enter Uložit")) :
                                             local("Enter Delete", "Enter Smazat"), cancel}, {}, 288);
        } else {
            switch (_mode) {
                case PackageDialogMode::Review:
                    hint = dialog.allowInstall ? keyHints({canScroll ? "F↑ X↓" : "", local("Enter Install", "Enter Instalovat"), cancel}, {}, 288)
                                               : keyHints({canScroll ? "F↑ X↓" : "", back}, {}, 288);
                    break;
                case PackageDialogMode::ConfirmEject:
                    hint = keyHints({local("Enter Eject", "Enter Vysunout"), cancel}, {}, 288);
                    break;
                case PackageDialogMode::ConfirmTransfer:
                    hint = keyHints({dialog.move ? local("Enter Move", "Enter Přesunout") : local("Enter Copy", "Enter Kopírovat"),
                                     cancel}, {}, 288);
                    break;
                case PackageDialogMode::Password:
                    hint = keyHints({local("Enter Continue", "Enter Pokračovat"), cancel}, {}, 288);
                    break;
                case PackageDialogMode::Transferring: if (!dialog.cancelling) hint = cancel; break;
                case PackageDialogMode::Result: hint = keyHints({canScroll ? "F↑ X↓" : "", back}, {}, 288); break;
                default: break;
            }
        }
        _logLabel = dialog.diagnosticLabel;
        _logStart = diagnostics.empty() ? -1 : lv_obj_get_y(_diagnostics);
        _resultScroll = canScroll && !deletion.active && _mode == PackageDialogMode::Result;
        _scrollConfirm = canScroll && (deletion.active || _mode == PackageDialogMode::Review);
        _scrollAction = deletion.active ? local("Enter Delete", "Enter Smazat") :
                        dialog.allowInstall ? local("Enter Install", "Enter Instalovat") : "";
        _scrollBack = deletion.active || dialog.allowInstall ? cancel : back;
        lv_obj_set_x(_hint, 8);
        lv_obj_set_width(_hint, 288);
        lv_obj_set_style_text_align(_hint, LV_TEXT_ALIGN_CENTER, 0);
        ui::hidden(_scrollUp, true);
        ui::hidden(_scrollDown, true);
        ui::hidden(_editSave, true);
        _editAction = rename.creating ? local("Enter Create", "Enter Vytvořit") : local("Enter Save", "Enter Uložit");
        if (rename.active) {
            _editError.clear();
            renderInput();
            refreshEditHint();
        } else {
            if (_resultScroll) updateResultHint();
            else if (_scrollConfirm) updateConfirmHint();
            else ui::keyLegend(_hint, hint);
        }
    }

    void setName(const std::string& name)
    {
        if (!_editing) return;
        if (name != lv_textarea_get_text(_input)) {
            _setting = true;
            lv_textarea_set_text(_input, name.c_str());
            lv_textarea_set_cursor_pos(_input, LV_TEXTAREA_CURSOR_LAST);
            _setting = false;
        }
        _editError.clear();
        lv_label_set_text(_text, "");
        renderInput();
        refreshEditHint();
    }

    void error(const std::string& message)
    {
        _editError = message;
        lv_label_set_text(_text, message.c_str());
        lv_obj_set_style_text_color(_text, lv_color_hex(ui::error), 0);
        refreshEditHint();
    }

    void scroll(int offset)
    {
        if (_mode == PackageDialogMode::Review || (_mode == PackageDialogMode::Result && !_editing)) {
            lv_obj_scroll_by_bounded(_body, 0, -offset, LV_ANIM_OFF);
            if (_resultScroll) updateResultHint();
            else if (_scrollConfirm) updateConfirmHint();
        }
    }

    void tick(uint32_t nowMs)
    {
        if (_editing && lv_textarea_get_cursor_pos(_input) != _lastCursor) renderInput();
        if (_indeterminate && (_mode == PackageDialogMode::Installing || _mode == PackageDialogMode::Transferring)) {
            constexpr int travel = 286 - 52;
            const int phase = (nowMs / 5) % (2 * travel);
            lv_obj_set_x(_progressFill, phase <= travel ? phase : 2 * travel - phase);
        }
    }

private:
    void renderInput()
    {
        const std::string_view value(lv_textarea_get_text(_input));
        const auto cursor = static_cast<size_t>(lv_textarea_get_cursor_pos(_input));
        _lastCursor = cursor;
        ui::hidden(_inputLeft, true);
        ui::hidden(_inputRight, true);
        if (value.empty()) {
            lv_obj_set_style_text_font(_inputText, uiFont12(), 0);
            lv_obj_set_style_text_color(_inputText, lv_color_hex(ui::secondary), 0);
            lv_obj_set_x(_inputText, 16);
            lv_label_set_text(_inputText, _inputPlaceholder);
            lv_obj_set_x(_inputCursor, 11);
            return;
        }

        std::vector<size_t> offsets{0};
        for (size_t i = 1; i < value.size(); ++i) {
            if ((static_cast<unsigned char>(value[i]) & 0xc0) != 0x80) offsets.push_back(i);
        }
        offsets.push_back(value.size());
        const auto width = [&](size_t start, size_t end) {
            return textWidth(value.substr(offsets[start], offsets[end] - offsets[start]), uiFont14());
        };
        constexpr int available = 264;
        constexpr int textStart = 11;
        constexpr int caretSpace = 4;
        const auto last = offsets.size() - 1;
        const auto at = std::min(cursor, last);
        size_t first = std::min(_inputFirst, at);
        while (first < at && width(first, at) > available - caretSpace) ++first;
        if (at == last) {
            while (first > 0 && width(first - 1, at) <= available - caretSpace) --first;
        }
        _inputFirst = first;
        auto end = at;
        while (end < last && width(first, end + 1) <= available - caretSpace) ++end;
        const auto visible = std::string(value.substr(offsets[first], offsets[end] - offsets[first]));
        lv_obj_set_x(_inputText, textStart);
        lv_obj_set_style_text_font(_inputText, uiFont14(), 0);
        lv_obj_set_style_text_color(_inputText, lv_color_hex(ui::white), 0);
        std::string displayed = visible;
        // Reserve an actual gap between glyphs where the caret sits.
        if (at < end) displayed.insert(offsets[at] - offsets[first], " ");
        lv_label_set_text(_inputText, displayed.c_str());
        lv_obj_set_x(_inputCursor, textStart + width(first, at) + (at == end));
        ui::hidden(_inputLeft, first == 0);
        ui::hidden(_inputRight, end == last);
    }

    void updateConfirmHint()
    {
        positionScrollHint(_scrollAction, _scrollBack);
    }

    void updateResultHint()
    {
        const int top = lv_obj_get_scroll_top(_body);
        const bool logVisible = _logStart >= 0 && top + lv_obj_get_height(_body) > _logStart;
        if (!_logLabel.empty() && _logStart >= 0) {
            if (logVisible) {
                ui::line(_identity, _identityText, 194, uiFont14(), std::string::npos, true);
                lv_obj_set_width(_identity, 194);
                lv_obj_set_x(_metaStatus, 208);
                lv_obj_set_y(_metaStatus, 31);
                lv_obj_set_width(_metaStatus, 88);
                ui::line(_metaStatus, _logLabel, 88, uiFont12());
            }
            if (!logVisible) {
                lv_obj_set_width(_identity, 288);
                ui::line(_identity, _identityText, 288, uiFont14(), std::string::npos, true);
            }
            ui::hidden(_metaStatus, !logVisible);
        }
        const auto* back = local("Esc Back", "Esc Zpět");
        positionScrollHint("", back);
        if (top > 0 && lv_obj_get_scroll_bottom(_body) <= 0 && logVisible && !_logLabel.empty()) {
            lv_obj_set_x(_hint, 8);
            lv_obj_set_width(_hint, 288);
            lv_obj_set_style_text_align(_hint, LV_TEXT_ALIGN_CENTER, 0);
            ui::hidden(_scrollUp, true);
            ui::keyLegend(_hint, keyHints({local("F↑ Scroll up", "F↑ Nahoru"), back}, {}, 288));
        }
        if (_logStart >= 0 && !logVisible && !_logLabel.empty()) {
            lv_obj_set_x(_hint, 8);
            lv_obj_set_width(_hint, 288);
            lv_obj_set_style_text_align(_hint, LV_TEXT_ALIGN_CENTER, 0);
            ui::hidden(_scrollUp, true);
            ui::hidden(_scrollDown, true);
            ui::keyLegend(_hint, keyHints({top > 0 ? "F↑" : "", local("X↓ View log", "X↓ K výpisu"),
                                         local("Esc Back", "Esc Zpět")}, {}, 288));
        }
    }

    void positionScrollHint(const char* action, const char* back)
    {
        const auto up = lv_obj_get_scroll_top(_body) > 0;
        const auto down = lv_obj_get_scroll_bottom(_body) > 0;
        const auto* font = uiFont12();
        const int upWidth = textWidth("F↑", font) + 1;
        const int downWidth = textWidth("X↓", font) + 1;
        const auto commands = keyHints({action, back}, {}, 220);
        const int backWidth = textWidth(commands, font) + 4;
        constexpr int gap = 8;
        const int x = (304 - upWidth - downWidth - backWidth - 2 * gap) / 2;
        lv_obj_set_x(_scrollUp, x);
        lv_obj_set_width(_scrollUp, upWidth);
        lv_obj_set_x(_scrollDown, x + upWidth + gap);
        lv_obj_set_width(_scrollDown, downWidth);
        lv_obj_set_x(_hint, x + upWidth + downWidth + 2 * gap);
        lv_obj_set_width(_hint, backWidth);
        lv_obj_set_style_text_align(_hint, LV_TEXT_ALIGN_LEFT, 0);
        ui::hidden(_scrollUp, !up);
        ui::hidden(_scrollDown, !down);
        ui::keyLegend(_hint, commands);
    }

    void updateThumb()
    {
        const int top = lv_obj_get_scroll_top(_body);
        const int range = top + lv_obj_get_scroll_bottom(_body);
        const bool hide = range <= 0 || (_mode != PackageDialogMode::Review && _mode != PackageDialogMode::Result);
        ui::hidden(_thumb, hide);
        ui::hidden(_scrollTrack, hide);
        if (range <= 0) return;
        const int track = lv_obj_get_height(_body);
        lv_obj_set_height(_scrollTrack, track);
        lv_obj_set_y(_scrollTrack, lv_obj_get_y(_body));
        const int height = std::max(12, track * track / (track + range));
        lv_obj_set_height(_thumb, height);
        lv_obj_set_y(_thumb, lv_obj_get_y(_body) + ui::scrollbarOffset(track - height, top, range));
    }

    void refreshEditHint()
    {
        const bool ready = _editError.empty() && *lv_textarea_get_text(_input);
        const auto* back = local("Esc Cancel", "Esc Zrušit");
        const auto* font = uiFont12();
        constexpr int gap = 10;
        const int actionWidth = textWidth(_editAction, font);
        const int backWidth = textWidth(back, font);
        const int start = (304 - actionWidth - backWidth - gap) / 2;
        lv_obj_set_x(_editSave, start);
        lv_obj_set_width(_editSave, actionWidth + 1);
        ui::keyLegend(_editSave, _editAction);
        ui::hidden(_editSave, !ready);
        lv_obj_set_x(_hint, start + actionWidth + gap);
        lv_obj_set_width(_hint, backWidth + 1);
        lv_obj_set_style_text_align(_hint, LV_TEXT_ALIGN_LEFT, 0);
        ui::keyLegend(_hint, back);
    }

    void edit(bool editing)
    {
        if (editing == _editing) return;
        _editing = editing;
        _inputFirst = 0;
        ui::hidden(_inputFrame, !editing);
        lv_obj_set_style_text_color(_text, lv_color_hex(ui::white), 0);
        if (!editing) {
            if (lv_obj_get_group(_input)) lv_group_remove_obj(_input);
        } else if (auto* group = keyboardGroup()) {
            lv_group_add_obj(group, _input);
            lv_group_focus_obj(_input);
            lv_group_set_editing(group, true);
        }
    }

    BrowserViewModel& _vm;
    lv_obj_t* _box;
    lv_obj_t* _symbol;
    lv_obj_t* _title;
    lv_obj_t* _identity;
    lv_obj_t* _meta;
    lv_obj_t* _warningSymbol;
    lv_obj_t* _warning;
    lv_obj_t* _metaStatus;
    lv_obj_t* _fixed;
    lv_obj_t* _fixedCross;
    lv_obj_t* _fixedWarning;
    lv_obj_t* _instruction;
    lv_obj_t* _body;
    lv_obj_t* _text;
    lv_obj_t* _version;
    lv_obj_t* _indented;
    lv_obj_t* _continuation;
    lv_obj_t* _technical;
    lv_obj_t* _diagnosticHeading;
    lv_obj_t* _diagnostics;
    lv_obj_t* _scrollTrack;
    lv_obj_t* _thumb;
    lv_obj_t* _passwordLine;
    lv_obj_t* _passwordCursor;
    lv_obj_t* _masked;
    lv_obj_t* _progressTrack;
    lv_obj_t* _progressFill;
    lv_obj_t* _hint;
    lv_obj_t* _editSave;
    lv_obj_t* _scrollUp;
    lv_obj_t* _scrollDown;
    lv_obj_t* _inputFrame;
    lv_obj_t* _input;
    lv_obj_t* _inputText;
    lv_obj_t* _inputLeft;
    lv_obj_t* _inputRight;
    lv_obj_t* _inputCursor;
    const char* _inputPlaceholder = "";
    size_t _inputFirst = 0;
    size_t _lastCursor = 0;
    PackageDialogMode _mode = PackageDialogMode::Closed;
    std::string _last_title;
    std::string _editAction;
    std::string _editError;
    std::string _logLabel;
    std::string _identityText;
    int _logStart = -1;
    bool _indeterminate = false;
    bool _resultScroll = false;
    bool _scrollConfirm = false;
    const char* _scrollAction = "";
    const char* _scrollBack = "";
    bool _setting = false;
    bool _editing = false;
};

BrowserView::BrowserView(BrowserViewModel& vm) : _vm(vm) {}
BrowserView::~BrowserView() { destroy(); }

void BrowserView::onEnter(lv_obj_t* parent)
{
    _root = ui::box(parent, 0, 0, 320, 170, ui::black);
    auto* header = ui::box(_root, 0, 0, 320, 20, ui::bar);
    _heading = ui::label(header, 8, 2, 96, uiFont14());
    _path = ui::label(header, 108, 2, 204, uiFont12(), ui::secondary);
    _free = ui::label(header, 224, 2, 84, uiFont12(), ui::secondary);
    lv_obj_set_style_text_align(_free, LV_TEXT_ALIGN_RIGHT, 0);
    auto* footer = ui::box(_root, 0, 150, 320, 20, ui::bar);
    _keys = ui::label(footer, 8, 2, 304, uiFont12());
    _trailingKeys = ui::label(footer, BrowserKeyHints::tabX, 2, BrowserKeyHints::trailingWidth, uiFont12());
    _files = std::make_unique<Rows>(_root, ui::black);
    _actions = std::make_unique<Rows>(_root, ui::dialog);
    _actions->reserve(0, rowHeight);
    auto* menuNote = ui::box(_root, 0, 124, 320, 26, ui::dialog);
    _menu_exit = ui::label(menuNote, 8, 5, 304, uiFont12(), ui::secondary);
    _menu_error = ui::label(menuNote, 22, 8, 290, uiFont12());
    _menu_error_icon = ui::errorCross(menuNote, 8, 11);
    ui::hidden(menuNote, true);
    _no_usb = ui::label(_root, 8, 89, 304, uiFont14(), ui::secondary);
    lv_obj_set_style_text_align(_no_usb, LV_TEXT_ALIGN_CENTER, 0);
    ui::hidden(_no_usb, true);
    auto* effects = ui::box(_root, 0, 0, 320, 150, ui::black);
    lv_obj_set_style_bg_opa(effects, LV_OPA_TRANSP, 0);
    _magic = std::make_unique<MagicView>(effects);
    lv_obj_move_foreground(header);
    lv_obj_move_foreground(footer);
    _toast = ui::label(_root, 4, 126, 312, uiFont12());
    ui::hidden(_toast, true);
    _dialog = std::make_unique<Dialog>(_root, _vm);
    _magic_seen = _vm.magic().get();

    _vm.currentDirectory().observe(this, [](void* ctx, const std::string&) {
        auto& self = *static_cast<BrowserView*>(ctx);
        self._files->setFiles(self._vm.entries().get(), self._vm.selectedIndex().get(), self._vm.inPlaces(), true);
        self.updateChrome();
    });
    _vm.entries().observe(this, [](void* ctx, const std::vector<FileEntry>& entries) {
        auto& self = *static_cast<BrowserView*>(ctx);
        self._files->setFiles(entries, self._vm.selectedIndex().get(), self._vm.inPlaces(), false);
        self.updateChrome();
    });
    _vm.selectedIndex().observe(this, [](void* ctx, const int& selection) {
        auto& self = *static_cast<BrowserView*>(ctx);
        self._files->select(selection);
        self.updateChrome();
        self.placeToast();
    });
    _vm.status().observe(this, [](void* ctx, const std::string& status) {
        static_cast<BrowserView*>(ctx)->updateStatus(status);
    });
    _vm.actionMenuOpen().observe(this, [](void* ctx, const bool& open) {
        static_cast<BrowserView*>(ctx)->updateMenu(open);
    });
    _vm.actionMenuSelectedIndex().observe(this, [](void* ctx, const int& index) {
        static_cast<BrowserView*>(ctx)->_actions->select(index);
    });
    _vm.pendingDelete().observe(this, [](void* ctx, const PendingDeleteFile&) {
        static_cast<BrowserView*>(ctx)->updateDialog();
    });
    _vm.pendingRename().observe(this, [](void* ctx, const PendingRenameFile&) {
        static_cast<BrowserView*>(ctx)->updateDialog();
    });
    _vm.pendingRenameName().observe(this, [](void* ctx, const std::string& name) {
        static_cast<BrowserView*>(ctx)->_dialog->setName(name);
    });
    _vm.packageDialog().observe(this, [](void* ctx, const PackageDialog&) {
        static_cast<BrowserView*>(ctx)->updateDialog();
    });
    _vm.packageScroll().observe(this, [](void* ctx, const int& offset) {
        static_cast<BrowserView*>(ctx)->_dialog->scroll(offset);
    });
    _vm.magic().observe(this, [](void* ctx, const uint32_t& serial) {
        auto& self = *static_cast<BrowserView*>(ctx);
        if (serial && serial != self._magic_seen) self._magic->generate(serial);
        else if (!serial) self._magic->stop();
        self._magic_seen = serial;
        self._files->showScrollbar(!serial);
        self.updateChrome();
    });
}

void BrowserView::onExit() { destroy(); }

void BrowserView::destroy()
{
    if (!_root) return;
    _vm.currentDirectory().removeObserver();
    _vm.entries().removeObserver();
    _vm.selectedIndex().removeObserver();
    _vm.status().removeObserver();
    _vm.actionMenuOpen().removeObserver();
    _vm.actionMenuSelectedIndex().removeObserver();
    _vm.pendingDelete().removeObserver();
    _vm.pendingRename().removeObserver();
    _vm.pendingRenameName().removeObserver();
    _vm.packageDialog().removeObserver();
    _vm.packageScroll().removeObserver();
    _vm.magic().removeObserver();
    _dialog.reset();
    _magic.reset();
    _actions.reset();
    _files.reset();
    lv_obj_delete(_root);
    _root = nullptr;
    _toast_duration = 0;
    _external_y = _external_height = 0;
}

void BrowserView::tick(uint32_t nowMs)
{
    if (_usb != _vm.hasRemovableVolumes() || _usb_action != _vm.volumeActionShown()) updateChrome();
    if (_toast_duration && lv_tick_elaps(_toast_started) >= _toast_duration) {
        _toast_duration = 0;
        placeToast();
    }
    _magic->tick(nowMs);
    if (_magic_seen && !_magic->active()) _vm.magic().set(0);
    _dialog->tick(nowMs);
}

void BrowserView::updateChrome()
{
    _usb = _vm.hasRemovableVolumes();
    const bool menu = _vm.actionMenuOpen().get();
    const bool places = _vm.inPlaces();
    const std::string title = menu ? (_vm.languageMenu() ? local("Language", "Jazyk") : local("Menu", "Nabídka")) :
                              places ? local("Places", "Umístění") : _vm.directoryTitle();
    _usb_action = _vm.volumeActionShown();
    const auto context = menu ? _vm.menuContext() : "";
    const auto path = !menu && !places ? _vm.directoryCaption() : "";
    const bool hasContext = !path.empty() || !context.empty();
    const int width = hasContext ? std::min(textWidth(title, uiFont14()), 158) : places ? 204 : 304;
    lv_obj_set_width(_heading, width);
    ui::line(_heading, title, width, uiFont14());
    if (hasContext) {
        const int pathX = 8 + width + (menu ? 4 : 8);
        const int pathWidth = 312 - pathX;
        lv_obj_set_x(_path, pathX);
        lv_obj_set_width(_path, pathWidth);
        const auto caption = context.empty() ? fitPath(path, pathWidth, uiFont12()) :
            fitText(context, pathWidth, uiFont12(), std::string::npos, true);
        lv_label_set_text(_path, caption.c_str());
    }
    ui::hidden(_path, !hasContext);
    _files->showPasteHint(!places && _vm.hasClipboard() && _vm.entries().get().empty(), _vm.clipboardMoves());
    ui::hidden(_free, !places || menu || !_usb);
    lv_label_set_text(_free, local("Free", "Volno"));
    const bool movement = menu ? _vm.actionCount() > 1 : _vm.entries().get().size() > 1;
    const bool contextualEject = places && !menu && _usb_action && _vm.hasMultipleRemovableVolumes() && _vm.canEject();
    _files->showEjectContext(contextualEject);
    const auto keys = _magic_seen ? BrowserKeyHints{local("Esc Back", "Esc Zpět"), {}} :
        browserKeyHints(places, _vm.canEject() && !_vm.languageMenu() && !contextualEject,
                        _usb && !_usb_action, _vm.selectedEntry() && !_vm.selectedEntry()->special,
                        movement, menu, _vm.hasMultipleRemovableVolumes());
    ui::hidden(_trailingKeys, keys.trailing.empty());
    lv_obj_set_width(_keys, keys.trailing.empty() ? 304 : BrowserKeyHints::leadingWidth);
    ui::keyLegend(_keys, keys.leading, false, menu ? " " : "");
    ui::keyLegend(_trailingKeys, keys.trailing, false);
    const bool languageError = menu && _vm.languageMenu() && _vm.status().get() == "Could not save language";
    lv_label_set_text(_menu_exit, local("Hold Esc · Exit", "Podržet Esc · Ukončit"));
    lv_label_set_text(_menu_error, local("Could not save language", "Jazyk se nepodařilo uložit"));
    ui::hidden(_menu_error, !languageError);
    ui::hidden(_menu_error_icon, !languageError);
    auto* menuNote = lv_obj_get_parent(_menu_exit);
    lv_obj_set_y(menuNote, languageError ? 102 : 124);
    lv_obj_set_height(menuNote, languageError ? 48 : 26);
    lv_obj_set_y(_menu_exit, languageError ? 27 : 5);
    lv_label_set_text(_no_usb, local("No USB drive detected", "Žádný USB disk"));
    ui::hidden(lv_obj_get_parent(_menu_exit), !menu || _vm.packageDialogOpen());
    ui::hidden(_no_usb, !places || _usb || _vm.modalInputActive() || _external_height || _toast_duration);
}

void BrowserView::updateMenu(bool open)
{
    if (open) {
        std::vector<RowData> rows;
        for (const auto action : _vm.availableActions()) {
            auto row = actionRow(action, _vm.selectedEntry());
            if (action == BrowserAction::Paste && _vm.clipboardMoves()) {
                row.name = local("Move here", "Přesunout sem");
            }
            if (action == BrowserAction::Eject && _vm.canEject() && !_vm.inPlaces()) {
                row.name += " " + _vm.directoryTitle();
            }
            rows.push_back(std::move(row));
        }
        _actions->setActions(std::move(rows), _vm.actionMenuSelectedIndex().get());
    }
    const bool modal = _vm.pendingDelete().get().active || _vm.pendingRename().get().active || _vm.packageDialogOpen();
    _actions->show(open && !modal);
    _files->show(!open && !modal);
    placeToast();
    updateChrome();
}

void BrowserView::updateDialog()
{
    _dialog->set(_vm.packageDialog().get(), _vm.pendingDelete().get(), _vm.pendingRename().get());
    const bool modal = _vm.pendingDelete().get().active || _vm.pendingRename().get().active || _vm.packageDialogOpen();
    ui::hidden(lv_obj_get_parent(_heading), modal);
    ui::hidden(lv_obj_get_parent(_keys), modal);
    const bool menu = _vm.actionMenuOpen().get();
    _files->show(!modal && !menu);
    _actions->show(!modal && menu);
    updateChrome();
    placeToast();
}

void BrowserView::updateStatus(const std::string& status)
{
    if (status == "Could not save language" && _vm.languageMenu()) {
        updateChrome();
        return;
    }
    if (status.empty() || status == "Ready" || status == "Empty folder") {
        _toast_duration = 0;
        placeToast();
        return;
    }
    const auto message = localizedMessage(status);
    if (_vm.pendingRename().get().active) {
        _dialog->error(message);
        return;
    }
    const bool success = status.rfind("Copied ", 0) == 0 || status.rfind("Moved ", 0) == 0 ||
                         status.rfind("Renamed ", 0) == 0 || status.rfind("Deleted ", 0) == 0 ||
                         status.rfind("Created ", 0) == 0;
    auto display = message;
    if (status.rfind("Copied ", 0) == 0) {
        display = local("Copied: ", "Zkopírováno: ") + status.substr(7);
    } else if (status.rfind("Created copy ", 0) == 0) {
        display = local("Created copy: ", "Nová kopie: ") + status.substr(13);
    } else if (status.rfind("Deleted ", 0) == 0) {
        display = local("Deleted “", "Smazáno „") + status.substr(8) + local("”", "“");
    } else if (status.rfind("Created ", 0) == 0 && status.rfind("Created copy ", 0) != 0) {
        display = inCzech() ? "Složka „" + status.substr(8) + "“ vytvořena" : "Created folder “" + status.substr(8) + "”";
    }
    if (status.rfind("Created copy ", 0) == 0 &&
        (textLength(display) + 2 > 36 || textWidth(display, uiFont12()) > 280)) {
        display = local("Created copy\n", "Vytvořena kopie\n") + status.substr(13);
    }
    ui::toast(_toast, display, 126, success ? ui::success : ui::white);
    updateChrome();
    _toast_started = lv_tick_get();
    _toast_duration = display.find('\n') == std::string::npos ? 3000 : 5000;
    placeToast();
}

int BrowserView::toastY(int height) const
{
    return _files && _files->selectedY() >= 0 && _files->selectedY() + rowHeight > 150 - height ? 20 : 150 - height;
}

void BrowserView::setExternalToast(int y, int height)
{
    if (!_root || (_external_y == y && _external_height == height)) return;
    _external_y = y;
    _external_height = height;
    placeToast();
}

void BrowserView::placeToast()
{
    if (!_files) return;
    ui::hidden(_no_usb, !_vm.inPlaces() || _usb || _vm.modalInputActive() || _external_height || _toast_duration);
    const bool visible = _toast_duration && !_vm.modalInputActive() && !_external_height;
    ui::hidden(_toast, !visible);
    if (!visible) {
        _files->reserve(_external_y, _external_height);
        return;
    }
    lv_obj_update_layout(_root);
    const int height = lv_obj_get_height(_toast);
    const int y = toastY(height);
    _files->reserve(y, height);
    lv_obj_set_y(_toast, y);
    lv_obj_move_foreground(_toast);
}

}  // namespace files
