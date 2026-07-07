#include "preview/audio/audio_preview_view_model.hpp"

#include <spdlog/spdlog.h>

namespace files {

AudioPreviewViewModel::AudioPreviewViewModel(AudioPreviewModel& model) : _model(model)
{
}

void AudioPreviewViewModel::onExit()
{
    spdlog::info("AudioPreviewViewModel exit");
    _model.pause();
}

void AudioPreviewViewModel::onKey(uint32_t key, FilesRouter& router)
{
    switch (key) {
        case '\x1b':
        case files_key::Left:
            router.back();
            break;
        case '5':
            _model.seek(-10.0f);
            break;
        case '6':
            _model.togglePlayPause();
            break;
        case '7':
            _model.seek(10.0f);
            break;
        case '8':
            _model.toggleSpeed();
            break;
        default:
            break;
    }
}

void AudioPreviewViewModel::tick(uint32_t nowMs)
{
    _model.tick(nowMs);
}

}  // namespace files
