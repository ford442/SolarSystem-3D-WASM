#include "SpaceWeather.h"
#include "Ephemeris.h"
#include "../Solar_System/BodyCatalog.generated.h"
#include "../Solar_System/OrbitLayout.h"
#include "../core/Json.h"
#include "../core/Log.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <glm/geometric.hpp>
#include <string>
#include <utility>
#include <vector>

namespace SpaceWeather {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;
constexpr int kEarthIndex = static_cast<int>(OrbitLayout::Body::Earth);
/** Fronts past this have left everything the scene draws (Neptune is at 30 AU). */
constexpr double kSystemEdgeAu = 60.0;
/** No CME in the list is followed for longer than this after launch. */
constexpr double kMaxCmeAgeDays = 400.0;

double Clamp(double value, double lo, double hi) {
    return std::min(std::max(value, lo), hi);
}

double Smoothstep(double edge0, double edge1, double x) {
    const double t = Clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

double WrapTwoPi(double angle) {
    const double wrapped = std::fmod(angle, 2.0 * kPi);
    return wrapped < 0.0 ? wrapped + 2.0 * kPi : wrapped;
}

/** Hathaway's u³ / (exp(u²) − 0.71). */
double HathawayCurve(double u) {
    if (u <= 0.0) {
        return 0.0;
    }
    const double u2 = u * u;
    if (u2 > 600.0) {
        return 0.0;
    }
    return u * u2 / (std::exp(u2) - 0.71);
}

struct HathawayExtremum {
    double u = 1.0;
    double value = 1.0;
};

/** Where the curve peaks (u ≈ 1.08) and its value there. */
const HathawayExtremum& HathawayPeak() {
    static const HathawayExtremum peak = [] {
        HathawayExtremum best{0.0, 0.0};
        for (int i = 1; i <= 4000; ++i) {
            const double u = i * 0.001;
            const double value = HathawayCurve(u);
            if (value > best.value) {
                best = {u, value};
            }
        }
        return best;
    }();
    return peak;
}

std::string EscapeJson(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 2);
    for (const char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char escaped[8];
                    std::snprintf(escaped, sizeof(escaped), "\\u%04x", static_cast<unsigned>(c));
                    out += escaped;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

/** Earth's heliocentric ecliptic position at `jd` (Standish). */
Ephemeris::HelioLB EarthAt(double julianDate) {
    return Ephemeris::Position(kEarthIndex, julianDate);
}

glm::dvec3 DirectionOf(const Ephemeris::HelioLB& p) {
    return SceneDirection(p.lonRad, p.latRad);
}

/** Kp at Earth `jd` with `cme` the only one active (for Calibrate). */
double EarthKpWith(const Cme& cme, double julianDate) {
    const Magnetosphere* earth = FindMagnetosphere(OrbitLayout::Body::Earth);
    const Ephemeris::HelioLB p = EarthAt(julianDate);
    const CycleState cycle = EvaluateCycle(SolarCycle(), julianDate);
    const std::vector<const Cme*> cmes{&cme};
    return EvaluateBody(*earth, p.lonRad, p.latRad, p.rAu, julianDate, cycle.phase, cmes, -1.0).kpUnclamped;
}

/** The strongest the CME gets at Earth around `arrivalJd`. */
double PeakStrengthAtEarth(const Cme& cme, double arrivalJd) {
    double peak = 0.0;
    for (int hour = -2; hour <= 30; ++hour) {
        const double jd = arrivalJd + hour / 24.0;
        const Ephemeris::HelioLB p = EarthAt(jd);
        peak = std::max(peak, CmeStrengthAt(cme, DirectionOf(p), p.rAu, jd));
    }
    return peak;
}

/**
 * The largest 3-hour mean of Kp at Earth from 2 h before `arrivalJd` to 30 h after: Kp is a
 * 3-hourly index, so that is what an observed peak Kp compares with. `saturate` caps each
 * sample at 9 as the index does; a fit leaves it off to push a "9" storm past it.
 */
double PeakKpAtEarth(const Cme& cme, double arrivalJd, bool saturate = true) {
    constexpr int kSamples = 65; // every 30 minutes over 32 hours
    constexpr int kWindow = 7;   // 3 hours, ends included
    double kp[kSamples];
    for (int i = 0; i < kSamples; ++i) {
        const double sample = EarthKpWith(cme, arrivalJd + (-2.0 + 0.5 * i) / 24.0);
        kp[i] = saturate ? std::min(sample, 9.0) : sample;
    }
    double peak = -1.0e9;
    for (int i = 0; i + kWindow <= kSamples; ++i) {
        double sum = 0.0;
        for (int j = 0; j < kWindow; ++j) {
            sum += kp[i + j];
        }
        peak = std::max(peak, sum / kWindow);
    }
    return peak;
}

void AppendStormJson(std::string& json, const char* key, const Cme* cme, int index) {
    json += '"';
    json += key;
    json += "\":";
    if (!cme) {
        json += "null";
        return;
    }
    char buffer[256];
    std::snprintf(buffer, sizeof(buffer),
                  R"({"index":%d,"launchJd":%.6f,"arrivalJd":%.6f,"speedKmS":%.0f,"kpMax":%.2f,"observedKp":%s,)",
                  index, cme->launchJd, cme->ArrivalAtEarthJd(), cme->speedKmS, cme->PeakKp(),
                  cme->kpMax >= 0.0 ? "true" : "false");
    json += buffer;
    json += "\"id\":\"" + EscapeJson(cme->id) + "\",\"name\":\"" + EscapeJson(cme->name) + "\",\"source\":\"" +
            EscapeJson(cme->source) + "\"}";
}

} // namespace

// --- Pressure balance -----------------------------------------------------------------------

double DynamicPressureNPa(double densityPerCm3, double speedKmS) {
    const double density = kProtonMassKg * densityPerCm3 * 1.0e6; // kg/m³
    const double speed = speedKmS * 1.0e3;                        // m/s
    return density * speed * speed * 1.0e9;
}

double StandoffRadii(double surfaceFieldTesla, double dynamicPressureNPa, double inflation) {
    const double pressure = std::max(dynamicPressureNPa, 1.0e-6) * 1.0e-9;
    const double boundaryField = kBoundaryFieldCompression * surfaceFieldTesla;
    return inflation * std::pow(boundaryField * boundaryField / (2.0 * kMu0 * pressure), 1.0 / 6.0);
}

double ShueFlaringExponent(double dynamicPressureNPa) {
    return 0.58 * (1.0 + 0.024 * std::log(std::max(dynamicPressureNPa, 0.01)));
}

double MagnetopauseRadius(double standoff, double flaringExponent, double cosTheta) {
    // The tail flares without bound as θ → π; past ~160° it is a cylinder for drawing purposes.
    const double denominator = std::max(1.0 + cosTheta, 0.06);
    return standoff * std::pow(2.0 / denominator, flaringExponent);
}

double QuietPressureNPa(double rAu) {
    const double r = std::max(rAu, 0.05);
    return DynamicPressureNPa(kQuietDensityAt1AuPerCm3 / (r * r), kSlowWindKmS);
}

double ParkerSpiralAngleRad(double rAu, double speedKmS, double heliolatitudeRad) {
    const double azimuthal = kSolarRotationRadPerSecond * rAu * kAuKm * std::cos(heliolatitudeRad);
    return std::atan2(azimuthal, std::max(speedKmS, 1.0));
}

// --- Ovals ----------------------------------------------------------------------------------

Oval OvalAt(const OvalModel& model, double kp) {
    const double k = Clamp(kp, 0.0, 9.0);
    Oval oval;
    oval.equatorwardMidnightDeg = model.equatorwardMidnight0 + model.equatorwardMidnightPerKp * k;
    oval.equatorwardNoonDeg = model.equatorwardNoon0 + model.equatorwardNoonPerKp * k;
    oval.polewardMidnightDeg = model.polewardMidnight0 + model.polewardMidnightPerKp * k;
    oval.polewardNoonDeg = model.polewardNoon0 + model.polewardNoonPerKp * k;
    // The band never inverts, whatever a row's slopes say.
    oval.polewardMidnightDeg = std::max(oval.polewardMidnightDeg, oval.equatorwardMidnightDeg + 1.0);
    oval.polewardNoonDeg = std::max(oval.polewardNoonDeg, oval.equatorwardNoonDeg + 1.0);
    return oval;
}

Oval EarthOval(double kp) {
    return OvalAt(FindMagnetosphere(OrbitLayout::Body::Earth)->oval, kp);
}

double EquatorwardLatitudeDeg(const Oval& oval, double mltRad) {
    const double night = 0.5 * (1.0 - std::cos(mltRad));
    return oval.equatorwardNoonDeg + (oval.equatorwardMidnightDeg - oval.equatorwardNoonDeg) * night;
}

double PolewardLatitudeDeg(const Oval& oval, double mltRad) {
    const double night = 0.5 * (1.0 - std::cos(mltRad));
    return oval.polewardNoonDeg + (oval.polewardMidnightDeg - oval.polewardNoonDeg) * night;
}

// --- Coupling -------------------------------------------------------------------------------

double CouplingRatio(double pressureNPa, double quietPressureNPa, double speedKmS, double geoeffectiveness) {
    const double pressure = std::max(pressureNPa, 1.0e-9) / std::max(quietPressureNPa, 1.0e-9);
    return std::pow(pressure, 2.0 / 3.0) * (std::max(speedKmS, 1.0) / kSlowWindKmS) * std::max(geoeffectiveness, 0.0);
}

double KpFromCouplingUnclamped(double couplingRatio) {
    return kQuietKp + kKpPerDoubling * std::log2(std::max(couplingRatio, 1.0e-6));
}

double KpFromCoupling(double couplingRatio) {
    return Clamp(KpFromCouplingUnclamped(couplingRatio), 0.0, 9.0);
}

// --- The cycle ------------------------------------------------------------------------------

CycleParams CycleFromStar(const BodyCatalog::StarRow& star) {
    CycleParams params;
    params.periodYears = star.cyclePeriodYears;
    params.minimumYear = star.cycleMinimumYear;
    params.referenceCycle = star.cycleAtMinimum;
    params.meanAmplitude = star.cycleMeanPeakSunspots > 0.0f ? star.cycleMeanPeakSunspots : 179.0;
    params.solarAmplitudes = star.cycleAtMinimum > 0;
    return params;
}

CycleParams SolarCycle() {
    return CycleFromStar(BodyCatalog::kSolStar);
}

double DecimalYear(double julianDate) {
    return 2000.0 + (julianDate - Ephemeris::kJ2000) / 365.25;
}

double JulianDateFromDecimalYear(double year) {
    return Ephemeris::kJ2000 + (year - 2000.0) * 365.25;
}

double CycleRiseYears(double amplitude, double periodYears) {
    // Waldmeier effect: big cycles rise fast. 4.5 years for a mean cycle, ~5.3 for cycle 24.
    const double rise = Clamp(4.5 + (179.0 - amplitude) * 0.012, 3.5, 6.0);
    return rise * periodYears / 11.0;
}

double CycleShape(double yearsSinceMinimum, double amplitude, double periodYears) {
    if (yearsSinceMinimum <= 0.0 || periodYears <= 0.0) {
        return 0.0;
    }
    const HathawayExtremum& peak = HathawayPeak();
    const double bYears = CycleRiseYears(amplitude, periodYears) / peak.u;
    return amplitude * HathawayCurve(yearsSinceMinimum / bYears) / peak.value;
}

double SolarCycleAmplitude(int number, double fallback) {
    // SILSO version 2.0 13-month smoothed maxima, rounded (cycle 25's is provisional).
    static constexpr double kMaxima[] = {
        144.1, 193.0, 264.3, 235.3, 82.0,  81.2,  119.2, 244.9, 219.9, 186.2, 234.0, 124.4, 146.5,
        107.1, 175.7, 130.2, 198.6, 218.7, 285.0, 156.6, 232.9, 212.5, 180.3, 116.4, 160.9,
    };
    constexpr int kCount = static_cast<int>(sizeof(kMaxima) / sizeof(kMaxima[0]));
    return number >= 1 && number <= kCount ? kMaxima[number - 1] : fallback;
}

double SporerLatitudeDeg(double yearsSinceMinimum) {
    return 28.0 * std::exp(-std::max(yearsSinceMinimum, 0.0) * 12.0 / 90.0);
}

CycleState EvaluateCycle(const CycleParams& params, double julianDate) {
    CycleState state;
    if (params.periodYears <= 0.0) {
        return state;
    }
    const double year = DecimalYear(julianDate);
    const double cycles = std::floor((year - params.minimumYear) / params.periodYears);
    const double start = params.minimumYear + cycles * params.periodYears;
    state.cycleNumber = params.referenceCycle + static_cast<int>(cycles);
    state.yearsSinceMinimum = year - start;
    state.phase = state.yearsSinceMinimum / params.periodYears;

    const auto amplitude = [&](int number) {
        return params.solarAmplitudes ? SolarCycleAmplitude(number, params.meanAmplitude) : params.meanAmplitude;
    };
    const auto band = [&](double years, double sunspots) {
        SpotBand b;
        b.latitudeDeg = SporerLatitudeDeg(years);
        b.halfWidthDeg = 4.0 + 4.0 * std::exp(-years * 12.0 / 60.0);
        b.coverage = Clamp(sunspots / kFullCoverageSunspots, 0.0, 1.0);
        return b;
    };
    const double current = CycleShape(state.yearsSinceMinimum, amplitude(state.cycleNumber), params.periodYears);
    const double previousYears = state.yearsSinceMinimum + params.periodYears;
    const double previous = CycleShape(previousYears, amplitude(state.cycleNumber - 1), params.periodYears);
    state.sunspotNumber = current + previous;
    state.current = band(state.yearsSinceMinimum, current);
    state.previous = band(previousYears, previous);
    return state;
}

// --- The wind -------------------------------------------------------------------------------

double StreamerBeltHalfWidthDeg(double cyclePhase) {
    // The streamer belt (slow wind) is ~±20° near minimum and fills everything near maximum.
    const double maximum = std::exp(-std::pow((cyclePhase - 0.42) / 0.2, 2.0));
    return 20.0 + 55.0 * maximum;
}

Wind AmbientWind(double rAu, double heliolatitudeRad, double sourceLongitudeRad, double cyclePhase) {
    const double beltDeg = StreamerBeltHalfWidthDeg(cyclePhase);
    const double polar = Smoothstep(beltDeg, beltDeg + 10.0, std::abs(heliolatitudeRad) / kDegToRad);
    // Two recurrent fast streams per rotation, strongest in the declining phase.
    const double streamAmplitude = 0.35 + 0.45 * std::exp(-std::pow((cyclePhase - 0.75) / 0.18, 2.0));
    const double stream = streamAmplitude * std::pow(std::max(0.0, std::cos(2.0 * sourceLongitudeRad)), 4.0);
    const double fast = std::max(polar, stream);

    const double r = std::max(rAu, 0.05);
    Wind wind;
    wind.speedKmS = kSlowWindKmS + (kFastWindKmS - kSlowWindKmS) * fast;
    wind.densityPerCm3 = (kQuietDensityAt1AuPerCm3 + (2.5 - kQuietDensityAt1AuPerCm3) * fast) / (r * r);
    wind.pressureNPa = DynamicPressureNPa(wind.densityPerCm3, wind.speedKmS);
    return wind;
}

double SourceLongitudeRad(double longitudeRad, double rAu, double speedKmS, double julianDate) {
    const double seconds = (julianDate - Ephemeris::kJ2000) * kSecondsPerDay;
    const double lag = kSolarRotationRadPerSecond * rAu * kAuKm / std::max(speedKmS, 1.0);
    return WrapTwoPi(longitudeRad - kSolarRotationRadPerSecond * seconds + lag);
}

// --- CMEs -----------------------------------------------------------------------------------

double Cme::ArrivalAtEarthJd() const {
    if (observedArrivalJd > 0.0) {
        return observedArrivalJd;
    }
    return hitsEarth ? modelArrivalJd : 0.0;
}

double Cme::PeakKp() const {
    if (kpMax >= 0.0) {
        return kpMax;
    }
    return hitsEarth ? modelKpMax : -1.0;
}

double DragFrontKm(double speedKmS, double ambientKmS, double dragPerKm, double launchRadiusKm, double seconds) {
    if (seconds <= 0.0) {
        return launchRadiusKm;
    }
    const double relative = speedKmS - ambientKmS;
    if (dragPerKm <= 0.0 || std::abs(relative) < 1.0e-6) {
        return launchRadiusKm + speedKmS * seconds;
    }
    const double sign = relative > 0.0 ? 1.0 : -1.0;
    return launchRadiusKm + ambientKmS * seconds + sign / dragPerKm * std::log1p(sign * dragPerKm * relative * seconds);
}

double DragSpeedKmS(double speedKmS, double ambientKmS, double dragPerKm, double seconds) {
    if (seconds <= 0.0) {
        return speedKmS;
    }
    const double relative = speedKmS - ambientKmS;
    if (dragPerKm <= 0.0 || std::abs(relative) < 1.0e-6) {
        return speedKmS;
    }
    const double sign = relative > 0.0 ? 1.0 : -1.0;
    return relative / (1.0 + sign * dragPerKm * relative * seconds) + ambientKmS;
}

double FrontKm(const Cme& cme, double julianDate) {
    const double seconds = (julianDate - cme.launchJd) * kSecondsPerDay;
    if (seconds < 0.0) {
        return 0.0;
    }
    return DragFrontKm(cme.modelSpeedKmS, cme.ambientSpeedKmS, cme.dragPerKm, cme.launchRadiusKm, seconds);
}

double FrontSpeedKmS(const Cme& cme, double julianDate) {
    const double seconds = (julianDate - cme.launchJd) * kSecondsPerDay;
    return DragSpeedKmS(cme.modelSpeedKmS, cme.ambientSpeedKmS, cme.dragPerKm, std::max(seconds, 0.0));
}

double ArrivalJd(const Cme& cme, double distanceKm) {
    if (distanceKm <= cme.launchRadiusKm) {
        return cme.launchJd;
    }
    double lo = 0.0;
    double hi = 2.0 * 365.25; // days
    if (FrontKm(cme, cme.launchJd + hi) < distanceKm) {
        return 0.0;
    }
    for (int i = 0; i < 64; ++i) {
        const double mid = 0.5 * (lo + hi);
        (FrontKm(cme, cme.launchJd + mid) < distanceKm ? lo : hi) = mid;
    }
    return cme.launchJd + 0.5 * (lo + hi);
}

namespace {

struct CmeSample {
    double strength = 0.0;    // field and speed: the sheath plus the ejecta behind it
    double compression = 0.0; // density: the sheath just behind the shock
};

CmeSample SampleCme(const Cme& cme, const glm::dvec3& direction, double rAu, double julianDate) {
    CmeSample sample;
    const double frontAu = FrontKm(cme, julianDate) / kAuKm;
    if (frontAu <= 0.0 || rAu > frontAu) {
        return sample;
    }
    const double innerAu = frontAu * (1.0 - kShellThickness);
    if (rAu < innerAu) {
        return sample;
    }
    const double cosAngle = glm::dot(direction, cme.direction);
    const double edge = std::cos(std::min(cme.halfAngleRad * kFlankReach, kPi));
    const double core = std::cos(cme.halfAngleRad * 0.6);
    const double angular = Smoothstep(edge, core, cosAngle);
    if (angular <= 0.0) {
        return sample;
    }
    // x = 0 at the shock, 1 at the back of the ejecta. Kp peaks hours behind the shock and
    // holds through the magnetic cloud; the density pile-up is the first few hours.
    const double x = (frontAu - rAu) / (frontAu - innerAu);
    const double rise = Smoothstep(0.0, 0.04, x);
    const double body = 0.75 + 0.25 * std::exp(-std::pow((x - 0.15) / 0.2, 2.0));
    const double tail = 1.0 - Smoothstep(0.65, 1.0, x);
    sample.strength = angular * rise * body * tail;
    sample.compression = angular * rise * std::exp(-x / 0.15);
    return sample;
}

} // namespace

double CmeStrengthAt(const Cme& cme, const glm::dvec3& direction, double rAu, double julianDate) {
    return SampleCme(cme, direction, rAu, julianDate).strength;
}

Wind WindAt(const Wind& ambient, const std::vector<const Cme*>& cmes, const glm::dvec3& direction, double rAu,
            double julianDate) {
    Wind wind = ambient;
    double densityFactor = 1.0;
    for (std::size_t i = 0; i < cmes.size(); ++i) {
        const Cme& cme = *cmes[i];
        const CmeSample sample = SampleCme(cme, direction, rAu, julianDate);
        if (sample.strength <= 0.0) {
            continue;
        }
        const double speed = ambient.speedKmS + (FrontSpeedKmS(cme, julianDate) - ambient.speedKmS) * sample.strength;
        wind.speedKmS = std::max(wind.speedKmS, speed);
        // The sheath behind the shock is compressed about fivefold.
        densityFactor = std::max(densityFactor, 1.0 + 4.0 * sample.compression);
        if (sample.strength > wind.cmeStrength) {
            wind.cmeStrength = sample.strength;
            wind.geoeffectiveness = cme.geoeffectiveness;
            wind.cme = static_cast<int>(i);
        }
    }
    wind.densityPerCm3 = ambient.densityPerCm3 * densityFactor;
    wind.pressureNPa = DynamicPressureNPa(wind.densityPerCm3, wind.speedKmS);
    return wind;
}

glm::dvec3 SceneDirection(double eclipticLongitudeRad, double eclipticLatitudeRad) {
    const double cosLat = std::cos(eclipticLatitudeRad);
    return {cosLat * std::cos(eclipticLongitudeRad), std::sin(eclipticLatitudeRad),
            cosLat * std::sin(eclipticLongitudeRad)};
}

void Calibrate(Cme& cme) {
    // Stonyhurst longitude is measured from the Sun–Earth line toward the west limb, which is
    // the direction Earth orbits: ecliptic longitude grows with it.
    const Ephemeris::HelioLB earthAtLaunch = EarthAt(cme.launchJd);
    cme.direction = SceneDirection(earthAtLaunch.lonRad + cme.heeqLongitudeRad, cme.heeqLatitudeRad);
    cme.modelSpeedKmS = cme.speedKmS;
    cme.ambientSpeedKmS = kSlowWindKmS;

    if (cme.observedArrivalJd > cme.launchJd) {
        const double seconds = (cme.observedArrivalJd - cme.launchJd) * kSecondsPerDay;
        const double distance = EarthAt(cme.observedArrivalJd).rAu * kAuKm;
        const auto miss = [&](double drag) {
            return DragFrontKm(cme.modelSpeedKmS, cme.ambientSpeedKmS, drag, cme.launchRadiusKm, seconds) - distance;
        };
        double lo = 0.0;
        double hi = 1.0e-6; // km⁻¹: far beyond the 0.1–2 × 10⁻⁷ fits of Vršnak et al.
        const double atLo = miss(lo);
        const double atHi = miss(hi);
        if ((atLo > 0.0) != (atHi > 0.0)) {
            for (int i = 0; i < 80; ++i) {
                const double mid = 0.5 * (lo + hi);
                ((miss(mid) > 0.0) == (atLo > 0.0) ? lo : hi) = mid;
            }
            cme.dragPerKm = 0.5 * (lo + hi);
        } else {
            // No drag fits (the coronagraph speed was a projection, or the event too slow):
            // the mean speed that makes the observed transit.
            cme.dragPerKm = 0.0;
            cme.modelSpeedKmS = (distance - cme.launchRadiusKm) / seconds;
        }
    }

    cme.modelArrivalJd = ArrivalJd(cme, EarthAt(cme.launchJd + 2.0).rAu * kAuKm);
    const double arrival = cme.observedArrivalJd > 0.0 ? cme.observedArrivalJd : cme.modelArrivalJd;
    cme.hitsEarth = arrival > 0.0 && PeakStrengthAtEarth(cme, arrival) > 0.2;
    cme.modelKpMax = -1.0;
    if (!cme.hitsEarth) {
        return;
    }
    if (cme.kpMax >= 0.0) {
        // Kp saturates at 9, so a "9" storm is fitted a little past it to hold there.
        const double target = cme.kpMax >= 8.67 ? 9.6 : cme.kpMax;
        cme.geoeffectiveness = 1.0;
        for (int iteration = 0; iteration < 5; ++iteration) {
            const double modelled = PeakKpAtEarth(cme, arrival, false);
            cme.geoeffectiveness =
                Clamp(cme.geoeffectiveness * std::exp2((target - modelled) / kKpPerDoubling), 0.05, 60.0);
        }
    }
    cme.modelKpMax = PeakKpAtEarth(cme, arrival);
}

double ParseIsoUtc(const std::string& text) {
    int year = 0, month = 0, day = 0, hour = 0, minute = 0;
    double second = 0.0;
    const int fields = std::sscanf(text.c_str(), "%d-%d-%dT%d:%d:%lf", &year, &month, &day, &hour, &minute, &second);
    if (fields < 3 || month < 1 || month > 12 || day < 1 || day > 31 || hour < 0 || hour > 24 || minute < 0 ||
        minute > 59 || second < 0.0 || second >= 61.0) {
        return 0.0;
    }
    if (fields == 4) {
        return 0.0; // "T12" with no minutes
    }
    return Ephemeris::JulianDateFromYmd(year, month, day) + (hour + minute / 60.0 + second / 3600.0) / 24.0;
}

bool ParseCmeCatalog(const Json::Value& document, CmeCatalog& out, std::string& error) {
    out.events.clear();
    const Json::Value& events = document["events"];
    if (!events.IsArray()) {
        error = "cmes.json: no \"events\" array";
        return false;
    }
    int skipped = 0;
    for (const Json::Value& row : events.Items()) {
        Cme cme;
        cme.id = row["id"].AsString("");
        cme.name = row["name"].AsString(cme.id);
        cme.launchJd = ParseIsoUtc(row["launch"].AsString(""));
        cme.speedKmS = row["speedKmS"].AsNumber(0.0);
        if (cme.id.empty() || cme.launchJd <= 0.0 || !(cme.speedKmS > 0.0)) {
            ++skipped;
            continue;
        }
        cme.launchRadiusKm = std::max(row["launchRadiusRsun"].AsNumber(1.0), 1.0) * kSunRadiusKm;
        cme.halfAngleRad = Clamp(row["halfAngleDeg"].AsNumber(45.0), 5.0, 120.0) * kDegToRad;
        cme.heeqLongitudeRad = row["longitudeDeg"].AsNumber(0.0) * kDegToRad;
        cme.heeqLatitudeRad = Clamp(row["latitudeDeg"].AsNumber(0.0), -90.0, 90.0) * kDegToRad;
        cme.observedArrivalJd = row["earthArrival"].IsString() ? ParseIsoUtc(row["earthArrival"].AsString()) : 0.0;
        if (cme.observedArrivalJd <= cme.launchJd) {
            cme.observedArrivalJd = 0.0;
        }
        cme.kpMax = row["kpMax"].IsNumber() ? Clamp(row["kpMax"].AsNumber(), 0.0, 9.0) : -1.0;
        cme.source = row["source"].AsString("curated");
        Calibrate(cme);
        out.events.push_back(std::move(cme));
    }
    std::stable_sort(out.events.begin(), out.events.end(),
                     [](const Cme& a, const Cme& b) { return a.launchJd < b.launchJd; });
    if (skipped > 0) {
        LOG_WARN("SpaceWeather", "cmes.json: skipped %d event(s) with no id, launch time or speed", skipped);
    }
    return true;
}

bool LoadCmeCatalog(const std::string& path, CmeCatalog& out, std::string& error) {
    Json::Value document;
    if (!Json::ParseFile(path, document, error)) {
        return false;
    }
    return ParseCmeCatalog(document, out, error);
}

// --- Magnetospheres -------------------------------------------------------------------------

const std::vector<Magnetosphere>& Magnetospheres() {
    // Equatorial surface fields of the dipoles (NASA fact sheets; Connerney for Jupiter). The
    // ovals are in magnetic latitude: Earth's after Feldstein–Starkov, the giants' main ovals
    // from HST and Juno/Cassini images, which barely move with the wind.
    static const std::vector<Magnetosphere> kBodies = {
        {OrbitLayout::Body::Mercury,
         "mercury",
         "Mercury",
         1.95e-7,
         1.0,
         {70.0, -0.6, 74.0, -0.4, 76.0, -0.4, 80.0, -0.3}},
        {OrbitLayout::Body::Earth, "earth", "Earth", 3.12e-5, 1.0, {66.0, -1.95, 74.0, -1.0, 71.5, -0.5, 78.0, -0.6}},
        {OrbitLayout::Body::Jupiter,
         "jupiter",
         "Jupiter",
         4.17e-4,
         1.6,
         {72.0, -0.35, 74.0, -0.25, 76.0, -0.2, 78.0, -0.2}},
        {OrbitLayout::Body::Saturn, "saturn", "Saturn", 2.1e-5, 1.25, {72.0, -0.5, 75.0, -0.4, 76.0, -0.3, 79.0, -0.3}},
        {OrbitLayout::Body::Uranus, "uranus", "Uranus", 2.3e-5, 1.0, {68.0, -0.4, 70.0, -0.3, 76.0, -0.3, 78.0, -0.3}},
        {OrbitLayout::Body::Neptune,
         "neptune",
         "Neptune",
         1.42e-5,
         1.0,
         {68.0, -0.4, 70.0, -0.3, 76.0, -0.3, 78.0, -0.3}},
    };
    return kBodies;
}

const Magnetosphere* FindMagnetosphere(OrbitLayout::Body body) {
    for (const Magnetosphere& m : Magnetospheres()) {
        if (m.body == body) {
            return &m;
        }
    }
    return nullptr;
}

BodyState EvaluateBody(const Magnetosphere& m, double lonRad, double latRad, double rAu, double julianDate,
                       double cyclePhase, const std::vector<const Cme*>& cmes, double kpOverride) {
    BodyState state;
    state.magnetosphere = &m;
    state.rAu = rAu;
    const glm::dvec3 direction = SceneDirection(lonRad, latRad);
    const double source = SourceLongitudeRad(lonRad, rAu, kSlowWindKmS, julianDate);
    const Wind ambient = AmbientWind(rAu, latRad, source, cyclePhase);
    state.wind = WindAt(ambient, cmes, direction, rAu, julianDate);

    const double quiet = QuietPressureNPa(rAu);
    state.standoffRadii = StandoffRadii(m.surfaceFieldTesla, state.wind.pressureNPa, m.inflation);
    state.quietStandoffRadii = StandoffRadii(m.surfaceFieldTesla, quiet, m.inflation);
    state.flaringExponent = ShueFlaringExponent(state.wind.pressureNPa * (QuietPressureNPa(1.0) / quiet));
    const double geo = 1.0 + (state.wind.geoeffectiveness - 1.0) * state.wind.cmeStrength;
    state.coupling = CouplingRatio(state.wind.pressureNPa, quiet, state.wind.speedKmS, geo);
    state.kpUnclamped = KpFromCouplingUnclamped(state.coupling);
    state.kp = kpOverride >= 0.0 ? Clamp(kpOverride, 0.0, 9.0) : Clamp(state.kpUnclamped, 0.0, 9.0);
    state.oval = OvalAt(m.oval, state.kp);
    return state;
}

// --- State ----------------------------------------------------------------------------------

void State::SetCatalog(CmeCatalog catalog) {
    _active.clear();
    _catalog = std::move(catalog);
}

const Cme& State::LaunchCme(double julianDate, double speedKmS, double halfAngleDeg) {
    Cme cme;
    char name[64];
    int year = 0, month = 0, day = 0;
    Ephemeris::YmdFromJulianDate(julianDate, year, month, day);
    std::snprintf(name, sizeof(name), "CME launched %04d-%02d-%02d", year, month, day);
    cme.id = "user-" + std::to_string(_launched.size() + 1);
    cme.name = name;
    cme.launchJd = julianDate;
    cme.speedKmS = speedKmS;
    cme.halfAngleRad = halfAngleDeg * kDegToRad;
    cme.source = "user";
    // A hand-launched CME carries a southward field: a storm, not just a pressure pulse.
    cme.geoeffectiveness = 2.0;
    Calibrate(cme);
    _launched.push_back(std::move(cme));
    return _launched.back();
}

int State::FindEventNear(double julianDate, double windowDays) const {
    int best = -1;
    double bestDistance = windowDays;
    for (std::size_t i = 0; i < _catalog.events.size(); ++i) {
        const Cme& cme = _catalog.events[i];
        double distance = std::abs(cme.launchJd - julianDate);
        const double arrival = cme.ArrivalAtEarthJd();
        if (arrival > 0.0) {
            distance = std::min(distance, std::abs(arrival - julianDate));
        }
        if (distance <= bestDistance) {
            bestDistance = distance;
            best = static_cast<int>(i);
        }
    }
    return best;
}

int State::NextStorm(double julianDate, double minKp) const {
    int best = -1;
    double bestArrival = 0.0;
    for (std::size_t i = 0; i < _catalog.events.size(); ++i) {
        const Cme& cme = _catalog.events[i];
        const double arrival = cme.ArrivalAtEarthJd();
        if (arrival > julianDate && cme.PeakKp() >= minKp && (best < 0 || arrival < bestArrival)) {
            best = static_cast<int>(i);
            bestArrival = arrival;
        }
    }
    return best;
}

int State::PreviousStorm(double julianDate, double minKp) const {
    int best = -1;
    double bestArrival = 0.0;
    for (std::size_t i = 0; i < _catalog.events.size(); ++i) {
        const Cme& cme = _catalog.events[i];
        const double arrival = cme.ArrivalAtEarthJd();
        if (arrival > 0.0 && arrival <= julianDate && cme.PeakKp() >= minKp && arrival > bestArrival) {
            best = static_cast<int>(i);
            bestArrival = arrival;
        }
    }
    return best;
}

void State::Evaluate(double julianDate, const std::vector<glm::dvec3>& positions) {
    _jd = julianDate;
    _cycle = EvaluateCycle(_cycleParams, julianDate);

    // Fronts still inside the system, newest first. The list is sorted by launch, so only the
    // tail within kMaxCmeAgeDays of the date can qualify.
    _active.clear();
    const auto consider = [&](const Cme& cme) {
        if (cme.launchJd > julianDate || julianDate - cme.launchJd > kMaxCmeAgeDays) {
            return;
        }
        if (FrontKm(cme, julianDate) / kAuKm > kSystemEdgeAu) {
            return;
        }
        _active.push_back(&cme);
    };
    const auto first = std::lower_bound(_catalog.events.begin(), _catalog.events.end(), julianDate - kMaxCmeAgeDays,
                                        [](const Cme& cme, double jd) { return cme.launchJd < jd; });
    for (auto it = first; it != _catalog.events.end() && it->launchJd <= julianDate; ++it) {
        consider(*it);
    }
    for (const Cme& cme : _launched) {
        consider(cme);
    }
    std::sort(_active.begin(), _active.end(), [](const Cme* a, const Cme* b) { return a->launchJd > b->launchJd; });
    if (_active.size() > kMaxActiveCmes) {
        _active.resize(kMaxActiveCmes);
    }

    _bodies.clear();
    const std::vector<Magnetosphere>& bodies = Magnetospheres();
    for (std::size_t i = 0; i < bodies.size() && i < positions.size(); ++i) {
        const glm::dvec3& p = positions[i];
        _bodies.push_back(EvaluateBody(bodies[i], p.x, p.y, p.z, julianDate, _cycle.phase, _active, _kpOverride));
    }
}

const BodyState* State::Body(OrbitLayout::Body body) const {
    for (const BodyState& state : _bodies) {
        if (state.magnetosphere && state.magnetosphere->body == body) {
            return &state;
        }
    }
    return nullptr;
}

double State::Kp() const {
    const BodyState* earth = Body(OrbitLayout::Body::Earth);
    if (earth) {
        return earth->kp;
    }
    return _kpOverride >= 0.0 ? _kpOverride : kQuietKp;
}

std::string State::ToJson(const std::string& extraFields) const {
    std::string json = "{";
    if (!extraFields.empty()) {
        json += extraFields;
        json += ',';
    }
    char buffer[512];
    std::snprintf(buffer, sizeof(buffer),
                  R"("jd":%.6f,"kp":%.2f,"kpOverride":%.2f,"sunspotNumber":%.1f,"cycle":%d,"cyclePhase":%.4f,)"
                  R"("spotLatitudeDeg":%.2f,)",
                  _jd, Kp(), _kpOverride, _cycle.sunspotNumber, _cycle.cycleNumber, _cycle.phase,
                  _cycle.current.latitudeDeg);
    json += buffer;

    json += "\"cme\":";
    if (_active.empty()) {
        json += "null";
    } else {
        // The one doing the most somewhere: the strongest at any magnetosphere, else the newest.
        const Cme* shown = _active.front();
        double strongest = 0.0;
        for (const BodyState& body : _bodies) {
            if (body.wind.cme >= 0 && body.wind.cmeStrength > strongest) {
                strongest = body.wind.cmeStrength;
                shown = _active[static_cast<std::size_t>(body.wind.cme)];
            }
        }
        int index = -1;
        for (std::size_t i = 0; i < _catalog.events.size(); ++i) {
            if (&_catalog.events[i] == shown) {
                index = static_cast<int>(i);
            }
        }
        std::snprintf(buffer, sizeof(buffer),
                      R"({"index":%d,"launchJd":%.6f,"arrivalJd":%.6f,"speedKmS":%.0f,"frontAu":%.4f,)"
                      R"("frontSpeedKmS":%.0f,"kpMax":%.2f,"hitsEarth":%s,)",
                      index, shown->launchJd, shown->ArrivalAtEarthJd(), shown->speedKmS, FrontKm(*shown, _jd) / kAuKm,
                      FrontSpeedKmS(*shown, _jd), shown->PeakKp(), shown->hitsEarth ? "true" : "false");
        json += buffer;
        json += "\"id\":\"" + EscapeJson(shown->id) + "\",\"name\":\"" + EscapeJson(shown->name) + "\",\"source\":\"" +
                EscapeJson(shown->source) + "\"}";
    }
    json += ',';

    const int next = NextStorm(_jd);
    const int previous = PreviousStorm(_jd);
    AppendStormJson(json, "nextStorm", next >= 0 ? &_catalog.events[static_cast<std::size_t>(next)] : nullptr, next);
    json += ',';
    AppendStormJson(json, "lastStorm", previous >= 0 ? &_catalog.events[static_cast<std::size_t>(previous)] : nullptr,
                    previous);

    json += ",\"bodies\":[";
    for (std::size_t i = 0; i < _bodies.size(); ++i) {
        const BodyState& body = _bodies[i];
        const double night = 3.14159265358979323846;
        std::snprintf(
            buffer, sizeof(buffer),
            R"(%s{"index":%d,"id":"%s","name":"%s","rAu":%.4f,"standoffRadii":%.2f,"quietStandoffRadii":%.2f,)"
            R"("windSpeedKmS":%.0f,"densityCm3":%.3f,"pressureNPa":%.4f,"kp":%.2f,"ovalLatDeg":%.2f,)"
            R"("cmeStrength":%.3f,"storm":%s})",
            i > 0 ? "," : "", static_cast<int>(body.magnetosphere->body), body.magnetosphere->id,
            body.magnetosphere->name, body.rAu, body.standoffRadii, body.quietStandoffRadii, body.wind.speedKmS,
            body.wind.densityPerCm3, body.wind.pressureNPa, body.kp, EquatorwardLatitudeDeg(body.oval, night),
            body.wind.cmeStrength, body.kp >= 5.0 ? "true" : "false");
        json += buffer;
    }
    json += "]}";
    return json;
}

} // namespace SpaceWeather
