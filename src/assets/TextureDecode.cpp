#include "TextureDecode.h"
#include "BlockCompression.h"
#include "TextureFormatSupport.h"
#include "../3rdparty/ktx2_reader.h"
#include "../3rdparty/nv_dds.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <istream>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <vector>
#include <ios>

namespace {

// GL enum values, spelled out so this file needs no GL header (it runs in jobs).
constexpr std::uint32_t kGlRed = 0x1903;
constexpr std::uint32_t kGlRgba = 0x1908;
constexpr std::uint32_t kGlUnsignedByte = 0x1401;
constexpr std::uint32_t kGlHalfFloat = 0x140B;
constexpr std::uint32_t kGlR8 = 0x8229;
constexpr std::uint32_t kGlRgb8 = 0x8051;
constexpr std::uint32_t kGlRgba8 = 0x8058;
constexpr std::uint32_t kGlRgbS3tcDxt1 = 0x83F0;
constexpr std::uint32_t kGlRgbaS3tcDxt1 = 0x83F1;

bool HasSuffix(const std::string& value, const std::string& suffix) {
    return value.size() > suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

/** A read-only istream over bytes we already hold: nv_dds parses from a stream. */
class ByteStreamBuf : public std::streambuf {
public:
    ByteStreamBuf(const std::uint8_t* data, std::size_t size) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast): streambuf's get area is non-const but never written
        char* begin = const_cast<char*>(reinterpret_cast<const char*>(data));
        setg(begin, begin, begin + size);
    }
};

void ValidateSize(const std::string& path, std::uint32_t width, std::uint32_t height,
                  const TextureDecodeTarget& target) {
    if (target.maxTextureSize <= 0) {
        return;
    }
    const auto limit = static_cast<std::uint32_t>(target.maxTextureSize);
    if (width > limit || height > limit) {
        throw std::runtime_error("Texture dimensions " + std::to_string(width) + "x" + std::to_string(height) +
                                 " exceed GL_MAX_TEXTURE_SIZE (" + std::to_string(target.maxTextureSize) + ") for " +
                                 path);
    }
}

void AppendLevel(TexturePayload& payload, std::uint32_t width, std::uint32_t height, const std::uint8_t* data,
                 std::size_t size) {
    TexturePayload::Level level;
    level.width = width;
    level.height = height;
    level.offset = payload.storage.size();
    level.size = size;
    payload.storage.insert(payload.storage.end(), data, data + size);
    payload.levels.push_back(level);
}

TexturePayload DecodeKtx2(const std::string& path, const std::vector<std::uint8_t>& bytes,
                          const TextureDecodeTarget& target) {
    // Format-specific pack (BC/ASTC/ETC2), encoded offline into what this GPU reported, so
    // nothing is transcoded: the level bytes go to the GPU as they are. A pack this GPU
    // cannot take is an error, not a silent downgrade.
    const ktx2::File file = ktx2::Parse(bytes.data(), bytes.size(), path);
    const std::uint32_t internalFormat = TextureFormats::GlInternalFormatFromVkFormat(file.vkFormat);
    if (internalFormat == 0) {
        throw std::runtime_error("KTX2 " + path + ": vkFormat " + std::to_string(file.vkFormat) +
                                 " has no GL mapping in this build");
    }
    if (!TextureFormats::IsFormatSupported(file.vkFormat, target.formats)) {
        throw std::runtime_error("KTX2 " + path + ": vkFormat " + std::to_string(file.vkFormat) +
                                 " is not supported by this GPU/browser (expected the '" +
                                 TextureFormats::PreferredPackName(target.formats) + "' pack)");
    }

    TexturePayload payload;
    payload.path = path;
    payload.width = file.pixelWidth;
    payload.height = std::max<std::uint32_t>(file.pixelHeight, 1);
    ValidateSize(path, payload.width, payload.height, target);

    const TextureFormats::BlockLayout layout = TextureFormats::LayoutFromVkFormat(file.vkFormat);
    payload.compressed = layout.compressed;
    payload.texelOrBlockBytes = layout.blockBytes;
    payload.blockWidth = layout.blockWidth;
    payload.blockHeight = layout.blockHeight;
    if (layout.compressed) {
        payload.internalFormat = internalFormat;
    } else {
        const TextureFormats::UncompressedUpload upload = TextureFormats::UncompressedUploadFromVkFormat(file.vkFormat);
        payload.internalFormat = upload.internalFormat;
        payload.format = upload.format;
        payload.type = upload.type;
    }

    for (std::size_t level = 0; level < file.levels.size(); ++level) {
        const ktx2::Level& entry = file.levels[level];
        const std::size_t expected = TextureFormats::ExpectedLevelBytes(file.vkFormat, entry.width, entry.height);
        if (expected == 0 || entry.byteLength < expected || entry.byteOffset + expected > bytes.size()) {
            throw std::runtime_error("KTX2 " + path + ": level " + std::to_string(level) +
                                     " is shorter than its declared " + std::to_string(entry.width) + "x" +
                                     std::to_string(entry.height) + " payload");
        }
        AppendLevel(payload, entry.width, entry.height, bytes.data() + entry.byteOffset, expected);
    }
    // Half-float is filterable but not colour-renderable without EXT_color_buffer_float, so
    // glGenerateMipmap would fail on it in WebGL 2.
    payload.allowGenerateMipmap = !payload.compressed && payload.levels.size() == 1 && payload.type != kGlHalfFloat;
    return payload;
}

