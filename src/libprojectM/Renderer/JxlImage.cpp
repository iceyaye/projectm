#include "Renderer/JxlImage.hpp"

#include <Logging.hpp>

#include <jxl/decode.h>
#include <jxl/decode_cxx.h>
#include <jxl/encode.h> // For JxlColorEncodingSetToSRGB()
#include <jxl/resizable_parallel_runner.h>
#include <jxl/resizable_parallel_runner_cxx.h>

#include <fstream>
#include <new>

namespace libprojectM {
namespace Renderer {
namespace JxlImage {

namespace {

// Texture files are untrusted input, and a few bytes of JPEG XL can describe a huge image.
// Reject anything larger than these limits before allocating memory for it. Decoding needs
// several times the output size internally: a 4096x4096 lossless image peaks at about 500 MB.
constexpr uint32_t MaxDimension{16384};                 //!< Maximum width or height in pixels.
constexpr uint64_t MaxPixels{4096 * 4096};              //!< Maximum pixel count, 64 MiB as RGBA.
constexpr uint64_t MaxSamples{MaxPixels * 4};           //!< Maximum pixel count times channel count, including extra channels.
constexpr std::streamoff MaxFileSize{64 * 1024 * 1024}; //!< Maximum file size in bytes.

} // namespace

auto Load(const std::string& filePath, int& width, int& height) -> std::vector<uint8_t>
{
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file)
    {
        LOG_DEBUG("[JxlImage] Failed to open file: " + filePath);
        return {};
    }

    std::streamoff const fileSize = file.tellg();
    if (fileSize <= 0 || fileSize > MaxFileSize)
    {
        LOG_DEBUG("[JxlImage] File is empty or too large: " + filePath);
        return {};
    }

    // Fail like stbi_load() does if memory runs out, instead of throwing into the render loop.
    std::vector<uint8_t> fileData;
    try
    {
        fileData.resize(static_cast<size_t>(fileSize));
    }
    catch (const std::bad_alloc&)
    {
        LOG_DEBUG("[JxlImage] Not enough memory to read file: " + filePath);
        return {};
    }

    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(fileData.data()), fileSize))
    {
        LOG_DEBUG("[JxlImage] Failed to read file: " + filePath);
        return {};
    }

    return LoadFromMemory(fileData.data(), fileData.size(), width, height);
}

