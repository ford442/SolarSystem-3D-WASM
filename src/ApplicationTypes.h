#ifndef SOLARSYSTEM_APPLICATION_TYPES_H
#define SOLARSYSTEM_APPLICATION_TYPES_H

#include "Solar_System/Atmosphere.h"
#include "Solar_System/Clouds.h"
#include "Solar_System/Planet.h"
#include "Solar_System/PlanetaryRing.h"
#include "Solar_System/Satellite.h"
#include <memory>
#include <vector>

struct RenderableAtmosphere {
    std::unique_ptr<Atmosphere> atmosphere; // tuning numbers live on atmosphere->GetRow()
    float parentEarthSizeCoefficient;
};

struct RenderableSceneComponent {
    // Light frustum, fitted each frame to this component's bodies (Application::UpdateScene).
    glm::mat4 lightSpaceMatrix{1.0f};
    glm::vec2 shadowDepthRange{0.0f, 1.0f}; // near, far - near along the light, scene units
    // The box is fitted to the cap of the planet the camera sees (gfx/ShadowFit.h) rather than
    // ±3 radii; kept between frames for the hysteresis, with the half-extent it last logged.
    bool shadowVisibleCap = false;
    float shadowHalfExtent = 0.0f;
    // Whether any view sees one of its bodies this frame (Application::CullScene). A hidden
    // component skips its shadow pass too: its shadow map only shades its own bodies.
    bool visible = true;
    std::shared_ptr<Planet> planet;
    std::vector<std::shared_ptr<Satellite>> satellites;
    std::vector<RenderableAtmosphere> atmospheres;
    std::unique_ptr<Clouds> clouds;
    std::unique_ptr<PlanetaryRing> planetaryRing;

    /** Every object of the component (planet, moons, shells, ring), for bounds and culling. */
    template <class F>
    void ForEachObject(F&& visit) const {
        visit(static_cast<SpaceObject&>(*planet));
        for (const auto& satellite : satellites) {
            visit(static_cast<SpaceObject&>(*satellite));
        }
        for (const auto& renderable : atmospheres) {
            visit(static_cast<SpaceObject&>(*renderable.atmosphere));
        }
        if (clouds) {
            visit(static_cast<SpaceObject&>(*clouds));
        }
        if (planetaryRing) {
            visit(static_cast<SpaceObject&>(*planetaryRing));
        }
    }
};

#endif // SOLARSYSTEM_APPLICATION_TYPES_H
