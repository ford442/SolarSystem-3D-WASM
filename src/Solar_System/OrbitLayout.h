#ifndef SOLARSYSTEM_ORBITLAYOUT_H
#define SOLARSYSTEM_ORBITLAYOUT_H

#include <glm/vec3.hpp>
#include <string>

// Canonical planet indices match FocusPlanet / planet_facts.json (0=Sun … 9=Pluto).
namespace OrbitLayout {

enum class Body : int {
    Sun = 0,
    Mercury = 1,
    Venus = 2,
    Earth = 3,
    Mars = 4,
    Jupiter = 5,
    Saturn = 6,
    Uranus = 7,
    Neptune = 8,
    Pluto = 9
};

enum class ScaleMode : int {
    Compressed = 0,
    Realistic = 1
};

void SetScaleMode(ScaleMode mode);
ScaleMode GetScaleMode();

/** Offset from the Sun in scene units for the active scale mode. */
glm::vec3 GetOffset(Body body);

/** Semi-major axis in astronomical units (NASA fact-sheet averages). */
float GetAuDistance(Body body);

/** Current scene distance from the Sun (length of GetOffset). */
float GetSceneDistance(Body body);

/** Compressed-scene offset baked into the original art direction. */
glm::vec3 GetCompressedOffset(Body body);

/** Map manifest / UI name to body id; returns Sun for unknown names. */
Body BodyFromName(const std::string& name);

} // namespace OrbitLayout

#endif // SOLARSYSTEM_ORBITLAYOUT_H
