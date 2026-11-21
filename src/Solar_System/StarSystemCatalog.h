#ifndef SOLARSYSTEM_STARSYSTEMCATALOG_H
#define SOLARSYSTEM_STARSYSTEMCATALOG_H

#include "BodyCatalog.h"
#include "OrbitLayout.h"

#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace Json {
class Value;
}

/**
 * Which star system the scene is (docs/ARCHITECTURE.md §12).
 *
 * Sol is compiled in (BodyCatalog.generated.h, OrbitLayoutBodies.generated.inc, the Standish
 * ephemeris) and is the default. Any other system is a generated JSON file,
 * resource/systems/generated/<id>.runtime.json, written by scripts/generate-planet-metadata.mjs
 * from resource/systems/<id>.catalog.json: ParseRuntimeJson turns it into the same row
 * structs, and Activate makes them what BodyCatalog::Entries() and OrbitLayout serve. No
 * system but Sol needs a line of C++.
 *
 * GL-free and single-threaded: activate only from the main thread, between frames, with no
 * scene object holding a row (Application::LoadSystem tears the scene down first).
 */
namespace StarSystemCatalog {

constexpr const char* kSolId = "sol";

/** A loaded system: rows plus the storage their const char* / const wchar_t* point into. */
struct System {
    std::string id;
    std::wstring nameEn;
    std::wstring nameRu;
    double distancePc = 0.0;
    std::string spectralType;
    double habitableZoneInnerAu = 0.0;
    double habitableZoneOuterAu = 0.0;

    BodyCatalog::StarRow star{};
    std::vector<BodyCatalog::Entry> entries;
    std::vector<BodyCatalog::AtmosphereRow> atmospheres;
    std::vector<BodyCatalog::AuroraRow> auroras;
    std::vector<OrbitLayout::BodyRow> layout;

    /** Stable storage for the rows' strings (a deque never moves what it holds). */
    const char* Intern(const std::string& text);
    const wchar_t* InternWide(const std::wstring& text);

private:
    std::deque<std::string> _strings;
    std::deque<std::wstring> _wideStrings;
};

/** resource/systems/generated/<id>.runtime.json. */
std::string RuntimeJsonPath(const std::string& id);

/** True for ids the loader accepts: lowercase letters, digits and '-', 1–48 characters. */
bool IsValidId(const std::string& id);

/** Build a System from a parsed runtime JSON document; null (and `error`) if it is malformed. */
std::unique_ptr<System> ParseRuntimeJson(const Json::Value& document, std::string& error);

/** Read and parse resource/systems/generated/<id>.runtime.json (MEMFS on web: it is preloaded). */
std::unique_ptr<System> LoadRuntimeJson(const std::string& path, std::string& error);

/** Make `system` the active one: its rows for BodyCatalog, its layout for OrbitLayout. */
void Activate(std::unique_ptr<System> system);

/** Back to the compiled Sol tables. */
void ActivateSol();

/** The active system's id ("sol" by default). */
const std::string& ActiveId();
bool IsSol();

/** The active system, or nullptr while it is Sol (whose rows are the compiled tables). */
const System* ActiveSystem();

/** UTF-8 → wide string (catalog names are UTF-8; SpaceObject names are std::wstring). */
std::wstring Widen(const std::string& utf8);

/**
 * The "Earth-like" planet of the active system: the focus index whose insolation is closest to
 * Earth's (StarPhysics::HabitabilityDistance), preferring rocky bodies (under 1.8 R⊕). The
 * camera lands on it after a jump. 3 (Earth) for Sol; 0 if the system has no planet.
 */
int EarthLikeFocusIndex();

/**
 * The focus index a deep link's `planet=` names in the active system: a body id
 * ("trappist_1_e"), the letter or name after the host's ("e", "proxima_cen_b" → "b"), or a
 * number below OrbitLayout::BodyCount(). Case-insensitive; -1 when nothing matches.
 */
int FocusIndexForName(const std::string& name);

} // namespace StarSystemCatalog

#endif // SOLARSYSTEM_STARSYSTEMCATALOG_H
