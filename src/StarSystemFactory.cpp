#include "Application.h"
#include "QualitySettings.h"
#include "Solar_System/CatalogClouds.h"
#include "Solar_System/CatalogSatellite.h"
#include "Solar_System/SystemVisuals.h"
#include "Solar_System/AtmosphereModel.h"
#include "Auxiliary_Modules/FloatLutTexture.h"
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
    std::vector<TextureImage2D> diffuses;
    diffuses.push_back(LoadLodTexture(entry.lod.diffuse));
    return SatelliteInfo(model, entry.earthRadiusScale, shader, std::move(diffuses),
                         LoadLodTexture(entry.lod.normal), entry.displayNameEn, entry.displayNameRu,
                         LoadLodTexture(entry.lod.specular));
}

// Physical rows get their baked LUTs whatever the current preset, so switching to
// Medium/Full later needs no reload. Any problem leaves the shell on the O'Neil path.
void LoadAtmosphereLuts(Atmosphere& atmosphere) {
    const BodyCatalog::AtmosphereRow& row = atmosphere.GetRow();
    if (!row.physical.enabled) {
        return;
    }
    auto transmittance = LoadRgba16fLut(AtmosphereModel::TransmittanceLutPath(row.bodyId),
                                        AtmosphereModel::kTransmittanceWidth, AtmosphereModel::kTransmittanceHeight);
    auto multiScattering = LoadRgba16fLut(AtmosphereModel::MultiScatteringLutPath(row.bodyId),
                                          AtmosphereModel::kMultiScatteringSize, AtmosphereModel::kMultiScatteringSize);
    for (auto* lut : {&transmittance, &multiScattering}) {
        if (*lut && (*lut)->mappingVersion != AtmosphereModel::kLutMappingVersion) {
            std::cout << "[Atmosphere] " << row.bodyId << ": LUT mapping version " << (*lut)->mappingVersion
                      << " != " << AtmosphereModel::kLutMappingVersion << " (rebake); using the O'Neil shell" << std::endl;
            lut->reset();
        }
    }
    if (!transmittance || !multiScattering) {
        return;
    }
    const std::string expected = AtmosphereModel::ParamsHashHex(row.physical);
    if (transmittance->paramsHash != expected) {
        // Still usable (same mapping), just baked from other numbers; CI's
        // AtmosphereLutsFresh test is what keeps this from shipping.
        std::cout << "[Atmosphere] " << row.bodyId << ": LUTs were baked from parameters "
                  << transmittance->paramsHash << ", the catalog is " << expected
                  << " — rerun atmosphere_lut_baker" << std::endl;
    }
    atmosphere.SetLuts(std::move(transmittance->texture), std::move(multiScattering->texture));
}

RenderableAtmosphere MakeAtmosphere(const MeshHolder& sphereModel, Shader& atmosphereShader,
                                    const BodyCatalog::AtmosphereRow& row,
                                    const std::shared_ptr<SpaceObject>& parent,
                                    float parentRadius, float parentEarthSize) {
    RenderableAtmosphere renderable;
    renderable.atmosphere = std::make_unique<Atmosphere>(sphereModel, atmosphereShader, row, parent, parentRadius);
    renderable.parentEarthSizeCoefficient = parentEarthSize;
    LoadAtmosphereLuts(*renderable.atmosphere);
    return renderable;
}

} // namespace

