#pragma once

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

/** Same layout the old SOLAR_TEXTURE_PAIR macro baked in, for a texture id like "Ceres_Diffuse". */
inline Paths ForTextureId(const std::string& textureId) {
    return {"resource/textures_low/" + textureId + "_Low.dds",
            "resource/textures_mid/" + textureId + "_Mid.dds",
            "resource/textures/" + textureId + ".dds"};
}

} // namespace TexturePaths
