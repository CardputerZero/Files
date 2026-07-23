#include "preview/image/jpeg_decoder.hpp"

#include <lvgl.h>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void verifyImage(const std::string& path, uint32_t expected_width = 0, uint32_t expected_height = 0)
{
    auto image = files::decodeJpegFile(path);
    require(static_cast<bool>(image), "failed to decode " + path);
    require(image->header.cf == LV_COLOR_FORMAT_RGB888, "unexpected color format for " + path);
    require(image->header.w > 0 && image->header.h > 0, "empty dimensions for " + path);
    require(image->header.stride >= image->header.w * 3, "invalid stride for " + path);
    require(image->data != nullptr, "missing pixel buffer for " + path);

    if (expected_width != 0) {
        require(image->header.w == expected_width, "unexpected width for " + path);
    }
    if (expected_height != 0) {
        require(image->header.h == expected_height, "unexpected height for " + path);
    }

    const auto* pixels          = static_cast<const uint8_t*>(image->data);
    bool contains_nonzero_pixel = false;
    for (uint32_t index = 0; index < image->data_size; ++index) {
        if (pixels[index] != 0) {
            contains_nonzero_pixel = true;
            break;
        }
    }
    require(contains_nonzero_pixel, "decoded image is entirely black for " + path);
}

}  // namespace

int main(int argc, char** argv)
{
    lv_init();
    try {
        verifyImage(FILES_TEST_JPEG_PATH, 105, 40);
        std::remove(FILES_TEST_MISSING_JPEG_PATH);
        require(!files::decodeJpegFile(FILES_TEST_MISSING_JPEG_PATH), "missing JPEG unexpectedly decoded");

        {
            std::ofstream invalid_image(FILES_TEST_INVALID_JPEG_PATH, std::ios::binary | std::ios::trunc);
            require(static_cast<bool>(invalid_image), "failed to create invalid JPEG fixture");
            invalid_image << "not a JPEG";
        }
        require(!files::decodeJpegFile(FILES_TEST_INVALID_JPEG_PATH), "invalid JPEG unexpectedly decoded");

        for (int index = 1; index < argc; ++index) {
            verifyImage(argv[index]);
        }
    } catch (const std::exception& error) {
        std::cerr << "jpeg_decoder_test: " << error.what() << '\n';
        lv_deinit();
        return 1;
    }

    lv_deinit();
    return 0;
}
