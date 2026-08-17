#pragma once

#include "preview/image/draw_buffer.hpp"

namespace files {

DrawBufferPtr decodeBmpFile(const std::string& path);

}  // namespace files
