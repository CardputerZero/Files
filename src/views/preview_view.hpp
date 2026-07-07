#pragma once

#include "view_models/preview_view_model.hpp"
#include "views/view.hpp"
#include <lvgl/lvgl_cpp/obj.hpp>
#include <memory>

namespace files {

class PreviewView : public View {
public:
    explicit PreviewView(PreviewViewModel& vm);
    ~PreviewView() override;

    void onEnter(lv_obj_t* parent) override;
    void onExit() override;

private:
    PreviewViewModel& _vm;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _root;
    PreviewPage* _attached_page = nullptr;

    void destroy();
    void renderPage(PreviewPage* page);
    static void onPageChanged(void* context, PreviewPage* const& page);
};

}  // namespace files
