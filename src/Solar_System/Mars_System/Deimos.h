#ifndef SOLARSYSTEM_DEIMOS_H
#define SOLARSYSTEM_DEIMOS_H
#include "../Satellite.h"

class Deimos : public Satellite {
public:
    explicit Deimos(const SatelliteInfo& satelliteInfo, std::shared_ptr<SpaceObject> parent);
    void AdjustToParent(float timeScale) override;
    void Render() const override;

private:
    std::vector<TextureImage2D> _diffuses;
    TextureImage2D _normalMap;
};

#endif //SOLARSYSTEM_DEIMOS_H
