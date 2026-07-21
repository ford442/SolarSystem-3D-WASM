#ifndef SOLARSYSTEM_ASTEROIDORBIT_H
#define SOLARSYSTEM_ASTEROIDORBIT_H

#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

/** Keplerian helpers shared by AsteroidField and unit tests (no GL). */
namespace AsteroidOrbit {

constexpr uint32_t kDefaultSeed = 0xA57E201Du;
constexpr int kMaxInstances = 4096;
constexpr int kCometCount = 3;

struct OrbitalElements {
    float semiMajorAu = 2.5f;
    float eccentricity = 0.05f;
    float inclinationRad = 0.0f;
    float argPeriapsisRad = 0.0f;
    float longAscNodeRad = 0.0f;
    float meanAnomalyRad = 0.0f;
    float meanMotionRadPerSec = 0.0f;
    float visualRadius = 0.4f;
    float spinPhaseRad = 0.0f;
    float spinRateRadPerSec = 0.2f;
    glm::vec3 color{0.55f, 0.50f, 0.45f};
};

struct CometDef {
    const char* name;
    float perihelionAu;
    float eccentricity;
    float inclinationDeg;
    float argPeriapsisDeg;
    float longAscNodeDeg;
    float meanAnomaly0Deg;
    float periodYears;
    float nucleusRadius;
    glm::vec3 color;
};

const CometDef* GetCometDefs();

std::vector<OrbitalElements> GenerateBelt(uint32_t seed, int count);
std::vector<OrbitalElements> GenerateComets();

float SolveKepler(float meanAnomaly, float eccentricity);
glm::vec3 OrbitalToCartesian(float radius, float trueAnomaly,
                             float inclination, float argPeriapsis, float longAscNode);
glm::vec3 HeliocentricPosition(const OrbitalElements& el);
float CurrentRadiusAu(const OrbitalElements& el);

} // namespace AsteroidOrbit

#endif // SOLARSYSTEM_ASTEROIDORBIT_H
