#include "CatalogMaterial.h"
#include "../gfx/TextureUnits.h"
#include "../Auxiliary_Modules/Shader.h"

namespace CatalogMaterial {

void BindMaterial(const Shader& shader, const Material& material, const Textures& textures) {
    // A flag is only honoured when its map is actually resident; the shader would otherwise
    // sample whatever the previous draw left on that unit.
    const bool hasSpecular = material.hasSpecular && textures.specular != 0;
    const bool hasNight = material.hasNight && textures.night != 0;
    const bool hasClouds = material.hasClouds && textures.clouds != 0;

    shader.SetBool("hasNightTexture", hasNight);
    shader.SetBool("hasSpecularMap", hasSpecular);
    shader.SetBool("hasSpecular", hasSpecular);
    shader.SetBool("hasClouds", hasClouds);
    shader.SetBool("isUseSphereIntersect", material.useSphereIntersect);
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
