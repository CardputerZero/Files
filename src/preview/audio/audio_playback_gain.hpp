#pragma once

#include <cstddef>

namespace files::audio_playback_gain {

float observePeak(float current_peak, const float* samples, size_t sample_count);
float fullScaleGain(float peak);
float apply(float sample, float gain);

}  // namespace files::audio_playback_gain
