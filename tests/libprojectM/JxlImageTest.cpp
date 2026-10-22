#include <gtest/gtest.h>

#include <Renderer/JxlImage.hpp>

#include <stb_image.h>

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

static constexpr auto jxlImageTestDataPath{PROJECTM_TEST_DATA_DIR "/JxlImage/"};

namespace JxlImage = libprojectM::Renderer::JxlImage;

namespace {

auto ReadFile(const std::string& fileName) -> std::vector<uint8_t>
{
    std::ifstream file(std::string(jxlImageTestDataPath) + fileName, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

/**
 * Decodes the JPEG XL file and the PNG file with stb_image and expects the same RGBA data.
 * Each channel value may differ by up to the given tolerance.
 */
void ExpectSameAsPng(const std::string& jxlFileName, const std::string& pngFileName, int tolerance = 0)
{
    int jxlWidth{};
    int jxlHeight{};
    auto const jxlPixels = JxlImage::Load(std::string(jxlImageTestDataPath) + jxlFileName, jxlWidth, jxlHeight);
    ASSERT_FALSE(jxlPixels.empty());

    int pngWidth{};
    int pngHeight{};
    std::unique_ptr<stbi_uc, decltype(&free)> const pngPixels(stbi_load((std::string(jxlImageTestDataPath) + pngFileName).c_str(), &pngWidth, &pngHeight, nullptr, 4), free);
    ASSERT_NE(pngPixels.get(), nullptr);

    ASSERT_EQ(jxlWidth, pngWidth);
    ASSERT_EQ(jxlHeight, pngHeight);
    ASSERT_EQ(jxlPixels.size(), static_cast<size_t>(pngWidth * pngHeight * 4));

    for (size_t index = 0; index < jxlPixels.size(); index++)
    {
        ASSERT_NEAR(jxlPixels[index], pngPixels.get()[index], tolerance) << "Pixel " << index / 4 << ", channel " << index % 4;
    }
}

void ExpectRejected(const std::string& jxlFileName)
{
    int width{};
    int height{};
    EXPECT_TRUE(JxlImage::Load(std::string(jxlImageTestDataPath) + jxlFileName, width, height).empty());
}

} // namespace

// The lossless fixtures are encoded from the PNG files with "cjxl -d 0".
TEST(JxlImage, DecodeRgba)
{
    ExpectSameAsPng("rgba.jxl", "rgba.png");
}

TEST(JxlImage, DecodeRgbWithoutAlpha)
{
    ExpectSameAsPng("rgb.jxl", "rgb.png");
}

TEST(JxlImage, DecodeGrayscale)
{
    ExpectSameAsPng("gray.jxl", "gray.png");
}

TEST(JxlImage, DecodeGrayscaleWithAlpha)
{
    ExpectSameAsPng("gray_alpha.jxl", "gray_alpha.png");
}

TEST(JxlImage, DecodeFirstAnimationFrame)
{
    // The first frame of the animation is rgb.png, the second frame has inverted colors.
    ExpectSameAsPng("animated.jxl", "rgb.png");
}

TEST(JxlImage, DecodeLossyToSrgb)
{
    // Lossy (XYB) image tagged as Display P3, made with:
    //   cjxl rgb.png lossy_p3.jxl -d 1 -x color_space=RGB_D65_DCI_Rel_SRG
    // The reference was converted to sRGB by djxl using a color management system:
    //   djxl lossy_p3.jxl lossy_p3_srgb.png --color_space=RGB_D65_SRG_Rel_SRG
    // Without the conversion, the decoder would return P3 values, which differ by up to 110.
    ExpectSameAsPng("lossy_p3.jxl", "lossy_p3_srgb.png", 2);
}

TEST(JxlImage, DecodeFromMemory)
{
    auto const fileData = ReadFile("rgb.jxl");
    ASSERT_FALSE(fileData.empty());

    int width{};
    int height{};
    auto const pixels = JxlImage::LoadFromMemory(fileData.data(), fileData.size(), width, height);

    EXPECT_EQ(width, 16);
    EXPECT_EQ(height, 16);
    EXPECT_EQ(pixels.size(), static_cast<size_t>(16 * 16 * 4));
}

// The size limit fixtures are single-color images made with jxl_from_tree.
TEST(JxlImage, AcceptMaximumWidth)
{
    // 16384x1 pixels, the maximum accepted width.
    int width{};
    int height{};
    auto const pixels = JxlImage::Load(std::string(jxlImageTestDataPath) + "max_width.jxl", width, height);

    ASSERT_EQ(width, 16384);
    ASSERT_EQ(height, 1);
    ASSERT_EQ(pixels.size(), static_cast<size_t>(16384 * 4));
    EXPECT_EQ(pixels[0], 100);
    EXPECT_EQ(pixels[3], 255);
}

TEST(JxlImage, RejectTooWideImage)
{
    // 16385x1 pixels, one more than the maximum accepted width.
    ExpectRejected("too_wide.jxl");
}

TEST(JxlImage, RejectTooManyPixels)
{
    // 16384x1025 pixels. Both sides are within the limit, but the pixel count is above 4096x4096.
    ExpectRejected("too_many_pixels.jxl");
}

TEST(JxlImage, RejectMissingFile)
{
    ExpectRejected("does-not-exist.jxl");
}

TEST(JxlImage, RejectNonJxlData)
{
    auto const fileData = ReadFile("rgb.png");
    ASSERT_FALSE(fileData.empty());

    int width{};
    int height{};
    EXPECT_TRUE(JxlImage::LoadFromMemory(fileData.data(), fileData.size(), width, height).empty());
}

TEST(JxlImage, RejectTruncatedData)
{
    auto const fileData = ReadFile("rgba.jxl");
    ASSERT_FALSE(fileData.empty());

    int width{};
    int height{};
    for (size_t size = 0; size < fileData.size(); size++)
    {
        EXPECT_TRUE(JxlImage::LoadFromMemory(fileData.data(), size, width, height).empty()) << "Size " << size;
    }
}
