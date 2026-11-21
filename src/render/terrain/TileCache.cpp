#include "TileCache.h"

#include "../../core/Log.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace Terrain {

namespace {

constexpr const char* kFormat = "ssterrain-1";

/** Enough JSON for tileset.json: one object of strings, numbers and number arrays; anything else is skipped. */
class FlatJson {
public:
    explicit FlatJson(const std::string& text) : _text(text) {}

    template <typename OnField>
    bool Object(OnField&& onField) {
        if (!Consume('{')) {
            return false;
        }
        if (Consume('}')) {
            return AtEnd();
        }
        do {
            std::string key;
            if (!String(key) || !Consume(':') || !onField(key, *this)) {
                return false;
            }
        } while (Consume(','));
        return Consume('}') && AtEnd();
    }

    bool String(std::string& out) {
        if (!Consume('"')) {
            return false;
        }
        out.clear();
        while (_at < _text.size() && _text[_at] != '"') {
            char c = _text[_at++];
            if (c == '\\' && _at < _text.size()) {
                c = _text[_at++];
                if (c == 'u') { // tileset.json is ASCII; keep the escape as written
                    out += "\\u";
                    continue;
                }
                c = c == 'n' ? '\n' : (c == 't' ? '\t' : c);
            }
            out.push_back(c);
        }
        return Consume('"');
    }

    bool Number(double& out) {
        Space();
        const char* begin = _text.c_str() + _at;
        char* end = nullptr;
        out = std::strtod(begin, &end);
        if (end == begin || !std::isfinite(out)) {
            return false;
        }
        _at += static_cast<std::size_t>(end - begin);
        return true;
    }

    bool Numbers(std::vector<double>& out) {
        if (!Consume('[')) {
            return false;
        }
        if (Consume(']')) {
            return true;
        }
        do {
            double value;
            if (!Number(value)) {
                return false;
            }
            out.push_back(value);
        } while (Consume(','));
        return Consume(']');
    }

    bool Skip() {
        Space();
        if (_at >= _text.size()) {
            return false;
        }
        const char c = _text[_at];
        if (c == '"') {
            std::string ignored;
            return String(ignored);
        }
        if (c == '{' || c == '[') {
            const char close = c == '{' ? '}' : ']';
            ++_at;
            if (Consume(close)) {
                return true;
            }
            do {
                if (c == '{') {
                    std::string key;
                    if (!String(key) || !Consume(':')) {
                        return false;
                    }
                }
                if (!Skip()) {
                    return false;
                }
            } while (Consume(','));
            return Consume(close);
        }
        for (const char* word : {"true", "false", "null"}) {
            const std::size_t length = std::char_traits<char>::length(word);
            if (_text.compare(_at, length, word) == 0) {
                _at += length;
                return true;
            }
        }
        double ignored;
        return Number(ignored);
    }

private:
    void Space() {
        while (_at < _text.size() && std::isspace(static_cast<unsigned char>(_text[_at]))) {
            ++_at;
        }
    }
    bool Consume(char c) {
        Space();
        if (_at < _text.size() && _text[_at] == c) {
            ++_at;
            return true;
        }
        return false;
    }
    bool AtEnd() {
        Space();
        return _at == _text.size();
    }

    const std::string& _text;
    std::size_t _at = 0;
};

} // namespace

std::optional<TileSet> ParseTileSet(const std::string& json, std::string* error) {
    const auto fail = [error](const std::string& why) -> std::optional<TileSet> {
        if (error) {
            *error = why;
        }
        return std::nullopt;
    };
    TileSet set;
    std::string format;
    double maxLevel = -1.0;
    bool haveRadius = false;
    FlatJson reader(json);
    const bool parsed = reader.Object([&](const std::string& key, FlatJson& value) {
        if (key == "format") {
            return value.String(format);
        }
        if (key == "body") {
            return value.String(set.body);
        }
        if (key == "attribution") {
            return value.String(set.attribution);
        }
        if (key == "radiusKm") {
            haveRadius = true;
            return value.Number(set.radiusKm);
        }
        if (key == "maxLevel") {
            return value.Number(maxLevel);
        }
        if (key == "minHeightM") {
            return value.Number(set.minHeightM);
        }
        if (key == "maxHeightM") {
            return value.Number(set.maxHeightM);
        }
        if (key == "faces") {
            std::vector<double> faces;
            if (!value.Numbers(faces)) {
                return false;
            }
            for (const double face : faces) {
                if (face != std::floor(face) || face < 0.0 || face >= kFaceCount) {
                    return false;
                }
                set.faces |= static_cast<uint8_t>(1u << static_cast<int>(face));
            }
            return true;
        }
        return value.Skip();
    });
    if (!parsed) {
        return fail("not a tile set object");
    }
    if (format != kFormat) {
        return fail("format \"" + format + "\", expected \"" + kFormat + "\"");
    }
    if (!haveRadius || !(set.radiusKm > 0.0) || maxLevel != std::floor(maxLevel) || maxLevel < 0.0 ||
        maxLevel > kMaxLevel || set.maxHeightM < set.minHeightM || set.body.empty()) {
        return fail("bad body, radiusKm, maxLevel or height range");
    }
    set.maxLevel = static_cast<int>(maxLevel);
    return set;
}

