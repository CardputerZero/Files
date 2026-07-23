#pragma once

#include <lvgl.h>
#include <memory>
#include <string>

namespace files {

struct DrawBufferDeleter {
    void operator()(lv_draw_buf_t* buffer) const noexcept;
};

using DrawBufferPtr = std::unique_ptr<lv_draw_buf_t, DrawBufferDeleter>;

DrawBufferPtr decodeJpegFile(const std::string& path);

}  // namespace files
