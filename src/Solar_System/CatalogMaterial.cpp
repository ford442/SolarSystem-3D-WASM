#include "CatalogMaterial.h"
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

    shader.SetInt("mainDiffuseTexture", kDiffuseUnit);
    shader.SetInt("normalMap", kNormalUnit);
    shader.SetInt("specularMap", kSpecularUnit);
    shader.SetInt("nightTexture", kNightUnit);
    shader.SetInt("cloudTexture", kCloudUnit);

    glBindTextureUnit(kDiffuseUnit, textures.diffuse);
    glBindTextureUnit(kNormalUnit, textures.normal);
    if (hasSpecular) {
        glBindTextureUnit(kSpecularUnit, textures.specular);
    }
    if (hasNight) {
        glBindTextureUnit(kNightUnit, textures.night);
    }
    if (hasClouds) {
        glBindTextureUnit(kCloudUnit, textures.clouds);
    }
}

} // namespace CatalogMaterial
