#pragma once

// Runtime GPU texture-format selection, shared by the loader and the asset-path
// builder.
//
// The renderer used to assume DXT/S3TC everywhere: `TextureImage2D` loaded a DDS,
// and a GPU without WEBGL_compressed_texture_s3tc (Safari, iOS, most Android GPUs)
// fell through to the 4x4 checkerboard. This module is the single place that decides
// *which* GPU format a given GL context actually wants, and how an asset path maps
// onto the pack encoded in that format.
//
// GL-free by design (the GL enum values are spec-fixed and spelled out here) so the
// mapping and the path rules can be unit tested natively without a context.

#include <cstddef>
#include <cstdint>
#include <string>

namespace TextureFormats {

/** The compressed-texture extensions the loader cares about, as probed from the context. */
struct FormatCapabilities {
    bool s3tc = false;   // WEBGL_compressed_texture_s3tc / EXT_texture_compression_s3tc
    bool bptc = false;   // EXT_texture_compression_bptc (BC7)
    bool etc2 = false;   // WEBGL_compressed_texture_etc
    bool astc = false;   // WEBGL_compressed_texture_astc
};

/** Block geometry of a vkFormat. For uncompressed formats the block is 1x1. */
struct BlockLayout {
    std::uint32_t blockWidth = 0;
    std::uint32_t blockHeight = 0;
    std::uint32_t blockBytes = 0;
    bool compressed = false;

    bool IsValid() const { return blockWidth != 0 && blockHeight != 0 && blockBytes != 0; }
};

/** glTexImage2D arguments for an uncompressed vkFormat; `internalFormat == 0` if it has none. */
struct UncompressedUpload {
    std::uint32_t internalFormat = 0;
    std::uint32_t format = 0;
    std::uint32_t type = 0;
};

/** GL internal format for a vkFormat, or 0 when this loader has no mapping for it. */
std::uint32_t GlInternalFormatFromVkFormat(std::uint32_t vkFormat);

BlockLayout LayoutFromVkFormat(std::uint32_t vkFormat);

UncompressedUpload UncompressedUploadFromVkFormat(std::uint32_t vkFormat);

/** Bytes one mip level of w*h occupies, or 0 for an unmapped format. */
std::size_t ExpectedLevelBytes(std::uint32_t vkFormat, std::uint32_t width, std::uint32_t height);

/** True if `caps` exposes the extension family this vkFormat belongs to. */
bool IsFormatSupported(std::uint32_t vkFormat, const FormatCapabilities& caps);

/**
 * Name of the texture pack this GPU should download, in preference order:
 *
 *   "bc7"   BPTC — best quality, desktop GL 4.x and WebGL 2 with EXT_texture_compression_bptc
 *   "bc3"   S3TC/DXT5 — the historical desktop-Chrome path
 *   "astc"  iOS/Safari and modern Android
 *   "etc2"  older Android / WebGL 2 with WEBGL_compressed_texture_etc
 *   "rgba8" nothing compressed is available; ship uncompressed (CI, software GL)
 *
 * BC is tried before ASTC so desktop keeps a BC format rather than drifting onto an
 * emulated ASTC path, while a GPU with no BC at all still lands on a real compressed
 * format instead of the checkerboard.
 */
const char* PreferredPackName(const FormatCapabilities& caps);

/**
 * The pack this process loads from, or "" for the legacy `.dds` layout (the default,
 * so a deployment that has not published a KTX2 pack is unaffected). Set once at
 * startup from `window.__solarSystemTexturePack`; see Application::LoadCoreResources.
 */
void SetTexturePack(std::string packName);
const std::string& GetTexturePack();

/**
 * Map a legacy `.dds` asset path onto the active pack, e.g. with pack "astc"
 *   resource/textures_low/Ceres_Diffuse_Low.dds
 *     -> resource/textures_low/astc/Ceres_Diffuse_Low.ktx2
 * Returns `ddsPath` unchanged when no pack is active or the path is not a `.dds`.
 */
std::string VariantPath(const std::string& ddsPath);

} // namespace TextureFormats
