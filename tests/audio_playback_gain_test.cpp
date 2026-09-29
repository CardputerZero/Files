#include "preview/audio/audio_playback_gain.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {

bool nearlyEqual(float lhs, float rhs, float epsilon = 0.0001f)
{
    return std::abs(lhs - rhs) <= epsilon;
}

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

}  // namespace

int main()
{
    const float samples[] = {-0.25f, 0.125f, 0.2f};
    const float peak      = files::audio_playback_gain::observePeak(0.0f, samples, 3);
    require(nearlyEqual(peak, 0.25f), "absolute peak");
    require(nearlyEqual(files::audio_playback_gain::fullScaleGain(peak), 4.0f), "full-scale gain");
    require(nearlyEqual(files::audio_playback_gain::apply(0.125f, 4.0f), 0.5f), "sample gain");
    require(nearlyEqual(files::audio_playback_gain::apply(0.5f, 4.0f), 1.0f), "positive limit");
    require(nearlyEqual(files::audio_playback_gain::apply(-0.5f, 4.0f), -1.0f), "negative limit");
    require(nearlyEqual(files::audio_playback_gain::fullScaleGain(0.0f), 1.0f), "silent gain");
    require(nearlyEqual(files::audio_playback_gain::fullScaleGain(1.0f), 1.0f), "full-scale unity gain");
    require(nearlyEqual(files::audio_playback_gain::observePeak(0.0f, nullptr, 3), 0.0f), "null samples");

    const float exceptional[] = {
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
        -0.4f,
    };
    require(nearlyEqual(files::audio_playback_gain::observePeak(0.0f, exceptional, 3), 0.4f),
            "non-finite peak samples");
    require(nearlyEqual(files::audio_playback_gain::apply(std::numeric_limits<float>::quiet_NaN(), 2.0f), 0.0f),
            "non-finite output sample");
    std::cout << "Audio playback gain tests passed\n";
    return 0;
}