void Application::InitStarSystem() {
    _sphereModel = std::make_unique<MeshHolder>("resource/models/sphere.obj");

    StarInfo sunInfo(*_sphereModel, _renderer.StarShader(), _renderer.StarGlowShader(), TextureImage2D(TexturePaths::Resolve("resource/textures_low/Star_Spectrum_Low.dds")),
                     _renderer.GetStarTuning().temperatureKelvin, 696342.0, glm::vec3(0.99607843, 0.890196078, 0.725490196), L"Sun", L"Солнце");
    _sun = make_shared<Sun>(std::move(sunInfo));
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

    PlanetInfo info = MakePlanetInfo(sphereModel, *primary, _renderer.PlanetShader());
    shared_ptr<Planet> planet = make_shared<CatalogBody>(std::move(info), _sun, *primary);
    planet->SetMagneticField(
        MagneticFieldCatalog::IntrinsicParamsForBody(static_cast<OrbitLayout::Body>(primary->index)));

    vector<shared_ptr<Satellite>> satellites;
    // Moons whose catalog row carries render.atmosphere (Titan). Their shells render with
    // this component, after the primary's.
    vector<pair<shared_ptr<Satellite>, const BodyCatalog::AtmosphereRow*>> satelliteAtmospheres;
    for (const BodyCatalog::Entry& entry : BodyCatalog::kEntries) {
        if (entry.kind != BodyCatalog::Kind::Satellite || !entry.parentId ||
            !BodyCatalog::StrEq(entry.parentId, primary->id)) {
            continue;
        }
        if (entry.meshPath) {
            MeshHolder mesh(entry.meshPath);
            SatelliteInfo satInfo = MakeSatelliteInfo(mesh, entry, _renderer.PlanetShader());
            auto sat = make_shared<CatalogSatellite>(std::move(satInfo), planet, entry);
            if (const auto* row = BodyCatalog::FindAtmosphere(entry.id)) {
                satelliteAtmospheres.emplace_back(sat, row);
            }
            satellites.push_back(std::move(sat));
        } else {
            SatelliteInfo satInfo = MakeSatelliteInfo(sphereModel, entry, _renderer.PlanetShader());
            auto sat = make_shared<CatalogSatellite>(std::move(satInfo), planet, entry);
            if (const auto* row = BodyCatalog::FindAtmosphere(entry.id)) {
                satelliteAtmospheres.emplace_back(sat, row);
            }
            satellites.push_back(std::move(sat));
        }
    }

    RenderableSceneComponent component;
    if (const auto* row = BodyCatalog::FindAtmosphere(primary->id)) {
        component.atmospheres.push_back(MakeAtmosphere(sphereModel, _renderer.AtmosphereShader(), *row, planet,
                                                       planet->GetRadius(), planet->GetEarthSizeCoefficient()));
    }
    for (const auto& [satellite, row] : satelliteAtmospheres) {
        component.atmospheres.push_back(MakeAtmosphere(sphereModel, _renderer.AtmosphereShader(), *row, satellite,
                                                       satellite->GetRadius(), satellite->GetEarthSizeCoefficient()));
    }

    if (primary->hasCloudLayer && primary->cloudLayer.diffuse) {
        const TexturePaths::Paths cloudDiffuse = TexturePaths::ForTextureId(primary->cloudLayer.diffuse);
        const TexturePaths::Paths cloudNormal = TexturePaths::ForTextureId(
            primary->cloudLayer.normal ? primary->cloudLayer.normal : primary->cloudLayer.diffuse);
        CloudsInfo cloudsInfo(sphereModel, _renderer.CloudsShader(), primary->cloudLayer.scaleFactor,
                              TextureImage2D(GetTexturePath(cloudDiffuse.low, cloudDiffuse.high)),
                              TextureImage2D(GetTexturePath(cloudNormal.low, cloudNormal.high)));
        component.clouds = make_unique<CatalogClouds>(std::move(cloudsInfo), planet, *primary);
    }

    if (primary->hasRings) {
        if (const auto* ringSpec = SystemVisuals::FindRing(primary->id)) {
            MeshHolder ringModel(ringSpec->modelPath);
            const TexturePaths::Paths ringTex = TexturePaths::ForTextureId(ringSpec->textureId);
            PlanetaryRingInfo ringInfo(ringModel, ringSpec->innerRadius, ringSpec->outerRadius,
                                       _renderer.PlanetShader(),
                                       TextureImage2D(GetTexturePath(ringTex.low, ringTex.high)));
            if (BodyCatalog::StrEq(primary->id, "saturn")) {
                component.planetaryRing = make_unique<SaturnRing>(std::move(ringInfo), planet);
            } else if (BodyCatalog::StrEq(primary->id, "uranus")) {
                component.planetaryRing = make_unique<UranusRing>(std::move(ringInfo), planet);
            }
        }
    }

    const float lightFar = primary->lightFarUsesPlanetDistance
                               ? glm::length(_sun->GetPosition() - planet->GetPosition()) + 50.0f
                               : _camera.GetFar();
    const glm::mat4 lightProjection =
        glm::ortho(-planet->GetRadius() * 3.0f, planet->GetRadius() * 3.0f,
                   -planet->GetRadius() * 3.0f, planet->GetRadius() * 3.0f, _camera.GetNear(), lightFar);
    // Same frame as the per-frame matrix in ProcessSceneComponentsRendering, which replaces
    // this one before the first shadow pass.
    const glm::mat4 lightView(glm::lookAt(glm::dvec3(_sun->GetPosition()), glm::dvec3(planet->GetPosition()),
                                          glm::dvec3(0.0, 1.0, 0.0)));

    component.lightSpaceMatrix = lightProjection * lightView;
    component.planet = std::move(planet);
    component.satellites = std::move(satellites);
    _renderableSceneComponents.push_back(std::move(component));
}
