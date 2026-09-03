#pragma once

#include "preview/preview_support.hpp"
#include <memory>
#include <string>

namespace files {

std::unique_ptr<PreviewSupport> createTextPreviewSupport();
std::unique_ptr<PreviewPage> createTextPreviewPage(std::string title, std::string content);

}  // namespace files
