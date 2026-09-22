// Covers the GL-free halves of the multi-format texture path: the KTX2 container
// reader, the vkFormat -> GL mapping / pack selection, and the software BC decoder.

#include "3rdparty/ktx2_reader.h"
#include "Auxiliary_Modules/BlockCompression.h"
#include "Auxiliary_Modules/TextureFormatSupport.h"

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

namespace {

constexpr std::uint32_t kVkBc3Unorm    = 137;
constexpr std::uint32_t kVkAstc4x4     = 157;
constexpr std::uint32_t kVkAstc6x6     = 165;
constexpr std::uint32_t kVkEtc2Rgba8   = 151;
constexpr std::uint32_t kVkRgba8Unorm  = 37;

constexpr std::uint32_t kGlRgbaS3tcDxt1 = 0x83F1;
constexpr std::uint32_t kGlRgbaS3tcDxt3 = 0x83F2;
constexpr std::uint32_t kGlRgbaS3tcDxt5 = 0x83F3;

void PushU32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xFF));
    }
}

void PushU64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xFF));
    }
}

/** Build a minimal, valid KTX2 file with `levelCount` levels of `vkFormat`. */
std::vector<std::uint8_t> MakeKtx2(std::uint32_t vkFormat,
                                   std::uint32_t width,
                                   std::uint32_t height,
                                   std::uint32_t levelCount,
                                   std::uint32_t supercompression = 0) {
    std::vector<std::uint8_t> file;
    const std::uint8_t identifier[12] = {0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32,
                                         0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};
    file.insert(file.end(), identifier, identifier + 12);
    PushU32(file, vkFormat);
    PushU32(file, 1);            // typeSize
    PushU32(file, width);
    PushU32(file, height);
    PushU32(file, 0);            // pixelDepth
    PushU32(file, 0);            // layerCount
    PushU32(file, 1);            // faceCount
    PushU32(file, levelCount);
    PushU32(file, supercompression);
    PushU32(file, 0);            // dfdByteOffset
    PushU32(file, 0);            // dfdByteLength
    PushU32(file, 0);            // kvdByteOffset
    PushU32(file, 0);            // kvdByteLength
    PushU64(file, 0);            // sgdByteOffset
    PushU64(file, 0);            // sgdByteLength

    const std::size_t levelIndexOffset = file.size();
    file.resize(levelIndexOffset + static_cast<std::size_t>(levelCount) * 24, 0);

    // Append each level's payload and back-fill its index entry.
    for (std::uint32_t level = 0; level < levelCount; ++level) {
        const std::uint32_t levelWidth = std::max<std::uint32_t>(width >> level, 1);
        const std::uint32_t levelHeight = std::max<std::uint32_t>(height >> level, 1);
        const std::size_t bytes = TextureFormats::ExpectedLevelBytes(vkFormat, levelWidth, levelHeight);
        const std::size_t offset = file.size();
        file.resize(offset + bytes, static_cast<std::uint8_t>(0xA0 + level));

        std::vector<std::uint8_t> entry;
        PushU64(entry, offset);
        PushU64(entry, bytes);
        PushU64(entry, bytes);
        std::memcpy(file.data() + levelIndexOffset + static_cast<std::size_t>(level) * 24,
                    entry.data(), entry.size());
    }

    return file;
}

} // namespace

// ---- KTX2 container ------------------------------------------------------------

TEST(Ktx2Reader, ParsesLevelIndexAndMipDimensions) {
    const std::vector<std::uint8_t> file = MakeKtx2(kVkBc3Unorm, 64, 32, 3);

    const ktx2::File parsed = ktx2::Parse(file.data(), file.size(), "bc3.ktx2");
    EXPECT_EQ(parsed.vkFormat, kVkBc3Unorm);
    EXPECT_EQ(parsed.pixelWidth, 64u);
    EXPECT_EQ(parsed.pixelHeight, 32u);
    ASSERT_EQ(parsed.levels.size(), 3u);

    EXPECT_EQ(parsed.levels[0].width, 64u);
    EXPECT_EQ(parsed.levels[0].height, 32u);
    EXPECT_EQ(parsed.levels[1].width, 32u);
    EXPECT_EQ(parsed.levels[1].height, 16u);
    EXPECT_EQ(parsed.levels[2].width, 16u);
    EXPECT_EQ(parsed.levels[2].height, 8u);

    // BC3 is 16 bytes per 4x4 block.
    EXPECT_EQ(parsed.levels[0].byteLength, (64u / 4) * (32u / 4) * 16u);
    EXPECT_LE(parsed.levels[2].byteOffset + parsed.levels[2].byteLength, file.size());
    EXPECT_TRUE(parsed.IsCompressedBlockFormat());
}

