#pragma once

#include "preview/preview_support.hpp"

namespace files {

std::unique_ptr<PreviewSupport> createVideoPreviewSupport();

}  // namespace files
