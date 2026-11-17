#pragma once
// The *active* star system's catalog rows. Every caller reads bodies through here, never
// through the generated kSol* tables, so the same code draws Sol (compiled in, byte for byte
// what it always was) and any system StarSystemCatalog has loaded from
// resource/systems/generated/<id>.runtime.json. See docs/ARCHITECTURE.md §12.
//
// Pointers and references into these rows stay valid until the next
// StarSystemCatalog::Activate*, which Application::LoadSystem only calls after it has torn
// the scene down.

#include "BodyCatalog.generated.h" // IWYU pragma: export

#include <cstddef>

namespace BodyCatalog {

/** A contiguous run of rows, iterable with range-for. */
template <class T>
struct Rows {
    const T* first = nullptr;
    const T* last = nullptr;
    const T* begin() const { return first; }
    const T* end() const { return last; }
    std::size_t size() const { return static_cast<std::size_t>(last - first); }
    bool empty() const { return first == last; }
};

/** Every body row of the active system (planets, dwarf planets, comets, moons; not the star). */
Rows<Entry> Entries();
/** The active system's render.atmosphere rows. */
Rows<AtmosphereRow> Atmospheres();
/** The active system's render.aurora rows (the look; the oval is SpaceWeather's). */
Rows<AuroraRow> Auroras();
/** The active system's star (render.star on index 0). */
const StarRow& Star();

/** Row for a catalog index, or nullptr when that body has no render descriptor. */
const Entry* FindByIndex(int index);
const Entry* FindById(const char* id);
const AtmosphereRow* FindAtmosphere(const char* bodyId);
const AuroraRow* FindAurora(const char* bodyId);
/** Primary (non-satellite) body for a staged-loading initTag, or nullptr. */
const Entry* FindPrimaryByInitTag(const char* initTag);

} // namespace BodyCatalog
