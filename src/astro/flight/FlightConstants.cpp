#include "astro/flight/FlightConstants.h"
#include <cctype>
#include <cmath>

namespace Flight {

namespace {

// GM of the planet itself (not its system: a flyby hyperbola is about the planet), JPL DE440.
// Radii are equatorial; the minimum flyby altitudes are conservative mission-design floors
// (atmospheres, Jupiter's radiation belts, Saturn's rings), not hard physical limits.
constexpr BodyConstants kBodies[] = {
    {1, "mercury", "Mercury", 22031.868551, 2439.7, 0.387098, 200.0},
    {2, "venus", "Venus", 324858.592, 6051.8, 0.723332, 250.0},
    {3, "earth", "Earth", 398600.435507, 6378.137, 1.000001, 300.0},
    {4, "mars", "Mars", 42828.375214, 3396.19, 1.523679, 200.0},
    {5, "jupiter", "Jupiter", 126686531.9, 71492.0, 5.2026, 3.0 * 71492.0},
    {6, "saturn", "Saturn", 37931206.2, 60268.0, 9.5549, 60268.0},
    {7, "uranus", "Uranus", 5793951.3, 25559.0, 19.2184, 4000.0},
    {8, "neptune", "Neptune", 6835099.5, 24764.0, 30.110, 4000.0},
    {9, "pluto", "Pluto", 869.6, 1188.3, 39.482, 1000.0},
    {10, "ceres", "Ceres", 62.6284, 469.7, 2.7675, 100.0},
    {11, "vesta", "Vesta", 17.288245, 262.7, 2.3615, 100.0},
};

bool EqualsIgnoreCase(const char* a, const char* b) {
    for (; *a && *b; ++a, ++b) {
        if (std::tolower(static_cast<unsigned char>(*a)) != std::tolower(static_cast<unsigned char>(*b))) {
            return false;
        }
    }
    return *a == *b;
}

} // namespace

const BodyConstants* FindBody(int index) {
    for (const BodyConstants& body : kBodies) {
        if (body.index == index) {
            return &body;
        }
    }
    return nullptr;
}

const BodyConstants* FindBody(const char* id) {
    if (!id) {
        return nullptr;
    }
    for (const BodyConstants& body : kBodies) {
        if (EqualsIgnoreCase(body.id, id)) {
            return &body;
        }
    }
    return nullptr;
}

double SphereOfInfluenceKm(const BodyConstants& body) {
    return body.smaAu * kAuKm * std::pow(body.mu / kMuSun, 0.4);
}

} // namespace Flight
