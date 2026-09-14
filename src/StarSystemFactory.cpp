#include "Application.h"
#include "QualitySettings.h"
#include "Solar_System/CatalogClouds.h"
#include "Solar_System/CatalogSatellite.h"
#include "Solar_System/SystemVisuals.h"
#include <iostream>
#include <unordered_set>

using namespace std;

namespace {

TextureImage2D LoadLodTexture(const char* textureId) {
    if (!textureId || textureId[0] == '\0') {
        return TextureImage2D();
    }
    const TexturePaths::Paths paths = TexturePaths::ForTextureId(textureId);
    return TextureImage2D(GetTexturePath(paths.low, paths.high));
}

PlanetInfo MakePlanetInfo(const MeshHolder& sphereModel, const BodyCatalog::Entry& entry, Shader& shader) {
    std::vector<TextureImage2D> diffuses;
    diffuses.push_back(LoadLodTexture(entry.lod.diffuse));
    if (entry.lod.clouds) {
        diffuses.push_back(LoadLodTexture(entry.lod.clouds));
    }
    if (entry.lod.night) {
        diffuses.push_back(LoadLodTexture(entry.lod.night));
    }
    return PlanetInfo(sphereModel, entry.earthRadiusScale, shader, std::move(diffuses),
                      LoadLodTexture(entry.lod.normal), entry.displayNameEn, entry.displayNameRu,
                      LoadLodTexture(entry.lod.specular));
}

SatelliteInfo MakeSatelliteInfo(const MeshHolder& model, const BodyCatalog::Entry& entry, Shader& shader) {
    return SatelliteInfo(model, entry.earthRadiusScale, shader, {LoadLodTexture(entry.lod.diffuse)},
                         LoadLodTexture(entry.lod.normal), entry.displayNameEn, entry.displayNameRu,
                         LoadLodTexture(entry.lod.specular));
}

RenderableAtmosphere MakeAtmosphere(const MeshHolder& sphereModel, Shader& atmosphereShader,
                                    const SystemVisuals::AtmosphereSpec& spec,
                                    const std::shared_ptr<SpaceObject>& parent,
                                    float parentRadius, float parentEarthSize, bool toneMapping) {
    const float inner = spec.innerRadiusMinusEpsilon ? parentRadius - 0.00007f : parentRadius;
    AtmosphereInfo info(sphereModel, atmosphereShader, spec.scaleFactor, spec.color, inner,
                        spec.outerRadius, spec.mieTint);
    RenderableAtmosphere renderable;
    renderable.atmosphere = std::make_unique<Atmosphere>(info, parent);
    renderable.hScaleFactor = spec.hScaleFactor;
    renderable.parentEarthSizeCoefficient = parentEarthSize;
    renderable.isUseToneMapping = toneMapping;
    return renderable;
}

} // namespace

void Application::InitStarSystem() {
    _sphereModel = std::make_unique<MeshHolder>("resource/models/sphere.obj");

    _starGlowShader = make_unique<Shader>("resource/shaders/starGlow.vs", "resource/shaders/starGlow.fs");
    StarInfo sunInfo(*_sphereModel, *_mainStarShader, *_starGlowShader, TextureImage2D("resource/textures_low/Star_Spectrum_Low.dds"),
                     _starTemperatureInKelvin, 696342.0, glm::vec3(0.99607843, 0.890196078, 0.725490196), L"Sun", L"Солнце");
    _sun = make_shared<Sun>(sunInfo);
    _sun->SetMagneticField(MagneticFieldCatalog::IntrinsicParamsForBody(OrbitLayout::Body::Sun));

#ifdef __EMSCRIPTEN__
    LoadPlanetSystemManifests();
#else
    std::unordered_set<std::string> initialized;
    for (const BodyCatalog::Entry& entry : BodyCatalog::kEntries) {
        if (entry.kind == BodyCatalog::Kind::Satellite) {
            continue;
        }
        if (!entry.initTag || initialized.count(entry.initTag)) {
            continue;
        }
        initialized.insert(entry.initTag);
        InitCatalogSystem(*_sphereModel, entry.initTag);
    }
#endif
}

void Application::InitCatalogBody(const MeshHolder& sphereModel, OrbitLayout::Body body) {
    const BodyCatalog::Entry* entry = BodyCatalog::FindByIndex(static_cast<int>(body));
    if (!entry) {
        std::cerr << "[CatalogBody] No render descriptor for focus index "
                  << static_cast<int>(body) << " — check planets.catalog.json" << std::endl;
        return;
    }
    InitCatalogSystem(sphereModel, entry->initTag);
}

