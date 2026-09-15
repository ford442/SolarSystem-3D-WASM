#ifndef SOLARSYSTEM_SATELLITE_H
#define SOLARSYSTEM_SATELLITE_H
#include "SpaceObject.h"
#include "Star.h"
#include "TexturePaths.h"
#include "../Auxiliary_Modules/TextureImage2D.h"
#include "../Auxiliary_Modules/TextureLODController.h"
#include <vector>
#include <memory>

struct SatelliteInfo {
    MeshHolder satelliteModel;
    float earthSizeCoefficient;
    const Shader* satelliteShader;
    std::vector<TextureImage2D> diffuseTextures;
    TextureImage2D normalMap;
    TextureImage2D specularTexture;
    std::wstring engName;
    std::wstring otherLangName;

    explicit SatelliteInfo(MeshHolder model, float earthSizeCoefficient, const Shader& shader, std::vector<TextureImage2D> diffuses, const TextureImage2D& normalMap,
                       std::wstring engName = L"", std::wstring otherLangName = L"", const TextureImage2D& specular = TextureImage2D()) :
                       satelliteModel(std::move(model)), earthSizeCoefficient(earthSizeCoefficient), satelliteShader(&shader),
                       diffuseTextures(std::move(diffuses)), normalMap(normalMap), specularTexture(specular), engName(std::move(engName)),
                       otherLangName(std::move(otherLangName)) {}
};

class Satellite : public SpaceObject {
public:
    explicit Satellite(const SatelliteInfo& satelliteInfo, std::shared_ptr<SpaceObject> parent);
    std::shared_ptr<SpaceObject> GetParent() const;
    float GetRadius() const;
    float GetEarthSizeCoefficient() const;
    void LoadHighResIfClose(const glm::vec3& cameraPosition);
    virtual void AdjustToParent(float timeScale) = 0;

    /**
     * Whether this moon is currently placed from the ephemeris rather than from a circular
     * art orbit. Only an ephemeris-placed moon may cast an eclipse shadow: a circular moon
     * sits exactly in its parent's equatorial plane, so its shadow would strike every
     * single orbit instead of on the rare occasions a real one does.
     */
    virtual bool IsEphemerisPlaced() const { return false; }

    /**
     * Scene units per kilometre for this moon's orbit, or 0 when unknown.
     *
     * The scene draws bodies, moon orbits, and planet orbits at three different scales, so
     * "how big is this in scene units" has no single answer. This is the conversion for the
     * moon-orbit scale specifically, which is the frame eclipse geometry lives in — see
     * docs/ARCHITECTURE.md § 11.
     */
    virtual float OrbitSceneUnitsPerKm() const { return 0.0f; }

protected:
    std::shared_ptr<SpaceObject> _parent;
    float _radius = 2.0; // Radius of the earth 3d model in Blender
    float _earthSizeCoefficient;
    void ConfigureDiffuseLOD(TextureImage2D& diffuse, const std::string& lowPath,
                             const std::string& midPath, const std::string& highPath,
                             const std::string& label);

private:
    TextureLODController _diffuseLOD;
};

#endif //SOLARSYSTEM_SATELLITE_H
