#include "StarSystemCatalog.h"

#include "BodyCatalog.h"
#include "OrbitLayout.h"

#include "../astro/StarPhysics.h"
#include "../core/Json.h"

#include <cmath>
#include <cstdlib>
#include <string>
#include <cstdint>
#include <iterator>
#include <memory>
#include <utility>

namespace StarSystemCatalog {
namespace {

std::unique_ptr<System> g_active; // null => Sol
std::string g_activeId = kSolId;

float F(const Json::Value& v, float fallback = 0.0f) {
    return static_cast<float>(v.AsNumber(static_cast<double>(fallback)));
}

BodyCatalog::Rgb RgbFrom(const Json::Value& v) {
    return {F(v[std::size_t{0}]), F(v[std::size_t{1}]), F(v[std::size_t{2}])};
}

bool KindFrom(const std::string& kind, BodyCatalog::Kind& out) {
    if (kind == "planet") {
        out = BodyCatalog::Kind::Planet;
    } else if (kind == "dwarf_planet") {
        out = BodyCatalog::Kind::DwarfPlanet;
    } else if (kind == "satellite") {
        out = BodyCatalog::Kind::Satellite;
    } else if (kind == "comet") {
        out = BodyCatalog::Kind::Comet;
    } else if (kind == "star") {
        out = BodyCatalog::Kind::Star;
    } else {
        return false;
    }
    return true;
}

const char* NullableString(System& system, const Json::Value& v) {
    return v.IsString() ? system.Intern(v.AsString()) : nullptr;
}

bool ParseEntry(System& system, const Json::Value& e, BodyCatalog::Entry& out, std::string& error) {
    out = BodyCatalog::Entry{};
    if (!e.IsObject() || !e["id"].IsString() || !e["index"].IsNumber()) {
        error = "entry without id/index";
        return false;
    }
    const std::string id = e["id"].AsString();
    out.index = static_cast<int>(e["index"].AsNumber());
    out.id = system.Intern(id);
    out.parentId = NullableString(system, e["parentId"]);
    out.initTag = system.Intern(e["initTag"].AsString(id));
    if (!KindFrom(e["kind"].AsString(), out.kind) || out.kind == BodyCatalog::Kind::Star) {
        error = id + ": unknown or star kind \"" + e["kind"].AsString() + "\"";
        return false;
    }
    out.displayNameEn = system.InternWide(Widen(e["displayNameEn"].AsString(id)));
    out.displayNameRu = system.InternWide(Widen(e["displayNameRu"].AsString(e["displayNameEn"].AsString(id))));
    out.auDistance = F(e["auDistance"]);
    out.orbitalPeriodDays = F(e["orbitalPeriodDays"]);
    out.earthRadiusScale = F(e["earthRadiusScale"], 1.0f);
    out.radiusKm = F(e["radiusKm"]);
    if (!(out.radiusKm > 0.0f)) {
        error = id + ": radiusKm must be positive (realistic scale draws the true radius)";
        return false;
    }
    out.axialTiltDegrees = F(e["axialTiltDegrees"]);
    out.artTiltXDegrees = F(e["artTiltXDegrees"]);
    out.oblateness = F(e["oblateness"]);
    out.yawOffsetDegrees = F(e["yawOffsetDegrees"]);
    out.artRollDegrees = F(e["artRollDegrees"]);
    const Json::Value& flags = e["shaderFlags"];
    out.shaderFlags = {flags["hasNightTexture"].AsBool(), flags["hasSpecularMap"].AsBool(),
                       flags["hasClouds"].AsBool()};
    out.useSphereIntersect = e["useSphereIntersect"].AsBool();
    out.ambientFactor = F(e["ambientFactor"]);
    const Json::Value& lod = e["lod"];
    out.lod.diffuse = system.Intern(lod["diffuse"].AsString(""));
    out.lod.normal = system.Intern(lod["normal"].AsString(""));
    out.lod.specular = NullableString(system, lod["specular"]);
    out.lod.night = NullableString(system, lod["night"]);
    out.lod.clouds = NullableString(system, lod["clouds"]);
    out.meshPath = NullableString(system, e["meshPath"]);
    out.sceneOrbitRadius = F(e["sceneOrbitRadius"]);
    out.orbitPlane = e["orbitPlane"].AsString("xz") == "xy" ? BodyCatalog::OrbitPlane::XY : BodyCatalog::OrbitPlane::XZ;
    out.initialAnomalyRad = F(e["initialAnomalyRad"]);
    out.spinDegPerSimSecond = F(e["spinDegPerSimSecond"]);
    const Json::Value& k = e["keplerian"];
    out.keplerian = {F(k["aKm"]),
                     F(k["e"]),
                     F(k["iDeg"]),
                     F(k["OmegaDeg"]),
                     F(k["omegaDeg"]),
                     F(k["M0Deg"]),
                     F(k["nDegPerDay"]),
                     F(k["OmegaDotDegPerDay"]),
                     F(k["omegaDotDegPerDay"])};
    const Json::Value& cloud = e["cloudLayer"];
    out.cloudLayer = {NullableString(system, cloud["diffuse"]), NullableString(system, cloud["normal"]),
                      F(cloud["scaleFactor"]), F(cloud["spinDegPerSimSecond"]), F(cloud["ambientFactor"])};
    out.hasCloudLayer = e["hasCloudLayer"].AsBool();
    out.hasRings = e["hasRings"].AsBool();
    out.hasTerrain = e["hasTerrain"].AsBool();
    out.terrainAttribution = NullableString(system, e["terrainAttribution"]);
    const Json::Value& p = e["procedural"];
    out.procedural.enabled = p["enabled"].AsBool();
    out.procedural.mode = static_cast<uint8_t>(p["mode"].AsNumber(0.0));
    out.procedural.seed = F(p["seed"]);
    for (std::size_t i = 0; i < 3; ++i) {
        out.procedural.colors[i] = RgbFrom(p["colors"][i]);
    }
    out.procedural.bands = F(p["bands"]);
    out.procedural.seaLevel = F(p["seaLevel"]);
    out.procedural.polarCap = F(p["polarCap"]);
    out.procedural.emissive = F(p["emissive"]);
    out.procedural.equilibriumTemperatureK = F(p["equilibriumTemperatureK"]);
    out.placeFromElements = e["placeFromElements"].AsBool();
    return true;
}

BodyCatalog::AtmosphereRow ParseAtmosphere(System& system, const Json::Value& a) {
    BodyCatalog::AtmosphereRow row{};
    row.bodyId = system.Intern(a["bodyId"].AsString());
    const Json::Value& o = a["oneil"];
    row.oneil = {F(o["shellScale"]),       RgbFrom(o["color"]),   o["innerRadiusMinusEpsilon"].AsBool(),
                 F(o["outerRadius"]),      RgbFrom(o["mieTint"]), F(o["hScaleFactor"]),
                 o["toneMapping"].AsBool()};
    const Json::Value& p = a["physical"];
    if (p.IsObject()) {
        row.physical = {true,
                        F(p["groundRadiusKm"]),
                        F(p["topRadiusKm"]),
                        F(p["thicknessScale"]),
                        RgbFrom(p["rayleighScattering"]),
                        F(p["rayleighScaleHeightKm"]),
                        RgbFrom(p["mieScattering"]),
                        RgbFrom(p["mieExtinction"]),
                        F(p["mieScaleHeightKm"]),
                        F(p["miePhaseG"]),
                        RgbFrom(p["absorptionExtinction"]),
                        F(p["absorptionCenterKm"]),
                        F(p["absorptionHalfWidthKm"]),
                        RgbFrom(p["groundAlbedo"]),
                        F(p["exposure"])};
    }
    return row;
}

OrbitLayout::BodyRow ParseLayoutRow(const Json::Value& r) {
    OrbitLayout::BodyRow row;
    const Json::Value& offset = r["compressedOffset"];
    row.compressedOffset = glm::vec3(F(offset[std::size_t{0}]), F(offset[std::size_t{1}]), F(offset[std::size_t{2}]));
    row.auDistance = F(r["auDistance"]);
    row.orbitalPeriodDays = F(r["orbitalPeriodDays"]);
    row.inclinationDeg = F(r["inclinationDeg"]);
    row.siderealRotationDays = F(r["siderealRotationDays"]);
    row.primeMeridianDegJ2000 = r["primeMeridianDegJ2000"].AsNumber();
    row.primeMeridianRateDegPerDay = r["primeMeridianRateDegPerDay"].AsNumber();
    row.trueRadius = r["trueRadius"].AsBool();
    const Json::Value& k = r["kepler"];
    if (k.IsObject()) {
        row.kepler.aAu = k["aAu"].AsNumber();
        row.kepler.e = k["e"].AsNumber();
        row.kepler.iDeg = k["iDeg"].AsNumber();
        row.kepler.OmegaDeg = k["OmegaDeg"].AsNumber();
        row.kepler.omegaDeg = k["omegaDeg"].AsNumber();
        row.kepler.M0Deg = k["M0Deg"].AsNumber();
        row.kepler.nDegPerDay = k["nDegPerDay"].AsNumber();
    }
    return row;
}

} // namespace

const char* System::Intern(const std::string& text) {
    _strings.push_back(text);
    return _strings.back().c_str();
}

const wchar_t* System::InternWide(const std::wstring& text) {
    _wideStrings.push_back(text);
    return _wideStrings.back().c_str();
}

std::wstring Widen(const std::string& utf8) {
    std::wstring out;
    out.reserve(utf8.size());
    for (std::size_t i = 0; i < utf8.size();) {
        const auto c = static_cast<unsigned char>(utf8[i]);
        uint32_t cp = 0;
        std::size_t extra = 0;
        if (c < 0x80) {
            cp = c;
        } else if ((c & 0xE0) == 0xC0) {
            cp = c & 0x1F;
            extra = 1;
        } else if ((c & 0xF0) == 0xE0) {
            cp = c & 0x0F;
            extra = 2;
        } else if ((c & 0xF8) == 0xF0) {
            cp = c & 0x07;
            extra = 3;
        } else {
            cp = 0xFFFD; // stray continuation byte
        }
        ++i;
        for (std::size_t n = 0; n < extra && i < utf8.size(); ++n, ++i) {
            cp = (cp << 6) | (static_cast<unsigned char>(utf8[i]) & 0x3F);
        }
        if (sizeof(wchar_t) == 2 && cp > 0xFFFF) {
            // UTF-16 wchar_t (Windows): a surrogate pair.
            cp -= 0x10000;
            out.push_back(static_cast<wchar_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<wchar_t>(0xDC00 + (cp & 0x3FF)));
        } else {
            out.push_back(static_cast<wchar_t>(cp));
        }
    }
    return out;
}

std::string RuntimeJsonPath(const std::string& id) {
    return "resource/systems/generated/" + id + ".runtime.json";
}

bool IsValidId(const std::string& id) {
    if (id.empty() || id.size() > 48) {
        return false;
    }
    for (const char c : id) {
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) {
            return false;
        }
    }
    return true;
}

