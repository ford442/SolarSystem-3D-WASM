#pragma once

// Software decoder for the BC1/BC2/BC3 (DXT1/DXT3/DXT5) block formats.
//
// Why this exists: every DDS asset in the tree is DXT, and a GPU without
// WEBGL_compressed_texture_s3tc — Safari, iOS, most Android GPUs — cannot accept
// those blocks at all. Rather than dropping such a device to the 4x4 checkerboard,
// the loader decodes the blocks on the CPU once and uploads plain RGBA8.
//
// This is the *compatibility* path, not the shipping one: it costs 4 bytes per texel
// of GPU memory and a decode pass per mip. The shipping answer for those devices is
// an ASTC/ETC2 KTX2 pack (TextureFormatSupport::PreferredPackName); this keeps the
// existing DDS content usable everywhere in the meantime, and keeps CI and software
// GL working with no compressed-texture support whatsoever.
//
// GL-free so it can be unit tested natively.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace BlockCompression {

/** True for the three GL S3TC internal formats this decoder understands. */
bool IsDecodableS3tcFormat(std::uint32_t glInternalFormat);

/**
 * Decode one mip level into tightly packed RGBA8, row 0 first.
 *
 * `blockData`/`blockDataSize` is the compressed level as stored in the DDS; `width`
 * and `height` are that level's dimensions and need not be multiples of four (the
 * partial blocks at the right/bottom edge are cropped). Returns an empty vector if
 * the format is not decodable or the buffer is too short for the stated dimensions,
 * so callers can fall through to their existing error path.
 */
std::vector<std::uint8_t> DecodeS3tcToRgba8(std::uint32_t glInternalFormat,
                                            const std::uint8_t* blockData,
                                            std::size_t blockDataSize,
                                            std::uint32_t width,
                                            std::uint32_t height);

} // namespace BlockCompression
