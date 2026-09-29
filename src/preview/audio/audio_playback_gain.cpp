#include "preview/audio/audio_playback_gain.hpp"

#include <algorithm>
#include <cmath>

namespace files::audio_playback_gain {
namespace {

constexpr float kMaxFullScaleGain = 32767.0f;

}  // namespace

float observePeak(float current_peak, const float* samples, size_t sample_count)
{
    float peak = std::isfinite(current_peak) && current_peak > 0.0f ? current_peak : 0.0f;
    if (!samples) {
        return peak;
    }

    for (size_t i = 0; i < sample_count; ++i) {
        const float magnitude = std::abs(samples[i]);
        if (std::isfinite(magnitude)) {
            peak = std::max(peak, magnitude);
        }
    }
    return peak;
}

float fullScaleGain(float peak)
{
    if (!std::isfinite(peak) || peak <= 0.0f || peak >= 1.0f) {
        return 1.0f;
    }
    return std::min(1.0f / peak, kMaxFullScaleGain);
}

float apply(float sample, float gain)
{
    if (!std::isfinite(sample)) {
        return 0.0f;
    }
    if (!std::isfinite(gain) || gain <= 0.0f) {
        gain = 1.0f;
    }
    return std::clamp(sample * gain, -1.0f, 1.0f);
}

}  // namespace files::audio_playback_gain
