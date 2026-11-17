#include <gtest/gtest.h>

#include "assets/BlockCompression.h"
#include "assets/TextureDecode.h"
#include "core/JobSystem.h"
#include "dds_test_writer.h"

#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr std::uint32_t kGlRgba = 0x1908;
constexpr std::uint32_t kGlRgb8 = 0x8051;
constexpr std::uint32_t kGlRgba8 = 0x8058;
constexpr std::uint32_t kGlRgbaS3tcDxt1 = 0x83F1;
constexpr std::uint32_t kGlRgbaS3tcDxt5 = 0x83F3;

TextureDecodeTarget Target(bool s3tc, bool webGl = false, int maxTextureSize = 0) {
    TextureDecodeTarget target;
    target.formats.s3tc = s3tc;
    target.webGl = webGl;
    target.maxTextureSize = maxTextureSize;
    return target;
}

} // namespace

TEST(TextureDecode, DxtStaysCompressedWhenTheGpuTakesS3tc) {
    const auto file = DdsTestWriter::Make(DdsTestWriter::Format::Dxt1, 64, 32, 7);
    const TexturePayload payload = DecodeTexture("a.dds", file, Target(true));
    EXPECT_TRUE(payload.compressed);
    EXPECT_EQ(payload.internalFormat, kGlRgbaS3tcDxt1);
    EXPECT_EQ(payload.width, 64u);
    EXPECT_EQ(payload.height, 32u);
    EXPECT_EQ(payload.texelOrBlockBytes, 8u);
    EXPECT_EQ(payload.blockWidth, 4u);
    EXPECT_FALSE(payload.allowGenerateMipmap);
    ASSERT_EQ(payload.levels.size(), 7u);
    EXPECT_EQ(payload.levels[0].size, 16u * 8u * 8u);
    EXPECT_EQ(payload.levels[1].width, 32u);
    EXPECT_EQ(payload.levels[6].width, 1u);
    // Level 0 is the file's blocks, byte for byte.
    EXPECT_EQ(std::memcmp(payload.LevelData(0), file.data() + 128, payload.levels[0].size), 0);
}

TEST(TextureDecode, DxtIsCpuDecodedWithoutS3tc) {
    const auto file = DdsTestWriter::Make(DdsTestWriter::Format::Dxt5, 32, 32, 6);
    const TexturePayload payload = DecodeTexture("a.dds", file, Target(false));
    EXPECT_FALSE(payload.compressed);
    EXPECT_EQ(payload.internalFormat, kGlRgba8);
    EXPECT_EQ(payload.format, kGlRgba);
    EXPECT_EQ(payload.texelOrBlockBytes, 4u);
    ASSERT_EQ(payload.levels.size(), 6u);
    const std::vector<std::uint8_t> expected =
        BlockCompression::DecodeS3tcToRgba8(kGlRgbaS3tcDxt5, file.data() + 128, 8 * 8 * 16, 32, 32);
    ASSERT_EQ(payload.levels[0].size, expected.size());
    EXPECT_EQ(std::memcmp(payload.LevelData(0), expected.data(), expected.size()), 0);
}

TEST(TextureDecode, WebGlWidensRgbToRgbaAndNativeKeepsRgb8) {
    const auto file = DdsTestWriter::Make(DdsTestWriter::Format::Rgb24, 5, 3, 1);
    const TexturePayload native = DecodeTexture("a.dds", file, Target(true));
    EXPECT_EQ(native.internalFormat, kGlRgb8);
    EXPECT_EQ(native.texelOrBlockBytes, 3u);
    EXPECT_EQ(native.levels[0].size, 5u * 3u * 3u);
    EXPECT_TRUE(native.allowGenerateMipmap);

    const TexturePayload web = DecodeTexture("a.dds", file, Target(true, true));
    EXPECT_EQ(web.internalFormat, kGlRgba8);
    EXPECT_EQ(web.format, kGlRgba);
    ASSERT_EQ(web.levels[0].size, 5u * 3u * 4u);
    for (std::size_t texel = 0; texel < 15; ++texel) {
        EXPECT_EQ(web.LevelData(0)[texel * 4 + 0], native.LevelData(0)[texel * 3 + 0]);
        EXPECT_EQ(web.LevelData(0)[texel * 4 + 2], native.LevelData(0)[texel * 3 + 2]);
        EXPECT_EQ(web.LevelData(0)[texel * 4 + 3], 255);
    }
}

TEST(TextureDecode, WebGlPadsATinyDxtBase) {
    const auto file = DdsTestWriter::Make(DdsTestWriter::Format::Dxt1, 2, 2, 1);
    EXPECT_TRUE(DecodeTexture("a.dds", file, Target(true, true)).padBaseToBlock);
    EXPECT_FALSE(DecodeTexture("a.dds", file, Target(true, false)).padBaseToBlock);
}

TEST(TextureDecode, RejectsOversizeAndGarbage) {
    const auto file = DdsTestWriter::Make(DdsTestWriter::Format::Rgba32, 64, 64, 1);
    EXPECT_THROW(DecodeTexture("big.dds", file, Target(true, false, 32)), std::runtime_error);
    EXPECT_NO_THROW(DecodeTexture("big.dds", file, Target(true, false, 64)));
    const std::vector<std::uint8_t> garbage(256, 0x5A);
    EXPECT_THROW(DecodeTexture("junk.dds", garbage, Target(true)), std::runtime_error);
    EXPECT_THROW(DecodeTexture("junk.ktx2", garbage, Target(true)), std::runtime_error);
}

TEST(TextureDecode, ReadsTheCommittedPlaceholdersAndLuts) {
    const std::string root = SOLARSYSTEM_SOURCE_DIR;
    const TexturePayload dds = DecodeTexture(
        "Titan_Diffuse_Low.dds", ReadTextureFile(root + "/resource/textures_low/Titan_Diffuse_Low.dds"), Target(true));
    EXPECT_EQ(dds.width, 4u);
    EXPECT_FALSE(dds.compressed);
    const TexturePayload lut =
        DecodeTexture("earth_transmittance.ktx2",
                      ReadTextureFile(root + "/resource/atmosphere/earth_transmittance.ktx2"), Target(true));
    EXPECT_GT(lut.width, 0u);
    EXPECT_FALSE(lut.compressed);
    EXPECT_FALSE(lut.allowGenerateMipmap); // half-float: no glGenerateMipmap in WebGL 2
    EXPECT_THROW(ReadTextureFile(root + "/resource/no/such.dds"), std::runtime_error);
}

TEST(TextureDecode, AJobDecodesTheSamePayloadAsInline) {
    Jobs::Stop();
    Jobs::Start(2);
    const auto file = DdsTestWriter::Make(DdsTestWriter::Format::Dxt1, 128, 128, 8, 7);
    auto future = Jobs::Submit([file] { return DecodeTexture("a.dds", file, Target(false)); });
    const TexturePayload inline_ = DecodeTexture("a.dds", file, Target(false));
    const TexturePayload threaded = future.Get();
    Jobs::Stop();
    EXPECT_EQ(threaded.storage, inline_.storage);
    EXPECT_EQ(threaded.levels.size(), inline_.levels.size());
}