TEST(Ktx2Reader, RejectsNonKtx2AndTruncatedFiles) {
    const std::vector<std::uint8_t> notKtx2 = {'D', 'D', 'S', ' ', 0, 0, 0, 0,
                                               0, 0, 0, 0, 0, 0, 0, 0};
    EXPECT_FALSE(ktx2::HasIdentifier(notKtx2.data(), notKtx2.size()));
    EXPECT_THROW(ktx2::Parse(notKtx2.data(), notKtx2.size(), "x"), std::runtime_error);

    std::vector<std::uint8_t> truncated = MakeKtx2(kVkBc3Unorm, 64, 32, 3);
    truncated.resize(truncated.size() - 16);
    EXPECT_THROW(ktx2::Parse(truncated.data(), truncated.size(), "x"), std::runtime_error);
}

TEST(Ktx2Reader, RejectsSupercompressionAndBasisPayloads) {
    // Scheme 2 is Zstandard; we ship uncompressed-block KTX2 so no decompressor is linked.
    const std::vector<std::uint8_t> zstd = MakeKtx2(kVkBc3Unorm, 8, 8, 1, /*supercompression=*/2);
    EXPECT_THROW(ktx2::Parse(zstd.data(), zstd.size(), "z"), std::runtime_error);

    // vkFormat 0 means a Basis Universal payload, which would need a transcoder.
    std::vector<std::uint8_t> basis = MakeKtx2(kVkBc3Unorm, 8, 8, 1);
    basis[12] = 0;
    EXPECT_THROW(ktx2::Parse(basis.data(), basis.size(), "b"), std::runtime_error);
}

TEST(Ktx2Reader, TreatsLevelCountZeroAsOneStoredLevel) {
    std::vector<std::uint8_t> file = MakeKtx2(kVkBc3Unorm, 16, 16, 1);
    file[12 + 28] = 0;  // levelCount -> 0 ("generate mips at load")

    const ktx2::File parsed = ktx2::Parse(file.data(), file.size(), "gen.ktx2");
    EXPECT_EQ(parsed.levelCount, 0u);
    ASSERT_EQ(parsed.levels.size(), 1u);
    EXPECT_EQ(parsed.levels[0].width, 16u);
}

// ---- Format mapping and pack selection -----------------------------------------

TEST(TextureFormatSupport, MapsVkFormatsToGlInternalFormats) {
    EXPECT_EQ(TextureFormats::GlInternalFormatFromVkFormat(kVkBc3Unorm), 0x83F3u);   // DXT5
    EXPECT_EQ(TextureFormats::GlInternalFormatFromVkFormat(145), 0x8E8Cu);           // BC7
    EXPECT_EQ(TextureFormats::GlInternalFormatFromVkFormat(kVkEtc2Rgba8), 0x9278u);  // RGBA8 ETC2 EAC
    EXPECT_EQ(TextureFormats::GlInternalFormatFromVkFormat(kVkAstc4x4), 0x93B0u);
    EXPECT_EQ(TextureFormats::GlInternalFormatFromVkFormat(kVkAstc4x4 + 1), 0x93D0u); // sRGB 4x4
    EXPECT_EQ(TextureFormats::GlInternalFormatFromVkFormat(kVkAstc6x6), 0x93B4u);
    EXPECT_EQ(TextureFormats::GlInternalFormatFromVkFormat(kVkRgba8Unorm), 0x8058u);
    EXPECT_EQ(TextureFormats::GlInternalFormatFromVkFormat(999999), 0u);
}

TEST(TextureFormatSupport, ComputesLevelSizesWithPartialBlocks) {
    EXPECT_EQ(TextureFormats::ExpectedLevelBytes(kVkBc3Unorm, 8, 8), 4u * 16u);
    // A 5x5 BC3 level still occupies a full 2x2 grid of blocks.
    EXPECT_EQ(TextureFormats::ExpectedLevelBytes(kVkBc3Unorm, 5, 5), 4u * 16u);
    // ASTC 6x6: 128 bits per block whatever the footprint.
    EXPECT_EQ(TextureFormats::ExpectedLevelBytes(kVkAstc6x6, 12, 12), 4u * 16u);
    EXPECT_EQ(TextureFormats::ExpectedLevelBytes(kVkRgba8Unorm, 4, 4), 64u);
    EXPECT_EQ(TextureFormats::ExpectedLevelBytes(999999, 4, 4), 0u);
}

