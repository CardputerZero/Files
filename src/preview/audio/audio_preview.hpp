#pragma once

#include "preview/preview_support.hpp"
#include <memory>

namespace files {

std::unique_ptr<PreviewSupport> createAudioPreviewSupport();

}  // namespace files