std::string TileSetPath(const std::string& body) {
    return "resource/terrain/" + body + "/tileset.json";
}

std::string TilePath(const std::string& body, const TileKey& key) {
    return "resource/terrain/" + body + "/" + std::to_string(key.face) + "/" + std::to_string(key.level) + "/" +
           std::to_string(key.x) + "/" + std::to_string(key.y) + ".ktx2";
}

class TileCache::SlotSource : public TileSource {
public:
    SlotSource(TileCache& cache, int slot) : _cache(cache), _slot(slot) {}

    const HeightTile* Root(int face) override {
        return _cache.Find(_slot, TileKey{static_cast<uint8_t>(face), 0, 0, 0});
    }

    const HeightTile* Child(const HeightTile& parent, int quadrant, const RequestPriority& priority) override {
        const TileKey key = parent.key.Child(quadrant);
        if (const HeightTile* resident = _cache.Find(_slot, key)) {
            return resident;
        }
        const uint64_t id = Id(_slot, key);
        const TileSet* set = _cache.Set(_slot);
        const bool hasData = ((parent.childMask >> quadrant) & 1u) && set && key.level <= set->maxLevel &&
                             !_cache._absent.count(id);
        if (hasData) {
            if (!_cache._inFlight.count(id)) {
                _cache.Want(_slot, key, priority);
            }
            return nullptr;
        }
        if (_cache._synthesizedThisFrame >= _cache._limits.maxSynthesizedPerFrame) {
            return nullptr;
        }
        ++_cache._synthesizedThisFrame;
        return _cache.Insert(_slot, UpsampleChild(parent, quadrant), false);
    }

    void Touch(const HeightTile& tile) override {
        const auto found = _cache._entries.find(Id(_slot, tile.key));
        if (found != _cache._entries.end()) {
            found->second->lastUsed = _cache._frame;
        }
    }

private:
    TileCache& _cache;
    int _slot;
};

TileCache::TileCache(FetchFn fetch) : _fetch(std::move(fetch)) {}

TileCache::Body& TileCache::BodyAt(int slot) {
    Body& body = _bodies[slot];
    if (!body.source) {
        body.source = std::make_unique<SlotSource>(*this, slot);
    }
    return body;
}

TileCache::SetState TileCache::Prepare(int slot, const std::string& name) {
    Body& body = BodyAt(slot);
    if (body.state == SetState::Idle) {
        body.name = name;
        body.state = SetState::Loading;
        const auto arrivals = _arrivals;
        _fetch(TileSetPath(name), [arrivals, slot](std::optional<std::vector<uint8_t>> bytes) {
            arrivals->push_back(Arrival{slot, true, TileKey{}, std::move(bytes)});
        });
    }
    return body.state;
}

TileCache::SetState TileCache::State(int slot) const {
    const auto found = _bodies.find(slot);
    return found == _bodies.end() ? SetState::Idle : found->second.state;
}

const TileSet* TileCache::Set(int slot) const {
    const auto found = _bodies.find(slot);
    return found != _bodies.end() && found->second.set ? &*found->second.set : nullptr;
}

bool TileCache::Ready(int slot) const {
    const auto found = _bodies.find(slot);
    return found != _bodies.end() && found->second.state == SetState::Ready &&
           found->second.rootsResident == kFaceCount;
}

TileSource& TileCache::Source(int slot) {
    return *BodyAt(slot).source;
}

const HeightTile* TileCache::Find(int slot, const TileKey& key) const {
    const auto found = _entries.find(Id(slot, key));
    return found == _entries.end() ? nullptr : &found->second->tile;
}

const HeightTile* TileCache::Insert(int slot, HeightTile tile, bool pinned) {
    auto entry = std::make_unique<Entry>();
    entry->tile = std::move(tile);
    entry->tile.uid = _nextUid++;
    entry->lastUsed = _frame;
    entry->pinned = pinned;
    std::unique_ptr<Entry>& stored = _entries[Id(slot, entry->tile.key)];
    if (stored) {
        _evicted.push_back(stored->tile.uid);
    }
    stored = std::move(entry);
    return &stored->tile;
}

void TileCache::Want(int slot, const TileKey& key, const RequestPriority& priority) {
    const auto [at, inserted] = _wanted.try_emplace(Id(slot, key), Request{slot, key, priority});
    if (!inserted && priority < at->second.priority) {
        at->second.priority = priority;
    }
}

void TileCache::Fetch(const Request& request) {
    _inFlight.insert(Id(request.slot, request.key));
    ++_requested;
    const auto arrivals = _arrivals;
    const int slot = request.slot;
    const TileKey key = request.key;
    _fetch(TilePath(BodyAt(slot).name, key), [arrivals, slot, key](std::optional<std::vector<uint8_t>> bytes) {
        arrivals->push_back(Arrival{slot, false, key, std::move(bytes)});
    });
}