/** Level 0 is the CDDSImage itself; get_mipmap(i) is level i + 1. */
void DdsLevel(nv_dds::CDDSImage& image, unsigned int level, const std::uint8_t*& data, unsigned int& width,
              unsigned int& height, unsigned int& size) {
    if (level == 0) {
        data = static_cast<std::uint8_t*>(image);
        width = image.get_width();
        height = image.get_height();
        size = image.get_size();
        return;
    }
    const nv_dds::CSurface& surface = image.get_mipmap(level - 1);
    data = static_cast<std::uint8_t*>(surface);
    width = surface.get_width();
    height = surface.get_height();
    size = surface.get_size();
}

TexturePayload DecodeDds(const std::string& path, const std::vector<std::uint8_t>& bytes,
                         const TextureDecodeTarget& target) {
    ByteStreamBuf buffer(bytes.data(), bytes.size());
    std::istream stream(&buffer);
    nv_dds::CDDSImage image;
    image.load(stream, false);

    TexturePayload payload;
    payload.path = path;
    payload.width = image.get_width();
    payload.height = image.get_height();
    ValidateSize(path, payload.width, payload.height, target);
    const unsigned int levelCount = image.get_num_mipmaps() + 1;
    const std::uint32_t format = image.get_format();

    if (image.is_compressed() && target.formats.s3tc) {
        payload.compressed = true;
        payload.internalFormat = format;
        payload.blockWidth = 4;
        payload.blockHeight = 4;
        payload.texelOrBlockBytes = (format == kGlRgbS3tcDxt1 || format == kGlRgbaS3tcDxt1) ? 8 : 16;
        payload.padBaseToBlock = target.webGl;
        for (unsigned int level = 0; level < levelCount; ++level) {
            const std::uint8_t* data = nullptr;
            unsigned int width = 0, height = 0, size = 0;
            DdsLevel(image, level, data, width, height, size);
            AppendLevel(payload, width, height, data, size);
        }
        return payload;
    }

    payload.format = kGlRgba;
    payload.type = kGlUnsignedByte;
    payload.internalFormat = kGlRgba8;
    payload.texelOrBlockBytes = 4;

    if (image.is_compressed()) {
        // Safari/iOS and most Android GPUs: no S3TC. Decode the blocks to RGBA8 here rather
        // than dropping the body to the fallback checkerboard (see BlockCompression.h).
        if (!BlockCompression::IsDecodableS3tcFormat(format)) {
            throw std::runtime_error("Compressed textures are not supported by this GPU/browser and format 0x" +
                                     std::to_string(static_cast<unsigned>(format)) + " has no software decoder for " +
                                     path);
        }
        for (unsigned int level = 0; level < levelCount; ++level) {
            const std::uint8_t* blocks = nullptr;
            unsigned int width = 0, height = 0, size = 0;
            DdsLevel(image, level, blocks, width, height, size);
            const std::vector<std::uint8_t> rgba =
                BlockCompression::DecodeS3tcToRgba8(format, blocks, size, width, height);
            if (rgba.empty()) {
                if (level == 0) {
                    throw std::runtime_error("Software DXT decode failed for " + path);
                }
                break; // a short chain is survivable
            }
            AppendLevel(payload, width, height, rgba.data(), rgba.size());
        }
        payload.allowGenerateMipmap = payload.levels.size() == 1;
        return payload;
    }

    // Uncompressed DDS: rows are tightly packed (nv_dds reads them so). Native core profile
    // takes the file's channel order (BGR/BGRA) with a sized internal format; WebGL 2 has no
    // BGR, nv_dds already swizzled those to RGB(A) there, and RGB is widened to RGBA so
    // glGenerateMipmap accepts it.
    const unsigned int components = image.get_components();
    const bool widen = target.webGl && components == 3;
    if (components == 1) {
        payload.internalFormat = kGlR8;
        payload.format = kGlRed;
        payload.texelOrBlockBytes = 1;
    } else if (components == 3 && !widen) {
        payload.internalFormat = kGlRgb8;
        payload.format = format;
        payload.texelOrBlockBytes = 3;
    } else if (!widen) {
        payload.format = format;
    }
    for (unsigned int level = 0; level < levelCount; ++level) {
        const std::uint8_t* data = nullptr;
        unsigned int width = 0, height = 0, size = 0;
        DdsLevel(image, level, data, width, height, size);
        if (!widen) {
            AppendLevel(payload, width, height, data, size);
            continue;
        }
        const std::size_t pixels = static_cast<std::size_t>(width) * height;
        if (size < pixels * 3) {
            throw std::runtime_error("DDS " + path + ": level " + std::to_string(level) + " is short");
        }
        std::vector<std::uint8_t> rgba(pixels * 4);
        for (std::size_t i = 0; i < pixels; ++i) {
            std::memcpy(&rgba[i * 4], data + i * 3, 3);
            rgba[i * 4 + 3] = 255;
        }
        AppendLevel(payload, width, height, rgba.data(), rgba.size());
    }
    payload.allowGenerateMipmap = payload.levels.size() == 1;
    return payload;
}

} // namespace

std::vector<std::uint8_t> ReadTextureFile(const std::string& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        throw std::runtime_error("cannot open " + path);
    }
    const std::streamoff size = stream.tellg();
    if (size <= 0) {
        throw std::runtime_error("empty file " + path);
    }
    stream.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!stream.read(reinterpret_cast<char*>(bytes.data()), size)) {
        throw std::runtime_error("short read on " + path);
    }
    return bytes;
}

TexturePayload DecodeTexture(const std::string& path, const std::vector<std::uint8_t>& bytes,
                             const TextureDecodeTarget& target) {
    TexturePayload payload =
        HasSuffix(path, ".ktx2") ? DecodeKtx2(path, bytes, target) : DecodeDds(path, bytes, target);
    if (payload.levels.empty() || payload.width == 0 || payload.height == 0) {
        throw std::runtime_error("no image levels in " + path);
    }
    return payload;
}
