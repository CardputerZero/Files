#include "preview/text/text_preview.hpp"

#include "assets/assets.h"
#include "assets/font_assets.hpp"
#include "core/files_language.hpp"
#include "views/ui_style.hpp"
#include <algorithm>
#if defined(__unix__) || defined(__APPLE__)
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
#endif
#include <fstream>
#include <optional>
#include <lvgl/lvgl_cpp/label.hpp>
#include <lvgl/lvgl_cpp/obj.hpp>
#include <string_view>
#include <utility>

namespace files {
namespace {

constexpr int32_t kScreenWidth          = 320;
constexpr int32_t kScreenHeight         = 170;
constexpr int32_t kPathBarHeight        = 20;
constexpr int32_t kTextPanelY           = kPathBarHeight;
constexpr int32_t kTextPanelH           = 126;
constexpr int32_t kTextPaddingX         = 8;
constexpr int32_t kTextPaddingY         = 0;
constexpr int32_t kScrollStep           = 18;
constexpr int32_t kPageScrollStep       = 108;
constexpr int32_t kScrollbarX           = 315;
constexpr int32_t kScrollbarY           = kTextPanelY;
constexpr int32_t kScrollbarW           = 3;
constexpr int32_t kScrollbarH           = kTextPanelH;
constexpr int32_t kScrollbarMinH        = 12;
constexpr uint32_t kHoldRepeatDelayMs   = 320;
constexpr uint32_t kHoldRepeatMs        = 90;
constexpr uint32_t kTextColor           = 0xFFFFFF;
constexpr uint32_t kMutedTextColor      = ui::error;
constexpr uint32_t kScrollbarTrackColor = ui::track;
constexpr uint32_t kScrollbarThumbColor = ui::secondary;
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

bool readTextSample(const FileEntry& file, std::string& sample)
{
#if defined(__unix__) || defined(__APPLE__)
    // Use the same non-blocking/fstat protection as the full preview read.
    // The type sniff runs before the preview page is constructed and must not
    // be able to block on a FIFO or device node if the path changes mid-check.
    const int fd = ::open(file.path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }

    struct stat stat_buffer {};
    if (::fstat(fd, &stat_buffer) != 0 || !S_ISREG(stat_buffer.st_mode)) {
        ::close(fd);
        return false;
    }

    sample.assign(kTextSniffBytes, '\0');
    size_t offset = 0;
    while (offset < sample.size()) {
        const ssize_t bytes_read = ::read(fd, sample.data() + offset, sample.size() - offset);
        if (bytes_read > 0) {
            offset += static_cast<size_t>(bytes_read);
            continue;
        }
        if (bytes_read < 0 && errno == EINTR) {
            continue;
        }
        break;
    }
    sample.resize(offset);
    ::close(fd);
    return true;
#else
    std::ifstream stream(file.path, std::ios::binary);
    if (!stream) {
        return false;
    }
    sample.assign(kTextSniffBytes, '\0');
    stream.read(sample.data(), static_cast<std::streamsize>(sample.size()));
    sample.resize(static_cast<size_t>(stream.gcount()));
    return true;
#endif
}

std::optional<std::string> readTextContent(const FileEntry& file, int& error, bool& limited)
{
#if defined(__unix__) || defined(__APPLE__)
    // Open non-blocking and verify the node after opening as well.  The status
    // check in supports() protects the normal path, while this second check
    // closes the replacement race for a node swapped in between selection and
    // preview creation.
    const int fd = ::open(file.path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        error = errno;
        return std::nullopt;
    }

    struct stat stat_buffer {};
    const int status = ::fstat(fd, &stat_buffer);
    if (status != 0 || !S_ISREG(stat_buffer.st_mode)) {
        error = status != 0 ? errno : EINVAL;
        ::close(fd);
        return std::nullopt;
    }

    const uint64_t file_size = stat_buffer.st_size > 0 ? static_cast<uint64_t>(stat_buffer.st_size) : 0;
    const uint64_t limit     = std::min(file_size, kMaxPreviewBytes);
    std::string content(static_cast<size_t>(limit), '\0');
    size_t offset = 0;
    while (offset < content.size()) {
        const ssize_t bytes_read = ::read(fd, content.data() + offset, content.size() - offset);
        if (bytes_read > 0) {
            offset += static_cast<size_t>(bytes_read);
            continue;
        }
        if (bytes_read < 0 && errno == EINTR) {
            continue;
        }
        if (bytes_read < 0) {
            error = errno;
            ::close(fd);
            return std::nullopt;
        }
        break;
    }
    ::close(fd);
    content.resize(offset);
    limited = file_size > kMaxPreviewBytes;
    if (limited && !content.empty()) {
        // The byte limit can land inside the final UTF-8 codepoint.
        auto lead = content.size() - 1;
        while (lead && (static_cast<unsigned char>(content[lead]) & 0xc0) == 0x80) --lead;
        const auto ch = static_cast<unsigned char>(content[lead]);
        const size_t bytes = ch < 0x80 ? 1 : ch < 0xe0 ? 2 : ch < 0xf0 ? 3 : 4;
        if (content.size() - lead < bytes) content.resize(lead);
    }
    return content;
#else
    std::ifstream stream(file.path, std::ios::binary);
    if (!stream) {
        error = errno ? errno : EIO;
        return std::nullopt;
    }

    const uint64_t limit = std::min(file.size, kMaxPreviewBytes);
    std::string content(static_cast<size_t>(limit), '\0');
    stream.read(content.data(), static_cast<std::streamsize>(content.size()));
    if (stream.bad()) {
        error = errno ? errno : EIO;
        return std::nullopt;
    }
    content.resize(static_cast<size_t>(stream.gcount()));
    limited = file.size > kMaxPreviewBytes;
    return content;
#endif
}

class TextPreviewPage : public PreviewPage {
public:
    TextPreviewPage(std::string title, std::string content) : _title(std::move(title)), _content(std::move(content)), _recolor(true)
    {
    }

