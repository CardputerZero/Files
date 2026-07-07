#include "preview/audio/audio_preview_model.hpp"

#include <miniaudio.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>

namespace files {
namespace {

constexpr ma_uint32 kPlaybackChannels   = 2;
constexpr ma_uint32 kPlaybackSampleRate = 48000;
constexpr ma_format kPlaybackFormat     = ma_format_f32;

const char* maResultName(ma_result result)
{
    switch (result) {
        case MA_SUCCESS:
            return "MA_SUCCESS";
        case MA_NO_BACKEND:
            return "MA_NO_BACKEND";
        case MA_NO_DEVICE:
            return "MA_NO_DEVICE";
        case MA_DEVICE_NOT_INITIALIZED:
            return "MA_DEVICE_NOT_INITIALIZED";
        case MA_FAILED_TO_INIT_BACKEND:
            return "MA_FAILED_TO_INIT_BACKEND";
        case MA_FAILED_TO_OPEN_BACKEND_DEVICE:
            return "MA_FAILED_TO_OPEN_BACKEND_DEVICE";
        default:
            return "MA_ERROR";
    }
}

uint32_t speedStep(float speed)
{
    if (speed >= 5.0f) {
        return 5;
    }
    if (speed >= 2.0f) {
        return 2;
    }
    return 1;
}

}  // namespace

struct AudioPreviewModel::Impl {
    ma_context context{};
    ma_device device{};
    ma_decoder decoder{};

    bool context_inited = false;
    bool device_inited  = false;
    bool decoder_inited = false;

    std::mutex decoder_mutex;
    std::atomic<bool> playing{false};
    std::atomic<bool> finished{false};
    std::atomic<uint32_t> speed_step{1};
    ma_uint64 total_frames = 0;

    ~Impl()
    {
        cleanup();
    }

    bool load(const FileEntry& file)
    {
        cleanupDecoder();
        finished.store(false);
        playing.store(false);

        ma_decoder_config decoder_config =
            ma_decoder_config_init(kPlaybackFormat, kPlaybackChannels, kPlaybackSampleRate);
        ma_result result = ma_decoder_init_file(file.path.c_str(), &decoder_config, &decoder);
        if (result != MA_SUCCESS) {
            spdlog::error("AudioPreviewModel: ma_decoder_init_file failed, path={}, result={} {}", file.path,
                          static_cast<int>(result), maResultName(result));
            return false;
        }
        decoder_inited = true;

        result = ma_decoder_get_length_in_pcm_frames(&decoder, &total_frames);
        if (result != MA_SUCCESS) {
            total_frames = 0;
            spdlog::warn("AudioPreviewModel: failed to get length, result={} {}", static_cast<int>(result),
                         maResultName(result));
        }

        spdlog::info("AudioPreviewModel: loaded path={}, frames={}, duration={:.2f}s", file.path, total_frames,
                     durationSec());
        return ensureDevice();
    }

    bool play()
    {
        if (!decoder_inited) {
            spdlog::warn("AudioPreviewModel: play ignored, no file loaded");
            return false;
        }
        if (!ensureDevice()) {
            return false;
        }

        if (cursorFrame() >= total_frames && total_frames > 0) {
            seekSeconds(0.0f);
        }

        finished.store(false);
        playing.store(true);

        ma_result result = ma_device_start(&device);
        if (result != MA_SUCCESS) {
            playing.store(false);
            spdlog::error("AudioPreviewModel: ma_device_start failed, result={} {}", static_cast<int>(result),
                          maResultName(result));
            return false;
        }

        spdlog::info("AudioPreviewModel: play");
        return true;
    }

    void pause()
    {
        playing.store(false);
        stopDevice();
        spdlog::info("AudioPreviewModel: pause");
    }

