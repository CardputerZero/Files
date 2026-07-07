#include "view_models/preview_view_model.hpp"

namespace files {

PreviewViewModel::PreviewViewModel(FilesRouter& router, FilesModel& model) : ViewModel(router), _model(model)
{
}

void PreviewViewModel::onExit()
{
    _model.preview().close();
}

void PreviewViewModel::onKey(uint32_t key)
{
    PreviewPage* page = _model.preview().page().get();
    if (page) {
        page->onKey(key, _router);
    }
}

void PreviewViewModel::onKeyState(uint32_t key, bool pressed)
{
    PreviewPage* page = _model.preview().page().get();
    if (page) {
        page->onKeyState(key, pressed, _router);
    }
}

void PreviewViewModel::tick(uint32_t nowMs)
{
    PreviewPage* page = _model.preview().page().get();
    if (page) {
        page->tick(nowMs);
        if (page->shouldClose()) {
            _router.back();
        }
    }
}

bool PreviewViewModel::suspendsHostRendering() const
{
    PreviewPage* page = _model.preview().page().get();
    return page && page->suspendsHostRendering();
}

}  // namespace files
