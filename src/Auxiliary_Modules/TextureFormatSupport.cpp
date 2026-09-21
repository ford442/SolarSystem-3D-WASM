#include "TextureFormatSupport.h"

#include <utility>

namespace TextureFormats {
namespace {

// ---- Vulkan core format enum values (VkFormat), spec-fixed ----------------------
constexpr std::uint32_t kVkR8G8B8Unorm       = 23;
constexpr std::uint32_t kVkR8G8B8Srgb        = 29;
constexpr std::uint32_t kVkR8G8B8A8Unorm     = 37;
constexpr std::uint32_t kVkR8G8B8A8Srgb      = 43;
constexpr std::uint32_t kVkBc1RgbUnorm       = 131;
constexpr std::uint32_t kVkBc1RgbSrgb        = 132;
constexpr std::uint32_t kVkBc1RgbaUnorm      = 133;
constexpr std::uint32_t kVkBc1RgbaSrgb       = 134;
constexpr std::uint32_t kVkBc2Unorm          = 135;
constexpr std::uint32_t kVkBc2Srgb           = 136;
constexpr std::uint32_t kVkBc3Unorm          = 137;
constexpr std::uint32_t kVkBc3Srgb           = 138;
constexpr std::uint32_t kVkBc7Unorm          = 145;
constexpr std::uint32_t kVkBc7Srgb           = 146;
constexpr std::uint32_t kVkEtc2Rgb8Unorm     = 147;
constexpr std::uint32_t kVkEtc2Rgb8Srgb      = 148;
constexpr std::uint32_t kVkEtc2Rgb8A1Unorm   = 149;
constexpr std::uint32_t kVkEtc2Rgb8A1Srgb    = 150;
constexpr std::uint32_t kVkEtc2Rgba8Unorm    = 151;
constexpr std::uint32_t kVkEtc2Rgba8Srgb     = 152;
constexpr std::uint32_t kVkAstcFirst         = 157;  // VK_FORMAT_ASTC_4x4_UNORM_BLOCK
constexpr std::uint32_t kVkAstcLast          = 184;  // VK_FORMAT_ASTC_12x12_SRGB_BLOCK

// ---- GL enum values, spec-fixed -------------------------------------------------
constexpr std::uint32_t kGlRgb8                       = 0x8051;
constexpr std::uint32_t kGlRgba8                      = 0x8058;
constexpr std::uint32_t kGlSrgb8                      = 0x8C41;
constexpr std::uint32_t kGlSrgb8Alpha8                = 0x8C43;
constexpr std::uint32_t kGlRgb                        = 0x1907;
constexpr std::uint32_t kGlRgba                       = 0x1908;
constexpr std::uint32_t kGlUnsignedByte               = 0x1401;
constexpr std::uint32_t kGlRgbS3tcDxt1                = 0x83F0;
constexpr std::uint32_t kGlRgbaS3tcDxt1               = 0x83F1;
constexpr std::uint32_t kGlRgbaS3tcDxt3               = 0x83F2;
constexpr std::uint32_t kGlRgbaS3tcDxt5               = 0x83F3;
constexpr std::uint32_t kGlSrgbS3tcDxt1               = 0x8C4C;
constexpr std::uint32_t kGlSrgbAlphaS3tcDxt1          = 0x8C4D;
constexpr std::uint32_t kGlSrgbAlphaS3tcDxt3          = 0x8C4E;
constexpr std::uint32_t kGlSrgbAlphaS3tcDxt5          = 0x8C4F;
constexpr std::uint32_t kGlRgbaBptcUnorm              = 0x8E8C;
constexpr std::uint32_t kGlSrgbAlphaBptcUnorm         = 0x8E8D;
constexpr std::uint32_t kGlRgb8Etc2                   = 0x9274;
constexpr std::uint32_t kGlSrgb8Etc2                  = 0x9275;
constexpr std::uint32_t kGlRgb8PunchthroughAlpha1Etc2 = 0x9276;
constexpr std::uint32_t kGlSrgb8PunchthroughAlpha1Etc2= 0x9277;
constexpr std::uint32_t kGlRgba8Etc2Eac               = 0x9278;
constexpr std::uint32_t kGlSrgb8Alpha8Etc2Eac         = 0x9279;
constexpr std::uint32_t kGlAstcUnormFirst             = 0x93B0;  // COMPRESSED_RGBA_ASTC_4x4_KHR
constexpr std::uint32_t kGlAstcSrgbFirst              = 0x93D0;  // ..._SRGB8_ALPHA8_ASTC_4x4_KHR

// The 14 ASTC block footprints, in the order the VkFormat and GL enums both use.
constexpr std::uint32_t kAstcBlockSizes[14][2] = {
    {4, 4},  {5, 4},  {5, 5},  {6, 5},  {6, 6},  {8, 5},   {8, 6},
    {8, 8}, {10, 5}, {10, 6}, {10, 8}, {10, 10}, {12, 10}, {12, 12},
};

bool IsAstc(std::uint32_t vkFormat) {
    return vkFormat >= kVkAstcFirst && vkFormat <= kVkAstcLast;
}

std::string& MutableTexturePack() {
    static std::string pack;
    return pack;
}

} // namespace

std::uint32_t GlInternalFormatFromVkFormat(std::uint32_t vkFormat) {
    switch (vkFormat) {
        case kVkR8G8B8Unorm:      return kGlRgb8;
        case kVkR8G8B8Srgb:       return kGlSrgb8;
        case kVkR8G8B8A8Unorm:    return kGlRgba8;
        case kVkR8G8B8A8Srgb:     return kGlSrgb8Alpha8;
        case kVkBc1RgbUnorm:      return kGlRgbS3tcDxt1;
        case kVkBc1RgbSrgb:       return kGlSrgbS3tcDxt1;
        case kVkBc1RgbaUnorm:     return kGlRgbaS3tcDxt1;
        case kVkBc1RgbaSrgb:      return kGlSrgbAlphaS3tcDxt1;
        case kVkBc2Unorm:         return kGlRgbaS3tcDxt3;
        case kVkBc2Srgb:          return kGlSrgbAlphaS3tcDxt3;
        case kVkBc3Unorm:         return kGlRgbaS3tcDxt5;
        case kVkBc3Srgb:          return kGlSrgbAlphaS3tcDxt5;
        case kVkBc7Unorm:         return kGlRgbaBptcUnorm;
        case kVkBc7Srgb:          return kGlSrgbAlphaBptcUnorm;
        case kVkEtc2Rgb8Unorm:    return kGlRgb8Etc2;
        case kVkEtc2Rgb8Srgb:     return kGlSrgb8Etc2;
        case kVkEtc2Rgb8A1Unorm:  return kGlRgb8PunchthroughAlpha1Etc2;
        case kVkEtc2Rgb8A1Srgb:   return kGlSrgb8PunchthroughAlpha1Etc2;
        case kVkEtc2Rgba8Unorm:   return kGlRgba8Etc2Eac;
        case kVkEtc2Rgba8Srgb:    return kGlSrgb8Alpha8Etc2Eac;
        default: break;
    }

    if (IsAstc(vkFormat)) {
        // ASTC formats alternate UNORM, SRGB per footprint in both enums.
        const std::uint32_t index = (vkFormat - kVkAstcFirst) / 2;
        const bool srgb = ((vkFormat - kVkAstcFirst) % 2) != 0;
        return (srgb ? kGlAstcSrgbFirst : kGlAstcUnormFirst) + index;
    }

    return 0;
}

BlockLayout LayoutFromVkFormat(std::uint32_t vkFormat) {
    BlockLayout layout;

    switch (vkFormat) {
        case kVkR8G8B8Unorm:
        case kVkR8G8B8Srgb:
            return {1, 1, 3, false};
        case kVkR8G8B8A8Unorm:
        case kVkR8G8B8A8Srgb:
            return {1, 1, 4, false};
        // 8 bytes per 4x4 block: BC1 and the ETC2 formats with no (or 1-bit) alpha.
        case kVkBc1RgbUnorm:
        case kVkBc1RgbSrgb:
        case kVkBc1RgbaUnorm:
        case kVkBc1RgbaSrgb:
        case kVkEtc2Rgb8Unorm:
        case kVkEtc2Rgb8Srgb:
        case kVkEtc2Rgb8A1Unorm:
        case kVkEtc2Rgb8A1Srgb:
            return {4, 4, 8, true};
        // 16 bytes per 4x4 block: BC2/BC3/BC7 and ETC2+EAC.
        case kVkBc2Unorm:
        case kVkBc2Srgb:
        case kVkBc3Unorm:
        case kVkBc3Srgb:
        case kVkBc7Unorm:
        case kVkBc7Srgb:
        case kVkEtc2Rgba8Unorm:
        case kVkEtc2Rgba8Srgb:
            return {4, 4, 16, true};
        default:
            break;
    }

    if (IsAstc(vkFormat)) {
        const std::uint32_t index = (vkFormat - kVkAstcFirst) / 2;
        // Every ASTC block is 128 bits regardless of footprint.
        return {kAstcBlockSizes[index][0], kAstcBlockSizes[index][1], 16, true};
    }

    return layout;
}

UncompressedUpload UncompressedUploadFromVkFormat(std::uint32_t vkFormat) {
    switch (vkFormat) {
        case kVkR8G8B8Unorm:   return {kGlRgb8,        kGlRgb,  kGlUnsignedByte};
        case kVkR8G8B8Srgb:    return {kGlSrgb8,       kGlRgb,  kGlUnsignedByte};
        case kVkR8G8B8A8Unorm: return {kGlRgba8,       kGlRgba, kGlUnsignedByte};
        case kVkR8G8B8A8Srgb:  return {kGlSrgb8Alpha8, kGlRgba, kGlUnsignedByte};
        default:               return {};
    }
}

std::size_t ExpectedLevelBytes(std::uint32_t vkFormat, std::uint32_t width, std::uint32_t height) {
    const BlockLayout layout = LayoutFromVkFormat(vkFormat);
    if (!layout.IsValid() || width == 0 || height == 0) {
        return 0;
    }
    const std::size_t blocksX = (static_cast<std::size_t>(width) + layout.blockWidth - 1) / layout.blockWidth;
    const std::size_t blocksY = (static_cast<std::size_t>(height) + layout.blockHeight - 1) / layout.blockHeight;
    return blocksX * blocksY * layout.blockBytes;
}

bool IsFormatSupported(std::uint32_t vkFormat, const FormatCapabilities& caps) {
    const BlockLayout layout = LayoutFromVkFormat(vkFormat);
    if (!layout.IsValid()) {
        return false;
    }
    if (!layout.compressed) {
        return true;  // RGB8/RGBA8 is always uploadable.
    }
    if (IsAstc(vkFormat)) {
        return caps.astc;
    }
    switch (vkFormat) {
        case kVkBc7Unorm:
        case kVkBc7Srgb:
            return caps.bptc;
        case kVkEtc2Rgb8Unorm:
        case kVkEtc2Rgb8Srgb:
        case kVkEtc2Rgb8A1Unorm:
        case kVkEtc2Rgb8A1Srgb:
        case kVkEtc2Rgba8Unorm:
        case kVkEtc2Rgba8Srgb:
            return caps.etc2;
        default:
            return caps.s3tc;  // the BC1/BC2/BC3 rows above
    }
}

const char* PreferredPackName(const FormatCapabilities& caps) {
    if (caps.bptc) return "bc7";
    if (caps.s3tc) return "bc3";
    if (caps.astc) return "astc";
    if (caps.etc2) return "etc2";
    return "rgba8";
}

void SetTexturePack(std::string packName) {
    MutableTexturePack() = std::move(packName);
}

const std::string& GetTexturePack() {
    return MutableTexturePack();
}

std::string VariantPath(const std::string& ddsPath) {
    const std::string& pack = GetTexturePack();
    if (pack.empty()) {
        return ddsPath;
    }

    static const std::string kDdsSuffix = ".dds";
    if (ddsPath.size() <= kDdsSuffix.size() ||
        ddsPath.compare(ddsPath.size() - kDdsSuffix.size(), kDdsSuffix.size(), kDdsSuffix) != 0) {
        return ddsPath;
    }

    const std::size_t slash = ddsPath.find_last_of('/');
    const std::string directory = (slash == std::string::npos) ? std::string() : ddsPath.substr(0, slash + 1);
    const std::string stem = ddsPath.substr(directory.size(), ddsPath.size() - directory.size() - kDdsSuffix.size());
    return directory + pack + "/" + stem + ".ktx2";
}

} // namespace TextureFormats