    void seekSeconds(float seconds)
    {
        if (!decoder_inited) {
            return;
        }

        const float clamped_sec = std::clamp(seconds, 0.0f, durationSec());
        const auto target_frame = static_cast<ma_uint64>(std::round(clamped_sec * kPlaybackSampleRate));

        std::lock_guard<std::mutex> lock(decoder_mutex);
        ma_result result = ma_decoder_seek_to_pcm_frame(&decoder, target_frame);
        if (result != MA_SUCCESS) {
            spdlog::warn("AudioPreviewModel: seek failed, target={:.2f}s, result={} {}", clamped_sec,
                         static_cast<int>(result), maResultName(result));
            return;
        }

        finished.store(false);
        spdlog::info("AudioPreviewModel: seek {:.2f}s", clamped_sec);
    }

    float progressSec()
    {
        return static_cast<float>(cursorFrame()) / static_cast<float>(kPlaybackSampleRate);
    }

    float durationSec() const
    {
        return kPlaybackSampleRate == 0 ? 0.0f
                                        : static_cast<float>(total_frames) / static_cast<float>(kPlaybackSampleRate);
    }

    bool consumeFinished()
    {
        return finished.exchange(false);
    }

    void setSpeed(float speed)
    {
        speed_step.store(speedStep(speed));
    }

    void cleanup()
    {
        playing.store(false);
        stopDevice();
        cleanupDecoder();
        cleanupDevice();

        if (context_inited) {
            ma_context_uninit(&context);
            context_inited = false;
        }
    }

private:
    bool ensureDevice()
    {
        if (device_inited) {
            return true;
        }

        if (!initContext()) {
            return false;
        }

        ma_device_config device_config  = ma_device_config_init(ma_device_type_playback);
        device_config.playback.format   = kPlaybackFormat;
        device_config.playback.channels = kPlaybackChannels;
        device_config.sampleRate        = kPlaybackSampleRate;
        device_config.dataCallback      = dataCallback;
        device_config.pUserData         = this;

        ma_result result = ma_device_init(&context, &device_config, &device);
        if (result != MA_SUCCESS) {
            spdlog::error("AudioPreviewModel: ma_device_init failed, result={} {}", static_cast<int>(result),
                          maResultName(result));
            return false;
        }

        device_inited = true;
        spdlog::info("AudioPreviewModel: playback device initialized, channels={}, sampleRate={}", kPlaybackChannels,
                     kPlaybackSampleRate);
        return true;
    }

    bool initContext()
    {
        if (context_inited) {
            return true;
        }

        ma_result result = ma_context_init(nullptr, 0, nullptr, &context);
        if (result != MA_SUCCESS) {
            spdlog::error("AudioPreviewModel: ma_context_init default backend failed, result={} {}",
                          static_cast<int>(result), maResultName(result));
            return false;
        }
        spdlog::info("AudioPreviewModel: miniaudio context initialized with default backend");
        context_inited = true;
        return true;
    }

    void cleanupDevice()
    {
        if (device_inited) {
            ma_device_uninit(&device);
            device_inited = false;
        }
    }

    void stopDevice()
    {
        if (device_inited && ma_device_get_state(&device) != ma_device_state_stopped) {
            ma_device_stop(&device);
        }
    }

    void cleanupDecoder()
    {
        stopDevice();

        std::lock_guard<std::mutex> lock(decoder_mutex);
        if (decoder_inited) {
            ma_decoder_uninit(&decoder);
            decoder_inited = false;
        }
        total_frames = 0;
    }

    ma_uint64 cursorFrame()
    {
        if (!decoder_inited) {
            return 0;
        }

        std::lock_guard<std::mutex> lock(decoder_mutex);
        ma_uint64 cursor = 0;
        ma_result result = ma_decoder_get_cursor_in_pcm_frames(&decoder, &cursor);
        if (result != MA_SUCCESS) {
            return 0;
        }
        return cursor;
    }

