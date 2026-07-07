#pragma once

#include "models/files_model.hpp"
#include "view_models/view_model.hpp"
#include <tools/observable/single_observable.hpp>

namespace files {

class PreviewViewModel : public ViewModel {
public:
    PreviewViewModel(FilesRouter& router, FilesModel& model);

    PageId pageId() const override
    {
        return PageId::Preview;
    }

    void onExit() override;
    void onKey(uint32_t key) override;
    void onKeyState(uint32_t key, bool pressed) override;
    void tick(uint32_t nowMs) override;
    bool suspendsHostRendering() const override;

    smooth_ui_toolkit::SingleObservable<PreviewPage*>& page()
    {
        return _model.preview().page();
    }

private:
    FilesModel& _model;
};

}  // namespace files
