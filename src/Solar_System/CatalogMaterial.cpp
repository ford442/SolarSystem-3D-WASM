#include "CatalogMaterial.h"
#include "../gfx/TextureUnits.h"
#include "../Auxiliary_Modules/Shader.h"

namespace CatalogMaterial {

void BindMaterial(const Shader& shader, const Material& material, const Textures& textures) {
    const unsigned variant = Variant(material, textures);
    const bool hasSpecular = (variant & kSpecularMap) != 0;
    const bool hasNight = (variant & kNightTexture) != 0;
    const bool hasClouds = (variant & kClouds) != 0;

    shader.SetFloat("ambientFactor", material.ambientFactor);

    glBindTextureUnit(Unit(TextureUnit::Diffuse), textures.diffuse);
    glBindTextureUnit(Unit(TextureUnit::Normal), textures.normal);
    if (hasSpecular) {
        glBindTextureUnit(Unit(TextureUnit::Specular), textures.specular);
    }
    if (hasNight) {
        glBindTextureUnit(Unit(TextureUnit::Night), textures.night);
    }
    if (hasClouds) {
        glBindTextureUnit(Unit(TextureUnit::Clouds), textures.clouds);
    }
}

} // namespace CatalogMaterial
