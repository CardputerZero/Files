#include "preview/preview_support.hpp"

#include "preview/audio/audio_preview.hpp"
#include "preview/image/image_preview.hpp"
#include "preview/info/info_preview.hpp"
#include "preview/text/text_preview.hpp"
#include "preview/video/video_preview.hpp"
#include <cstring>
#include <utility>

namespace files {

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
