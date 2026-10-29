#include "Planet.h"

Planet::Planet(const PlanetInfo& planetInfo, std::shared_ptr<Star> parentStar) :
    SpaceObject(planetInfo.planetModel, planetInfo.engName, planetInfo.otherLangName), _parentStar(std::move(parentStar)),
    _earthSizeCoefficient(planetInfo.earthSizeCoefficient)
{
    _radius *= _earthSizeCoefficient;
}

float Planet::GetRadius() const {
    return _radius * SizeFactor();
}

float Planet::GetEarthSizeCoefficient() const {
    return _earthSizeCoefficient;
}

float Planet::GetEffectiveLODThreshold(const TextureLodPolicy& policy) const {
    return _lodThreshold * SizeFactor() * policy.planetThresholdScale;
}