auto LoadFromMemory(const uint8_t* data, size_t size, int& width, int& height) -> std::vector<uint8_t>
{
    // The runner must outlive the decoder, so it is declared first.
    auto runner = JxlResizableParallelRunnerMake(nullptr);
    auto decoder = JxlDecoderMake(nullptr);
    if (!runner || !decoder)
    {
        LOG_DEBUG("[JxlImage] Failed to create JPEG XL decoder.");
        return {};
    }

    // Unpremultiply alpha, as the other texture formats use straight alpha.
    if (JxlDecoderSubscribeEvents(decoder.get(), JXL_DEC_BASIC_INFO | JXL_DEC_COLOR_ENCODING | JXL_DEC_FULL_IMAGE) != JXL_DEC_SUCCESS ||
        JxlDecoderSetParallelRunner(decoder.get(), JxlResizableParallelRunner, runner.get()) != JXL_DEC_SUCCESS ||
        JxlDecoderSetUnpremultiplyAlpha(decoder.get(), JXL_TRUE) != JXL_DEC_SUCCESS ||
        JxlDecoderSetInput(decoder.get(), data, size) != JXL_DEC_SUCCESS)
    {
        LOG_DEBUG("[JxlImage] Failed to set up JPEG XL decoder.");
        return {};
    }
    JxlDecoderCloseInput(decoder.get());

    // Always request RGBA. The decoder expands grayscale to RGB and adds opaque alpha if the image has none.
    JxlPixelFormat const format{4, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0};
    JxlBasicInfo info{};
    std::vector<uint8_t> pixels;

    for (;;)
    {
        switch (JxlDecoderProcessInput(decoder.get()))
        {
            case JXL_DEC_BASIC_INFO:
                if (JxlDecoderGetBasicInfo(decoder.get(), &info) != JXL_DEC_SUCCESS)
                {
                    LOG_DEBUG("[JxlImage] Failed to read JPEG XL image header.");
                    return {};
                }

                // Sizes are already swapped if the orientation requires it, as the decoder applies it.
                if (info.xsize == 0 || info.ysize == 0 ||
                    info.xsize > MaxDimension || info.ysize > MaxDimension ||
                    static_cast<uint64_t>(info.xsize) * info.ysize > MaxPixels)
                {
                    LOG_DEBUG("[JxlImage] Image size " + std::to_string(info.xsize) + "x" + std::to_string(info.ysize) + " exceeds the limit.");
                    return {};
                }

                // The decoder keeps a full-size plane for every channel. The format allows thousands of
                // extra channels, so count them too.
                if (static_cast<uint64_t>(info.xsize) * info.ysize * (static_cast<uint64_t>(info.num_color_channels) + info.num_extra_channels) > MaxSamples)
                {
                    LOG_DEBUG("[JxlImage] Image has too many channels for its size: " + std::to_string(info.num_extra_channels) + " extra channels.");
                    return {};
                }

                JxlResizableParallelRunnerSetThreads(runner.get(), JxlResizableParallelRunnerSuggestThreads(info.xsize, info.ysize));
                break;

            case JXL_DEC_COLOR_ENCODING:
                // Lossy images are stored as XYB, which the decoder can convert to any color space.
                // Request sRGB, which is what the other texture formats are assumed to be in.
                // Images stored in their original color space are returned as-is, like stb_image does.
                if (!info.uses_original_profile)
                {
                    JxlColorEncoding srgb{};
                    JxlColorEncodingSetToSRGB(&srgb, info.num_color_channels == 1 ? JXL_TRUE : JXL_FALSE);
                    if (JxlDecoderSetPreferredColorProfile(decoder.get(), &srgb) != JXL_DEC_SUCCESS)
                    {
                        LOG_DEBUG("[JxlImage] Failed to set sRGB output color profile.");
                        return {};
                    }
                }
                break;

            case JXL_DEC_NEED_IMAGE_OUT_BUFFER: {
                size_t bufferSize{};
                if (JxlDecoderImageOutBufferSize(decoder.get(), &format, &bufferSize) != JXL_DEC_SUCCESS ||
                    bufferSize != static_cast<size_t>(info.xsize) * info.ysize * 4)
                {
                    LOG_DEBUG("[JxlImage] Unexpected JPEG XL output buffer size.");
                    return {};
                }

                try
                {
                    pixels.resize(bufferSize);
                }
                catch (const std::bad_alloc&)
                {
                    LOG_DEBUG("[JxlImage] Not enough memory for the decoded image.");
                    return {};
                }

                if (JxlDecoderSetImageOutBuffer(decoder.get(), &format, pixels.data(), pixels.size()) != JXL_DEC_SUCCESS)
                {
                    LOG_DEBUG("[JxlImage] Failed to set JPEG XL output buffer.");
                    return {};
                }
                break;
            }

            case JXL_DEC_FULL_IMAGE:
                if (pixels.empty())
                {
                    LOG_DEBUG("[JxlImage] JPEG XL decoder returned no image data.");
                    return {};
                }

                // Stop after the first frame, so animations only show their first image.
                width = static_cast<int>(info.xsize);
                height = static_cast<int>(info.ysize);
                return pixels;

            default:
                // JXL_DEC_ERROR, or the data ended before a full frame was decoded.
                LOG_DEBUG("[JxlImage] Failed to decode JPEG XL image data.");
                return {};
        }
    }
}

} // namespace JxlImage
} // namespace Renderer
} // namespace libprojectM
