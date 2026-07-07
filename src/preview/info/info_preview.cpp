#include "preview/info/info_preview.hpp"

#include "assets/assets.h"
#include "assets/font_assets.hpp"
#include <ctime>
#include <iomanip>
#include <lvgl/lvgl_cpp/image.hpp>
#include <lvgl/lvgl_cpp/label.hpp>
#include <lvgl/lvgl_cpp/obj.hpp>
#include <sstream>

namespace files {
namespace {

std::string sizeText(const FileEntry& file)
{
    if (file.directory) {
        return "-";
    }
    if (file.size >= 1024 * 1024) {
        std::ostringstream stream;
        stream << (file.size / (1024 * 1024)) << " MB";
        return stream.str();
    }
    if (file.size >= 1024) {
        std::ostringstream stream;
        stream << (file.size / 1024) << " KB";
        return stream.str();
    }
    return std::to_string(file.size) + " B";
}

std::string modifiedText(int64_t unixSec)
{
    if (unixSec <= 0) {
        return "-";
    }

    std::time_t time = static_cast<std::time_t>(unixSec);
    std::tm tm{};
    if (!localtime_r(&time, &tm)) {
        return "-";
    }

    std::ostringstream stream;
    stream << std::put_time(&tm, "%Y-%m-%d %H:%M");
    return stream.str();
}

std::string fieldText(const char* label, const std::string& value)
{
    return std::string(label) + ": " + (value.empty() ? "-" : value);
}

const lv_image_dsc_t* imageForEntry(const FileEntry& entry)
{
    if (entry.icon == "folder") {
        return &image_icon_folder;
    }
    if (entry.icon == "audio") {
        return &image_icon_audio;
    }
    if (entry.icon == "video") {
        return &image_icon_video;
    }
    if (entry.icon == "image") {
        return &image_icon_image;
    }
    if (entry.icon == "text") {
        return &image_icon_text;
    }
    if (entry.icon == "scripts") {
        return &image_icon_scripts;
    }
    return &image_icon_default;
}

class InfoPreviewPage : public PreviewPage {
public:
    explicit InfoPreviewPage(FileEntry file) : _file(std::move(file)), _title(_file.name.empty() ? "Info" : _file.name)
    {
    }

    const std::string& title() const override
    {
        return _title;
    }

    void attach(lv_obj_t* parent) override
    {
        _root = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(parent);
        _root->setSize(320, 170);
        _root->setBgColor(lv_color_hex(0x000000));
        _root->setBgOpa(LV_OPA_COVER);
        _root->setBorderWidth(0);
        _root->setPaddingAll(0);
        _root->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

        _file_icon = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Image>(_root->raw_ptr());
        _file_icon->setSize(20, 20);
        _file_icon->setPos(10, 10);
        _file_icon->setSrc(imageForEntry(_file));

        _title_label = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Label>(_root->raw_ptr());
        _title_label->setTextFont(uiFont14());
        _title_label->setTextColor(lv_color_hex(0xFFFFFF));
        _title_label->setLongMode(LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
        _title_label->setSize(272, LV_SIZE_CONTENT);
        _title_label->setPos(38, 8);
        _title_label->setText(_title.c_str());

        _field_labels.reserve(5);
        for (int i = 0; i < 5; ++i) {
            auto label = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Label>(_root->raw_ptr());
            label->setTextFont(uiMonoFont12());
            label->setTextColor(lv_color_hex(0xA7A7A7));
            label->setLongMode(LV_LABEL_LONG_DOT);
            label->setSize(300, 16);
            label->setPos(10, 44 + i * 20);
            _field_labels.push_back(std::move(label));
        }

        _path_title_label = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Label>(_root->raw_ptr());
        _path_title_label->setText("Path:");
        _path_title_label->setTextFont(uiMonoFont12());
        _path_title_label->setTextColor(lv_color_hex(0xA7A7A7));
        _path_title_label->setSize(40, 16);
        _path_title_label->setPos(10, 44 + 5 * 20);

        _path_value_label = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Label>(_root->raw_ptr());
        _path_value_label->setTextFont(uiMonoFont12());
        _path_value_label->setTextColor(lv_color_hex(0xA7A7A7));
        _path_value_label->setLongMode(LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
        _path_value_label->setSize(254, lv_font_get_line_height(uiMonoFont12()));
        _path_value_label->setPos(56, 44 + 5 * 20);

        renderFile();
    }

    void detach() override
    {
        _field_labels.clear();
        _path_value_label.reset();
        _path_title_label.reset();
        _title_label.reset();
        _file_icon.reset();
        _root.reset();
    }

    void onKey(uint32_t key, FilesRouter& router) override
    {
        switch (key) {
            case files_key::Left:
            case '\x1b':
                router.back();
                break;
            default:
                break;
        }
    }

private:
    FileEntry _file;
    std::string _title;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _root;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Image> _file_icon;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _title_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _path_title_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _path_value_label;
    std::vector<std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label>> _field_labels;

    void renderFile()
    {
        if (_field_labels.size() < 5) {
            return;
        }

        const std::string extension = _file.directory ? "-" : _file.extension;
        const std::string hidden    = _file.hidden ? "Yes" : "No";
        _field_labels[0]->setText(fieldText("Kind", fileKindName(_file.kind)).c_str());
        _field_labels[1]->setText(fieldText("Size", sizeText(_file)).c_str());
        _field_labels[2]->setText(fieldText("Ext", extension).c_str());
        _field_labels[3]->setText(fieldText("Modified", modifiedText(_file.modifiedUnixSec)).c_str());
        _field_labels[4]->setText(fieldText("Hidden", hidden).c_str());
        if (_path_value_label) {
            _path_value_label->setText(_file.path.empty() ? "-" : _file.path.c_str());
        }
    }
};

class InfoPreviewSupport : public PreviewSupport {
public:
    const char* id() const override
    {
        return "info";
    }

    bool supports(const FileEntry& file) const override
    {
        (void)file;
        return true;
    }

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
