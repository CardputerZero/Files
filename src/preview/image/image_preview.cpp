#include "preview/image/image_preview.hpp"

#include "assets/assets.h"
#include "assets/font_assets.hpp"
#include "preview/common/bottom_key_bar.hpp"
#include <lvgl/lvgl_cpp/image.hpp>
#include <lvgl/lvgl_cpp/label.hpp>
#include <lvgl/lvgl_cpp/obj.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace files {
namespace {

constexpr int32_t kScreenWidth      = 320;
constexpr int32_t kScreenHeight     = 170;
constexpr int32_t kTitleWidth       = 230;
constexpr int32_t kTitleY           = -5;
constexpr int32_t kPreviewX         = 20;
constexpr int32_t kPreviewY         = 18;
constexpr int32_t kPreviewWidth     = 280;
constexpr int32_t kPreviewHeight    = 110;
constexpr int32_t kFullscreenX      = 0;
constexpr int32_t kFullscreenY      = 0;
constexpr int32_t kFullscreenWidth  = kScreenWidth;
constexpr int32_t kFullscreenHeight = kScreenHeight;
constexpr int32_t kMoveStep         = 16;
constexpr uint32_t kMinScale        = 32;
constexpr uint32_t kMaxScale        = 2048;
constexpr uint32_t kScaleStep       = 32;

bool extensionUsuallyImage(const std::string& extension)
{
    constexpr std::string_view kImageExtensions[] = {
        ".bmp", ".gif", ".jpeg", ".jpg", ".png",
    };
    return std::find(std::begin(kImageExtensions), std::end(kImageExtensions), extension) != std::end(kImageExtensions);
}

bool extensionIsGif(const std::string& extension)
{
    return extension == ".gif";
}

std::string lvglPath(const std::string& path)
{
    if (path.size() >= 2 && path[1] == ':') {
        return path;
    }
    return "A:" + path;
}

std::string fileTitle(const FileEntry& file)
{
    std::string name      = file.name.empty() ? file.path : file.name;
    const std::string ext = file.extension;
    if (!ext.empty() && name.size() > ext.size()) {
        std::string tail = name.substr(name.size() - ext.size());
        std::transform(tail.begin(), tail.end(), tail.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (tail == ext) {
            name.resize(name.size() - ext.size());
        }
    }
    return name;
}

uint32_t fitScale(uint32_t imageWidth, uint32_t imageHeight, uint32_t boundsWidth, uint32_t boundsHeight)
{
    if (imageWidth == 0 || imageHeight == 0 || boundsWidth == 0 || boundsHeight == 0) {
        return LV_SCALE_NONE;
    }

    const float scale = std::min(static_cast<float>(boundsWidth) / static_cast<float>(imageWidth),
                                 static_cast<float>(boundsHeight) / static_cast<float>(imageHeight));
    return std::clamp(static_cast<uint32_t>(std::round(scale * static_cast<float>(LV_SCALE_NONE))), kMinScale,
                      kMaxScale);
}

class ImagePreviewPage : public PreviewPage {
public:
    explicit ImagePreviewPage(FileEntry file)
        : _file(std::move(file)), _title(fileTitle(_file)), _lvgl_path(lvglPath(_file.path))
    {
        _is_gif = extensionIsGif(_file.extension);
        if (_is_gif) {
            uint16_t width  = 0;
            uint16_t height = 0;
            if (lv_gif_get_size(_lvgl_path.c_str(), &width, &height)) {
                _image_width  = width;
                _image_height = height;
            }
        } else {
            lv_image_header_t header{};
            if (lv_image_decoder_get_info(_lvgl_path.c_str(), &header) == LV_RESULT_OK) {
                _image_width  = header.w;
                _image_height = header.h;
            }
        }
    }

    const std::string& title() const override
    {
        return _title;
    }

    void attach(lv_obj_t* parent) override
    {
        detach();

        _root = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(parent);
        _root->setSize(kScreenWidth, kScreenHeight);
        _root->setBgColor(lv_color_hex(0x000000));
        _root->setBgOpa(LV_OPA_COVER);
        _root->setBorderWidth(0);
        _root->setPaddingAll(0);
        _root->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
        _root->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

        _title_label = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Label>(_root->raw_ptr());
        _title_label->setText(_title.c_str());
        _title_label->setTextFont(uiFont14());
        _title_label->setTextColor(lv_color_hex(0x777777));
        _title_label->setTextAlign(LV_TEXT_ALIGN_CENTER);
        _title_label->setLongMode(LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
        _title_label->setSize(kTitleWidth, LV_SIZE_CONTENT);
        _title_label->align(LV_ALIGN_TOP_MID, 0, kTitleY);

        _viewport = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(_root->raw_ptr());
        _viewport->setBgColor(lv_color_hex(0x000000));
        _viewport->setBgOpa(LV_OPA_COVER);
        _viewport->setBorderWidth(0);
        _viewport->setPaddingAll(0);
        _viewport->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
        _viewport->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_clip_corner(_viewport->raw_ptr(), true, LV_PART_MAIN);

        if (_is_gif) {
            _gif = lv_gif_create(_viewport->raw_ptr());
            lv_gif_set_src(_gif, _lvgl_path.c_str());
            lv_image_set_pivot(_gif, LV_PCT(50), LV_PCT(50));
            _preview_loaded = lv_gif_is_loaded(_gif);
        } else {
            _image = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Image>(_viewport->raw_ptr());
            _image->setSrc(_lvgl_path.c_str());
            _image->setPivot(LV_PCT(50), LV_PCT(50));
            _preview_loaded = _image_width > 0 && _image_height > 0;
        }

        _error_label = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Label>(_viewport->raw_ptr());
        _error_label->setText("No preview");
        _error_label->setTextFont(uiFont14());
        _error_label->setTextColor(lv_color_hex(0x777777));
        _error_label->setTextAlign(LV_TEXT_ALIGN_CENTER);
        _error_label->setSize(lv_pct(100), LV_SIZE_CONTENT);
        _error_label->align(LV_ALIGN_CENTER, 0, 0);
        if (_preview_loaded) {
            _error_label->addFlag(LV_OBJ_FLAG_HIDDEN);
        }

        _key_bar = std::make_unique<BottomKeyBar>(_root->raw_ptr());
        _key_bar->setItems({
            {'4', &image_icon_fullscreen},
            {'5', &image_icon_zoom_out},
            {'7', &image_icon_zoom_in},
            {'8', &image_icon_reset},
        });

        resetForCurrentMode();
    }

    void detach() override
    {
        _key_bar.reset();
        _error_label.reset();
        _image.reset();
        _gif = nullptr;
        _viewport.reset();
        _title_label.reset();
        _root.reset();
    }

    void onKey(uint32_t key, FilesRouter& router) override
    {
        switch (key) {
            case '\x1b':
            case files_key::Left:
                if (key == files_key::Left) {
                    move(-kMoveStep, 0);
                } else {
                    router.back();
                }
                break;
            case files_key::Right:
                move(kMoveStep, 0);
                break;
            case files_key::Up:
                move(0, -kMoveStep);
                break;
            case files_key::Down:
                move(0, kMoveStep);
                break;
            case '4':
                _fullscreen = !_fullscreen;
                resetForCurrentMode();
                break;
            case '5':
                zoom(-static_cast<int32_t>(kScaleStep));
                break;
            case '7':
                zoom(static_cast<int32_t>(kScaleStep));
                break;
            case '8':
                resetForCurrentMode();
                break;
            default:
                break;
        }
    }

    void tick(uint32_t nowMs) override
    {
        (void)nowMs;
        if (_key_bar) {
            _key_bar->tick();
        }
    }

private:
    FileEntry _file;
    std::string _title;
    std::string _lvgl_path;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _root;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _title_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _viewport;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Image> _image;
    lv_obj_t* _gif = nullptr;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _error_label;
    std::unique_ptr<BottomKeyBar> _key_bar;
    uint32_t _image_width  = 0;
    uint32_t _image_height = 0;
    uint32_t _scale        = LV_SCALE_NONE;
    int32_t _offset_x      = 0;
    int32_t _offset_y      = 0;
    bool _fullscreen       = false;
    bool _is_gif           = false;
    bool _preview_loaded   = false;

    int32_t viewportWidth() const
    {
        return _fullscreen ? kFullscreenWidth : kPreviewWidth;
    }

    int32_t viewportHeight() const
    {
        return _fullscreen ? kFullscreenHeight : kPreviewHeight;
    }

    uint32_t defaultScale() const
    {
        return fitScale(_image_width, _image_height, static_cast<uint32_t>(viewportWidth()),
                        static_cast<uint32_t>(viewportHeight()));
    }

    void resetForCurrentMode()
    {
        _scale    = defaultScale();
        _offset_x = 0;
        _offset_y = 0;
        applyLayout();
    }

    void move(int32_t dx, int32_t dy)
    {
        _offset_x += dx;
        _offset_y += dy;
        applyImageTransform();
    }

    void zoom(int32_t delta)
    {
        const auto next = static_cast<int32_t>(_scale) + delta;
        _scale =
            static_cast<uint32_t>(std::clamp(next, static_cast<int32_t>(kMinScale), static_cast<int32_t>(kMaxScale)));
        applyImageTransform();
    }

    void applyLayout()
    {
        if (!_viewport) {
            return;
        }

        if (_fullscreen) {
            _viewport->setPos(kFullscreenX, kFullscreenY);
            _viewport->setSize(kFullscreenWidth, kFullscreenHeight);
            if (_title_label) {
                _title_label->addFlag(LV_OBJ_FLAG_HIDDEN);
            }
            if (_key_bar) {
                _key_bar->setItems({});
            }
        } else {
            _viewport->setPos(kPreviewX, kPreviewY);
            _viewport->setSize(kPreviewWidth, kPreviewHeight);
            if (_title_label) {
                _title_label->removeFlag(LV_OBJ_FLAG_HIDDEN);
            }
            if (_key_bar) {
                _key_bar->setItems({
                    {'4', &image_icon_fullscreen},
                    {'5', &image_icon_zoom_out},
                    {'7', &image_icon_zoom_in},
                    {'8', &image_icon_reset},
                });
            }
        }
        if (_error_label) {
            _error_label->setSize(viewportWidth(), LV_SIZE_CONTENT);
            _error_label->align(LV_ALIGN_CENTER, 0, 0);
        }
        applyImageTransform();
    }

    void applyImageTransform()
    {
        lv_obj_t* image_obj = _is_gif ? _gif : (_image ? _image->raw_ptr() : nullptr);
        if (!image_obj || _image_width == 0 || _image_height == 0) {
            return;
        }

        lv_obj_set_size(image_obj, static_cast<int32_t>(_image_width), static_cast<int32_t>(_image_height));
        lv_image_set_scale(image_obj, _scale);
        lv_obj_align(image_obj, LV_ALIGN_CENTER, _offset_x, _offset_y);
    }
};

class ImagePreviewSupport : public PreviewSupport {
public:
    const char* id() const override
    {
        return "image";
    }

    bool supports(const FileEntry& file) const override
    {
        if (file.directory) {
            return false;
        }
        return file.kind == FileKind::Image && extensionUsuallyImage(file.extension);
    }

    std::unique_ptr<PreviewPage> open(const FileEntry& file) const override
    {
        return std::make_unique<ImagePreviewPage>(file);
    }
};

}  // namespace

std::unique_ptr<PreviewSupport> createImagePreviewSupport()
{
    return std::make_unique<ImagePreviewSupport>();
}

}  // namespace files
