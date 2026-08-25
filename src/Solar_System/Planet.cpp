#include "Planet.h"
#include "../SimState.h"

Planet::Planet(const PlanetInfo& planetInfo, std::shared_ptr<Star> parentStar) :
    SpaceObject(planetInfo.planetModel, *planetInfo.planetShader, planetInfo.engName, planetInfo.otherLangName), _parentStar(std::move(parentStar)),
    _earthSizeCoefficient(planetInfo.earthSizeCoefficient)
{
    _radius *= _earthSizeCoefficient;
}

float Planet::GetRadius() const {
    return _radius;
}

float Planet::GetEarthSizeCoefficient() const {
    return _earthSizeCoefficient;
}

float Planet::GetEffectiveLODThreshold() const {
#ifdef __EMSCRIPTEN__
    // Medium uses a 1.5x distance multiplier. Expressed as a threshold, this
    // means the camera must be 1.5x closer than full quality before upgrading.
    if (gSimState->qualityPreset == 1) {
        return _lodThreshold / 1.5f;
    }
#endif
    return _lodThreshold;
}
