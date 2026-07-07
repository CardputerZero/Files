#include "views/preview_view.hpp"

namespace files {
namespace {

constexpr int32_t kScreenWidth  = 320;
constexpr int32_t kScreenHeight = 170;

}  // namespace

PreviewView::PreviewView(PreviewViewModel& vm) : _vm(vm)
{
}

PreviewView::~PreviewView()
{
    destroy();
}

void PreviewView::onEnter(lv_obj_t* parent)
{
    _root = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(parent);
    _root->setSize(kScreenWidth, kScreenHeight);
    _root->setBgColor(lv_color_hex(0x000000));
    _root->setBgOpa(LV_OPA_COVER);
    _root->setBorderWidth(0);
    _root->setPaddingAll(0);
    _root->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
    _root->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _vm.page().observe(this, onPageChanged);
    renderPage(_vm.page().get());
}

void PreviewView::onExit()
{
    destroy();
}

void PreviewView::destroy()
{
    if (_attached_page) {
        _attached_page->detach();
        _attached_page = nullptr;
    }
    _root.reset();
}

void PreviewView::renderPage(PreviewPage* page)
{
    if (!_root || !page) {
        return;
    }
    if (_attached_page == page) {
        return;
    }
    if (_attached_page) {
        _attached_page->detach();
    }
    _attached_page = page;
    page->attach(_root->raw_ptr());
}

void PreviewView::onPageChanged(void* context, PreviewPage* const& page)
{
    static_cast<PreviewView*>(context)->renderPage(page);
}

}  // namespace files