std::unique_ptr<System> ParseRuntimeJson(const Json::Value& document, std::string& error) {
    if (document["format"].AsString() != "solarsystem-runtime-catalog" || document["version"].AsNumber() != 1.0) {
        error = "not a solarsystem-runtime-catalog v1 document";
        return nullptr;
    }
    auto system = std::make_unique<System>();
    system->id = document["id"].AsString();
    if (!IsValidId(system->id)) {
        error = "invalid system id \"" + system->id + "\"";
        return nullptr;
    }
    system->nameEn = Widen(document["name"]["en"].AsString(system->id));
    system->nameRu = Widen(document["name"]["ru"].AsString(document["name"]["en"].AsString(system->id)));
    system->distancePc = document["distancePc"].AsNumber();
    system->spectralType = document["spectralType"].AsString("");
    system->habitableZoneInnerAu = document["habitableZoneAu"]["innerAu"].AsNumber();
    system->habitableZoneOuterAu = document["habitableZoneAu"]["outerAu"].AsNumber();

    const Json::Value& star = document["star"];
    const double teff = star["effectiveTemperatureK"].AsNumber();
    const double radiusKm = star["radiusKm"].AsNumber();
    if (!(teff >= 1000.0) || !(radiusKm > 0.0)) {
        error = system->id + ": star needs effectiveTemperatureK and radiusKm";
        return nullptr;
    }
    BodyCatalog::StarRow& s = system->star;
    s.id = system->Intern(star["id"].AsString(system->id));
    s.displayNameEn = system->InternWide(Widen(star["displayNameEn"].AsString(system->id)));
    s.displayNameRu =
        system->InternWide(Widen(star["displayNameRu"].AsString(star["displayNameEn"].AsString(system->id))));
    s.spectralType = system->Intern(star["spectralType"].AsString(""));
    s.effectiveTemperatureK = static_cast<float>(teff);
    // The glow/corona ramp stays at the Sun's calibration for every star: a star's colour is
    // its Krystek tint (StarPhysics::RelativeTint) times that ramp, so it is applied once.
    s.glowTemperatureK = F(star["glowTemperatureK"], BodyCatalog::kSolStar.glowTemperatureK);
    s.radiusKm = radiusKm;
    s.radiusSolar = F(star["radiusSolar"], static_cast<float>(radiusKm / StarPhysics::kSunRadiusKm));
    s.luminositySolar =
        F(star["luminositySolar"], static_cast<float>(StarPhysics::LuminosityFromRadius(s.radiusSolar, teff)));
    s.massSolar = F(star["massSolar"]);
    s.activity = F(star["activity"]);
    s.cyclePeriodYears = F(star["cyclePeriodYears"]);
    s.cycleMinimumYear = F(star["cycleMinimumYear"]);
    s.cycleAtMinimum = static_cast<int>(star["cycleAtMinimum"].AsNumber(0.0));
    s.cycleMeanPeakSunspots = F(star["cycleMeanPeakSunspots"]);

    for (const Json::Value& row : document["layout"].Items()) {
        system->layout.push_back(ParseLayoutRow(row));
    }
    if (system->layout.empty()) {
        error = system->id + ": empty layout (the star is row 0)";
        return nullptr;
    }
    for (std::size_t i = 0; i < system->layout.size(); ++i) {
        if (static_cast<std::size_t>(document["layout"][i]["index"].AsNumber(-1.0)) != i) {
            error = system->id + ": layout rows must be focus indices 0…N-1 in order";
            return nullptr;
        }
    }

    for (const Json::Value& e : document["entries"].Items()) {
        BodyCatalog::Entry entry{};
        if (!ParseEntry(*system, e, entry, error)) {
            error.insert(0, system->id + ": ");
            return nullptr;
        }
        system->entries.push_back(entry);
    }
    for (const Json::Value& a : document["atmospheres"].Items()) {
        system->atmospheres.push_back(ParseAtmosphere(*system, a));
    }
    for (const Json::Value& a : document["auroras"].Items()) {
        BodyCatalog::AuroraRow row{};
        row.bodyId = system->Intern(a["bodyId"].AsString());
        row.bottomKm = F(a["bottomKm"]);
        row.topKm = F(a["topKm"]);
        row.colorLow = RgbFrom(a["colorLow"]);
        row.colorHigh = RgbFrom(a["colorHigh"]);
        row.colorEdge = RgbFrom(a["colorEdge"]);
        row.brightness = F(a["brightness"], 1.0f);
        system->auroras.push_back(row);
    }
    return system;
}