void TileCache::Arrive(Arrival arrival) {
    Body& body = BodyAt(arrival.slot);
    if (arrival.isSet) {
        std::string error = "not found";
        std::optional<TileSet> set;
        if (arrival.bytes) {
            set = ParseTileSet(std::string(arrival.bytes->begin(), arrival.bytes->end()), &error);
        }
        if (!set) {
            body.state = SetState::Missing;
            LOG_INFO("Terrain", "%s: no tile set (%s); it stays the sphere", body.name.c_str(), error.c_str());
            return;
        }
        body.set = std::move(set);
        body.state = SetState::Ready;
        LOG_INFO("Terrain", "%s: tile set levels 0-%d, heights %.0f..%.0f m, faces 0x%02x", body.name.c_str(),
                 body.set->maxLevel, body.set->minHeightM, body.set->maxHeightM, body.set->faces);
        for (int face = 0; face < kFaceCount; ++face) {
            const TileKey root{static_cast<uint8_t>(face), 0, 0, 0};
            if ((body.set->faces >> face) & 1u) {
                Fetch(Request{arrival.slot, root, RequestPriority{}});
            } else {
                Insert(arrival.slot, FlatTile(root), true); // no data on this face: the sphere
                ++body.rootsResident;
            }
        }
        return;
    }

    const uint64_t id = Id(arrival.slot, arrival.key);
    if (!_inFlight.erase(id)) {
        return; // not ours any more
    }
    std::string error = "not found";
    std::optional<HeightTile> tile;
    if (arrival.bytes) {
        tile = ParseHeightTile(arrival.bytes->data(), arrival.bytes->size(), arrival.key, &error);
    }
    const bool root = arrival.key.level == 0;
    if (!tile) {
        ++_failed;
        _absent.insert(id);
        LOG_WARN("Terrain", "%s: %s: %s", body.name.c_str(), TilePath(body.name, arrival.key).c_str(), error.c_str());
        if (!root) {
            return; // the parent's upsampled child stands in
        }
        tile = FlatTile(arrival.key);
    }
    Insert(arrival.slot, std::move(*tile), root);
    body.rootsResident += root ? 1 : 0;
}

void TileCache::BeginFrame() {
    ++_frame;
    _synthesizedThisFrame = 0;
    while (!_arrivals->empty()) {
        Arrival arrival = std::move(_arrivals->front());
        _arrivals->pop_front();
        Arrive(std::move(arrival));
    }
}

void TileCache::EndFrame() {
    std::vector<std::pair<uint64_t, Request>> wanted(_wanted.begin(), _wanted.end());
    _wanted.clear();
    std::sort(wanted.begin(), wanted.end(), [](const auto& a, const auto& b) {
        if (a.second.priority < b.second.priority || b.second.priority < a.second.priority) {
            return a.second.priority < b.second.priority;
        }
        return a.first < b.first;
    });
    for (const auto& [id, request] : wanted) {
        if (static_cast<int>(_inFlight.size()) >= _limits.maxInFlight) {
            break;
        }
        Fetch(request);
    }
    Evict();
}

void TileCache::Evict() {
    const std::size_t budget = _limits.budgetBytes;
    if (_entries.size() * kTileGpuBytes <= budget) {
        return;
    }
    // Least recently used first, the deepest of a frame before its ancestors. Nothing this
    // frame touched goes: the frame's selection still points at it.
    std::vector<std::pair<uint64_t, const Entry*>> candidates;
    for (const auto& [id, entry] : _entries) {
        if (!entry->pinned && entry->lastUsed < _frame) {
            candidates.emplace_back(id, entry.get());
        }
    }
    std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
        if (a.second->lastUsed != b.second->lastUsed) {
            return a.second->lastUsed < b.second->lastUsed;
        }
        if (a.second->tile.key.level != b.second->tile.key.level) {
            return a.second->tile.key.level > b.second->tile.key.level;
        }
        return a.first < b.first;
    });
    for (const auto& [id, entry] : candidates) {
        if (_entries.size() * kTileGpuBytes <= budget) {
            break;
        }
        _evicted.push_back(entry->tile.uid);
        ++_evictedCount;
        _entries.erase(id);
    }
}

std::vector<uint64_t> TileCache::TakeEvicted() {
    std::vector<uint64_t> evicted;
    evicted.swap(_evicted);
    return evicted;
}

CacheStats TileCache::Stats() const {
    CacheStats stats;
    stats.resident = static_cast<int>(_entries.size());
    for (const auto& [id, entry] : _entries) {
        stats.synthesized += entry->tile.synthesized ? 1 : 0;
    }
    stats.bytes = _entries.size() * kTileGpuBytes;
    stats.inFlight = static_cast<int>(_inFlight.size());
    stats.requested = _requested;
    stats.failed = _failed;
    stats.evicted = _evictedCount;
    return stats;
}

} // namespace Terrain
