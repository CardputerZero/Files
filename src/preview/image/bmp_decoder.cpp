#include "preview/image/bmp_decoder.hpp"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace files {
namespace {

constexpr uint32_t kBitmapSignature      = 0x4D42;
constexpr uint32_t kBitmapInfoHeaderMin  = 40;
constexpr uint32_t kCompressionRgb       = 0;
constexpr uint32_t kCompressionBitfields = 3;
constexpr uint64_t kMaxDecodedBytes      = 64ULL * 1024ULL * 1024ULL;

struct FileCloser {
    void operator()(std::FILE* file) const noexcept
    {
        if (file != nullptr) {
            std::fclose(file);
        }
    }
};

using FilePtr = std::unique_ptr<std::FILE, FileCloser>;

uint16_t readU16(const uint8_t* data)
{
    return static_cast<uint16_t>(data[0]) | static_cast<uint16_t>(data[1]) << 8U;
}

uint32_t readU32(const uint8_t* data)
{
    return static_cast<uint32_t>(data[0]) | static_cast<uint32_t>(data[1]) << 8U |
           static_cast<uint32_t>(data[2]) << 16U | static_cast<uint32_t>(data[3]) << 24U;
}

int32_t readI32(const uint8_t* data)
{
    return static_cast<int32_t>(readU32(data));
}

bool readExact(std::FILE* file, void* destination, size_t size)
{
    return size == 0 || std::fread(destination, 1, size, file) == size;
}

bool seekAbsolute(std::FILE* file, uint64_t offset)
{
    if (offset > static_cast<uint64_t>(std::numeric_limits<long>::max())) {
        return false;
    }
    return std::fseek(file, static_cast<long>(offset), SEEK_SET) == 0;
}

struct ChannelMask {
    uint32_t mask = 0;
    uint8_t shift = 0;
    uint8_t bits  = 0;
};

ChannelMask makeChannelMask(uint32_t mask)
{
    ChannelMask channel{mask, 0, 0};
    if (mask == 0) {
        return channel;
    }
    while ((mask & 1U) == 0U) {
        mask >>= 1U;
        ++channel.shift;
    }
    while ((mask & 1U) != 0U) {
        mask >>= 1U;
        ++channel.bits;
    }
    return channel;
}

uint8_t extractChannel(uint32_t pixel, const ChannelMask& channel, uint8_t fallback)
{
    if (channel.mask == 0 || channel.bits == 0) {
        return fallback;
    }
    const uint32_t value = (pixel & channel.mask) >> channel.shift;
    const uint64_t max   = channel.bits == 32U ? UINT32_MAX : (1ULL << channel.bits) - 1ULL;
    return static_cast<uint8_t>((static_cast<uint64_t>(value) * 255U + max / 2U) / max);
}

}  // namespace

