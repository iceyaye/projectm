#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace libprojectM {
namespace Renderer {

/**
 * @brief Decodes JPEG XL images into 8-bit RGBA pixel data using libjxl.
 *
 * The returned pixel data has the same layout as stbi_load() returns with 4 requested channels:
 * rows from top to bottom, RGBA byte order, straight (non-premultiplied) alpha. Grayscale images
 * are expanded to RGB, images without alpha get an opaque alpha channel. Of animated images, only
 * the first frame is decoded.
 *
 * Lossy (XYB) images are converted to sRGB. Lossless images are returned in the color space they
 * are stored in, without applying any color profile, same as stb_image does.
 *
 * Images with more than 4096x4096 pixels, a side longer than 16384 pixels, or files larger than
 * 64 MiB are rejected.
 */
namespace JxlImage {

/**
 * @brief Loads and decodes a JPEG XL file.
 * @param filePath The path of the file to load.
 * @param[out] width The width of the decoded image in pixels.
 * @param[out] height The height of the decoded image in pixels.
 * @return The RGBA pixel data, width * height * 4 bytes, or an empty vector if the file couldn't be decoded.
 */
auto Load(const std::string& filePath, int& width, int& height) -> std::vector<uint8_t>;

/**
 * @brief Decodes a JPEG XL image from memory.
 * @param data The encoded image data.
 * @param size The size of the encoded image data in bytes.
 * @param[out] width The width of the decoded image in pixels.
 * @param[out] height The height of the decoded image in pixels.
 * @return The RGBA pixel data, width * height * 4 bytes, or an empty vector if the data couldn't be decoded.
 */
auto LoadFromMemory(const uint8_t* data, size_t size, int& width, int& height) -> std::vector<uint8_t>;

} // namespace JxlImage
} // namespace Renderer
} // namespace libprojectM
