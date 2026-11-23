#pragma once

#include "core/files_types.hpp"
#include <cstdint>

namespace files {

enum class AudioPlaybackState {
    Stopped,
    Ready,
    Playing,
    Paused,
    Error,
};

struct AudioPreviewFile {
    FileEntry entry;
    float durationSec = 0;
};

inline const char* audioPlaybackStateName(AudioPlaybackState state)
{
    switch (state) {
        case AudioPlaybackState::Ready: return "Ready";
        case AudioPlaybackState::Stopped:
            return "Stopped";
        case AudioPlaybackState::Playing:
            return "Playing";
        case AudioPlaybackState::Paused:
            return "Paused";
        case AudioPlaybackState::Error:
            return "Error";
        default:
            return "Unknown";
    }
}

}  // namespace files