    static void dataCallback(ma_device* device, void* output, const void* input, ma_uint32 frame_count)
    {
        (void)input;

        auto* self = static_cast<Impl*>(device->pUserData);
        auto* out  = static_cast<float*>(output);
        if (!self || !out || frame_count == 0) {
            return;
        }

        std::memset(out, 0, static_cast<size_t>(frame_count) * kPlaybackChannels * sizeof(float));
        if (!self->playing.load() || !self->decoder_inited) {
            return;
        }

        std::lock_guard<std::mutex> lock(self->decoder_mutex);
        if (!self->decoder_inited) {
            return;
        }

        const uint32_t step              = std::max<uint32_t>(1, self->speed_step.load());
        float frame[kPlaybackChannels]   = {};
        float discard[kPlaybackChannels] = {};

        for (ma_uint32 i = 0; i < frame_count; ++i) {
            ma_uint64 frames_read = 0;
            ma_decoder_read_pcm_frames(&self->decoder, frame, 1, &frames_read);
            if (frames_read == 0) {
                self->playing.store(false);
                self->finished.store(true);
                break;
            }

            std::memcpy(out + static_cast<size_t>(i) * kPlaybackChannels, frame, sizeof(frame));

            for (uint32_t skip = 1; skip < step; ++skip) {
                ma_uint64 discarded = 0;
                ma_decoder_read_pcm_frames(&self->decoder, discard, 1, &discarded);
                if (discarded == 0) {
                    self->playing.store(false);
                    self->finished.store(true);
                    return;
                }
            }
        }
    }
};

AudioPreviewModel::AudioPreviewModel() : _impl(std::make_unique<Impl>())
{
}

AudioPreviewModel::~AudioPreviewModel() = default;

bool AudioPreviewModel::load(const FileEntry& file)
{
    spdlog::info("AudioPreviewModel: load requested path={}", file.path);
    _state.set(AudioPlaybackState::Stopped);
    _progress_sec.set(0.0f);

    if (!_impl->load(file)) {
        _file.set(AudioPreviewFile{file, 0});
        return false;
    }

    const auto duration = static_cast<uint32_t>(std::ceil(std::max(0.0f, _impl->durationSec())));
    _file.set(AudioPreviewFile{file, duration});
    _state.set(AudioPlaybackState::Paused);
    _progress_sec.set(_impl->progressSec());
    return true;
}

void AudioPreviewModel::togglePlayPause()
{
    if (_state.get() == AudioPlaybackState::Playing) {
        pause();
        return;
    }

    if (_impl->play()) {
        _state.set(AudioPlaybackState::Playing);
    }
}

void AudioPreviewModel::pause()
{
    if (_state.get() != AudioPlaybackState::Playing) {
        return;
    }

    _impl->pause();
    _state.set(AudioPlaybackState::Paused);
    _progress_sec.set(_impl->progressSec());
}

void AudioPreviewModel::seek(float offsetSec)
{
    if (_state.get() == AudioPlaybackState::Stopped && _file.get().entry.path.empty()) {
        return;
    }

    const float next = _progress_sec.get() + offsetSec;
    _impl->seekSeconds(next);
    _progress_sec.set(_impl->progressSec());

    if (_state.get() == AudioPlaybackState::Stopped) {
        _state.set(AudioPlaybackState::Paused);
    }
}

void AudioPreviewModel::toggleSpeed()
{
    float current = _speed.get();
    float next    = 1.0f;
    if (current < 2.0f) {
        next = 2.0f;
    } else if (current < 5.0f) {
        next = 5.0f;
    }

    spdlog::info("AudioPreviewModel: speed {}x", next);
    _speed.set(next);
    _impl->setSpeed(next);
}

void AudioPreviewModel::tick(uint32_t nowMs)
{
    (void)nowMs;

    if (_state.get() == AudioPlaybackState::Playing) {
        _progress_sec.set(_impl->progressSec());
    }

    if (_impl->consumeFinished()) {
        _state.set(AudioPlaybackState::Stopped);
        _progress_sec.set(static_cast<float>(_file.get().durationSec));
        spdlog::info("AudioPreviewModel: finished");
    }
}

}  // namespace files
