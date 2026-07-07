#include "preview/text/text_preview.hpp"

#include "assets/assets.h"
#include "assets/font_assets.hpp"
#include <algorithm>
#include <fstream>
#include <lvgl/lvgl_cpp/label.hpp>
#include <lvgl/lvgl_cpp/obj.hpp>
#include <string_view>

namespace files {
namespace {

constexpr int32_t kScreenWidth          = 320;
constexpr int32_t kScreenHeight         = 170;
constexpr int32_t kPathBarHeight        = 21;
constexpr int32_t kTextPanelY           = 0;
constexpr int32_t kTextPanelH           = kScreenHeight;
constexpr int32_t kTextPaddingX         = 10;
constexpr int32_t kTextPaddingY         = 8;
constexpr int32_t kScrollStep           = 28;
constexpr int32_t kScrollbarX           = 314;
constexpr int32_t kScrollbarY           = kTextPanelY + 7;
constexpr int32_t kScrollbarW           = 2;
constexpr int32_t kScrollbarH           = kTextPanelH - 14;
constexpr int32_t kScrollbarMinH        = 18;
constexpr uint32_t kHoldRepeatDelayMs   = 320;
constexpr uint32_t kHoldRepeatMs        = 90;
constexpr uint32_t kPathBarColor        = 0x313131;
constexpr uint32_t kTextColor           = 0xFFFFFF;
constexpr uint32_t kMutedTextColor      = 0xA7A7A7;
constexpr uint32_t kScrollbarTrackColor = 0x2B2B2B;
constexpr uint32_t kScrollbarThumbColor = 0x848484;
constexpr uint64_t kMaxPreviewBytes     = 64 * 1024;
constexpr size_t kTextSniffBytes        = 4096;

bool extensionUsuallyText(const std::string& extension)
{
    constexpr std::string_view kTextExtensions[] = {
        ".bash", ".c",    ".cfg",  ".conf", ".cpp",  ".css", ".csv",  ".env", ".h",
        ".hpp",  ".html", ".ini",  ".js",   ".json", ".log", ".lua",  ".md",  ".py",
        ".rs",   ".sh",   ".toml", ".ts",   ".txt",  ".xml", ".yaml", ".yml", ".zsh",
    };
    return std::find(std::begin(kTextExtensions), std::end(kTextExtensions), extension) != std::end(kTextExtensions);
}

bool looksLikeTextBytes(const std::string& sample)
{
    if (sample.empty()) {
        return true;
    }

    size_t suspicious = 0;
    for (const unsigned char ch : sample) {
        if (ch == 0) {
            return false;
        }
        if (ch < 0x20 && ch != '\n' && ch != '\r' && ch != '\t' && ch != '\f' && ch != '\b') {
            ++suspicious;
        }
    }
    return suspicious * 100 / sample.size() < 5;
}

std::string readTextContent(const FileEntry& file)
{
    std::ifstream stream(file.path, std::ios::binary);
    if (!stream) {
        return "Read failed";
    }

    const uint64_t limit = std::min(file.size, kMaxPreviewBytes);
    std::string content(static_cast<size_t>(limit), '\0');
    stream.read(content.data(), static_cast<std::streamsize>(content.size()));
    content.resize(static_cast<size_t>(stream.gcount()));
    if (file.size > kMaxPreviewBytes) {
        content += "\n\n...";
    }
    return content;
}

class TextPreviewPage : public PreviewPage {
public:
    explicit TextPreviewPage(const FileEntry& file) : _file(file), _title(file.name), _content(readTextContent(file))
    {
    }

    const std::string& title() const override
    {
        return _title;
    }

    void attach(lv_obj_t* parent) override
    {
        _root = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(parent);
        _root->setSize(kScreenWidth, kScreenHeight);
        _root->setBgColor(lv_color_hex(0x191919));
        _root->setBgOpa(LV_OPA_COVER);
        _root->setBorderWidth(0);
        _root->setPaddingAll(0);
        _root->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
        _root->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

        createTextPanel();
        createTitleBar();
        createScrollbar();
        refreshScrollbar();
    }