    explicit TextPreviewPage(const FileEntry& file) : _title(file.name)
    {
        int error = 0;
        auto content = readTextContent(file, error, _limited);
        if (content) {
            _content = std::move(*content);
        } else {
            _read_failed = true;
            if (error == EACCES || error == EPERM) {
                _read_reason = local("Read permission denied.", "Chybí oprávnění ke čtení.");
            } else if (error == EIO) {
                _read_reason = local("Input/output error.", "Chyba vstupu/výstupu.");
            } else if (error == ENOENT) {
                _read_reason = local("The file is no longer available.", "Soubor už není dostupný.");
            } else {
                _read_reason.clear();
            }
        }
    }

    const std::string& title() const override
    {
        return _title;
    }

    void attach(lv_obj_t* parent) override
    {
        _root = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(parent);
        _root->setSize(kScreenWidth, kScreenHeight);
        _root->setBgColor(lv_color_hex(ui::black));
        _root->setBgOpa(LV_OPA_COVER);
        _root->setBorderWidth(0);
        _root->setPaddingAll(0);
        _root->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
        _root->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

        createTextPanel();
        auto* header = ui::caption(_root->raw_ptr(), 0, _title.empty() ? local("Preview", "Náhled") : _title, uiFont14());
        lv_obj_set_y(lv_obj_get_child(header, 0), 0);
        if (_limited) {
            const auto* notice = local("First 64 KiB", "Prvních 64 KiB");
            const int width = textWidth(notice, uiFont12());
            auto* status = ui::label(header, 312 - width, 2, width, uiFont12(), ui::secondary);
            lv_label_set_text(status, notice);
            auto* title = lv_obj_get_child(header, 0);
            lv_obj_set_width(title, 296 - width);
            ui::line(title, _title, 296 - width, uiFont14(), std::string::npos, true);
        }
        const auto footer = ui::caption(_root->raw_ptr(), 150, "");
        _keys = lv_obj_get_child(footer, 0);
        lv_obj_set_width(_keys, 252);
        createScrollbar();
        refreshScrollbar();
    }

