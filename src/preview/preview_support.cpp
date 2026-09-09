#include "preview/preview_support.hpp"

#include "preview/audio/audio_preview.hpp"
#include "preview/image/image_preview.hpp"
#include "preview/info/info_preview.hpp"
#include "preview/text/text_preview.hpp"
#include "preview/video/video_preview.hpp"
#include <cstring>
#include <filesystem>
#include <system_error>
#include <utility>

#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
#endif

namespace files {

bool isRegularPreviewFile(const FileEntry& file)
{
    if (file.directory || file.path.empty()) {
        return false;
    }

    std::error_code error;
    // Inspect the directory entry without following the link first.  This
    // keeps dangling links and direct device/FIFO entries out of the decoder
    // path before any open() is attempted.
    const std::filesystem::file_status link_status = std::filesystem::symlink_status(file.path, error);
    if (error || link_status.type() == std::filesystem::file_type::not_found) {
        return false;
    }

    error.clear();
    const std::filesystem::file_status target_status = std::filesystem::status(file.path, error);
    if (error || !std::filesystem::is_regular_file(target_status)) {
        return false;
    }

#if defined(__unix__) || defined(__APPLE__)
    // Close the check-to-open race.  O_NONBLOCK is important here: if a path
    // is replaced with a FIFO or device between the status calls and open(),
    // this probe must return immediately rather than hang the UI thread.
    const int fd = ::open(file.path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }

    struct stat stat_buffer {};
    const bool regular = ::fstat(fd, &stat_buffer) == 0 && S_ISREG(stat_buffer.st_mode);
    ::close(fd);
    return regular;
#else
    (void)link_status;
    return true;
#endif
}

PreviewRegistry::PreviewRegistry()
{
    add(createVideoPreviewSupport());
    add(createImagePreviewSupport());
    add(createAudioPreviewSupport());
    add(createTextPreviewSupport());
    add(createInfoPreviewSupport());
}

void PreviewRegistry::add(std::unique_ptr<PreviewSupport> support)
{
    if (support) {
        _supports.push_back(std::move(support));
    }
}

std::unique_ptr<PreviewPage> PreviewRegistry::open(const FileEntry& file) const
{
    // The generic preview path must never fall through to an Info page for a
    // device, FIFO, socket, or other non-regular entry.  Info remains
    // available through openWithSupport() for explicit metadata requests.
    if (!isRegularPreviewFile(file)) {
        return nullptr;
    }

    for (const auto& support : _supports) {
        if (support && support->supports(file)) {
            return support->open(file);
        }
    }
    return nullptr;
}

std::unique_ptr<PreviewPage> PreviewRegistry::openWithSupport(const FileEntry& file, const char* supportId) const
{
    if (!supportId) {
        return nullptr;
    }
    for (const auto& support : _supports) {
        if (support && std::strcmp(support->id(), supportId) == 0 && support->supports(file)) {
            return support->open(file);
        }
    }
    return nullptr;
}

}  // namespace files
