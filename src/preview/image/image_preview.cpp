#include "preview/image/image_preview.hpp"

#include "assets/assets.h"
#include "assets/font_assets.hpp"
#include "core/files_language.hpp"
#include "views/ui_style.hpp"
#include "preview/common/bottom_key_bar.hpp"
#include "preview/image/bmp_decoder.hpp"
#include "preview/image/jpeg_decoder.hpp"
#include <lvgl/lvgl_cpp/image.hpp>
#include <lvgl/lvgl_cpp/label.hpp>
#include <lvgl/lvgl_cpp/obj.hpp>
#include <algorithm>
#include <cctype>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace files {
namespace {

constexpr int32_t kScreenWidth                 = 320;
constexpr int32_t kScreenHeight                = 170;
constexpr int32_t kPreviewX                    = 8;
constexpr int32_t kPreviewY                    = 20;
constexpr int32_t kPreviewWidth                = 304;
constexpr int32_t kPreviewHeight               = 130;
constexpr int32_t kFullscreenX                 = 2;
constexpr int32_t kFullscreenY                 = 2;
constexpr int32_t kFullscreenWidth             = kScreenWidth - 4;
constexpr int32_t kFullscreenHeight            = kScreenHeight - 4;
constexpr int32_t kMoveStep                    = 16;
// LVGL image scales are expressed as 1/256.  The preview viewport is only
// 280x110, so camera photos can legitimately need a scale below 12.5% to fit
// without cropping.  Keep the lower bound at the smallest usable value and
// let fitScale() choose the actual contain scale.
constexpr uint32_t kMinScale                   = 1;
constexpr uint32_t kMaxScale                   = 2048;
constexpr int32_t kCounterClockwiseQuarterTurn = 2700;

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

bool extensionIsJpeg(const std::string& extension)
{
    return extension == ".jpeg" || extension == ".jpg";
}

bool extensionIsBmp(const std::string& extension)
{
    return extension == ".bmp";
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
    return file.name.empty() ? file.path : file.name;
}

uint32_t fitScale(uint32_t imageWidth, uint32_t imageHeight, uint32_t boundsWidth, uint32_t boundsHeight)
{
    if (imageWidth == 0 || imageHeight == 0 || boundsWidth == 0 || boundsHeight == 0) {
        return LV_SCALE_NONE;
    }

    // Use integer floor arithmetic so the transformed image never grows past
    // either viewport edge due to rounding at the contain boundary.
    const uint64_t width_scale  = (static_cast<uint64_t>(boundsWidth) * LV_SCALE_NONE) / imageWidth;
    const uint64_t height_scale = (static_cast<uint64_t>(boundsHeight) * LV_SCALE_NONE) / imageHeight;
    const uint64_t scale        = std::min(width_scale, height_scale);
    return std::clamp(static_cast<uint32_t>(scale), kMinScale, kMaxScale);
}

class ImagePreviewPage : public PreviewPage {
public:
    explicit ImagePreviewPage(FileEntry file)
        : _file(std::move(file)), _title(fileTitle(_file)), _lvgl_path(lvglPath(_file.path))
    {
        _is_gif  = extensionIsGif(_file.extension);
        _is_jpeg = extensionIsJpeg(_file.extension);
        _is_bmp  = extensionIsBmp(_file.extension);
        if (_is_gif) {
            uint16_t width  = 0;
            uint16_t height = 0;
            if (lv_gif_get_size(_lvgl_path.c_str(), &width, &height)) {
                _image_width  = width;
                _image_height = height;
            }
        } else if (_is_jpeg) {
            _jpeg_buffer = decodeJpegFile(_file.path);
            if (_jpeg_buffer) {
                _image_width  = _jpeg_buffer->header.w;
                _image_height = _jpeg_buffer->header.h;
            }
        } else if (_is_bmp) {
            _bmp_buffer = decodeBmpFile(_file.path);
            if (_bmp_buffer) {
                _image_width  = _bmp_buffer->header.w;
                _image_height = _bmp_buffer->header.h;
            }
        } else {
            lv_image_header_t header{};
            if (lv_image_decoder_get_info(_lvgl_path.c_str(), &header) == LV_RESULT_OK) {
                _image_width  = header.w;
                _image_height = header.h;
            }
        }
    }

