#pragma once

#include "../Auxiliary_Modules/TextureFormatSupport.h"

#include <string>

namespace TexturePaths {
struct Pair {
    const char* low;
    const char* mid;
    const char* high;
};

/** Runtime equivalent of Pair for catalog-driven bodies, whose texture ids are data. */
struct Paths {
    std::string low;
    std::string mid;
    std::string high;
};

/**
 * Map a `.dds` asset path onto the texture pack selected at startup, so every call site
 * can keep spelling paths the way the assets are named on disk. Returns the path
 * unchanged when no pack is active (the default, and all native builds).
 *
 * Cube maps (the skybox) are deliberately not routed through here: the KTX2 reader is
 * 2D-only, so those stay on DDS.
 */
inline std::string Resolve(const std::string& ddsPath) {
    return TextureFormats::VariantPath(ddsPath);
}

/** Same layout the old SOLAR_TEXTURE_PAIR macro baked in, for a texture id like "Ceres_Diffuse". */
inline Paths ForTextureId(const std::string& textureId) {
    return {Resolve("resource/textures_low/" + textureId + "_Low.dds"),
            Resolve("resource/textures_mid/" + textureId + "_Mid.dds"),
            Resolve("resource/textures/" + textureId + ".dds")};
}

} // namespace TexturePaths
