#include "CatalogMaterial.h"
#include "BodyCatalog.h"
#include "../gfx/TextureUnits.h"
#include "../gfx/Shader.h"

namespace CatalogMaterial {

void BindMaterial(const Shader& shader, const Material& material, const Textures& textures) {
    const unsigned variant = Variant(material, textures);
    const bool hasSpecular = (variant & kSpecularMap) != 0;
    const bool hasNight = (variant & kNightTexture) != 0;
    const bool hasClouds = (variant & kClouds) != 0;

    shader.SetFloat("ambientFactor", material.ambientFactor);

    if (variant & kProcedural) {
        const BodyCatalog::ProceduralSurface& p = material.procedural;
        shader.SetInt("procMode", p.mode);
        shader.SetFloat("procSeed", p.seed);
        shader.SetVec3("procColor0", glm::vec3(p.colors[0].r, p.colors[0].g, p.colors[0].b));
        shader.SetVec3("procColor1", glm::vec3(p.colors[1].r, p.colors[1].g, p.colors[1].b));
        shader.SetVec3("procColor2", glm::vec3(p.colors[2].r, p.colors[2].g, p.colors[2].b));
        shader.SetFloat("procBands", p.bands);
        shader.SetFloat("procSeaLevel", p.seaLevel);
        shader.SetFloat("procPolarCap", p.polarCap);
        shader.SetFloat("procEmissive", p.emissive);
        return; // no maps to bind
    }

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
