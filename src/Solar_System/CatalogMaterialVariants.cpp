// CatalogMaterial's permutation bookkeeping. Kept apart from BindMaterial (CatalogMaterial.cpp)
// so it has no GL or Shader dependency and the unit tests can link it.
#include "CatalogMaterial.h"

namespace CatalogMaterial {

unsigned Variant(const Material& material, const Textures& textures) {
    unsigned variant = 0;
    if (material.hasNight && textures.night != 0) {
        variant |= kNightTexture;
    }
    if (material.hasSpecular && textures.specular != 0) {
        variant |= kSpecularMap;
    }
    if (material.hasClouds && textures.clouds != 0) {
        variant |= kClouds;
    }
    if (material.useSphereIntersect) {
        variant |= kSphereIntersect;
    }
    return variant;
}

std::vector<unsigned> PossibleVariants(const Material& material) {
    const unsigned full = Variant(material, Textures{1, 1, 1, 1, 1});
    const unsigned maps = full & (kNightTexture | kSpecularMap | kClouds);
    std::vector<unsigned> variants;
    // Every subset of `maps` (the standard submask walk), each with the fixed bits.
    for (unsigned subset = maps;; subset = (subset - 1) & maps) {
        variants.push_back(subset | (full & ~maps));
        if (subset == 0) {
            break;
        }
    }
    return variants;
}

std::vector<std::string> Defines(unsigned variant) {
    std::vector<std::string> defines;
    if (variant & kNightTexture) {
        defines.emplace_back("HAS_NIGHT_TEXTURE");
    }
    if (variant & kSpecularMap) {
        defines.emplace_back("HAS_SPECULAR_MAP");
    }
    if (variant & kClouds) {
        defines.emplace_back("HAS_CLOUDS");
    }
    if (variant & kSphereIntersect) {
        defines.emplace_back("USE_SPHERE_INTERSECT");
    }
    return defines;
}

} // namespace CatalogMaterial
