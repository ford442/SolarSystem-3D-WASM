#ifndef SOLARSYSTEM_ASSETS_TEXTUREDECODE_H
#define SOLARSYSTEM_ASSETS_TEXTUREDECODE_H
#include "TextureFormatSupport.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/**
 * The CPU half of a texture load: container parse (DDS, KTX2), the CPU DXT decode for GPUs
 * without S3TC (BlockCompression.h), the WebGL RGB→RGBA widening. Everything here is
 * GL-free and touches no global state, so it runs in a job (core/JobSystem.h); the GL half —
 * TextureImage2D's upload of the payload — stays on the main thread.
 */
struct TexturePayload {
    struct Level {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::size_t offset = 0; // into `storage`
        std::size_t size = 0;
    };

    std::string path;
    std::uint32_t width = 0; // level 0, as the texture reports it
    std::uint32_t height = 0;
    bool compressed = false;
    std::uint32_t internalFormat = 0; // sized GL internal format (or the block format)
    std::uint32_t format = 0;         // uncompressed: glTexImage2D format / type
    std::uint32_t type = 0;
    // Uncompressed: bytes per texel (rows are tightly packed, upload with UNPACK_ALIGNMENT 1).
    // Compressed: bytes per block of blockWidth x blockHeight texels.
    std::uint32_t texelOrBlockBytes = 0;
    std::uint32_t blockWidth = 1;
    std::uint32_t blockHeight = 1;
    // WebGL wants a DXT base level of at least 4x4: declare the smaller base as 4 (same bytes).
    bool padBaseToBlock = false;
    // Uncompressed with a single level: glGenerateMipmap may fill the chain after upload.
    bool allowGenerateMipmap = false;
    std::vector<Level> levels;
    std::vector<std::uint8_t> storage;

    const std::uint8_t* LevelData(std::size_t level) const { return storage.data() + levels[level].offset; }
    std::size_t ByteSize() const { return storage.size(); }
};

/** What the decoder may assume about the GPU; a snapshot of GlCapabilities taken on the main thread. */
struct TextureDecodeTarget {
    TextureFormats::FormatCapabilities formats;
    int maxTextureSize = 0; // 0: no limit check
    bool webGl = false;     // WebGL 2 rules (RGB widened to RGBA, DXT base padded to 4)
};

/** Read a whole file (MEMFS on web). Throws std::runtime_error when missing or empty. */
std::vector<std::uint8_t> ReadTextureFile(const std::string& path);

/**
 * Parse `bytes` (a .dds or .ktx2, picked by the path's suffix) into a payload the GPU
 * described by `target` accepts. Throws std::runtime_error with the reason when it cannot:
 * the caller's fallback path (checkerboard, or keep the previous texture) takes over.
 */
TexturePayload DecodeTexture(const std::string& path, const std::vector<std::uint8_t>& bytes,
                             const TextureDecodeTarget& target);

#endif // SOLARSYSTEM_ASSETS_TEXTUREDECODE_H