std::unique_ptr<System> LoadRuntimeJson(const std::string& path, std::string& error) {
    Json::Value document;
    if (!Json::ParseFile(path, document, error)) {
        return nullptr;
    }
    return ParseRuntimeJson(document, error);
}

void Activate(std::unique_ptr<System> system) {
    if (!system) {
        ActivateSol();
        return;
    }
    g_active = std::move(system);
    g_activeId = g_active->id;
    OrbitLayout::SetSystemRows(g_active->layout.data(), static_cast<int>(g_active->layout.size()));
}

void ActivateSol() {
    OrbitLayout::SetSystemRows(nullptr, 0);
    g_active.reset();
    g_activeId = kSolId;
}

const std::string& ActiveId() {
    return g_activeId;
}

bool IsSol() {
    return g_active == nullptr;
}

const System* ActiveSystem() {
    return g_active.get();
}

int EarthLikeFocusIndex() {
    if (IsSol()) {
        return static_cast<int>(OrbitLayout::Body::Earth);
    }
    const double luminosity = BodyCatalog::Star().luminositySolar;
    int best = 0;
    double bestScore = 0.0;
    for (const BodyCatalog::Entry& entry : BodyCatalog::Entries()) {
        if (entry.kind == BodyCatalog::Kind::Satellite || entry.auDistance <= 0.0f) {
            continue;
        }
        // Rocky first: a temperate mini-Neptune is not where a reader expects to land.
        const double rockyPenalty = entry.earthRadiusScale < 1.8f ? 0.0 : 2.0;
        const double score = StarPhysics::HabitabilityDistance(luminosity, entry.auDistance) + rockyPenalty;
        if (best == 0 || score < bestScore) {
            best = entry.index;
            bestScore = score;
        }
    }
    return best;
}

