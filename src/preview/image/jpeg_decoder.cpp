#include "preview/image/jpeg_decoder.hpp"

#include <spdlog/spdlog.h>
#include <tjpgd.h>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

namespace files {
namespace {

constexpr size_t kWorkBufferSize    = 4096;
constexpr uint32_t kBytesPerPixel   = 3;
constexpr uint64_t kMaxDecodedBytes = 64ULL * 1024ULL * 1024ULL;

static_assert(JD_FORMAT == 0, "JPEG preview requires TJPGD RGB888 output");

struct FileCloser {
    void operator()(std::FILE* file) const noexcept
    {
        if (file != nullptr) {
            std::fclose(file);
        }
    }
};

using FilePtr = std::unique_ptr<std::FILE, FileCloser>;

struct DecodeContext {
    std::FILE* file              = nullptr;
    lv_draw_buf_t* output_buffer = nullptr;
    uint64_t written_pixels      = 0;
};

const char* resultName(JRESULT result)
{
    switch (result) {
        case JDR_OK:
            return "ok";
        case JDR_INTR:
            return "interrupted";
        case JDR_INP:
            return "input error";
        case JDR_MEM1:
            return "insufficient work memory";
        case JDR_MEM2:
            return "insufficient input buffer";
        case JDR_PAR:
            return "invalid parameter";
        case JDR_FMT1:
            return "invalid JPEG data";
        case JDR_FMT2:
            return "unsupported JPEG format";
        case JDR_FMT3:
            return "unsupported JPEG standard";
    }
    return "unknown error";
}

size_t readInput(JDEC* decoder, uint8_t* destination, size_t byte_count)
{
    auto* context = static_cast<DecodeContext*>(decoder->device);
    if (context == nullptr || context->file == nullptr) {
        return 0;
    }

    if (destination != nullptr) {
        return std::fread(destination, 1, byte_count, context->file);
    }

    if (byte_count > static_cast<size_t>(std::numeric_limits<long>::max())) {
        return 0;
    }
    return std::fseek(context->file, static_cast<long>(byte_count), SEEK_CUR) == 0 ? byte_count : 0;
}

int writeOutput(JDEC* decoder, void* pixels, JRECT* area)
{
    auto* context = static_cast<DecodeContext*>(decoder->device);
    if (context == nullptr || context->output_buffer == nullptr || pixels == nullptr || area == nullptr) {
        return 0;
    }

    if (area->right < area->left || area->bottom < area->top || area->right >= context->output_buffer->header.w ||
        area->bottom >= context->output_buffer->header.h) {
        return 0;
    }
    const uint32_t width  = static_cast<uint32_t>(area->right - area->left + 1);
    const uint32_t height = static_cast<uint32_t>(area->bottom - area->top + 1);

    const auto* source    = static_cast<const uint8_t*>(pixels);
    const size_t row_size = static_cast<size_t>(width) * kBytesPerPixel;
    for (uint32_t row = 0; row < height; ++row) {
        auto* destination = static_cast<uint8_t*>(lv_draw_buf_goto_xy(
            context->output_buffer, static_cast<uint32_t>(area->left), static_cast<uint32_t>(area->top) + row));
        if (destination == nullptr) {
            return 0;
        }
        std::memcpy(destination, source + row * row_size, row_size);
    }
    context->written_pixels += static_cast<uint64_t>(width) * height;
    return 1;
}

}  // namespace

void DrawBufferDeleter::operator()(lv_draw_buf_t* buffer) const noexcept
{
    if (buffer != nullptr) {
        lv_draw_buf_destroy(buffer);
    }
}

DrawBufferPtr decodeJpegFile(const std::string& path)
{
    FilePtr file(std::fopen(path.c_str(), "rb"));
    if (!file) {
        spdlog::warn("ImagePreview: cannot open JPEG path='{}': {}", path, std::strerror(errno));
        return {};
    }

    DecodeContext context{file.get(), nullptr, 0};
    std::array<uint8_t, kWorkBufferSize> work_buffer{};
    JDEC decoder{};
    const JRESULT prepare_result =
        jd_prepare(&decoder, readInput, work_buffer.data(), work_buffer.size(), static_cast<void*>(&context));
    if (prepare_result != JDR_OK) {
        spdlog::warn("ImagePreview: JPEG header decode failed path='{}' result={} ({})", path,
                     static_cast<int>(prepare_result), resultName(prepare_result));
        return {};
    }

    const uint32_t stride       = lv_draw_buf_width_to_stride(decoder.width, LV_COLOR_FORMAT_RGB888);
    const uint64_t decoded_size = static_cast<uint64_t>(stride) * decoder.height;
    if (decoder.width == 0 || decoder.height == 0 || stride > std::numeric_limits<uint16_t>::max() ||
        decoded_size > std::numeric_limits<uint32_t>::max() || decoded_size > kMaxDecodedBytes) {
        spdlog::warn("ImagePreview: unsupported JPEG dimensions path='{}' width={} height={} decodedBytes={}", path,
                     decoder.width, decoder.height, decoded_size);
        return {};
    }

    DrawBufferPtr output(lv_draw_buf_create(decoder.width, decoder.height, LV_COLOR_FORMAT_RGB888, stride));
    if (!output) {
        spdlog::warn("ImagePreview: cannot allocate JPEG buffer path='{}' width={} height={} decodedBytes={}", path,
                     decoder.width, decoder.height, decoded_size);
        return {};
    }

    context.output_buffer       = output.get();
    const JRESULT decode_result = jd_decomp(&decoder, writeOutput, 0);
    if (decode_result != JDR_OK) {
        spdlog::warn("ImagePreview: JPEG pixel decode failed path='{}' result={} ({}) width={} height={}", path,
                     static_cast<int>(decode_result), resultName(decode_result), decoder.width, decoder.height);
        return {};
    }

    const uint64_t expected_pixels = static_cast<uint64_t>(decoder.width) * decoder.height;
    if (context.written_pixels != expected_pixels) {
        spdlog::warn("ImagePreview: incomplete JPEG decode path='{}' expectedPixels={} writtenPixels={}", path,
                     expected_pixels, context.written_pixels);
        return {};
    }

    lv_draw_buf_flush_cache(output.get(), nullptr);

    spdlog::info("ImagePreview: decoded JPEG path='{}' width={} height={} bytes={}", path, decoder.width,
                 decoder.height, decoded_size);
    return output;
}

}  // namespace files
