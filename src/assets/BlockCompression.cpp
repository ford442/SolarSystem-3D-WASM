#include "BlockCompression.h"

#include <algorithm>
#include <cstring>

namespace BlockCompression {
namespace {

constexpr std::uint32_t kGlRgbS3tcDxt1  = 0x83F0;
constexpr std::uint32_t kGlRgbaS3tcDxt1 = 0x83F1;
constexpr std::uint32_t kGlRgbaS3tcDxt3 = 0x83F2;
constexpr std::uint32_t kGlRgbaS3tcDxt5 = 0x83F3;

struct Rgba {
    std::uint8_t r = 0, g = 0, b = 0, a = 255;
};

std::uint16_t ReadU16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

Rgba FromRgb565(std::uint16_t value) {
    Rgba c;
    const std::uint32_t r5 = (value >> 11) & 0x1F;
    const std::uint32_t g6 = (value >> 5) & 0x3F;
    const std::uint32_t b5 = value & 0x1F;
    // Replicate the high bits into the low ones so 0x1F maps to 255, not 248.
    c.r = static_cast<std::uint8_t>((r5 << 3) | (r5 >> 2));
    c.g = static_cast<std::uint8_t>((g6 << 2) | (g6 >> 4));
    c.b = static_cast<std::uint8_t>((b5 << 3) | (b5 >> 2));
    c.a = 255;
    return c;
}

std::uint8_t Lerp(std::uint8_t a, std::uint8_t b, int numerator, int denominator) {
    return static_cast<std::uint8_t>((a * (denominator - numerator) + b * numerator) / denominator);
}

/**
 * Decode the 8-byte colour half of a BC1/BC2/BC3 block into `out[16]`.
 *
 * `allowThreeColorMode` is false for BC2/BC3: those always use the four-colour
 * interpolation regardless of the endpoint ordering, because their alpha lives in a
 * separate block. Only standalone BC1 uses c0 <= c1 to mean "3 colours + transparent".
 */
void DecodeColorBlock(const std::uint8_t* block, bool allowThreeColorMode, Rgba out[16]) {
    const std::uint16_t c0 = ReadU16(block);
    const std::uint16_t c1 = ReadU16(block + 2);

    Rgba palette[4];
    palette[0] = FromRgb565(c0);
    palette[1] = FromRgb565(c1);

    if (!allowThreeColorMode || c0 > c1) {
        palette[2].r = Lerp(palette[0].r, palette[1].r, 1, 3);
        palette[2].g = Lerp(palette[0].g, palette[1].g, 1, 3);
        palette[2].b = Lerp(palette[0].b, palette[1].b, 1, 3);
        palette[3].r = Lerp(palette[0].r, palette[1].r, 2, 3);
        palette[3].g = Lerp(palette[0].g, palette[1].g, 2, 3);
        palette[3].b = Lerp(palette[0].b, palette[1].b, 2, 3);
    } else {
        palette[2].r = Lerp(palette[0].r, palette[1].r, 1, 2);
        palette[2].g = Lerp(palette[0].g, palette[1].g, 1, 2);
        palette[2].b = Lerp(palette[0].b, palette[1].b, 1, 2);
        palette[3] = Rgba{0, 0, 0, 0};  // punch-through transparent
    }

    for (int texel = 0; texel < 16; ++texel) {
        const std::uint8_t indexByte = block[4 + texel / 4];
        const std::uint32_t index = (indexByte >> ((texel % 4) * 2)) & 0x3;
        out[texel] = palette[index];
    }
}

/** BC2: 8 bytes of 4-bit-per-texel alpha, low nibble first. */
void DecodeBc2Alpha(const std::uint8_t* alphaBlock, Rgba out[16]) {
    for (int texel = 0; texel < 16; ++texel) {
        const std::uint8_t packed = alphaBlock[texel / 2];
        const std::uint32_t nibble = (texel % 2 == 0) ? (packed & 0x0F) : (packed >> 4);
        out[texel].a = static_cast<std::uint8_t>(nibble * 17);  // 0..15 -> 0..255
    }
}

/** BC3: two endpoints plus 16 three-bit indices packed into six bytes. */
void DecodeBc3Alpha(const std::uint8_t* alphaBlock, Rgba out[16]) {
    const std::uint8_t a0 = alphaBlock[0];
    const std::uint8_t a1 = alphaBlock[1];

    std::uint8_t palette[8];
    palette[0] = a0;
    palette[1] = a1;
    if (a0 > a1) {
        for (int i = 0; i < 6; ++i) {
            palette[2 + i] = Lerp(a0, a1, i + 1, 7);
        }
    } else {
        for (int i = 0; i < 4; ++i) {
            palette[2 + i] = Lerp(a0, a1, i + 1, 5);
        }
        palette[6] = 0;
        palette[7] = 255;
    }

    // The six index bytes are one little-endian 48-bit word, 3 bits per texel.
    std::uint64_t indices = 0;
    for (int i = 0; i < 6; ++i) {
        indices |= static_cast<std::uint64_t>(alphaBlock[2 + i]) << (8 * i);
    }
    for (int texel = 0; texel < 16; ++texel) {
        out[texel].a = palette[(indices >> (3 * texel)) & 0x7];
    }
}

} // namespace

bool IsDecodableS3tcFormat(std::uint32_t glInternalFormat) {
    return glInternalFormat == kGlRgbS3tcDxt1 || glInternalFormat == kGlRgbaS3tcDxt1 ||
           glInternalFormat == kGlRgbaS3tcDxt3 || glInternalFormat == kGlRgbaS3tcDxt5;
}

std::vector<std::uint8_t> DecodeS3tcToRgba8(std::uint32_t glInternalFormat,
                                            const std::uint8_t* blockData,
                                            std::size_t blockDataSize,
                                            std::uint32_t width,
                                            std::uint32_t height) {
    if (!IsDecodableS3tcFormat(glInternalFormat) || blockData == nullptr || width == 0 || height == 0) {
        return {};
    }

    const bool isDxt1 = (glInternalFormat == kGlRgbS3tcDxt1 || glInternalFormat == kGlRgbaS3tcDxt1);
    const bool isDxt3 = (glInternalFormat == kGlRgbaS3tcDxt3);
    const std::size_t bytesPerBlock = isDxt1 ? 8u : 16u;

    const std::size_t blocksX = (static_cast<std::size_t>(width) + 3) / 4;
    const std::size_t blocksY = (static_cast<std::size_t>(height) + 3) / 4;
    if (blockDataSize < blocksX * blocksY * bytesPerBlock) {
        return {};
    }

    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width) * height * 4);

    for (std::size_t by = 0; by < blocksY; ++by) {
        for (std::size_t bx = 0; bx < blocksX; ++bx) {
            const std::uint8_t* block = blockData + (by * blocksX + bx) * bytesPerBlock;

            Rgba texels[16];
            if (isDxt1) {
                DecodeColorBlock(block, /*allowThreeColorMode=*/true, texels);
            } else {
                DecodeColorBlock(block + 8, /*allowThreeColorMode=*/false, texels);
                if (isDxt3) {
                    DecodeBc2Alpha(block, texels);
                } else {
                    DecodeBc3Alpha(block, texels);
                }
            }

            // Crop the partial block at a right/bottom edge that is not a multiple of 4.
            const std::size_t copyWidth = std::min<std::size_t>(4, width - bx * 4);
            const std::size_t copyHeight = std::min<std::size_t>(4, height - by * 4);
            for (std::size_t y = 0; y < copyHeight; ++y) {
                std::uint8_t* row = rgba.data() + (((by * 4 + y) * width) + bx * 4) * 4;
                for (std::size_t x = 0; x < copyWidth; ++x) {
                    const Rgba& c = texels[y * 4 + x];
                    row[x * 4 + 0] = c.r;
                    row[x * 4 + 1] = c.g;
                    row[x * 4 + 2] = c.b;
                    row[x * 4 + 3] = c.a;
                }
            }
        }
    }

    return rgba;
}

} // namespace BlockCompression
