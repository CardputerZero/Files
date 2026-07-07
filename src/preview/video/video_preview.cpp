#include "preview/video/video_preview.hpp"

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

constexpr const char* kVideoFilter =
    "scale=320:170:force_original_aspect_ratio=decrease,"
    "pad=320:170:(ow-iw)/2:(oh-ih)/2:black,"
    "setsar=1,fps=25,setpts=N/(25*TB),format=rgb565le";
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

void execFfmpeg(const std::string& path, uint64_t offset_ms, bool has_audio, const std::string& pulse_sink)
{
    std::vector<std::string> args = {
        "ffmpeg", "-hide_banner", "-loglevel", "warning", "-nostdin", "-re", "-fflags", "+genpts",
    };
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
                                "-f",
                                "fbdev",
                                "/dev/fb0",
                            });
    if (has_audio) {
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

    const std::string& title() const override
    {
        return _title;
    }

    void attach(lv_obj_t* parent) override
    {
        (void)parent;
        startPlayer();
    }

    void detach() override
    {
        stopPlayer();
    }

    void onKey(uint32_t key, FilesRouter& router) override
    {
        if (key == '\x1b') {
            stopPlayer();
            router.back();
            return;
        }
        if (key == ' ') {
            togglePause();
            return;
        }
        if (key == files_key::Left) {
            seekRelative(-static_cast<int32_t>(kSeekStepMs));
            return;
        }
        if (key == files_key::Right) {
            seekRelative(static_cast<int32_t>(kSeekStepMs));
        }
    }

    void tick(uint32_t nowMs) override
    {
        (void)nowMs;
        pollPlayer();
    }

    bool shouldClose() const override
    {
        return _finished;
    }

    bool suspendsHostRendering() const override
    {
        return _started && !_finished;
    }

private:
    FileEntry _file;
    std::string _title;
    bool _started  = false;
    bool _finished = false;
    bool _paused   = false;

#if defined(__unix__) || defined(__APPLE__)
    pid_t _player_pid   = -1;
    bool _audio_checked = false;
    bool _has_audio     = false;
    std::string _pulse_sink;
    uint64_t _seek_offset_ms        = 0;
    uint32_t _segment_started_at_ms = 0;
    uint32_t _paused_at_ms          = 0;
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
        _finished = true;
#endif
    }

    void launchPlayer()
    {
#if defined(__unix__) || defined(__APPLE__)
        if (!_audio_checked) {
            _has_audio     = fileHasAudioStream(_file.path);
            _pulse_sink    = videoPulseSink();
            _audio_checked = true;
        }

        _player_pid = ::fork();
        if (_player_pid < 0) {
            spdlog::error("VideoPreview: failed to fork ffmpeg for {}: {}", _file.path, std::strerror(errno));
            _finished = true;
            return;
        }

        if (_player_pid == 0) {
            execFfmpeg(_file.path, _seek_offset_ms, _has_audio, _pulse_sink);
            _exit(127);
        }

        _paused                = false;
        _segment_started_at_ms = lv_tick_get();
        spdlog::info("VideoPreview: started ffmpeg pid={} audio={} pulse={} offsetMs={} path={}",
                     static_cast<int>(_player_pid), _has_audio, _has_audio ? _pulse_sink : "none", _seek_offset_ms,
                     _file.path);
#endif
    }

    void stopPlayer()
    {
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
            _finished   = true;
        } else if (result < 0 && errno != EINTR) {
            spdlog::warn("VideoPreview: waitpid failed for ffmpeg: {}", std::strerror(errno));
            _player_pid = -1;
            _finished   = true;
        }
#endif
    }

    uint64_t currentPlaybackMs(uint32_t now_ms) const
    {
#if defined(__unix__) || defined(__APPLE__)
        if (_paused && _player_pid <= 0) {
            return _seek_offset_ms;
        }
        const uint32_t effective_now = _paused ? _paused_at_ms : now_ms;
        return _seek_offset_ms + static_cast<uint32_t>(effective_now - _segment_started_at_ms);
#else
        (void)now_ms;
        return 0;
#endif
    }

    void togglePause()
    {
#if defined(__unix__) || defined(__APPLE__)
        if (_finished) {
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
        _seek_offset_ms = currentPlaybackMs(lv_tick_get());
        stopPlayer();
        _paused = true;
        spdlog::info("VideoPreview: paused ffmpeg offsetMs={}", _seek_offset_ms);
#endif
    }

    void seekRelative(int32_t delta_ms)
    {
#if defined(__unix__) || defined(__APPLE__)
        if (_finished) {
            return;
        }

        const uint64_t current_ms = currentPlaybackMs(lv_tick_get());
        if (delta_ms < 0 && current_ms < static_cast<uint64_t>(-delta_ms)) {
            _seek_offset_ms = 0;
        } else {
            _seek_offset_ms = static_cast<uint64_t>(static_cast<int64_t>(current_ms) + delta_ms);
        }

        if (_paused) {
            spdlog::info("VideoPreview: seek paused ffmpeg offsetMs={}", _seek_offset_ms);
            return;
        }

        stopPlayer();
        launchPlayer();
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
        if (file.directory) {
            return false;
        }
        return file.kind == FileKind::Video || extensionUsuallyVideo(file.extension);
    }

    std::unique_ptr<PreviewPage> open(const FileEntry& file) const override
    {
        return std::make_unique<VideoPreviewPage>(file);
    }
};

}  // namespace

std::unique_ptr<PreviewSupport> createVideoPreviewSupport()
{
    return std::make_unique<VideoPreviewSupport>();
}

}  // namespace files