TEST(TextureFormatSupport, PrefersBcOnDesktopAndAstcWhereBcIsAbsent) {
    // Desktop Chrome: keep a BC format rather than drifting onto an emulated path.
    EXPECT_STREQ(TextureFormats::PreferredPackName({true, true, false, false}), "bc7");
    EXPECT_STREQ(TextureFormats::PreferredPackName({true, false, false, false}), "bc3");
    // Safari / iOS: no S3TC at all, but ASTC is there.
    EXPECT_STREQ(TextureFormats::PreferredPackName({false, false, true, true}), "astc");
    EXPECT_STREQ(TextureFormats::PreferredPackName({false, false, true, false}), "etc2");
    // CI / software GL: nothing compressed.
    EXPECT_STREQ(TextureFormats::PreferredPackName({false, false, false, false}), "rgba8");
}

TEST(TextureFormatSupport, GatesFormatsOnTheMatchingExtension) {
    const TextureFormats::FormatCapabilities appleLike{false, false, false, true};
    EXPECT_TRUE(TextureFormats::IsFormatSupported(kVkAstc4x4, appleLike));
    EXPECT_FALSE(TextureFormats::IsFormatSupported(kVkBc3Unorm, appleLike));
    EXPECT_FALSE(TextureFormats::IsFormatSupported(kVkEtc2Rgba8, appleLike));
    // Uncompressed always uploads, whatever the extensions say.
    EXPECT_TRUE(TextureFormats::IsFormatSupported(kVkRgba8Unorm, appleLike));
}

TEST(TextureFormatSupport, VariantPathFollowsTheActivePack) {
    TextureFormats::SetTexturePack("");
    EXPECT_EQ(TextureFormats::VariantPath("resource/textures_low/Ceres_Diffuse_Low.dds"),
              "resource/textures_low/Ceres_Diffuse_Low.dds");

    TextureFormats::SetTexturePack("astc");
    EXPECT_EQ(TextureFormats::VariantPath("resource/textures_low/Ceres_Diffuse_Low.dds"),
              "resource/textures_low/astc/Ceres_Diffuse_Low.ktx2");
    EXPECT_EQ(TextureFormats::VariantPath("resource/textures/Saturn_Rings.dds"),
              "resource/textures/astc/Saturn_Rings.ktx2");
    // Non-.dds inputs (shaders, fonts, an already-resolved path) are left alone.
    EXPECT_EQ(TextureFormats::VariantPath("resource/shaders/planetLighting.fs"),
              "resource/shaders/planetLighting.fs");
    EXPECT_EQ(TextureFormats::VariantPath("resource/textures_low/astc/Ceres_Diffuse_Low.ktx2"),
              "resource/textures_low/astc/Ceres_Diffuse_Low.ktx2");

    TextureFormats::SetTexturePack("");  // leave the process-wide default as found
}

// ---- Software BC decode --------------------------------------------------------

namespace {

/** One BC1 block: endpoints `c0`/`c1` in RGB565, then four 2-bit indices per row. */
std::vector<std::uint8_t> MakeBc1Block(std::uint16_t c0, std::uint16_t c1, std::uint8_t indexByte) {
    return {static_cast<std::uint8_t>(c0 & 0xFF), static_cast<std::uint8_t>(c0 >> 8),
            static_cast<std::uint8_t>(c1 & 0xFF), static_cast<std::uint8_t>(c1 >> 8),
            indexByte, indexByte, indexByte, indexByte};
}

constexpr std::uint16_t kRed565 = 0xF800;
constexpr std::uint16_t kBlue565 = 0x001F;

} // namespace

TEST(BlockCompression, RecognisesOnlyTheS3tcFormats) {
    EXPECT_TRUE(BlockCompression::IsDecodableS3tcFormat(kGlRgbaS3tcDxt1));
    EXPECT_TRUE(BlockCompression::IsDecodableS3tcFormat(kGlRgbaS3tcDxt5));
    EXPECT_FALSE(BlockCompression::IsDecodableS3tcFormat(0x9278));  // ETC2
    EXPECT_FALSE(BlockCompression::IsDecodableS3tcFormat(0x8058));  // RGBA8
}

TEST(BlockCompression, DecodesBc1EndpointsExactly) {
    // index 0 everywhere -> every texel is endpoint 0 (pure red, opaque).
    const std::vector<std::uint8_t> block = MakeBc1Block(kRed565, kBlue565, 0x00);
    const std::vector<std::uint8_t> rgba =
        BlockCompression::DecodeS3tcToRgba8(kGlRgbaS3tcDxt1, block.data(), block.size(), 4, 4);

    ASSERT_EQ(rgba.size(), 4u * 4u * 4u);
    for (std::size_t texel = 0; texel < 16; ++texel) {
        EXPECT_EQ(rgba[texel * 4 + 0], 255) << "texel " << texel;
        EXPECT_EQ(rgba[texel * 4 + 1], 0);
        EXPECT_EQ(rgba[texel * 4 + 2], 0);
        EXPECT_EQ(rgba[texel * 4 + 3], 255);
    }
}

