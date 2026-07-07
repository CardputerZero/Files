#pragma once

#include "core/files_types.hpp"
#include <cstdint>

namespace files {

enum class AudioPlaybackState {
    Stopped,
    Playing,
    Paused,
};

struct AudioPreviewFile {
    FileEntry entry;
    uint32_t durationSec = 0;
};

inline const char* audioPlaybackStateName(AudioPlaybackState state)
{
    switch (state) {
        case AudioPlaybackState::Stopped:
            return "Stopped";
        case AudioPlaybackState::Playing:
            return "Playing";
        case AudioPlaybackState::Paused:
            return "Paused";
        default:
            return "Unknown";
    }
}

}  // namespace files