void Application::InitCatalogSystem(const MeshHolder& sphereModel, const std::string& initTag) {
    const BodyCatalog::Entry* primary = BodyCatalog::FindPrimaryByInitTag(initTag.c_str());
    if (!primary) {
        std::cerr << "[CatalogBody] No primary body for initTag \"" << initTag
                  << "\" — check planets.catalog.json" << std::endl;
        return;
    }

    PlanetInfo info = MakePlanetInfo(sphereModel, *primary, *_mainPlanetShader);
    shared_ptr<Planet> planet = make_shared<CatalogBody>(info, _sun, *primary);
    planet->SetMagneticField(
        MagneticFieldCatalog::IntrinsicParamsForBody(static_cast<OrbitLayout::Body>(primary->index)));

    vector<shared_ptr<Satellite>> satellites;
    shared_ptr<Satellite> titan;
    for (const BodyCatalog::Entry& entry : BodyCatalog::kEntries) {
        if (entry.kind != BodyCatalog::Kind::Satellite || !entry.parentId ||
            !BodyCatalog::StrEq(entry.parentId, primary->id)) {
            continue;
        }
        if (entry.meshPath) {
            MeshHolder mesh(entry.meshPath);
            SatelliteInfo satInfo = MakeSatelliteInfo(mesh, entry, *_mainPlanetShader);
            auto sat = make_shared<CatalogSatellite>(satInfo, planet, entry);
            if (BodyCatalog::StrEq(entry.id, "titan")) {
                titan = sat;
            }
            satellites.push_back(std::move(sat));
        } else {
            SatelliteInfo satInfo = MakeSatelliteInfo(sphereModel, entry, *_mainPlanetShader);
            auto sat = make_shared<CatalogSatellite>(satInfo, planet, entry);
            if (BodyCatalog::StrEq(entry.id, "titan")) {
                titan = sat;
            }
            satellites.push_back(std::move(sat));
        }
    }

    RenderableSceneComponent component;
    if (primary->hasAtmosphere) {
        if (const auto* spec = SystemVisuals::FindAtmosphere(primary->id)) {
            component.atmospheres.push_back(
                MakeAtmosphere(sphereModel, *_mainAtmosphereShader, *spec, planet,
                               planet->GetRadius(), planet->GetEarthSizeCoefficient(),
                               primary->atmosphereToneMapping));
        }
    }
    if (titan) {
        if (const auto* spec = SystemVisuals::FindAtmosphere("titan")) {
            component.atmospheres.push_back(
                MakeAtmosphere(sphereModel, *_mainAtmosphereShader, *spec, titan,
                               titan->GetRadius(), titan->GetEarthSizeCoefficient(), false));
        }
    }

    if (primary->hasCloudLayer && primary->cloudLayer.diffuse) {
        const TexturePaths::Paths cloudDiffuse = TexturePaths::ForTextureId(primary->cloudLayer.diffuse);
        const TexturePaths::Paths cloudNormal = TexturePaths::ForTextureId(
            primary->cloudLayer.normal ? primary->cloudLayer.normal : primary->cloudLayer.diffuse);
        CloudsInfo cloudsInfo(sphereModel, *_mainCloudsShader, primary->cloudLayer.scaleFactor,
                              TextureImage2D(GetTexturePath(cloudDiffuse.low, cloudDiffuse.high)),
                              TextureImage2D(GetTexturePath(cloudNormal.low, cloudNormal.high)));
        component.clouds = make_unique<CatalogClouds>(cloudsInfo, planet, *primary);
    }

    if (primary->hasRings) {
        if (const auto* ringSpec = SystemVisuals::FindRing(primary->id)) {
            MeshHolder ringModel(ringSpec->modelPath);
            const TexturePaths::Paths ringTex = TexturePaths::ForTextureId(ringSpec->textureId);
            PlanetaryRingInfo ringInfo(ringModel, ringSpec->innerRadius, ringSpec->outerRadius,
                                       *_mainPlanetShader,
                                       TextureImage2D(GetTexturePath(ringTex.low, ringTex.high)));
            if (BodyCatalog::StrEq(primary->id, "saturn")) {
                component.planetaryRing = make_unique<SaturnRing>(ringInfo, planet);
            } else if (BodyCatalog::StrEq(primary->id, "uranus")) {
                component.planetaryRing = make_unique<UranusRing>(ringInfo, planet);
            }
        }
    }

    const float lightFar = primary->lightFarUsesPlanetDistance
                               ? glm::length(_sun->GetPosition() - planet->GetPosition()) + 50.0f
                               : _camera.GetFar();
    const glm::mat4 lightProjection =
        glm::ortho(-planet->GetRadius() * 3.0f, planet->GetRadius() * 3.0f,
                   -planet->GetRadius() * 3.0f, planet->GetRadius() * 3.0f, _camera.GetNear(), lightFar);
    const glm::mat4 lightView =
        glm::lookAt(_sun->GetPosition(), planet->GetPosition() - _sun->GetPosition(), glm::vec3(0.0, 1.0, 0.0));

    component.lightSpaceMatrix = lightProjection * lightView;
    component.planet = move(planet);
    component.satellites = move(satellites);
    _renderableSceneComponents.push_back(move(component));
}