DrawBufferPtr decodeBmpFile(const std::string& path)
{
    FilePtr file(std::fopen(path.c_str(), "rb"));
    if (!file) {
        spdlog::warn("ImagePreview: cannot open BMP path='{}': {}", path, std::strerror(errno));
        return {};
    }

    std::array<uint8_t, 14> file_header{};
    std::array<uint8_t, 4> info_size_bytes{};
    if (!readExact(file.get(), file_header.data(), file_header.size()) ||
        readU16(file_header.data()) != kBitmapSignature ||
        !readExact(file.get(), info_size_bytes.data(), info_size_bytes.size())) {
        spdlog::warn("ImagePreview: invalid BMP file header path='{}'", path);
        return {};
    }

    const uint32_t info_size = readU32(info_size_bytes.data());
    if (info_size < kBitmapInfoHeaderMin || info_size > 1024U) {
        spdlog::warn("ImagePreview: unsupported BMP info header path='{}' size={}", path, info_size);
        return {};
    }

    std::vector<uint8_t> info_header(info_size);
    std::memcpy(info_header.data(), info_size_bytes.data(), info_size_bytes.size());
    if (!readExact(file.get(), info_header.data() + info_size_bytes.size(),
                   info_header.size() - info_size_bytes.size())) {
        spdlog::warn("ImagePreview: truncated BMP info header path='{}'", path);
        return {};
    }

    const int32_t width        = readI32(info_header.data() + 4);
    const int32_t signedHeight = readI32(info_header.data() + 8);
    const uint16_t planes      = readU16(info_header.data() + 12);
    const uint16_t bits        = readU16(info_header.data() + 14);
    const uint32_t compression = readU32(info_header.data() + 16);
    const uint32_t data_offset = readU32(file_header.data() + 10);

    if (width <= 0 || signedHeight == 0 || signedHeight == std::numeric_limits<int32_t>::min() || planes != 1 ||
        (bits != 16 && bits != 24 && bits != 32) ||
        (compression != kCompressionRgb && compression != kCompressionBitfields)) {
        spdlog::warn(
            "ImagePreview: unsupported BMP format path='{}' width={} height={} planes={} bpp={} compression={}", path,
            width, signedHeight, planes, bits, compression);
        return {};
    }
    if (compression == kCompressionBitfields && bits != 16 && bits != 32) {
        spdlog::warn("ImagePreview: unsupported BMP bitfield depth path='{}' bpp={}", path, bits);
        return {};
    }

    const uint32_t height =
        static_cast<uint32_t>(signedHeight < 0 ? -static_cast<int64_t>(signedHeight) : signedHeight);
    const bool top_down           = signedHeight < 0;
    const uint64_t row_bits       = static_cast<uint64_t>(static_cast<uint32_t>(width)) * bits;
    const uint64_t row_size       = ((row_bits + 31U) / 32U) * 4U;
    const uint64_t decoded_stride = static_cast<uint64_t>(static_cast<uint32_t>(width)) * 3U;
    if (decoded_stride != 0 && height > std::numeric_limits<uint64_t>::max() / decoded_stride) {
        spdlog::warn("ImagePreview: BMP decoded size overflow path='{}' width={} height={}", path, width, height);
        return {};
    }
    const uint64_t decoded_size = decoded_stride * height;
    if (row_size > std::numeric_limits<size_t>::max() || decoded_stride > std::numeric_limits<uint32_t>::max() ||
        decoded_size > std::numeric_limits<uint32_t>::max() || decoded_size > kMaxDecodedBytes ||
        data_offset < 14U + info_size) {
        spdlog::warn("ImagePreview: unsupported BMP dimensions path='{}' width={} height={} bytes={}", path, width,
                     height, decoded_size);
        return {};
    }

    // BI_RGB 16-bit bitmaps are RGB555. Explicit BI_BITFIELDS files below
    // provide their own masks (RGB565 is common for screenshots).
    ChannelMask red{0x7C00U, 10, 5};
    ChannelMask green{0x03E0U, 5, 5};
    ChannelMask blue{0x001FU, 0, 5};
    if (compression == kCompressionBitfields) {
        if (info_size < 52U) {
            std::array<uint8_t, 12> masks{};
            if (!readExact(file.get(), masks.data(), masks.size())) {
                spdlog::warn("ImagePreview: truncated BMP color masks path='{}'", path);
                return {};
            }
            red   = makeChannelMask(readU32(masks.data()));
            green = makeChannelMask(readU32(masks.data() + 4));
            blue  = makeChannelMask(readU32(masks.data() + 8));
        } else {
            constexpr size_t mask_offset = 40U;
            red                          = makeChannelMask(readU32(info_header.data() + mask_offset));
            green                        = makeChannelMask(readU32(info_header.data() + mask_offset + 4));
            blue                         = makeChannelMask(readU32(info_header.data() + mask_offset + 8));
        }
    }

    const uint32_t stride = static_cast<uint32_t>(decoded_stride);
    DrawBufferPtr output(lv_draw_buf_create(static_cast<uint32_t>(width), height, LV_COLOR_FORMAT_RGB888, stride));
    if (!output) {
        spdlog::warn("ImagePreview: cannot allocate BMP buffer path='{}' width={} height={} bytes={}", path, width,
                     height, decoded_size);
        return {};
    }

    std::vector<uint8_t> row(static_cast<size_t>(row_size));
    for (uint32_t y = 0; y < height; ++y) {
        const uint32_t source_y = top_down ? y : height - y - 1U;
        const uint64_t offset   = static_cast<uint64_t>(data_offset) + row_size * source_y;
        if (!seekAbsolute(file.get(), offset) || !readExact(file.get(), row.data(), row.size())) {
            spdlog::warn("ImagePreview: truncated BMP pixel data path='{}' row={}", path, y);
            return {};
        }

        auto* destination = static_cast<uint8_t*>(lv_draw_buf_goto_xy(output.get(), 0, y));
        if (destination == nullptr) {
            return {};
        }
        for (int32_t x = 0; x < width; ++x) {
            uint8_t red_value   = 0;
            uint8_t green_value = 0;
            uint8_t blue_value  = 0;
            if (bits == 24) {
                const auto* pixel = row.data() + static_cast<size_t>(x) * 3U;
                blue_value        = pixel[0];
                green_value       = pixel[1];
                red_value         = pixel[2];
            } else if (bits == 32 && compression == kCompressionRgb) {
                const auto* pixel = row.data() + static_cast<size_t>(x) * 4U;
                blue_value        = pixel[0];
                green_value       = pixel[1];
                red_value         = pixel[2];
            } else {
                const size_t bytes_per_pixel = bits / 8U;
                const auto* pixel            = row.data() + static_cast<size_t>(x) * bytes_per_pixel;
                uint32_t packed              = pixel[0] | static_cast<uint32_t>(pixel[1]) << 8U;
                if (bytes_per_pixel == 4U) {
                    packed |= static_cast<uint32_t>(pixel[2]) << 16U | static_cast<uint32_t>(pixel[3]) << 24U;
                }
                red_value   = extractChannel(packed, red, 0);
                green_value = extractChannel(packed, green, 0);
                blue_value  = extractChannel(packed, blue, 0);
            }
            auto* out_pixel = destination + static_cast<size_t>(x) * 3U;
            // LVGL's RGB888 draw-buffer format stores channels as B, G, R
            // in memory (the same layout produced by TJpgDec).
            out_pixel[0]    = blue_value;
            out_pixel[1]    = green_value;
            out_pixel[2]    = red_value;
        }
    }

    lv_draw_buf_flush_cache(output.get(), nullptr);
    spdlog::info("ImagePreview: decoded BMP path='{}' width={} height={} bpp={} topDown={}", path, width, height, bits,
                 top_down);
    return output;
}

}  // namespace files
