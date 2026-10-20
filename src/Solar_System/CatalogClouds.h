#ifndef SOLARSYSTEM_CATALOGCLOUDS_H
#define SOLARSYSTEM_CATALOGCLOUDS_H
#include "BodyCatalog.generated.h"
#include "Clouds.h"

/**
 * Outer-shell cloud layer driven by the parent planet's catalog cloudLayer block.
 * Tilt follows the parent body's catalog SSOT so Earth / Uranus / Neptune stay aligned.
 */
class CatalogClouds : public Clouds {
public:
    CatalogClouds(CloudsInfo&& cloudsInfo, std::shared_ptr<SpaceObject> parent,
                  const BodyCatalog::Entry& parentEntry);

    void AdjustToParent() override;
    void Draw(const Shader& shader) const override;

private:
    const BodyCatalog::Entry& _parentEntry;
};

#endif //SOLARSYSTEM_CATALOGCLOUDS_H
