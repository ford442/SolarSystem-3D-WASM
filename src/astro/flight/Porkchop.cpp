#include "astro/flight/Porkchop.h"
#include "astro/flight/FlightConstants.h"
#include "astro/flight/Lambert.h"
#include "astro/flight/StateVector.h"
#include "core/JobSystem.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <glm/geometric.hpp>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace Flight::Porkchop {

namespace {

constexpr int kRowsPerTile = 8;

void AppendArray(std::ostringstream& out, const std::vector<float>& values) {
    out << '[';
    char buffer[32];
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out << ',';
        }
        if (std::isfinite(values[i])) {
            std::snprintf(buffer, sizeof(buffer), "%.3f", static_cast<double>(values[i]));
            out << buffer;
        } else {
            out << "null";
        }
    }
    out << ']';
}

} // namespace

bool EvaluateCell(int departureBody, int arrivalBody, double departureJd, double tofDays, double& c3,
                  double& vInfArrive) {
    const StateVector from = PlanetState(departureBody, departureJd);
    const StateVector to = PlanetState(arrivalBody, departureJd + tofDays);
    Lambert::Solution solution;
    if (!Lambert::SolveSingle(kMuSun, from.r, to.r, tofDays * kSecondsPerDay, solution)) {
        return false;
    }
    const glm::dvec3 vInfDepart = solution.v1 - from.v;
    c3 = glm::dot(vInfDepart, vInfDepart);
    vInfArrive = glm::length(solution.v2 - to.v);
    return std::isfinite(c3) && std::isfinite(vInfArrive);
}

Grid Compute(const Request& request) {
    Grid grid;
    grid.request = request;
    grid.request.departureSteps = std::clamp(request.departureSteps, 2, 256);
    grid.request.tofSteps = std::clamp(request.tofSteps, 2, 256);
    grid.request.tofMinDays = std::max(1.0, request.tofMinDays);
    grid.request.tofMaxDays = std::max(grid.request.tofMinDays + 1.0, request.tofMaxDays);
    grid.request.departureSpanDays = std::max(1.0, request.departureSpanDays);
    const int nx = grid.request.departureSteps;
    const int ny = grid.request.tofSteps;
    grid.departureStepDays = grid.request.departureSpanDays / (nx - 1);
    grid.tofStepDays = (grid.request.tofMaxDays - grid.request.tofMinDays) / (ny - 1);
    const std::size_t cells = static_cast<std::size_t>(nx) * static_cast<std::size_t>(ny);
    grid.c3.assign(cells, std::numeric_limits<float>::quiet_NaN());
    grid.vInfArrive.assign(cells, std::numeric_limits<float>::quiet_NaN());
    if (!FindBody(grid.request.departureBody) || !FindBody(grid.request.arrivalBody) ||
        grid.request.departureBody == grid.request.arrivalBody) {
        return grid;
    }

    struct Tile {
        int firstRow = 0;
        std::vector<float> c3;
        std::vector<float> vInf;
    };
    std::vector<Jobs::Future<Tile>> jobs;
    for (int row = 0; row < ny; row += kRowsPerTile) {
        const int rows = std::min(kRowsPerTile, ny - row);
        // Each job owns its rows and writes nothing shared; the ephemeris is pure.
        jobs.push_back(Jobs::Submit([request = grid.request, depStep = grid.departureStepDays,
                                     tofStep = grid.tofStepDays, row, rows, nx] {
            Tile tile;
            tile.firstRow = row;
            tile.c3.assign(static_cast<std::size_t>(rows) * static_cast<std::size_t>(nx),
                           std::numeric_limits<float>::quiet_NaN());
            tile.vInf.assign(tile.c3.size(), std::numeric_limits<float>::quiet_NaN());
            for (int j = 0; j < rows; ++j) {
                const double tof = request.tofMinDays + (row + j) * tofStep;
                for (int i = 0; i < nx; ++i) {
                    double c3 = 0.0;
                    double vInf = 0.0;
                    if (EvaluateCell(request.departureBody, request.arrivalBody, request.departureStartJd + i * depStep,
                                     tof, c3, vInf)) {
                        const auto k =
                            (static_cast<std::size_t>(j) * static_cast<std::size_t>(nx)) + static_cast<std::size_t>(i);
                        tile.c3[k] = static_cast<float>(c3);
                        tile.vInf[k] = static_cast<float>(vInf);
                    }
                }
            }
            return tile;
        }));
    }
    grid.tiles = static_cast<int>(jobs.size());
    for (auto& job : jobs) {
        Tile tile = job.Get();
        std::copy(tile.c3.begin(), tile.c3.end(), grid.c3.begin() + (static_cast<std::ptrdiff_t>(tile.firstRow) * nx));
        std::copy(tile.vInf.begin(), tile.vInf.end(),
                  grid.vInfArrive.begin() + (static_cast<std::ptrdiff_t>(tile.firstRow) * nx));
    }

    double best = std::numeric_limits<double>::infinity();
    for (int j = 0; j < ny; ++j) {
        for (int i = 0; i < nx; ++i) {
            const auto k = (static_cast<std::size_t>(j) * static_cast<std::size_t>(nx)) + static_cast<std::size_t>(i);
            const double c3 = grid.c3[k];
            const double vInf = grid.vInfArrive[k];
            if (!std::isfinite(c3) || !std::isfinite(vInf)) {
                continue;
            }
            const double cost = c3 + vInf * vInf;
            if (cost < best) {
                best = cost;
                grid.bestDeparture = i;
                grid.bestTof = j;
            }
        }
    }
    if (grid.bestDeparture >= 0) {
        // The reported optimum is the cell's own Lambert solve in double, not its float copy.
        grid.bestDepartureJd = grid.DepartureJd(grid.bestDeparture);
        grid.bestTofDays = grid.TofDays(grid.bestTof);
        EvaluateCell(grid.request.departureBody, grid.request.arrivalBody, grid.bestDepartureJd, grid.bestTofDays,
                     grid.bestC3, grid.bestVInfArrive);
    }
    return grid;
}

std::string ToJson(const Grid& grid) {
    std::ostringstream out;
    char buffer[512];
    std::snprintf(buffer, sizeof(buffer),
                  R"({"departureBody":%d,"arrivalBody":%d,"departureStartJd":%.4f,"departureStepDays":%.6f,)"
                  R"("departureSteps":%d,"tofMinDays":%.4f,"tofStepDays":%.6f,"tofSteps":%d,"tiles":%d,)",
                  grid.request.departureBody, grid.request.arrivalBody, grid.request.departureStartJd,
                  grid.departureStepDays, grid.request.departureSteps, grid.request.tofMinDays, grid.tofStepDays,
                  grid.request.tofSteps, grid.tiles);
    out << buffer;
    if (grid.bestDeparture >= 0) {
        std::snprintf(buffer, sizeof(buffer),
                      R"("best":{"departureJd":%.4f,"tofDays":%.4f,"c3":%.4f,"vInfArrive":%.4f},)",
                      grid.bestDepartureJd, grid.bestTofDays, grid.bestC3, grid.bestVInfArrive);
        out << buffer;
    } else {
        out << R"("best":null,)";
    }
    out << R"("c3":)";
    AppendArray(out, grid.c3);
    out << R"(,"vInfArrive":)";
    AppendArray(out, grid.vInfArrive);
    out << '}';
    return out.str();
}

} // namespace Flight::Porkchop
