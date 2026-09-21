#ifndef SOLARSYSTEM_KTX2_READER_H
#define SOLARSYSTEM_KTX2_READER_H

// Minimal KTX2 container reader (Khronos KTX File Format Specification v2.0).
//
// Scope, deliberately: this parses the *container* only. It reads the header, the
// level index and the key/value data, and hands back a span per mip level. It does
// not decode, transcode or decompress anything:
//
//   * supercompressionScheme 0 (none) is supported,
//   * scheme 1 (BasisLZ) and 2 (Zstandard) are rejected with a clear message.
//
// That is the whole point of shipping *uncompressed-block* KTX2 (what `toktx` writes
// without --zcmp / --encode basis-lz): the payload of every level is already the exact
// byte sequence glCompressedTexImage2D wants, so no transcoder has to be linked into
// the Wasm module. See docs/plans/PORTING_GUIDE.md § "Texture containers".
//
// GL-free on purpose so it can be unit tested natively without a context.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ktx2 {

/** Sub-range of the caller's buffer holding one mip level, largest level first (index 0). */
struct Level {
    std::size_t byteOffset = 0;
    std::size_t byteLength = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

struct File {
    std::uint32_t vkFormat = 0;
    std::uint32_t typeSize = 1;
    std::uint32_t pixelWidth = 0;
    std::uint32_t pixelHeight = 0;
    std::uint32_t pixelDepth = 0;
    std::uint32_t layerCount = 0;
    std::uint32_t faceCount = 1;
    std::uint32_t levelCount = 1;
    std::uint32_t supercompressionScheme = 0;
    std::vector<Level> levels;

    bool IsCompressedBlockFormat() const;
};

/** True if `data` starts with the 12-byte KTX2 identifier. Never throws. */
bool HasIdentifier(const std::uint8_t* data, std::size_t size);

/**
 * Parse `data` in place. Every returned Level references bytes inside `data`.
 * Throws std::runtime_error with a diagnostic naming `debugName` on any malformed
 * field, out-of-range offset, or unsupported feature (supercompression, 3D, arrays,
 * cube maps) rather than returning a half-filled File.
 */
File Parse(const std::uint8_t* data, std::size_t size, const std::string& debugName);

} // namespace ktx2

#endif // SOLARSYSTEM_KTX2_READER_H
