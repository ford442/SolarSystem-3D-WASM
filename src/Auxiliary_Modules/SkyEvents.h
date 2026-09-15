#ifndef SOLARSYSTEM_SKYEVENTS_H
#define SOLARSYSTEM_SKYEVENTS_H

#include <string>

/**
 * Sky events derived from the active Ephemeris backend — nothing is baked or animated.
 *
 * Accuracy inherits from the backend (Standish Table 1, ~arcmin for the planets), so event
 * times are good to within an hour or so, which is what an educational "next conjunction"
 * hint needs. Not a flight-dynamics or observing-grade tool.
 */
namespace SkyEvents {

/** Catalog body indices the event search names beyond the planets. */
constexpr int kMoon = 12;
constexpr int kIo = 15;

/** Two bodies sharing a geocentric ecliptic longitude — the classical conjunction. */
struct Conjunction {
    bool valid = false;
    int bodyA = 0;              // Ephemeris / OrbitLayout::Body index, lower index first
    int bodyB = 0;
    double julianDate = 0.0;    // instant of equal geocentric ecliptic longitude
    double separationDeg = 0.0; // apparent separation at that instant (latitudes differ)
};

/** What kind of alignment an event is. Ordered for stable serialization. */
enum class EventKind : int {
    None = 0,
    Conjunction = 1,
    SolarEclipse = 2,
    LunarEclipse = 3,
    Transit = 4,
    ShadowTransit = 5,
};

/**
 * A single ephemeris-derived alignment. Nothing here is baked or animated: every field
 * comes out of the active Ephemeris backend at search time.
 *
 * `bodyA` is the body doing the occulting or transiting (Moon, Mercury, Io); `bodyB` is
 * what it acts against (Sun for a solar eclipse or transit, Earth for a lunar eclipse,
 * Jupiter for a shadow transit). `missDeg` is how far the centres miss at greatest
 * alignment — 0 means dead centre — and `limitDeg` is the separation below which the
 * event is happening at all, so `missDeg / limitDeg` is a rough magnitude.
 *
 * Accuracy: these are greatest-alignment instants from a ~arcmin planet series and
 * J2000 mean lunar/satellite elements. Expect the date to be right and the time to be
 * off by minutes to a couple of hours. Do not use them as contact times.
 */
struct SkyEvent {
    bool valid = false;
    EventKind kind = EventKind::None;
    int bodyA = 0;
    int bodyB = 0;
    double julianDate = 0.0;
    double missDeg = 0.0;
    double limitDeg = 0.0;
};

/** Geocentric ecliptic longitude and latitude of a body in degrees, as seen from Earth. */
void GeocentricLonLatDeg(int bodyIndex, double julianDate, double& lonDeg, double& latDeg);

/** Apparent angular separation of two bodies in degrees, as seen from Earth. */
double ApparentSeparationDeg(int bodyA, int bodyB, double julianDate);

/**
 * Next conjunction between two of the inner planets (Mercury, Venus, Mars) strictly after
 * fromJd, searching at most searchDays ahead. Returns an invalid result when none is found
 * in the window (which for these three would mean the window is only a few weeks long).
 */
Conjunction NextInnerPlanetConjunction(double fromJd, double searchDays = 730.0);

/**
 * Next solar eclipse (Moon's disc touching the Sun's, seen from Earth's centre) strictly
 * after fromJd. Geocentric, so it answers "is there an eclipse somewhere on Earth", not
 * "is one visible from here".
 */
SkyEvent NextSolarEclipse(double fromJd, double searchDays = 730.0);

/** Next lunar eclipse — the Moon entering Earth's umbral cone — strictly after fromJd. */
SkyEvent NextLunarEclipse(double fromJd, double searchDays = 730.0);

/** Next Mercury or Venus transit across the Sun's disc, strictly after fromJd. */
SkyEvent NextInferiorPlanetTransit(double fromJd, double searchDays = 4000.0);

/**
 * Next Galilean shadow transit — a moon's shadow falling on Jupiter's disc. Defaults to
 * Io, the showcase; pass any catalog Galilean index the backend has elements for.
 */
SkyEvent NextGalileanShadowTransit(double fromJd, double searchDays = 30.0,
                                   int satelliteIndex = kIo);

/** Earliest of every event type above (conjunctions included) in the window. */
SkyEvent NextEvent(double fromJd, double searchDays = 730.0);

/** The conjunction search expressed as a SkyEvent, for a uniform event surface. */
SkyEvent NextConjunctionEvent(double fromJd, double searchDays = 730.0);

/** One-line ASCII hint for any event kind, e.g. "Next solar eclipse: 2027-08-02". */
std::string FormatEvent(const SkyEvent& event);

/** Lowercase stable slug for an event kind, e.g. "solarEclipse"; "none" when invalid. */
const char* EventKindSlug(EventKind kind);

/** English body name for an index, e.g. "Venus"; empty for indices with no body. */
const char* BodyName(int bodyIndex);

/**
 * One-line hint, e.g. "Next conjunction: Venus-Mars, 2027-02-24 (1.2 deg apart)".
 * ASCII only, so callers can widen it with a plain char-by-char copy.
 * Empty for an invalid conjunction.
 */
std::string Format(const Conjunction& conjunction);

/** Format(NextInnerPlanetConjunction(...)); empty when the window holds no conjunction. */
std::string FormatNextInnerPlanetConjunction(double fromJd, double searchDays = 730.0);

} // namespace SkyEvents

#endif // SOLARSYSTEM_SKYEVENTS_H
