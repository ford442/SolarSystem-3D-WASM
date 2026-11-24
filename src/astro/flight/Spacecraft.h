#ifndef SOLARSYSTEM_ASTRO_FLIGHT_SPACECRAFT_H
#define SOLARSYSTEM_ASTRO_FLIGHT_SPACECRAFT_H
#include "astro/flight/FlightPlan.h"
#include "astro/flight/StateVector.h"
#include <glm/vec3.hpp>
#include <string>
#include <vector>

/**
 * The craft flying a Plan, as the scene sees it (docs/ARCHITECTURE.md §14). Its position is
 * a function of the date (like every pose here) and is kept in double heliocentric scene
 * units, like the camera's: FlightController moves it to the render frame each frame.
 *
 * Scene mapping. The scene draws Mercury … Vesta on circles of their mean distance (realistic)
 * or art radius (compressed), in the ephemeris direction, not at their true distance. A true
 * heliocentric trajectory would therefore end beside its target's disc. Each leg's position is
 * instead mapped with HelioAuToSceneDouble and then scaled radially by a factor that blends,
 * over the leg's time, from the departure body's drawn/true ratio to the arrival body's: the
 * craft leaves exactly from where its departure planet is drawn and arrives exactly on its
 * target, on the right date, in both scale modes. On realistic scale that factor stays within
 * a planet's eccentricity of 1 (Mars: ±9%).
 */
namespace Flight {

/** The catalog-style row a craft is drawn from. */
struct SpacecraftRow {
    std::string id = "craft";
    std::string name = "Spacecraft";
    glm::vec3 color{0.35f, 0.95f, 0.75f};
};

/** Heliocentric ecliptic km at `julianDate` → heliocentric scene units, stretched per leg as above. */
glm::dvec3 PlanToScene(const Plan& plan, double julianDate, const glm::dvec3& eclipticKm);

/** The radial factor PlanToScene applies at `julianDate` (exposed for tests). */
double SceneStretch(const Plan& plan, double julianDate);

class Spacecraft {
public:
    enum class Phase { BeforeLaunch, Cruise, Arrived };

    Spacecraft() = default;
    Spacecraft(SpacecraftRow row, Plan plan);

    const SpacecraftRow& Row() const { return _row; }
    const Plan& GetPlan() const { return _plan; }
    bool Valid() const { return _plan.ok && !_plan.legs.empty(); }

    Phase PhaseAt(double julianDate) const;
    /** Heliocentric state at `julianDate`, ecliptic km and km/s. */
    StateVector StateAt(double julianDate) const;
    /** Heliocentric scene position (double), for the active scale mode. */
    glm::dvec3 WorldPositionAt(double julianDate) const;
    /** Unit direction of motion in scene axes (the chase camera looks along it). */
    glm::dvec3 SceneForwardAt(double julianDate) const;

    /**
     * The whole planned path in heliocentric scene units, `perLeg` samples per leg (denser
     * near each encounter), first and last point on the bodies' drawn positions.
     */
    std::vector<glm::dvec3> PathScene(int perLeg) const;
    /** The dates PathScene samples at (same order and count). */
    std::vector<double> PathDates(int perLeg) const;

private:
    SpacecraftRow _row;
    Plan _plan;
};

} // namespace Flight

#endif // SOLARSYSTEM_ASTRO_FLIGHT_SPACECRAFT_H
