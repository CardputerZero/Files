#include "preview/image/draw_buffer.hpp"

namespace files {

void DrawBufferDeleter::operator()(lv_draw_buf_t* buffer) const noexcept
{
    if (buffer != nullptr) {
        lv_draw_buf_destroy(buffer);
    }
}

}  // namespace files