    bool usesWideFooter() const override { return _preview_loaded && !_fullscreen; }

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

        _title_bar = ui::caption(_root->raw_ptr(), 0, _title, uiFont14());
        _zoom_label = ui::label(_title_bar, 250, 2, 62, uiFont12(), ui::secondary);
        lv_obj_set_style_text_align(_zoom_label, LV_TEXT_ALIGN_RIGHT, 0);
        ui::hidden(_zoom_label, true);

        _viewport = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(_root->raw_ptr());
        _viewport->setBgColor(lv_color_hex(0x000000));
        _viewport->setBgOpa(LV_OPA_COVER);
        _viewport->setBorderWidth(0);
        _viewport->setPaddingAll(0);
        _viewport->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
        _viewport->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

        if (_is_gif) {
            _gif = lv_gif_create(_viewport->raw_ptr());
            lv_gif_set_src(_gif, _lvgl_path.c_str());
            lv_image_set_pivot(_gif, static_cast<int32_t>(_image_width / 2), static_cast<int32_t>(_image_height / 2));
            _preview_loaded = lv_gif_is_loaded(_gif);
        } else {
            _image = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Image>(_viewport->raw_ptr());
            if (_is_jpeg) {
                if (_jpeg_buffer) {
                    _image->setSrc(_jpeg_buffer.get());
                }
            } else if (_is_bmp) {
                if (_bmp_buffer) {
                    _image->setSrc(_bmp_buffer.get());
                }
            } else {
                _image->setSrc(_lvgl_path.c_str());
            }
            _image->setPivot(static_cast<int32_t>(_image_width / 2), static_cast<int32_t>(_image_height / 2));
            _preview_loaded = (_is_jpeg && static_cast<bool>(_jpeg_buffer)) ||
                              (_is_bmp && static_cast<bool>(_bmp_buffer)) ||
                              (!_is_jpeg && !_is_bmp && _image_width > 0 && _image_height > 0);
        }

        _error_label = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Label>(_viewport->raw_ptr());
        _error_label->setText(local("Cannot read image", "Obrázek nelze přečíst"));
        _error_label->setTextFont(uiFont14());
        _error_label->setTextColor(lv_color_hex(ui::error));
        _error_label->setTextAlign(LV_TEXT_ALIGN_CENTER);
        _error_label->setSize(lv_pct(100), LV_SIZE_CONTENT);
        if (_preview_loaded) {
            _error_label->addFlag(LV_OBJ_FLAG_HIDDEN);
        } else {
            const auto* message = local("Cannot read image", "Obrázek nelze přečíst");
            const int width = textWidth(message, uiFont14());
            const int start = (kPreviewWidth - width - 14) / 2;
            ui::errorCross(_viewport->raw_ptr(), start, (kPreviewHeight - 10) / 2);
            _error_label->setTextAlign(LV_TEXT_ALIGN_LEFT);
            _error_label->setSize(width + 1, LV_SIZE_CONTENT);
            _error_label->setPos(start + 14, (kPreviewHeight - lv_font_get_line_height(uiFont14())) / 2);
        }

        _key_bar = std::make_unique<BottomKeyBar>(_root->raw_ptr());
        if (_preview_loaded) {
            showWindowedKeys();
        } else {
            _key_bar->setItems({});
        }

        fitForCurrentMode();
    }

    void detach() override
    {
        _key_bar.reset();
        _error_label.reset();
        _image.reset();
        _gif = nullptr;
        _viewport.reset();
        _title_bar = nullptr;
        _root.reset();
    }

    void onKey(uint32_t key, FilesRouter& router) override
    {
        if (!_preview_loaded && key != '\x1b' && key != files_key::Left) return;
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
                toggleFullscreen();
                break;
            case '5':
                zoom(false);
                break;
            case '7':
                zoom(true);
                break;
            case '8':
                rotateCounterClockwise();
                break;
            default:
                break;
        }
    }

    void tick(uint32_t nowMs) override
    {
        (void)nowMs;
    }

