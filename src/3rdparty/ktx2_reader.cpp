#include "ktx2_reader.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace ktx2 {
namespace {

constexpr std::uint8_t kIdentifier[12] = {
    0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A
};

// KTX2 stores every scalar little-endian regardless of host byte order, so read
// them byte-wise instead of memcpy-ing into a host integer.
std::uint32_t ReadU32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint64_t ReadU64(const std::uint8_t* p) {
    return static_cast<std::uint64_t>(ReadU32(p)) |
           (static_cast<std::uint64_t>(ReadU32(p + 4)) << 32);
}

[[noreturn]] void Fail(const std::string& debugName, const std::string& why) {
    throw std::runtime_error("KTX2 " + debugName + ": " + why);
}

const char* SupercompressionName(std::uint32_t scheme) {
    switch (scheme) {
        case 1: return "BasisLZ";
        case 2: return "Zstandard";
        case 3: return "ZLIB";
        default: return "unknown";
    }
}

} // namespace

bool File::IsCompressedBlockFormat() const {
    // VK_FORMAT_BC1_RGB_UNORM_BLOCK (131) .. VK_FORMAT_ASTC_12x12_SRGB_BLOCK (184)
    // is one contiguous run of block-compressed formats in the Vulkan core enum.
    return vkFormat >= 131 && vkFormat <= 184;
}

bool HasIdentifier(const std::uint8_t* data, std::size_t size) {
    return data != nullptr && size >= sizeof(kIdentifier) &&
           std::memcmp(data, kIdentifier, sizeof(kIdentifier)) == 0;
}

File Parse(const std::uint8_t* data, std::size_t size, const std::string& debugName) {
    // 12-byte identifier + 9 header words + 4 index words + 2 index quadwords.
    constexpr std::size_t kHeaderSize = 12 + 9 * 4 + 4 * 4 + 2 * 8;
    if (data == nullptr || size < kHeaderSize) {
        Fail(debugName, "file is shorter than a KTX2 header");
    }
    if (!HasIdentifier(data, size)) {
        Fail(debugName, "missing KTX2 identifier");
    }

    File file;
    const std::uint8_t* h = data + 12;
    file.vkFormat               = ReadU32(h + 0);
    file.typeSize               = ReadU32(h + 4);
    file.pixelWidth             = ReadU32(h + 8);
    file.pixelHeight            = ReadU32(h + 12);
    file.pixelDepth             = ReadU32(h + 16);
    file.layerCount             = ReadU32(h + 20);
    file.faceCount              = ReadU32(h + 24);
    file.levelCount             = ReadU32(h + 28);
    file.supercompressionScheme = ReadU32(h + 32);

    if (file.supercompressionScheme != 0) {
        Fail(debugName, std::string("supercompressionScheme ") +
                            std::to_string(file.supercompressionScheme) + " (" +
                            SupercompressionName(file.supercompressionScheme) +
                            ") is not supported; re-encode without supercompression "
                            "(scripts/convert_textures_ktx2.py)");
    }
    if (file.vkFormat == 0) {
        Fail(debugName, "vkFormat 0 (VK_FORMAT_UNDEFINED) means a Basis Universal payload "
                        "that needs a transcoder; re-encode to a GPU block format");
    }
    if (file.pixelWidth == 0) {
        Fail(debugName, "pixelWidth is 0");
    }
    if (file.pixelDepth != 0) {
        Fail(debugName, "3D textures are not supported");
    }
    if (file.layerCount > 1) {
        Fail(debugName, "texture arrays are not supported");
    }
    if (file.faceCount != 1) {
        Fail(debugName, "cube maps are not supported");
    }

    // levelCount 0 asks the loader to generate the mip chain itself; the file still
    // carries exactly one level index entry for the base level.
    const std::uint32_t storedLevels = std::max<std::uint32_t>(file.levelCount, 1);
    const std::size_t levelIndexOffset = kHeaderSize;
    const std::size_t levelIndexSize = static_cast<std::size_t>(storedLevels) * 24;
    if (levelIndexSize / 24 != storedLevels || levelIndexOffset + levelIndexSize > size) {
        Fail(debugName, "level index runs past the end of the file (levelCount " +
                            std::to_string(file.levelCount) + ")");
    }

    const std::uint32_t baseHeight = std::max<std::uint32_t>(file.pixelHeight, 1);
    file.levels.reserve(storedLevels);
    for (std::uint32_t i = 0; i < storedLevels; ++i) {
        const std::uint8_t* entry = data + levelIndexOffset + static_cast<std::size_t>(i) * 24;
        const std::uint64_t byteOffset = ReadU64(entry + 0);
        const std::uint64_t byteLength = ReadU64(entry + 8);
        const std::uint64_t uncompressedByteLength = ReadU64(entry + 16);

        if (byteLength == 0) {
            Fail(debugName, "level " + std::to_string(i) + " has zero byteLength");
        }
        // With no supercompression the two lengths must agree; a mismatch means the
        // file was written by an encoder whose output we would silently misread.
        if (uncompressedByteLength != 0 && uncompressedByteLength != byteLength) {
            Fail(debugName, "level " + std::to_string(i) +
                                " byteLength/uncompressedByteLength disagree without supercompression");
        }
        if (byteOffset > size || byteLength > size - byteOffset) {
            Fail(debugName, "level " + std::to_string(i) + " payload runs past the end of the file");
        }

        Level level;
        level.byteOffset = static_cast<std::size_t>(byteOffset);
        level.byteLength = static_cast<std::size_t>(byteLength);
        level.width = std::max<std::uint32_t>(file.pixelWidth >> i, 1);
        level.height = std::max<std::uint32_t>(baseHeight >> i, 1);
        file.levels.push_back(level);
    }

    return file;
}

} // namespace ktx2
