#ifndef SOLARSYSTEM_APPLICATION_TYPES_H
#define SOLARSYSTEM_APPLICATION_TYPES_H

#include "Auxiliary_Modules/AuxiliaryModules.h"
#include "Solar_System/SolarSystem.h"
#include <memory>
#include <vector>

struct RenderableAtmosphere {
    std::unique_ptr<Atmosphere> atmosphere;
    float hScaleFactor, parentEarthSizeCoefficient;
    bool isUseToneMapping = false;
};

struct RenderableSceneComponent {
    glm::mat4 lightSpaceMatrix;
    std::shared_ptr<Planet> planet;
    std::vector<std::shared_ptr<Satellite>> satellites;
    std::vector<RenderableAtmosphere> atmospheres;
    std::unique_ptr<Clouds> clouds;
    std::unique_ptr<PlanetaryRing> planetaryRing;
};

#endif // SOLARSYSTEM_APPLICATION_TYPES_H
