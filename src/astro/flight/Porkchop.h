#ifndef SOLARSYSTEM_ASTRO_FLIGHT_PORKCHOP_H
#define SOLARSYSTEM_ASTRO_FLIGHT_PORKCHOP_H
#include <string>
#include <vector>

/**
 * A porkchop plot: departure C3 and arrival v∞ over a grid of departure date × time of
 * flight, one prograde 0-revolution Lambert solve per cell against the ephemeris. The grid is
 * cut into departure-row tiles that run as Jobs::Submit jobs (threaded on the -mt web build and
 * natively, inline on the single-threaded one: same numbers either way).
 */
namespace Flight::Porkchop {

struct Request {
    int departureBody = 3;
    int arrivalBody = 4;
    double departureStartJd = 0.0;
    double departureSpanDays = 120.0;
    double tofMinDays = 120.0;
    double tofMaxDays = 360.0;
    int departureSteps = 64; // columns
    int tofSteps = 48;       // rows
};

struct Grid {
    Request request;
    double departureStepDays = 0.0;
    double tofStepDays = 0.0;
    // Row-major [tofIndex * departureSteps + departureIndex]; NaN where Lambert has no solution.
    std::vector<float> c3;         // km^2/s^2
    std::vector<float> vInfArrive; // km/s
    // Cell with the lowest C3 + v∞² (the cheapest total excess energy).
    int bestDeparture = -1;
    int bestTof = -1;
    double bestDepartureJd = 0.0;
    double bestTofDays = 0.0;
    double bestC3 = 0.0;
    double bestVInfArrive = 0.0;
    int tiles = 0;

    double DepartureJd(int i) const { return request.departureStartJd + i * departureStepDays; }
    double TofDays(int j) const { return request.tofMinDays + j * tofStepDays; }
};

/** One cell: C3 (km^2/s^2) and arrival v∞ (km/s); false where Lambert has no solution. */
bool EvaluateCell(int departureBody, int arrivalBody, double departureJd, double tofDays, double& c3,
                  double& vInfArrive);

/** Compute the grid (tiles through Jobs). Steps are clamped to 2…256. */
Grid Compute(const Request& request);

/** The grid as JSON for the planner page (values rounded to 1e-3; null for no solution). */
std::string ToJson(const Grid& grid);

} // namespace Flight::Porkchop

#endif // SOLARSYSTEM_ASTRO_FLIGHT_PORKCHOP_H
