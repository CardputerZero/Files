#include "preview/video/video_preview.hpp"

#include "core/files_language.hpp"
#include "views/ui_style.hpp"
#include "preview/common/bottom_key_bar.hpp"
#include <array>

#include <spdlog/spdlog.h>
#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace files {
namespace {

bool extensionUsuallyVideo(const std::string& extension)
{
    constexpr std::string_view kVideoExtensions[] = {
        ".avi", ".m4v", ".mkv", ".mov", ".mp4", ".mpeg", ".mpg", ".webm",
    };
    return std::find(std::begin(kVideoExtensions), std::end(kVideoExtensions), extension) != std::end(kVideoExtensions);
}

constexpr int kVideoWidth = 316;
constexpr int kVideoHeight = 130;
constexpr int kFrameRate = 25;
constexpr const char* kVideoFilter =
    "scale=316:130:force_original_aspect_ratio=decrease,"
    "pad=316:130:(ow-iw)/2:(oh-ih)/2:black,setsar=1,fps=25:start_time=0,format=rgb565le";
constexpr uint32_t kSeekStepMs = 10000;

#if defined(__unix__) || defined(__APPLE__)
const char* videoPulseSink()
{
    const char* sink = std::getenv("FILES_VIDEO_PULSE_SINK");
    if (sink && sink[0] != '\0') {
        return sink;
    }
    return "default";
}

bool outputHasNonWhitespace(const char* buffer, ssize_t size)
{
    for (ssize_t i = 0; i < size; ++i) {
        if (!std::isspace(static_cast<unsigned char>(buffer[i]))) {
            return true;
        }
    }
    return false;
}

bool fileHasAudioStream(const std::string& path)
{
    int pipe_fds[2] = {-1, -1};
    if (::pipe(pipe_fds) != 0) {
        spdlog::warn("VideoPreview: failed to create ffprobe pipe: {}", std::strerror(errno));
        return false;
    }

    const pid_t pid = ::fork();
    if (pid < 0) {
        spdlog::warn("VideoPreview: failed to fork ffprobe: {}", std::strerror(errno));
        ::close(pipe_fds[0]);
        ::close(pipe_fds[1]);
        return false;
    }

    if (pid == 0) {
        ::close(pipe_fds[0]);
        ::dup2(pipe_fds[1], STDOUT_FILENO);
        ::close(pipe_fds[1]);

        const int null_fd = ::open("/dev/null", O_WRONLY);
        if (null_fd >= 0) {
            ::dup2(null_fd, STDERR_FILENO);
            ::close(null_fd);
        }

        ::execlp("ffprobe", "ffprobe", "-v", "error", "-select_streams", "a:0", "-show_entries", "stream=index", "-of",
                 "csv=p=0", path.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }

    ::close(pipe_fds[1]);
    bool has_audio = false;
    char buffer[64];
    while (true) {
        const ssize_t bytes = ::read(pipe_fds[0], buffer, sizeof(buffer));
        if (bytes > 0) {
            has_audio = has_audio || outputHasNonWhitespace(buffer, bytes);
            continue;
        }
        if (bytes == 0) {
            break;
        }
        if (errno != EINTR) {
            spdlog::warn("VideoPreview: failed to read ffprobe output: {}", std::strerror(errno));
            break;
        }
    }
    ::close(pipe_fds[0]);

    int status = 0;
    while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    return has_audio && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

std::string secondsArg(uint64_t offset_ms)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.3f", static_cast<double>(offset_ms) / 1000.0);
    return buffer;
}

void execFfmpeg(const std::string& path, uint64_t offset_ms, bool has_audio, const std::string& pulse_sink, bool still)
{
    std::vector<std::string> args = {
        "ffmpeg", "-hide_banner", "-loglevel", "warning", "-nostdin", "-fflags", "+genpts",
    };
    if (!still) args.emplace_back("-re");
    std::string seek_arg;
    if (offset_ms > 0) {
        seek_arg = secondsArg(offset_ms);
        args.emplace_back("-ss");
        args.emplace_back(seek_arg);
    }
    args.insert(args.end(), {
                                "-i",
                                path,
                                "-map",
                                "0:v:0",
                                "-vf",
                                kVideoFilter,
                                "-an",
                            });
    if (still) args.insert(args.end(), {"-frames:v", "1"});
    args.insert(args.end(), {"-f", "rawvideo", "-threads:v", "1", "pipe:1"});
    if (has_audio && !still) {
        args.insert(args.end(), {
                                    "-map",
                                    "0:a:0?",
                                    "-vn",
                                    "-ac",
                                    "2",
                                    "-ar",
                                    "48000",
                                    "-f",
                                    "pulse",
                                    pulse_sink,
                                });
    }

    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (auto& arg : args) {
        argv.push_back(arg.data());
    }
    argv.push_back(nullptr);
    ::execvp("ffmpeg", argv.data());
}
#endif

class VideoPreviewPage : public PreviewPage {
public:
    explicit VideoPreviewPage(FileEntry file) : _file(std::move(file)), _title(_file.name)
    {
        if (_title.empty()) {
            _title = _file.path;
        }
    }

    ~VideoPreviewPage() override
    {
        stopPlayer();
    }

    bool usesWideFooter() const override { return true; }

    const std::string& title() const override
    {
        return _title;
    }

    void attach(lv_obj_t* parent) override
    {
        _root = ui::box(parent, 0, 0, 320, 170, ui::black);
        auto* header = ui::caption(_root, 0, _title, uiFont14());
        _heading = lv_obj_get_child(header, 0);
        _state = ui::label(header, 250, 2, 62, uiFont12());
        const auto* paused = local("Ⅱ Paused", "Ⅱ Pauza");
        _pauseLabel = ui::label(header, 180, 2, textWidth(paused, uiFont12()) + 1, uiFont12());
        lv_label_set_text(_pauseLabel, paused);
        ui::hidden(_pauseLabel, true);
        lv_obj_set_style_text_align(_state, LV_TEXT_ALIGN_RIGHT, 0);
        ui::hidden(_state, true);
        _image = lv_image_create(_root);
        lv_obj_set_pos(_image, 2, 20);
        _frame.header.magic = LV_IMAGE_HEADER_MAGIC;
        _frame.header.cf = LV_COLOR_FORMAT_RGB565;
        _frame.header.w = kVideoWidth;
        _frame.header.h = kVideoHeight;
        _frame.header.stride = kVideoWidth * 2;
        _frame.data_size = _pixels.size();
        _frame.data = _pixels.data();
        lv_image_set_src(_image, &_frame);
        ui::hidden(_image, true);
        _status = ui::label(_root, 8, 76, 304, uiFont14());
        lv_obj_set_style_text_align(_status, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(_status, local("Opening video…", "Otevírám video…"));
        _detail = ui::label(_root, 8, 86, 304, uiFont12());
        lv_obj_set_style_text_align(_detail, LV_TEXT_ALIGN_CENTER, 0);
        ui::hidden(_detail, true);
        _keys = std::make_unique<BottomKeyBar>(_root);
        refreshKeys();
        startPlayer();
    }

    void detach() override
    {
        stopPlayer();
        _keys.reset();
        if (_root) lv_obj_delete(_root);
        _root = _status = nullptr;
    }

    void onKey(uint32_t key, FilesRouter& router) override
    {
        if (key == '\x1b') {
            stopPlayer();
            router.back();
            return;
        }
        if (!_frame_ready || _still_pending) return;
        if (key == ' ') {
            togglePause();
            refreshKeys();
            return;
        }
        if (key == files_key::Left) {
            seekRelative(-static_cast<int32_t>(kSeekStepMs));
            refreshKeys();
            return;
        }
        if (key == files_key::Right) {
            seekRelative(static_cast<int32_t>(kSeekStepMs));
            refreshKeys();
        }
    }

    void tick(uint32_t nowMs) override
    {
        (void)nowMs;
        readFrame();
        pollPlayer();
    }

    bool shouldClose() const override
    {
        return _finished;
    }

private:
    FileEntry _file;
    std::string _title;
    bool _started  = false;
    bool _finished = false;
    bool _paused   = false;
    bool _failed   = false;
    lv_obj_t* _root = nullptr;
    lv_obj_t* _status = nullptr;
    uint64_t _last_clock = 0;
    std::unique_ptr<BottomKeyBar> _keys;
    lv_obj_t* _heading = nullptr;
    lv_obj_t* _state = nullptr;
    lv_obj_t* _pauseLabel = nullptr;
    lv_obj_t* _detail = nullptr;

    lv_obj_t* _image = nullptr;
    lv_image_dsc_t _frame{};
    std::array<uint8_t, kVideoWidth * kVideoHeight * 2> _pixels{};
    std::array<uint8_t, kVideoWidth * kVideoHeight * 2> _buffer{};
    size_t _received = 0;
    int _pipe = -1;
    bool _frame_ready = false;
    bool _still_pending = false;
    uint64_t _segment_frames = 0;
    uint64_t _displayed_ms = 0;
    uint64_t _seek_offset_ms = 0;

    void readFrame()
    {
        if (_pipe < 0) return;
        while (_received < _buffer.size()) {
            const auto bytes = ::read(_pipe, _buffer.data() + _received, _buffer.size() - _received);
            if (bytes <= 0) return;
            _received += bytes;
        }
        _received = 0;
        _pixels = _buffer;
        _displayed_ms = _seek_offset_ms + (1000 * _segment_frames++) / kFrameRate;
        ui::hidden(_image, false);
        ui::hidden(_status, true);
        lv_obj_invalidate(_image);
        if (!_frame_ready || _still_pending) {
            _frame_ready = true;
            _still_pending = false;
            refreshKeys();
        } else if (_displayed_ms / 1000 != _last_clock) {
            refreshClock();
        }
    }

    void closePipe()
    {
        if (_pipe >= 0) ::close(_pipe);
        _pipe = -1;
        _received = 0;
    }

    void refreshClock()
    {
        const bool ready = !_failed && !_finished && _frame_ready;
        ui::hidden(_state, !ready);
        ui::hidden(_pauseLabel, !ready || !_paused);
        int titleWidth = 304;
        if (ready) {
            const auto seconds = _displayed_ms / 1000;
            _last_clock = seconds;
            const auto secondPart = seconds % 60;
            const auto clock = std::to_string(seconds / 60) + ":" + (secondPart < 10 ? "0" : "") + std::to_string(secondPart);
            const int clockWidth = std::max(textWidth("10:00", uiFont12()), textWidth(clock, uiFont12())) + 1;
            const int clockX = 312 - clockWidth;
            lv_obj_set_x(_state, clockX);
            lv_obj_set_width(_state, clockWidth);
            lv_label_set_text(_state, clock.c_str());
            titleWidth -= clockWidth + 8;
            if (_paused) {
                const int pauseWidth = textWidth(lv_label_get_text(_pauseLabel), uiFont12()) + 1;
                lv_obj_set_x(_pauseLabel, clockX - pauseWidth - 8);
                titleWidth -= pauseWidth + 8;
            }
        }
        lv_obj_set_width(_heading, titleWidth);
        lv_label_set_text(_heading, fitWordEdges(_title, titleWidth, uiFont14()).c_str());
    }

    void refreshKeys()
    {
        refreshClock();
        const bool ready = !_failed && !_finished && _frame_ready;
        const bool paused = ready && _paused;
        if (ready) {
            _keys->setSlots({{8, 121, paused ? local("Space Resume", "Mezera Pokračovat") :
                                              local("Space Pause", "Mezera Pauza")},
                             {136, 83, local("Z← C→ Seek", "Z← C→ Posun")},
                             {228, 53, local("Esc Back", "Esc Zpět")}});
        } else {
            _keys->setItems({});
        }
    }

    void showFailure()
    {
        _failed = true;
        _finished = false;
        ui::hidden(_image, true);
        const auto* title = local("Cannot play video", "Video nelze přehrát");
        const int width = textWidth(title, uiFont14());
        const int x = (320 - width - 14) / 2;
        lv_obj_set_pos(_status, x + 14, 65);
        lv_obj_set_width(_status, width + 1);
        lv_label_set_text(_status, title);
        ui::errorCross(_root, x, 69);
        lv_label_set_text(_detail, local("Decoding failed.", "Chyba dekódování."));
        ui::hidden(_detail, false);
        lv_obj_set_style_text_color(_status, lv_color_hex(ui::error), 0);
        ui::hidden(_status, false);
        refreshKeys();
    }

#if defined(__unix__) || defined(__APPLE__)
    pid_t _player_pid   = -1;
    bool _audio_checked = false;
    bool _has_audio     = false;
    std::string _pulse_sink;
#endif

    void startPlayer()
    {
        if (_started) {
            return;
        }
        _started = true;

#if defined(__unix__) || defined(__APPLE__)
        launchPlayer();
#else
        spdlog::error("VideoPreview: ffmpeg preview is unsupported on this platform");
        showFailure();
#endif
    }

    void launchPlayer(bool still = false)
    {
#if defined(__unix__) || defined(__APPLE__)
        if (!_audio_checked) {
            _has_audio     = fileHasAudioStream(_file.path);
            _pulse_sink    = videoPulseSink();
            _audio_checked = true;
        }

        int frames[2];
        if (::pipe(frames) != 0) {
            showFailure();
            return;
        }
#ifdef F_SETPIPE_SZ
        // A full frame in the pipe lets the UI keep pace without waiting on decode.
        (void)::fcntl(frames[0], F_SETPIPE_SZ, static_cast<int>(_buffer.size()));
#endif
        const int flags = ::fcntl(frames[0], F_GETFL);
        if (flags < 0 || ::fcntl(frames[0], F_SETFL, flags | O_NONBLOCK) < 0) {
            ::close(frames[0]);
            ::close(frames[1]);
            showFailure();
            return;
        }
        _player_pid = ::fork();
        if (_player_pid < 0) {
            spdlog::error("VideoPreview: failed to fork ffmpeg for {}: {}", _file.path, std::strerror(errno));
            ::close(frames[0]);
            ::close(frames[1]);
            showFailure();
            return;
        }

        if (_player_pid == 0) {
            ::close(frames[0]);
            if (::dup2(frames[1], STDOUT_FILENO) < 0) _exit(127);
            ::close(frames[1]);
            execFfmpeg(_file.path, _seek_offset_ms, _has_audio, _pulse_sink, still);
            _exit(127);
        }

        ::close(frames[1]);
        _pipe = frames[0];
        _paused = still;
        _still_pending = still;
        _segment_frames = 0;
        spdlog::info("VideoPreview: started ffmpeg pid={} audio={} pulse={} offsetMs={} path={}",
                     static_cast<int>(_player_pid), _has_audio, _has_audio ? _pulse_sink : "none", _seek_offset_ms,
                     _file.path);
#endif
    }

    void stopPlayer()
    {
        closePipe();
#if defined(__unix__) || defined(__APPLE__)
        if (_player_pid <= 0) {
            return;
        }

        int status = 0;
        if (::waitpid(_player_pid, &status, WNOHANG) == 0) {
            if (_paused) {
                ::kill(_player_pid, SIGCONT);
            }
            ::kill(_player_pid, SIGTERM);
            for (int attempt = 0; attempt < 10; ++attempt) {
                if (::waitpid(_player_pid, &status, WNOHANG) == _player_pid) {
                    break;
                }
                ::usleep(20000);
            }
            if (::waitpid(_player_pid, &status, WNOHANG) == 0) {
                ::kill(_player_pid, SIGKILL);
                (void)::waitpid(_player_pid, &status, 0);
            }
        }
        _player_pid = -1;
        _paused     = false;
#endif
    }

    void pollPlayer()
    {
#if defined(__unix__) || defined(__APPLE__)
        if (_player_pid <= 0) {
            return;
        }

        int status         = 0;
        const pid_t result = ::waitpid(_player_pid, &status, WNOHANG);
        if (result == _player_pid) {
            spdlog::info("VideoPreview: ffmpeg exited path={}", _file.path);
            _player_pid = -1;
            closePipe();
            if (_paused) {
                // An out-of-range seek emits no frame. Leave the last displayed
                // image and its timestamp available for seeking back or resuming.
                _still_pending = false;
                _seek_offset_ms = _displayed_ms;
                if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) showFailure();
            } else if (WIFEXITED(status) && WEXITSTATUS(status) == 0) _finished = true;
            else showFailure();
        } else if (result < 0 && errno != EINTR) {
            spdlog::warn("VideoPreview: waitpid failed for ffmpeg: {}", std::strerror(errno));
            _player_pid = -1;
            closePipe();
            showFailure();
        }
#endif
    }

    void togglePause()
    {
#if defined(__unix__) || defined(__APPLE__)
        if (_finished || _failed) {
            return;
        }

        if (_paused) {
            _paused = false;
            launchPlayer();
            spdlog::info("VideoPreview: resumed ffmpeg offsetMs={}", _seek_offset_ms);
            return;
        }

        if (_player_pid <= 0) {
            return;
        }
        _seek_offset_ms = _displayed_ms;
        stopPlayer();
        _paused = true;
        spdlog::info("VideoPreview: paused ffmpeg offsetMs={}", _seek_offset_ms);
#endif
    }

    void seekRelative(int32_t delta_ms)
    {
#if defined(__unix__) || defined(__APPLE__)
        if (_finished || _failed) {
            return;
        }

        const uint64_t current_ms = _displayed_ms;
        if (delta_ms < 0 && current_ms < static_cast<uint64_t>(-delta_ms)) {
            _seek_offset_ms = 0;
        } else {
            _seek_offset_ms = static_cast<uint64_t>(static_cast<int64_t>(current_ms) + delta_ms);
        }

        const bool paused = _paused;
        stopPlayer();
        launchPlayer(paused);
#endif
    }
};

class VideoPreviewSupport : public PreviewSupport {
public:
    const char* id() const override
    {
        return "video";
    }

    bool supports(const FileEntry& file) const override
    {
        if (!isRegularPreviewFile(file)) {
            return false;
        }
        return file.kind == FileKind::Video || extensionUsuallyVideo(file.extension);
    }

    std::unique_ptr<PreviewPage> open(const FileEntry& file) const override
    {
        if (!isRegularPreviewFile(file)) {
            return nullptr;
        }
        return std::make_unique<VideoPreviewPage>(file);
    }
};

}  // namespace

std::unique_ptr<PreviewSupport> createVideoPreviewSupport()
{
    return std::make_unique<VideoPreviewSupport>();
}

}  // namespace files