    void detach() override
    {
        _scrollbar_thumb.reset();
        _scrollbar_track.reset();
        _label.reset();
        _panel.reset();
        _title_label.reset();
        _path_bar.reset();
        _root.reset();
    }

    void onKey(uint32_t key, FilesRouter& router) override
    {
        switch (key) {
            case files_key::Left:
            case '\x1b':
                router.back();
                break;
            case files_key::Up:
                scroll(1);
                break;
            case files_key::Down:
                scroll(-1);
                break;
            default:
                break;
        }
    }

    void onKeyState(uint32_t key, bool pressed, FilesRouter& router) override
    {
        (void)router;
        int direction = 0;
        switch (key) {
            case files_key::Up:
                direction = 1;
                break;
            case files_key::Down:
                direction = -1;
                break;
            default:
                return;
        }

        if (pressed) {
            _held_scroll_direction = direction;
            _next_scroll_at_ms     = lv_tick_get() + kHoldRepeatDelayMs;
        } else if (_held_scroll_direction == direction) {
            _held_scroll_direction = 0;
            _next_scroll_at_ms     = 0;
        }
    }

    void tick(uint32_t nowMs) override
    {
        if (_held_scroll_direction != 0 && nowMs >= _next_scroll_at_ms) {
            scroll(_held_scroll_direction, LV_ANIM_OFF);
            _next_scroll_at_ms = nowMs + kHoldRepeatMs;
        }
        refreshScrollbar();
    }

private:
    FileEntry _file;
    std::string _title;
    std::string _content;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _root;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _path_bar;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _title_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _panel;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _scrollbar_track;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _scrollbar_thumb;
    int32_t _last_scroll_top    = -1;
    int32_t _last_scroll_bottom = -1;
    int _held_scroll_direction  = 0;
    uint32_t _next_scroll_at_ms = 0;

