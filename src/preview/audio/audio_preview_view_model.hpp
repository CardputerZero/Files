#pragma once

#include "core/files_router.hpp"
#include "preview/audio/audio_preview_model.hpp"

namespace files {

class AudioPreviewViewModel {
public:
    explicit AudioPreviewViewModel(AudioPreviewModel& model);

    void onExit();
    void onKey(uint32_t key, FilesRouter& router);
    void tick(uint32_t nowMs);

    smooth_ui_toolkit::SingleObservable<AudioPlaybackState>& state()
    {
        return _model.state();
    }

    smooth_ui_toolkit::SingleObservable<AudioPreviewFile>& file()
    {
        return _model.file();
    }

    smooth_ui_toolkit::SingleObservable<float>& progressSec()
    {
        return _model.progressSec();
    }

    smooth_ui_toolkit::SingleObservable<float>& speed()
    {
        return _model.speed();
    }

private:
    AudioPreviewModel& _model;
};

}  // namespace files