TEST(BlockCompression, Bc1PunchThroughAlphaOnlyWhenC0NotGreaterThanC1) {
    // c0 <= c1 selects the 3-colour mode, where index 3 is transparent black.
    const std::vector<std::uint8_t> punchThrough = MakeBc1Block(kBlue565, kRed565, 0xFF);
    const std::vector<std::uint8_t> transparent =
        BlockCompression::DecodeS3tcToRgba8(kGlRgbaS3tcDxt1, punchThrough.data(), punchThrough.size(), 4, 4);
    ASSERT_EQ(transparent.size(), 64u);
    EXPECT_EQ(transparent[3], 0);

    // c0 > c1 is the 4-colour mode: index 3 is an interpolated *opaque* colour.
    const std::vector<std::uint8_t> fourColor = MakeBc1Block(kRed565, kBlue565, 0xFF);
    const std::vector<std::uint8_t> opaque =
        BlockCompression::DecodeS3tcToRgba8(kGlRgbaS3tcDxt1, fourColor.data(), fourColor.size(), 4, 4);
    ASSERT_EQ(opaque.size(), 64u);
    EXPECT_EQ(opaque[3], 255);
}

TEST(BlockCompression, DecodesBc3AlphaEndpointsAndIgnoresColorBlockOrdering) {
    std::vector<std::uint8_t> block(16, 0);
    block[0] = 200;  // alpha endpoint 0
    block[1] = 40;   // alpha endpoint 1
    // All alpha indices 0 -> every texel takes endpoint 0.
    // Colour half uses c0 <= c1, which for BC3 must still mean the 4-colour mode
    // (opaque), not punch-through.
    block[8] = static_cast<std::uint8_t>(kBlue565 & 0xFF);
    block[9] = static_cast<std::uint8_t>(kBlue565 >> 8);
    block[10] = static_cast<std::uint8_t>(kRed565 & 0xFF);
    block[11] = static_cast<std::uint8_t>(kRed565 >> 8);
    block[12] = block[13] = block[14] = block[15] = 0xFF;  // colour index 3 everywhere

    const std::vector<std::uint8_t> rgba =
        BlockCompression::DecodeS3tcToRgba8(kGlRgbaS3tcDxt5, block.data(), block.size(), 4, 4);
    ASSERT_EQ(rgba.size(), 64u);
    for (std::size_t texel = 0; texel < 16; ++texel) {
        EXPECT_EQ(rgba[texel * 4 + 3], 200) << "texel " << texel;
    }
}

TEST(BlockCompression, DecodesBc2FourBitAlpha) {
    std::vector<std::uint8_t> block(16, 0);
    block[0] = 0xF0;  // texel 0 alpha nibble 0x0, texel 1 nibble 0xF
    block[8] = static_cast<std::uint8_t>(kRed565 & 0xFF);
    block[9] = static_cast<std::uint8_t>(kRed565 >> 8);

    const std::vector<std::uint8_t> rgba =
        BlockCompression::DecodeS3tcToRgba8(kGlRgbaS3tcDxt3, block.data(), block.size(), 4, 4);
    ASSERT_EQ(rgba.size(), 64u);
    EXPECT_EQ(rgba[0 * 4 + 3], 0);
    EXPECT_EQ(rgba[1 * 4 + 3], 255);
}

TEST(BlockCompression, CropsPartialEdgeBlocksAndRejectsShortBuffers) {
    // A 2x2 texture still occupies one full 4x4 block; the decode is cropped to 2x2.
    const std::vector<std::uint8_t> block = MakeBc1Block(kRed565, kBlue565, 0x00);
    const std::vector<std::uint8_t> rgba =
        BlockCompression::DecodeS3tcToRgba8(kGlRgbaS3tcDxt1, block.data(), block.size(), 2, 2);
    EXPECT_EQ(rgba.size(), 2u * 2u * 4u);

    // Too few bytes for the stated dimensions: report failure instead of reading past the end.
    EXPECT_TRUE(BlockCompression::DecodeS3tcToRgba8(kGlRgbaS3tcDxt1, block.data(), block.size(), 16, 16).empty());
    EXPECT_TRUE(BlockCompression::DecodeS3tcToRgba8(0x8058, block.data(), block.size(), 4, 4).empty());
}
