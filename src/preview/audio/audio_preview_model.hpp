#pragma once

#include "preview/audio/audio_preview_types.hpp"
#include <tools/observable/single_observable.hpp>
#include <memory>

namespace files {

class AudioPreviewModel {
public:
    AudioPreviewModel();
    ~AudioPreviewModel();

    smooth_ui_toolkit::SingleObservable<AudioPlaybackState>& state()
    {
        return _state;
    }

    smooth_ui_toolkit::SingleObservable<AudioPreviewFile>& file()
    {
        return _file;
    }

    smooth_ui_toolkit::SingleObservable<float>& progressSec()
    {
        return _progress_sec;
    }

    smooth_ui_toolkit::SingleObservable<float>& speed()
    {
        return _speed;
    }

    bool load(const FileEntry& file);
    void togglePlayPause();
    void pause();
    void seek(float offsetSec);
    void toggleSpeed();
    void tick(uint32_t nowMs);

private:
    struct Impl;

    smooth_ui_toolkit::SingleObservable<AudioPlaybackState> _state{AudioPlaybackState::Stopped};
    smooth_ui_toolkit::SingleObservable<AudioPreviewFile> _file{AudioPreviewFile{}};
    smooth_ui_toolkit::SingleObservable<float> _progress_sec{0.0f};
    smooth_ui_toolkit::SingleObservable<float> _speed{1.0f};
    std::unique_ptr<Impl> _impl;
};

}  // namespace files
