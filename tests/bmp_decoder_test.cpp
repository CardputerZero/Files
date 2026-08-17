#include "preview/image/bmp_decoder.hpp"

#include <lvgl.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void putU16(std::vector<uint8_t>& bytes, size_t offset, uint16_t value)
{
    bytes[offset]     = static_cast<uint8_t>(value & 0xFFU);
    bytes[offset + 1] = static_cast<uint8_t>(value >> 8U);
}

void putU32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value)
{
    bytes[offset]     = static_cast<uint8_t>(value & 0xFFU);
    bytes[offset + 1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
    bytes[offset + 2] = static_cast<uint8_t>((value >> 16U) & 0xFFU);
    bytes[offset + 3] = static_cast<uint8_t>(value >> 24U);
}

void putI32(std::vector<uint8_t>& bytes, size_t offset, int32_t value)
{
    putU32(bytes, offset, static_cast<uint32_t>(value));
}

void writeBmp(const std::filesystem::path& path, int32_t width, int32_t height, uint16_t bits, uint32_t compression,
              const std::vector<std::vector<std::array<uint8_t, 4>>>& rows, bool top_down)
{
    const size_t bytes_per_pixel = bits / 8U;
    const size_t row_size        = ((static_cast<size_t>(width) * bits + 31U) / 32U) * 4U;
    const uint32_t data_offset   = compression == 3 ? 66 : 54;
    const uint32_t image_size    = static_cast<uint32_t>(row_size * static_cast<size_t>(height));
    std::vector<uint8_t> bytes(data_offset + image_size, 0);
    putU16(bytes, 0, 0x4D42);
    putU32(bytes, 2, static_cast<uint32_t>(bytes.size()));
    putU32(bytes, 10, data_offset);
    putU32(bytes, 14, 40);
    putI32(bytes, 18, width);
    putI32(bytes, 22, top_down ? -height : height);
    putU16(bytes, 26, 1);
    putU16(bytes, 28, bits);
    putU32(bytes, 30, compression);
    putU32(bytes, 34, image_size);
    if (compression == 3) {
        putU32(bytes, 54, 0x00FF0000U);
        putU32(bytes, 58, 0x0000FF00U);
        putU32(bytes, 62, 0x000000FFU);
    }

    for (int32_t y = 0; y < height; ++y) {
        const int32_t source_y = top_down ? y : height - y - 1;
        auto* destination      = bytes.data() + data_offset + row_size * static_cast<size_t>(y);
        for (int32_t x = 0; x < width; ++x) {
            const auto pixel = rows[static_cast<size_t>(source_y)][static_cast<size_t>(x)];
            if (bits == 24) {
                destination[static_cast<size_t>(x) * bytes_per_pixel + 0] = pixel[2];
                destination[static_cast<size_t>(x) * bytes_per_pixel + 1] = pixel[1];
                destination[static_cast<size_t>(x) * bytes_per_pixel + 2] = pixel[0];
            } else if (bits == 16) {
                const uint16_t packed = static_cast<uint16_t>((pixel[0] >> 3U) << 10U) |
                                        static_cast<uint16_t>((pixel[1] >> 3U) << 5U) |
                                        static_cast<uint16_t>(pixel[2] >> 3U);
                destination[static_cast<size_t>(x) * bytes_per_pixel + 0] = static_cast<uint8_t>(packed);
                destination[static_cast<size_t>(x) * bytes_per_pixel + 1] = static_cast<uint8_t>(packed >> 8U);
            } else if (bits == 32) {
                destination[static_cast<size_t>(x) * bytes_per_pixel + 0] = pixel[2];
                destination[static_cast<size_t>(x) * bytes_per_pixel + 1] = pixel[1];
                destination[static_cast<size_t>(x) * bytes_per_pixel + 2] = pixel[0];
                destination[static_cast<size_t>(x) * bytes_per_pixel + 3] = pixel[3];
            }
        }
    }

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    require(static_cast<bool>(output), "failed to create BMP fixture");
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

std::array<uint8_t, 3> pixelAt(const files::DrawBufferPtr& image, uint32_t x, uint32_t y)
{
    const auto* row = static_cast<const uint8_t*>(image->data) + static_cast<size_t>(y) * image->header.stride;
    const auto* rgb = row + static_cast<size_t>(x) * 3U;
    // LVGL RGB888 buffers are stored as B, G, R; expose logical RGB to tests.
    return {rgb[2], rgb[1], rgb[0]};
}

void verifyPixel(const files::DrawBufferPtr& image, uint32_t x, uint32_t y, std::array<uint8_t, 3> expected)
{
    require(pixelAt(image, x, y) == expected, "decoded BMP pixel does not match expected color");
}

}  // namespace

int main()
{
    lv_init();
    const auto root = std::filesystem::temp_directory_path() / "files-bmp-decoder-test";
    std::filesystem::create_directories(root);
    const auto bottom_up = root / "bottom-up.bmp";
    const auto top_down  = root / "top-down.bmp";
    const auto rgb555    = root / "rgb555.bmp";
    const auto bitfields = root / "bitfields.bmp";

    try {
        const std::vector<std::vector<std::array<uint8_t, 4>>> colors = {
            {{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255}},
            {{255, 255, 255, 255}, {0, 0, 0, 255}, {255, 255, 0, 255}},
        };
        writeBmp(bottom_up, 3, 2, 24, 0, colors, false);
        auto image = files::decodeBmpFile(bottom_up.string());
        require(static_cast<bool>(image), "failed to decode bottom-up BMP");
        require(image->header.w == 3 && image->header.h == 2, "bottom-up BMP dimensions are wrong");
        verifyPixel(image, 0, 0, {255, 0, 0});
        verifyPixel(image, 2, 1, {255, 255, 0});

        writeBmp(top_down, 3, 2, 32, 0, colors, true);
        image = files::decodeBmpFile(top_down.string());
        require(static_cast<bool>(image), "failed to decode top-down BMP");
        verifyPixel(image, 0, 0, {255, 0, 0});
        verifyPixel(image, 1, 1, {0, 0, 0});

        writeBmp(bitfields, 3, 2, 32, 3, colors, true);
        image = files::decodeBmpFile(bitfields.string());
        require(static_cast<bool>(image), "failed to decode external-mask BMP");
        verifyPixel(image, 0, 0, {255, 0, 0});
        verifyPixel(image, 2, 1, {255, 255, 0});

        writeBmp(rgb555, 3, 2, 16, 0, colors, false);
        image = files::decodeBmpFile(rgb555.string());
        require(static_cast<bool>(image), "failed to decode RGB555 BMP");
        verifyPixel(image, 0, 0, {255, 0, 0});
        verifyPixel(image, 2, 1, {255, 255, 0});

        std::ofstream invalid(root / "invalid.bmp", std::ios::binary | std::ios::trunc);
        invalid << "not a BMP";
        invalid.close();
        require(!files::decodeBmpFile((root / "invalid.bmp").string()), "invalid BMP unexpectedly decoded");
    } catch (const std::exception& error) {
        std::cerr << "bmp_decoder_test: " << error.what() << '\n';
        std::filesystem::remove_all(root);
        lv_deinit();
        return 1;
    }

    std::filesystem::remove_all(root);
    lv_deinit();
    return 0;
}
