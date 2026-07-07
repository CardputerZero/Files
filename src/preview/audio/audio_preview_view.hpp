#pragma once

#include "preview/audio/audio_preview_view_model.hpp"
#include "preview/common/bottom_key_bar.hpp"
#include <core/animation/animate_value/animate_value.hpp>
#include <lvgl/lvgl_cpp/label.hpp>
#include <lvgl/lvgl_cpp/obj.hpp>
#include <memory>

namespace files {

class AudioPreviewView {
public:
    explicit AudioPreviewView(AudioPreviewViewModel& viewModel);
    ~AudioPreviewView();

    AudioPreviewView(const AudioPreviewView&)            = delete;
    AudioPreviewView& operator=(const AudioPreviewView&) = delete;

    void attach(lv_obj_t* parent);
    void detach();
    void tick(uint32_t nowMs);

private:
    class WaveformView;
    class ProgressBar;

    AudioPreviewViewModel& _view_model;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _root;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _fade_mask;
    smooth_ui_toolkit::AnimateValue _fade_mask_opacity;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _title_label;
    std::unique_ptr<WaveformView> _waveform;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _playhead;
    std::unique_ptr<ProgressBar> _progress_bar;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _elapsed_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _remaining_label;
    std::unique_ptr<BottomKeyBar> _key_bar;

    void renderState(AudioPlaybackState state);
    void renderFile(const AudioPreviewFile& file);
    void renderProgress(float progressSec);
    void renderSpeed(float speed);
    void updateKeyBar();
    static void onStateChanged(void* context, const AudioPlaybackState& state);
    static void onFileChanged(void* context, const AudioPreviewFile& file);
    static void onProgressChanged(void* context, const float& progressSec);
    static void onSpeedChanged(void* context, const float& speed);
};

}  // namespace files