private:
    FileEntry _file;
    std::string _title;
    std::string _lvgl_path;
    DrawBufferPtr _jpeg_buffer;
    DrawBufferPtr _bmp_buffer;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _root;
    lv_obj_t* _title_bar = nullptr;
    lv_obj_t* _zoom_label = nullptr;
    uint32_t _fit_scale = LV_SCALE_NONE;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _viewport;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Image> _image;
    lv_obj_t* _gif = nullptr;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _error_label;
    std::unique_ptr<BottomKeyBar> _key_bar;
    uint32_t _image_width  = 0;
    uint32_t _image_height = 0;
    uint32_t _scale        = LV_SCALE_NONE;
    int32_t _rotation      = 0;
    int32_t _view_x        = 0;
    int32_t _view_y        = 0;
    bool _fullscreen       = false;
    struct ImageTransform { uint32_t scale; int32_t x; int32_t y; };
    ImageTransform _windowed_transform{};
    ImageTransform _fullscreen_start{};
    int32_t _fullscreen_rotation = 0;
    bool _is_gif           = false;
    bool _is_jpeg          = false;
    bool _is_bmp           = false;
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
        const bool swaps_axes       = _rotation % 1800 != 0;
        const uint32_t image_width  = swaps_axes ? _image_height : _image_width;
        const uint32_t image_height = swaps_axes ? _image_width : _image_height;
        return fitScale(image_width, image_height, static_cast<uint32_t>(viewportWidth()),
                        static_cast<uint32_t>(viewportHeight()));
    }

    void toggleFullscreen()
    {
        const ImageTransform before{_scale, _view_x, _view_y};
        const auto previousFit = _fit_scale;
        _fullscreen = !_fullscreen;
        _fit_scale = defaultScale();
        _scale = std::clamp((_scale * _fit_scale + previousFit / 2) / previousFit, kMinScale, kMaxScale);
        if (_fullscreen) {
            _windowed_transform = before;
        } else if (before.scale == _fullscreen_start.scale && before.x == _fullscreen_start.x &&
                   before.y == _fullscreen_start.y && _rotation == _fullscreen_rotation) {
            _scale = _windowed_transform.scale;
            _view_x = _windowed_transform.x;
            _view_y = _windowed_transform.y;
        }
        refreshZoom();
        applyLayout();
        if (_fullscreen) {
            _fullscreen_start = {_scale, _view_x, _view_y};
            _fullscreen_rotation = _rotation;
        }
    }

    void fitForCurrentMode()
    {
        _fit_scale = defaultScale();
        _scale = _fit_scale;
        refreshZoom();
        _view_x = 0;
        _view_y = 0;
        applyLayout();
    }

    void rotateCounterClockwise()
    {
        _rotation = (_rotation + kCounterClockwiseQuarterTurn) % 3600;
        fitForCurrentMode();
    }

    void move(int32_t dx, int32_t dy)
    {
        _view_x += dx;
        _view_y += dy;
        applyImageTransform();
    }

    void zoom(bool zoomIn)
    {
        // Scale relative to the current size: 5/4 in, 4/5 out. A fixed delta
        // can collapse a fitted camera photo straight to the minimum scale.
        const uint32_t numerator   = zoomIn ? 5 : 4;
        const uint32_t denominator = zoomIn ? 4 : 5;
        uint32_t next = (_scale * numerator + denominator / 2) / denominator;
        // Keep both keys effective when rounding would leave the scale unchanged.
        next   = zoomIn ? std::max(next, _scale + 1) : std::min(next, _scale - 1);
        _scale = std::clamp(next, kMinScale, kMaxScale);
        refreshZoom();
        applyImageTransform();
    }

    void refreshZoom()
    {
        const bool zoomed = _scale != _fit_scale;
        const auto* fit = local("Fit", "Přizpůsobit");
        const int zoomWidth = std::max(62, textWidth(fit, uiFont12()) + 2);
        lv_obj_set_x(_zoom_label, 312 - zoomWidth);
        lv_obj_set_width(_zoom_label, zoomWidth);
        const int titleWidth = _preview_loaded ? 296 - zoomWidth : 304;
        auto* title = lv_obj_get_child(_title_bar, 0);
        lv_obj_set_width(title, titleWidth);
        ui::line(title, _title, titleWidth, uiFont14(), std::string::npos, true);
        lv_label_set_text_fmt(_zoom_label, local("%u%%", "%u %%"), (_scale * 100 + _fit_scale / 2) / _fit_scale);
        if (!zoomed) lv_label_set_text(_zoom_label, fit);
        ui::hidden(_zoom_label, !_preview_loaded);
    }

    void applyLayout()
    {
        if (!_viewport) {
            return;
        }

        if (_fullscreen) {
            _viewport->setPos(kFullscreenX, kFullscreenY);
            _viewport->setSize(kFullscreenWidth, kFullscreenHeight);
            _viewport->setRadius(0);
            lv_obj_set_style_clip_corner(_viewport->raw_ptr(), false, LV_PART_MAIN);
            ui::hidden(_title_bar, true);
            if (_key_bar) {
                _key_bar->show(false);
            }
        } else {
            _viewport->setPos(kPreviewX, kPreviewY);
            _viewport->setSize(kPreviewWidth, kPreviewHeight);
            _viewport->setRadius(0);
            lv_obj_set_style_clip_corner(_viewport->raw_ptr(), false, LV_PART_MAIN);
            ui::hidden(_title_bar, false);
            if (_key_bar && _preview_loaded) {
                showWindowedKeys();
            }
        }
        applyImageTransform();
    }

    void showWindowedKeys()
    {
        const auto* fullscreen = local("4 Full screen", "4 Celá obrazovka");
        const int zoomOut = 8 + textWidth(fullscreen, uiFont12()) + 10;
        const int zoomIn = zoomOut + textWidth("5 −", uiFont12()) + 10;
        const int rotate = zoomIn + textWidth("7 +", uiFont12()) + 10;
        const auto* rotation = local("8 Rotate", "8 Otočit");
        const int back = rotate + textWidth(rotation, uiFont12()) + 10;
        const auto* backAction = local("Esc Back", "Esc Zpět");
        _key_bar->setSlots({{8, zoomOut - 8, fullscreen}, {zoomOut, zoomIn - zoomOut, "5 −"},
                            {zoomIn, rotate - zoomIn, "7 +"}, {rotate, back - rotate, rotation},
                            {back, textWidth(backAction, uiFont12()) + 1, backAction}});
    }

    void applyImageTransform()
    {
        lv_obj_t* image_obj = _is_gif ? _gif : (_image ? _image->raw_ptr() : nullptr);
        if (!image_obj || _image_width == 0 || _image_height == 0) {
            return;
        }

        lv_obj_set_size(image_obj, static_cast<int32_t>(_image_width), static_cast<int32_t>(_image_height));
        lv_image_set_pivot(image_obj, static_cast<int32_t>(_image_width / 2), static_cast<int32_t>(_image_height / 2));
        lv_image_set_rotation(image_obj, _rotation);
        lv_image_set_scale(image_obj, _scale);

        const int32_t transformed_width  = lv_image_get_transformed_width(image_obj);
        const int32_t transformed_height = lv_image_get_transformed_height(image_obj);
        const int32_t pan_min_x          = viewportWidth() / 2 - transformed_width / 2;
        const int32_t pan_min_y          = viewportHeight() / 2 - transformed_height / 2;

        _view_x = transformed_width <= viewportWidth()
                      ? 0
                      : std::clamp(_view_x, pan_min_x, pan_min_x + transformed_width - viewportWidth());
        _view_y = transformed_height <= viewportHeight()
                      ? 0
                      : std::clamp(_view_y, pan_min_y, pan_min_y + transformed_height - viewportHeight());

        const int32_t image_x = viewportWidth() / 2 - static_cast<int32_t>(_image_width) / 2 - _view_x;
        const int32_t image_y = viewportHeight() / 2 - static_cast<int32_t>(_image_height) / 2 - _view_y;
        lv_obj_align(image_obj, LV_ALIGN_TOP_LEFT, image_x, image_y);
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
        if (!isRegularPreviewFile(file)) {
            return false;
        }
        return file.kind == FileKind::Image && extensionUsuallyImage(file.extension);
    }

    std::unique_ptr<PreviewPage> open(const FileEntry& file) const override
    {
        if (!isRegularPreviewFile(file)) {
            return nullptr;
        }
        return std::make_unique<ImagePreviewPage>(file);
    }
};

}  // namespace

std::unique_ptr<PreviewSupport> createImagePreviewSupport()
{
    return std::make_unique<ImagePreviewSupport>();
}

}  // namespace files
