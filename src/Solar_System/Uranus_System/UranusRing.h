#ifndef SOLARSYSTEM_URANUSRING_H
#define SOLARSYSTEM_URANUSRING_H
#include "../PlanetaryRing.h"

class UranusRing : public PlanetaryRing {
public:
    explicit UranusRing(PlanetaryRingInfo&& planetaryRingInfo, std::shared_ptr<Planet> parent);
    void AdjustToParent() override;
    void Draw(const Shader& shader) const override;
};

#endif //SOLARSYSTEM_URANUSRING_H
