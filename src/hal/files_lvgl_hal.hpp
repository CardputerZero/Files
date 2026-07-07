#pragma once

#include <lvgl.h>
#include <cstdint>

namespace files {

bool initLvglHal(int32_t width, int32_t height);
void shutdownLvglHal();

}  // namespace files