int FocusIndexForName(const std::string& name) {
    if (name.empty() || name.size() > 64) {
        return -1;
    }
    std::string wanted;
    wanted.reserve(name.size());
    bool numeric = true;
    for (const char c : name) {
        const char lower = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        wanted.push_back(lower == '-' ? '_' : lower);
        numeric = numeric && c >= '0' && c <= '9';
    }
    if (numeric) {
        const int index = std::atoi(wanted.c_str());
        return index < OrbitLayout::BodyCount() ? index : -1;
    }
    // The star is its own row (BodyCatalog::Star), always focus index 0.
    if (const char* starId = BodyCatalog::Star().id; starId && wanted == starId) {
        return 0;
    }
    std::string suffix = "_";
    suffix += wanted;
    for (const BodyCatalog::Entry& entry : BodyCatalog::Entries()) {
        if (!entry.id || entry.index >= OrbitLayout::BodyCount()) {
            continue;
        }
        const std::string id(entry.id);
        if (id == wanted ||
            (id.size() > suffix.size() && id.compare(id.size() - suffix.size(), suffix.size(), suffix) == 0)) {
            return entry.index;
        }
    }
    return -1;
}

} // namespace StarSystemCatalog

// --- BodyCatalog: the active system's rows -------------------------------------------------

namespace BodyCatalog {

Rows<Entry> Entries() {
    if (const StarSystemCatalog::System* system = StarSystemCatalog::ActiveSystem()) {
        return {system->entries.data(), system->entries.data() + system->entries.size()};
    }
    return {std::begin(kSolEntries), std::end(kSolEntries)};
}

Rows<AtmosphereRow> Atmospheres() {
    if (const StarSystemCatalog::System* system = StarSystemCatalog::ActiveSystem()) {
        return {system->atmospheres.data(), system->atmospheres.data() + system->atmospheres.size()};
    }
    return {std::begin(kSolAtmospheres), std::end(kSolAtmospheres)};
}

Rows<AuroraRow> Auroras() {
    if (const StarSystemCatalog::System* system = StarSystemCatalog::ActiveSystem()) {
        return {system->auroras.data(), system->auroras.data() + system->auroras.size()};
    }
    return {std::begin(kSolAuroras), std::end(kSolAuroras)};
}

const StarRow& Star() {
    if (const StarSystemCatalog::System* system = StarSystemCatalog::ActiveSystem()) {
        return system->star;
    }
    return kSolStar;
}

const Entry* FindByIndex(int index) {
    for (const Entry& entry : Entries()) {
        if (entry.index == index) {
            return &entry;
        }
    }
    return nullptr;
}

const Entry* FindById(const char* id) {
    for (const Entry& entry : Entries()) {
        if (StrEq(entry.id, id)) {
            return &entry;
        }
    }
    return nullptr;
}

const AtmosphereRow* FindAtmosphere(const char* bodyId) {
    for (const AtmosphereRow& row : Atmospheres()) {
        if (StrEq(row.bodyId, bodyId)) {
            return &row;
        }
    }
    return nullptr;
}

const AuroraRow* FindAurora(const char* bodyId) {
    for (const AuroraRow& row : Auroras()) {
        if (StrEq(row.bodyId, bodyId)) {
            return &row;
        }
    }
    return nullptr;
}

const Entry* FindPrimaryByInitTag(const char* initTag) {
    for (const Entry& entry : Entries()) {
        if (entry.kind != Kind::Satellite && StrEq(entry.initTag, initTag)) {
            return &entry;
        }
    }
    return nullptr;
}

} // namespace BodyCatalog