    void createTitleBar()
    {
        _path_bar = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(_panel->raw_ptr());
        _path_bar->setSize(kScreenWidth - kTextPaddingX * 2 - 8, kPathBarHeight);
        _path_bar->setPos(0, 0);
        _path_bar->setBgColor(lv_color_hex(kPathBarColor));
        _path_bar->setBgOpa(LV_OPA_COVER);
        _path_bar->setBorderWidth(0);
        _path_bar->setRadius(3);
        _path_bar->setPaddingAll(0);
        _path_bar->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
        _path_bar->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

        _title_label = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Label>(_path_bar->raw_ptr());
        _title_label->setText(_title.empty() ? "Preview" : _title.c_str());
        _title_label->setTextFont(uiFont10());
        _title_label->setTextColor(lv_color_hex(kTextColor));
        _title_label->setLongMode(LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
        const int32_t title_height = lv_font_get_line_height(uiFont10());
        _title_label->setSize(kScreenWidth - kTextPaddingX * 2 - 8, title_height);
        _title_label->setPos(0, std::max<int32_t>(0, (kPathBarHeight - title_height) / 2));
        lv_obj_set_style_text_align(_title_label->raw_ptr(), LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    }

    void createTextPanel()
    {
        _panel = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(_root->raw_ptr());
        _panel->setSize(kScreenWidth, kTextPanelH);
        _panel->setPos(0, kTextPanelY);
        _panel->setBgOpa(LV_OPA_TRANSP);
        _panel->setBorderWidth(0);
        _panel->setPadding(kTextPaddingY, kTextPaddingY, kTextPaddingX, kTextPaddingX);
        _panel->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
        _panel->setScrollDir(LV_DIR_VER);

        _label = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Label>(_panel->raw_ptr());
        _label->setText(_content.empty() ? "" : _content.c_str());
        _label->setTextFont(textPreviewFont());
        _label->setTextColor(lv_color_hex(_content == "Read failed" ? kMutedTextColor : kTextColor));
        _label->setLongMode(LV_LABEL_LONG_MODE_WRAP);
        _label->setSize(kScreenWidth - kTextPaddingX * 2 - 8, LV_SIZE_CONTENT);
        _label->setPos(0, kPathBarHeight + 8);
    }

    void createScrollbar()
    {
        _scrollbar_track = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(_root->raw_ptr());
        _scrollbar_track->setSize(kScrollbarW, kScrollbarH);
        _scrollbar_track->setPos(kScrollbarX, kScrollbarY);
        _scrollbar_track->setBgColor(lv_color_hex(kScrollbarTrackColor));
        _scrollbar_track->setBgOpa(LV_OPA_COVER);
        _scrollbar_track->setBorderWidth(0);
        _scrollbar_track->setRadius(kScrollbarW);
        _scrollbar_track->setPaddingAll(0);
        _scrollbar_track->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

        _scrollbar_thumb = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(_root->raw_ptr());
        _scrollbar_thumb->setSize(kScrollbarW, kScrollbarMinH);
        _scrollbar_thumb->setPos(kScrollbarX, kScrollbarY);
        _scrollbar_thumb->setBgColor(lv_color_hex(kScrollbarThumbColor));
        _scrollbar_thumb->setBgOpa(LV_OPA_COVER);
        _scrollbar_thumb->setBorderWidth(0);
        _scrollbar_thumb->setRadius(kScrollbarW);
        _scrollbar_thumb->setPaddingAll(0);
        _scrollbar_thumb->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    }

    void refreshScrollbar()
    {
        if (!_panel || !_scrollbar_track || !_scrollbar_thumb) {
            return;
        }

        const int32_t top    = lv_obj_get_scroll_top(_panel->raw_ptr());
        const int32_t bottom = lv_obj_get_scroll_bottom(_panel->raw_ptr());
        if (top == _last_scroll_top && bottom == _last_scroll_bottom) {
            return;
        }
        _last_scroll_top    = top;
        _last_scroll_bottom = bottom;

        const int32_t range = top + bottom;
        if (range <= 0) {
            _scrollbar_track->setOpa(LV_OPA_TRANSP);
            _scrollbar_thumb->setOpa(LV_OPA_TRANSP);
            return;
        }

        _scrollbar_track->setOpa(LV_OPA_COVER);
        _scrollbar_thumb->setOpa(LV_OPA_COVER);

        const int32_t visible = std::max<int32_t>(1, kTextPanelH - kTextPaddingY * 2);
        const int32_t content = visible + range;
        const int32_t thumbH  = std::clamp((visible * kScrollbarH) / content, kScrollbarMinH, kScrollbarH);
        const int32_t travel  = kScrollbarH - thumbH;
        const int32_t thumbY  = kScrollbarY + (travel > 0 ? (top * travel) / range : 0);
        _scrollbar_thumb->setSize(kScrollbarW, thumbH);
        _scrollbar_thumb->setPos(kScrollbarX, thumbY);
    }

    void scroll(int direction)
    {
        scroll(direction, LV_ANIM_ON);
    }

    void scroll(int direction, lv_anim_enable_t anim)
    {
        if (_panel && direction != 0) {
            _panel->scrollByBounded(0, direction * kScrollStep, anim);
            refreshScrollbar();
        }
    }
};

class TextPreviewSupport : public PreviewSupport {
public:
    const char* id() const override
    {
        return "text";
    }

    bool supports(const FileEntry& file) const override
    {
        if (file.directory) {
            return false;
        }
        if (file.kind == FileKind::Text || extensionUsuallyText(file.extension)) {
            return true;
        }

        std::ifstream stream(file.path, std::ios::binary);
        if (!stream) {
            return false;
        }
        std::string sample(kTextSniffBytes, '\0');
        stream.read(sample.data(), static_cast<std::streamsize>(sample.size()));
        sample.resize(static_cast<size_t>(stream.gcount()));
        return looksLikeTextBytes(sample);
    }

    std::unique_ptr<PreviewPage> open(const FileEntry& file) const override
    {
        return std::make_unique<TextPreviewPage>(file);
    }
};

}  // namespace

std::unique_ptr<PreviewSupport> createTextPreviewSupport()
{
    return std::make_unique<TextPreviewSupport>();
}

}  // namespace files