    void detach() override
    {
        _scrollbar_thumb.reset();
        _scrollbar_track.reset();
        _label.reset();
        _panel.reset();
        _keys = nullptr;
        _last_scroll_top = _last_scroll_bottom = -1;
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
            case files_key::PageUp:
                scrollBy(kPageScrollStep, LV_ANIM_OFF);
                break;
            case files_key::PageDown:
                scrollBy(-kPageScrollStep, LV_ANIM_OFF);
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
    std::string _title;
    std::string _content;
    bool _read_failed = false;
    bool _limited = false;
    std::string _read_reason;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _root;
    lv_obj_t* _keys = nullptr;
    bool _recolor = false;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _panel;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _scrollbar_track;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _scrollbar_thumb;
    int32_t _last_scroll_top    = -1;
    int32_t _last_scroll_bottom = -1;
    int _held_scroll_direction  = 0;
    uint32_t _next_scroll_at_ms = 0;

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

        if (!_read_failed && _content.empty()) {
            auto* empty = ui::label(_panel->raw_ptr(), 0, 56, 304, uiFont14(), ui::secondary);
            lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
            lv_label_set_text(empty, local("Empty file", "Prázdný soubor"));
            return;
        }
        _label = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Label>(_panel->raw_ptr());
        const auto display = !_content.empty() && _content.front() == '/' &&
            _content.find_first_of(" \t\r\n") == std::string::npos
            ? wrapPath(_content, 296, uiFont14()) : _content;
        _label->setText(_read_failed ?
            (_read_reason.empty() ? local("The file could not be read", "Soubor se nepodařilo přečíst") :
                                   local("Read failed", "Čtení se nezdařilo")) : display.c_str());
        lv_label_set_recolor(_label->raw_ptr(), _recolor);
        _label->setTextFont(uiFont14());
        lv_obj_set_style_text_line_space(_label->raw_ptr(), kScrollStep - lv_font_get_line_height(uiFont14()), 0);
        _label->setTextColor(lv_color_hex(_read_failed ? kMutedTextColor : kTextColor));
        _label->setLongMode(LV_LABEL_LONG_MODE_WRAP);
        _label->setSize(kScreenWidth - kTextPaddingX * 2 - 8, LV_SIZE_CONTENT);
        _label->setPos(0, 0);
        if (_read_failed) {
            _label->setPos(14, 2);
            _label->setSize(282, LV_SIZE_CONTENT);
            ui::errorCross(_panel->raw_ptr(), 0, 6);
            if (_read_reason.empty()) return;
            auto* reason = ui::label(_panel->raw_ptr(), 0, kScrollStep + 3, 296, uiFont14());
            lv_label_set_text(reason, _read_reason.c_str());
        }
    }

    void createScrollbar()
    {
        _scrollbar_track = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(_root->raw_ptr());
        _scrollbar_track->setSize(kScrollbarW, kScrollbarH);
        _scrollbar_track->setPos(kScrollbarX, kScrollbarY);
        _scrollbar_track->setBgColor(lv_color_hex(kScrollbarTrackColor));
        ui::box(_scrollbar_track->raw_ptr(), 0, 0, 3, 1, ui::secondary);
        ui::box(_scrollbar_track->raw_ptr(), 0, kScrollbarH - 1, 3, 1, ui::secondary);
        _scrollbar_track->setBgOpa(LV_OPA_COVER);
        _scrollbar_track->setBorderWidth(0);
        _scrollbar_track->setRadius(0);
        _scrollbar_track->setPaddingAll(0);
        _scrollbar_track->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

        _scrollbar_thumb = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(_root->raw_ptr());
        _scrollbar_thumb->setSize(kScrollbarW, kScrollbarMinH);
        _scrollbar_thumb->setPos(kScrollbarX, kScrollbarY);
        _scrollbar_thumb->setBgColor(lv_color_hex(kScrollbarThumbColor));
        _scrollbar_thumb->setBgOpa(LV_OPA_COVER);
        _scrollbar_thumb->setBorderWidth(0);
        _scrollbar_thumb->setRadius(0);
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
        const auto* movement = top > 0 ? (bottom > 0 ? "F↑ X↓" : "F↑") : (bottom > 0 ? "X↓" : "");
        const auto hint = keyHints({movement, local("Esc Back", "Esc Zpět")}, {});
        ui::keyLegend(_keys, hint);
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
        scroll(direction, LV_ANIM_OFF);
    }

    void scroll(int direction, lv_anim_enable_t anim)
    {
        scrollBy(direction * kScrollStep, anim);
    }

    void scrollBy(int32_t distance, lv_anim_enable_t anim)
    {
        if (_panel && distance != 0) {
            _panel->scrollByBounded(0, distance, anim);
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
        if (!isRegularPreviewFile(file)) {
            return false;
        }
        if (file.kind == FileKind::Text || extensionUsuallyText(file.extension)) {
            return true;
        }

        std::string sample;
        if (!readTextSample(file, sample)) {
            return false;
        }
        return looksLikeTextBytes(sample);
    }

    std::unique_ptr<PreviewPage> open(const FileEntry& file) const override
    {
        if (!isRegularPreviewFile(file)) {
            return nullptr;
        }
        return std::make_unique<TextPreviewPage>(file);
    }
};

}  // namespace

std::unique_ptr<PreviewSupport> createTextPreviewSupport()
{
    return std::make_unique<TextPreviewSupport>();
}

std::unique_ptr<PreviewPage> createTextPreviewPage(std::string title, std::string content)
{
    return std::make_unique<TextPreviewPage>(std::move(title), std::move(content));
}

}  // namespace files
