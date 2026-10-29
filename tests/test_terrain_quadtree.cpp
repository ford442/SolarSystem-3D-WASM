#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "3rdparty/ktx2_writer.h"
#include "Solar_System/SceneScale.h"
#include "render/terrain/TerrainSystem.h"

using namespace Terrain;

namespace {

constexpr double kMetre = 1.0e-3 / SceneScale::kKmPerSceneUnit;
constexpr double kMoonRadiusKm = 1737.4;
constexpr double kMeshRadius = SceneScale::kMeshRadius;

/** The test_surface_patch.cpp sense of "to the millimetre": 1 mm, or 1e-6 of the distance from the eye. */
double Tolerance(const glm::dvec3& fromEye) {
    return std::max(1.0e-3 * kMetre, 1.0e-6 * glm::length(fromEye));
}

/** Realistic Moon's model matrix 3×3, in float as the app has it: sphere.obj (radius 2) at 1737.4 km, tilted and spun. */
glm::dmat3 MoonShape() {
    const float scale = static_cast<float>(SceneScale::KmToScene(kMoonRadiusKm) / kMeshRadius);
    glm::mat4 m = glm::rotate(glm::mat4(1.0f), glm::radians(6.7f), glm::vec3(0.0f, 0.0f, 1.0f));
    m = glm::rotate(m, glm::radians(-75.0f + 211.3f), glm::vec3(0.0f, 1.0f, 0.0f));
    return glm::dmat3(glm::mat3(m) * scale);
}

/** Model direction at selenographic latitude/longitude (longitude is -atan2(z, x)). */
glm::dvec3 LatLon(double latDeg, double lonDeg) {
    const double lat = glm::radians(latDeg), lon = glm::radians(lonDeg);
    return {std::cos(lat) * std::cos(lon), std::sin(lat), -std::cos(lat) * std::sin(lon)};
}

const glm::dvec3 kShackleton = LatLon(-89.9, 129.8);

/** Metres of relief at a direction, at several scales: what a baker would sample. */
double Field(const glm::dvec3& d) {
    return 3000.0 * std::sin(5.0 * d.x + 1.0) * std::cos(4.0 * d.z) + 800.0 * std::sin(37.0 * d.y + 2.0 * d.x) +
           90.0 * std::sin(411.0 * d.x + 263.0 * d.z) + 12.0 * std::cos(5003.0 * d.y - 3001.0 * d.x);
}

/** A tile sampled from Field at its grid points, apron included: neighbours share their edges exactly. */
HeightTile FieldTile(const TileKey& key, uint8_t childMask) {
    HeightTile tile = FlatTile(key);
    tile.synthesized = false;
    tile.childMask = childMask;
    const TileExtent e = Extent(key);
    const FaceBasis basis = Basis(key.face);
    std::vector<double> ws(kTileStride), wt(kTileStride);
    for (int g = -kTileApron; g <= kTileQuads + kTileApron; ++g) {
        ws[static_cast<std::size_t>(g + kTileApron)] = Warp(e.s0 + g * e.size / kTileQuads);
        wt[static_cast<std::size_t>(g + kTileApron)] = Warp(e.t0 + g * e.size / kTileQuads);
    }
    for (int gy = -kTileApron; gy <= kTileQuads + kTileApron; ++gy) {
        for (int gx = -kTileApron; gx <= kTileQuads + kTileApron; ++gx) {
            const glm::dvec3 direction = glm::normalize(basis.n + ws[static_cast<std::size_t>(gx + kTileApron)] * basis.u +
                                                        wt[static_cast<std::size_t>(gy + kTileApron)] * basis.v);
            tile.heights[static_cast<std::size_t>((gy + kTileApron) * kTileStride + gx + kTileApron)] =
                static_cast<float>(Field(direction));
        }
    }
    const auto [low, high] = std::minmax_element(tile.heights.begin(), tile.heights.end());
    tile.minHeight = *low;
    tile.maxHeight = *high;
    return tile;
}

/** The file scripts/bake-terrain-tiles.py would write for a tile. */
std::vector<uint8_t> EncodeTile(const HeightTile& tile) {
    std::vector<uint16_t> texels(tile.heights.size());
    const double range = std::max(1e-3, static_cast<double>(tile.maxHeight - tile.minHeight));
    for (std::size_t i = 0; i < texels.size(); ++i) {
        texels[i] = static_cast<uint16_t>(std::lround((tile.heights[i] - tile.minHeight) / range * 65535.0));
    }
    return ktx2::WriteR16Unorm(kTileStride, kTileStride, texels,
                               {{"KTXorientation", "ru"},
                                {"SSterrain", TileMetadata(tile.minHeight, static_cast<float>(tile.minHeight + range),
                                                           tile.childMask)}});
}

/** A body with the eye at the render origin, `metres` above Field's ground under `direction`. */
BodyFrame MoonUnderEye(const glm::dvec3& direction, double metres, double fade = 1.0) {
    BodyFrame body;
    body.shape = MoonShape();
    body.meshRadius = kMeshRadius;
    body.heightScale = fade * kMeshRadius / (kMoonRadiusKm * 1000.0);
    body.minHeightM = -4000.0;
    body.maxHeightM = 4000.0;
    // Eye straight above the surface point along the world normal of the sphere.
    const glm::dvec3 surface = body.shape * (glm::normalize(direction) * kMeshRadius);
    const glm::dvec3 up = glm::normalize(glm::transpose(glm::inverse(body.shape)) * glm::normalize(direction));
    const double ground = fade * Field(glm::normalize(direction));
    body.center = -(surface + up * ((ground + metres) * kMetre));
    return body;
}

/** Every tile to any depth, from Field down to `dataLevel` and upsampled past it; some may be held back. */
class FieldSource : public TileSource {
public:
    explicit FieldSource(int dataLevel = 30) : _dataLevel(dataLevel) {}

    const HeightTile* Root(int face) override { return Get(TileKey{static_cast<uint8_t>(face), 0, 0, 0}, nullptr, 0); }
    const HeightTile* Child(const HeightTile& parent, int quadrant, const RequestPriority& priority) override {
        const TileKey key = parent.key.Child(quadrant);
        if (held.count(key.Packed())) {
            requests.emplace_back(key, priority);
            return nullptr;
        }
        return Get(key, &parent, quadrant);
    }
    void Touch(const HeightTile& tile) override { touched.insert(tile.key.Packed()); }

    std::set<uint64_t> held;
    std::vector<std::pair<TileKey, RequestPriority>> requests;
    std::set<uint64_t> touched;

private:
    const HeightTile* Get(const TileKey& key, const HeightTile* parent, int quadrant) {
        auto found = _tiles.find(key.Packed());
        if (found == _tiles.end()) {
            HeightTile tile = key.level <= _dataLevel || !parent
                ? FieldTile(key, key.level < _dataLevel ? 0xF : 0)
                : UpsampleChild(*parent, quadrant);
            found = _tiles.emplace(key.Packed(), std::move(tile)).first;
        }
        return &found->second;
    }

    int _dataLevel;
    std::map<uint64_t, HeightTile> _tiles;
};

/** What terrain.vs draws for grid point (gx, gy) of a tile at `heightM`: the float path, render frame. */
glm::dvec3 ShaderPosition(const BodyFrame& body, const DrawTile& draw, int gx, int gy, double heightM) {
    const glm::vec3 origin(draw.center);
    const glm::mat3 shape(body.shape);
    const glm::vec3 offset = GridOffset(draw.frame, draw.tile->key.face, glm::vec2(gx, gy),
                                        static_cast<float>(body.meshRadius), static_cast<float>(heightM * body.heightScale));
    return glm::dvec3(origin + shape * offset);
}

glm::dvec3 ExactPosition(const BodyFrame& body, const TileKey& key, double gx, double gy, double heightM) {
    return body.center + body.shape * GridPointExact(key, gx, gy, body.meshRadius, heightM * body.heightScale);
}

DrawTile Draw(const BodyFrame& body, const HeightTile& tile) {
    DrawTile draw;
    draw.tile = &tile;
    draw.frame = MakeTileFrame(tile.key, body.meshRadius);
    draw.center = body.center + body.shape * draw.frame.centerModel;
    return draw;
}

std::vector<std::pair<uint64_t, glm::dvec2>> Keys(const Selection& selection) {
    std::vector<std::pair<uint64_t, glm::dvec2>> keys;
    for (const DrawTile& draw : selection.tiles) {
        keys.emplace_back(draw.tile->key.Packed(), draw.morph);
    }
    std::sort(keys.begin(), keys.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    return keys;
}

constexpr double kPixelsPerRadian = 720.0 / (2.0 * 0.41421356); // 720 lines, 45° vertical field of view

} // namespace

TEST(TerrainCubeSphere, FacePointsRoundTripThroughDirections) {
    for (int face = 0; face < kFaceCount; ++face) {
        for (const double s : {-0.999, -0.5, 0.0, 0.25, 0.999}) {
            for (const double t : {-0.75, 0.0, 0.6}) {
                const FacePoint point = FromDirection(Direction(face, s, t) * 3.7);
                EXPECT_EQ(point.face, face);
                EXPECT_NEAR(point.s, s, 1e-12);
                EXPECT_NEAR(point.t, t, 1e-12);
            }
        }
    }
    const TileKey key = TileAt(FromDirection(Direction(4, 0.3, -0.2)), 3);
    EXPECT_EQ(key.face, 4);
    EXPECT_EQ(key.x, 5u); // (0.3 + 1) / 2 * 8 = 5.2
    EXPECT_EQ(key.y, 3u); // (−0.2 + 1) / 2 * 8 = 3.2
    EXPECT_EQ(key.Child(3).Parent(), key);
    EXPECT_EQ(key.Child(2).Quadrant(), 2);
    // u × v = n on every face: tiles wind counter-clockwise seen from outside.
    for (int face = 0; face < kFaceCount; ++face) {
        const FaceBasis b = Basis(face);
        EXPECT_NEAR(glm::distance(glm::cross(b.u, b.v), b.n), 0.0, 1e-15);
    }
}

namespace {

const LevelRanges& MoonRanges() {
    static const LevelRanges ranges = LevelRanges::Make(SceneScale::KmToScene(kMoonRadiusKm), kPixelsPerRadian, 8.0);
    return ranges;
}

/**
 * The eye over face point (s, t) of `face` as close as a tile of `level` is ever drawn from:
 * its split distance. (A tile nearer than that has split into smaller ones.)
 */
BodyFrame EyeAsCloseAsLevel(int face, double s, double t, int level) {
    return MoonUnderEye(Direction(face, s, t), MoonRanges().split[level] / kMetre);
}

} // namespace

TEST(TerrainCubeSphere, FloatVerticesLandToTheMillimetreAtEveryLevel) {
    const FacePoint point = FromDirection(kShackleton);
    for (int level = 0; level <= kMaxLevel; ++level) {
        const HeightTile tile = FlatTile(TileAt(point, level), 1234.5f);
        const TileExtent e = Extent(tile.key);
        // Over a corner of the tile: the vertices nearest the eye are the furthest from the tile's centre.
        const BodyFrame body = EyeAsCloseAsLevel(tile.key.face, e.s0 + 0.01 * e.size, e.t0 + 0.02 * e.size, level);
        const DrawTile draw = Draw(body, tile);
        for (const int gx : {0, 1, 17, 32, 63, 64}) {
            for (const int gy : {0, 5, 32, 64}) {
                const glm::dvec3 exact = ExactPosition(body, tile.key, gx, gy, 1234.5);
                EXPECT_LE(glm::distance(ShaderPosition(body, draw, gx, gy, 1234.5), exact), Tolerance(exact))
                    << "level " << level << " grid " << gx << "," << gy;
            }
        }
    }
}

TEST(TerrainQuadtree, NeighbourEdgesAndSkirtsMeetToTheMillimetre) {
    const FacePoint point = FromDirection(kShackleton);

    // Same level: a neighbour along u on the face, and the tile over the cube edge.
    for (const int level : {2, 6, 12, 17, 21}) {
        const TileKey a = TileAt(point, level);
        std::vector<std::pair<TileKey, TileKey>> pairs;
        pairs.push_back({a, TileKey{a.face, a.level, a.x + 1, a.y}});
        const uint32_t last = (1u << level) - 1u;
        pairs.push_back({TileKey{0, a.level, last, a.y}, TileKey{5, a.level, 0, a.y}}); // +X's s = 1 is −Z's s = −1
        for (const auto& [ka, kb] : pairs) {
            const TileExtent e = Extent(ka);
            const BodyFrame body = EyeAsCloseAsLevel(ka.face, e.s0 + e.size, e.t0 + 0.5 * e.size, level);
            const HeightTile ta = FieldTile(ka, 0), tb = FieldTile(kb, 0);
            const DrawTile da = Draw(body, ta), db = Draw(body, tb);
            for (int gy = 0; gy <= kTileQuads; gy += 7) {
                const glm::dvec3 pa = ShaderPosition(body, da, kTileQuads, gy, ta.At(kTileQuads, gy));
                const glm::dvec3 pb = ShaderPosition(body, db, 0, gy, tb.At(0, gy));
                EXPECT_NEAR(ta.At(kTileQuads, gy), tb.At(0, gy), 1e-3) << "edge samples disagree";
                EXPECT_LE(glm::distance(pa, pb), Tolerance(pa)) << "level " << level << " row " << gy;
            }
        }
    }

    // A level step: the finer tile, fully morphed, against its coarser neighbour's edge.
    // Odd vertices sit on the coarse edge to the millimetre once quads are small against the
    // radius; at any level the gap is inside the skirt.
    for (const int level : {3, 8, 15, 19}) {
        const TileKey coarseKey = TileAt(point, level);
        const TileKey fineParentKey{coarseKey.face, coarseKey.level, coarseKey.x + 1, coarseKey.y};
        const HeightTile coarse = FieldTile(coarseKey, 0xF), fineParent = FieldTile(fineParentKey, 0xF);
        const TileKey fineKey = fineParentKey.Child(0); // the left column of children borders `coarse`
        const HeightTile fine = FieldTile(fineKey, 0);
        const TileExtent e = Extent(coarseKey);
        const BodyFrame body = EyeAsCloseAsLevel(coarseKey.face, e.s0 + e.size, e.t0 + 0.25 * e.size, level);
        const DrawTile dc = Draw(body, coarse), df = Draw(body, fine);
        const double skirt = SkirtDepth(fineKey, fine, body);
        for (int gy = 0; gy <= kTileQuads; ++gy) {
            const double h = MorphedHeight(fine, &fineParent, 0, gy, 1.0);
            const glm::dvec3 pf = ShaderPosition(body, df, 0, gy, h);
            // The coarse tile's edge at the same point: its vertices gy/2 (and the next) on column 64.
            const int below = gy / 2, above = (gy + 1) / 2;
            const glm::dvec3 pc = 0.5 * (ShaderPosition(body, dc, kTileQuads, below, coarse.At(kTileQuads, below)) +
                                         ShaderPosition(body, dc, kTileQuads, above, coarse.At(kTileQuads, above)));
            const double gap = glm::distance(pf, pc);
            EXPECT_LE(gap, skirt * body.MinScale()) << "level " << level;
            if (level >= 15) {
                EXPECT_LE(gap, Tolerance(pf)) << "level " << level << " row " << gy;
            }
        }
        // A skirt hangs straight down from the edge: its bottom is the edge vertex lowered by its depth.
        const glm::dvec3 top = ShaderPosition(body, df, 0, 10, fine.At(0, 10));
        const glm::dvec3 bottom = ShaderPosition(body, df, 0, 10, fine.At(0, 10) - skirt / body.heightScale);
        EXPECT_NEAR(glm::distance(top, bottom), skirt * body.MaxScale(), Tolerance(top));
        EXPECT_GT(skirt, 0.0);
    }
}

TEST(TerrainQuadtree, SplitAndMergeAreDeterministicForAGivenErrorAndCamera) {
    const LevelRanges ranges = LevelRanges::Make(SceneScale::KmToScene(kMoonRadiusKm), kPixelsPerRadian, 8.0);
    const std::vector<double> altitudes{400000.0, 120000.0, 30000.0, 8000.0, 1500.0, 1000.0, 300.0, 40.0, 2.0};
    std::vector<std::vector<std::pair<uint64_t, glm::dvec2>>> descent;
    FieldSource first(7), second(7);
    for (const double metres : altitudes) {
        const BodyFrame body = MoonUnderEye(kShackleton, metres);
        Selection a, b;
        Select(body, ranges, SelectInput{}, first, a);
        Select(body, ranges, SelectInput{}, second, b);
        ASSERT_FALSE(a.tiles.empty());
        EXPECT_EQ(Keys(a), Keys(b)) << metres << " m";
        descent.push_back(Keys(a));
        // Nearest first.
        for (std::size_t i = 1; i < a.tiles.size(); ++i) {
            EXPECT_LE(a.tiles[i - 1].distance, a.tiles[i].distance);
        }
    }
    // Lower means deeper, and climbing back out gives exactly the lists of the way down.
    FieldSource shared(7);
    for (std::size_t i = altitudes.size(); i-- > 0;) {
        const BodyFrame body = MoonUnderEye(kShackleton, altitudes[i]);
        Selection selection;
        Select(body, ranges, SelectInput{}, shared, selection);
        EXPECT_EQ(Keys(selection), descent[i]) << altitudes[i] << " m on the way up";
    }
    Selection high, low;
    FieldSource source(7);
    Select(MoonUnderEye(kShackleton, 400000.0), ranges, SelectInput{}, source, high);
    Select(MoonUnderEye(kShackleton, 1000.0), ranges, SelectInput{}, source, low);
    EXPECT_GT(low.deepest, high.deepest);
    EXPECT_GE(low.deepest, 12); // a kilometre up: quads of a few metres under the eye
    EXPECT_GT(low.horizonCulled, 0);
}

TEST(TerrainQuadtree, SplittingAndMergingDoNotPop) {
    // Walk the eye down and back up in small steps. A tile that appears or disappears
    // between steps must, at that moment, show its parent's surface: its vertices are past
    // their morph zone. Measured as the largest on-screen jump of any vertex.
    const LevelRanges ranges = LevelRanges::Make(SceneScale::KmToScene(kMoonRadiusKm), kPixelsPerRadian, 8.0);
    FieldSource source(8);
    std::map<uint64_t, DrawTile> previous;
    double worstPixels = 0.0;
    int changes = 0;
    const auto check = [&](const BodyFrame& body, const DrawTile& draw) {
        if (!draw.parent) {
            return;
        }
        for (int gy = 0; gy <= kTileQuads; gy += 3) {
            for (int gx = 0; gx <= kTileQuads; gx += 3) {
                const glm::dvec3 own = ExactPosition(body, draw.tile->key, gx, gy, draw.tile->At(gx, gy));
                const double k = MorphWeight(draw.morph, glm::length(own));
                const double shown = MorphedHeight(*draw.tile, draw.parent, gx, gy, k);
                const double parent = ParentSurfaceHeight(*draw.parent, draw.tile->key.Quadrant(), gx, gy);
                const double jump = std::abs(shown - parent) * body.heightScale * body.MaxScale();
                worstPixels = std::max(worstPixels, jump / glm::length(own) * kPixelsPerRadian);
            }
        }
    };
    std::vector<double> path;
    for (double metres = 300000.0; metres > 500.0; metres *= 0.985) {
        path.push_back(metres);
    }
    for (std::size_t i = path.size(); i-- > 0;) {
        path.push_back(path[i]);
    }
    for (const double metres : path) {
        const BodyFrame body = MoonUnderEye(kShackleton, metres);
        Selection selection;
        Select(body, ranges, SelectInput{}, source, selection);
        std::map<uint64_t, DrawTile> current;
        for (const DrawTile& draw : selection.tiles) {
            current.emplace(draw.tile->key.Packed(), draw);
        }
        // Only splits and merges: a far tile can also come and go over the horizon.
        const auto parentIn = [](const std::map<uint64_t, DrawTile>& tiles, const DrawTile& draw) {
            return draw.parent && tiles.count(draw.parent->key.Packed());
        };
        if (!previous.empty()) {
            for (const auto& [key, draw] : current) {
                if (!previous.count(key) && parentIn(previous, draw)) {
                    check(body, draw); // split in this step
                    ++changes;
                }
            }
            for (const auto& [key, draw] : previous) {
                if (!current.count(key) && parentIn(current, draw)) {
                    check(body, draw); // merged away in this step, seen from where the eye is now
                    ++changes;
                }
            }
        }
        previous = std::move(current);
    }
    EXPECT_GT(changes, 100);
    EXPECT_LT(worstPixels, 0.1);
}

TEST(TerrainQuadtree, AMissingChildDrawsItsParentAndIsAskedForByError) {
    const LevelRanges ranges = LevelRanges::Make(SceneScale::KmToScene(kMoonRadiusKm), kPixelsPerRadian, 8.0);
    const BodyFrame body = MoonUnderEye(kShackleton, 2000.0);
    const FacePoint point = FromDirection(kShackleton);
    FieldSource all(9);
    Selection full;
    Select(body, ranges, SelectInput{}, all, full);

    FieldSource partial(9);
    const TileKey blocked = TileAt(point, 6);
    partial.held.insert(blocked.Packed());
    Selection selection;
    Select(body, ranges, SelectInput{}, partial, selection);
    EXPECT_GT(selection.waiting, 0);
    const DrawTile* standIn = nullptr;
    for (const DrawTile& draw : selection.tiles) {
        if (draw.tile->key == blocked.Parent()) {
            standIn = &draw;
        }
        EXPECT_FALSE(draw.tile->key.level == 6 && draw.tile->key.Parent() == blocked.Parent())
            << "a sibling of the missing tile was drawn without it";
    }
    ASSERT_NE(standIn, nullptr);
    EXPECT_TRUE(standIn->nearSurface); // a coarse tile two kilometres from the eye gets fragment depth
    for (const DrawTile& draw : full.tiles) {
        EXPECT_FALSE(draw.nearSurface) << "at its own level a tile is never that close";
    }
    ASSERT_FALSE(partial.requests.empty());
    EXPECT_EQ(partial.requests.front().first, blocked);
    EXPECT_LT(selection.tiles.size(), full.tiles.size());

    // The one the eye is over outranks one off to the side at the same error.
    RequestPriority centre{3, 0.1}, side{3, 0.9}, coarse{5, 1.2};
    EXPECT_TRUE(centre < side);
    EXPECT_TRUE(coarse < centre);
    EXPECT_FALSE(side < centre);

    partial.held.clear();
    Selection arrived;
    Select(body, ranges, SelectInput{}, partial, arrived);
    EXPECT_EQ(Keys(arrived), Keys(full));
}

TEST(TerrainQuadtree, TheFarSideIsBehindTheHorizon) {
    const glm::dvec3 center(0.0, -10.0, 0.0);
    const glm::dvec3 eye(0.0, 0.05, 0.0); // 0.05 above a sphere of radius 10
    EXPECT_TRUE(BehindHorizon(eye, center, 10.0, BoundingSphere{glm::vec3(0.0f, -20.0f, 0.0f), 0.5f}));
    EXPECT_TRUE(BehindHorizon(eye, center, 10.0, BoundingSphere{glm::vec3(10.0f, -10.0f, 0.0f), 0.5f}));
    EXPECT_FALSE(BehindHorizon(eye, center, 10.0, BoundingSphere{glm::vec3(0.5f, 0.0f, 0.0f), 0.1f}));
    // A peak over the horizon is kept.
    EXPECT_FALSE(BehindHorizon(eye, center, 10.0, BoundingSphere{glm::vec3(1.0f, -0.05f, 0.0f), 0.3f}));
    EXPECT_FALSE(BehindHorizon(glm::dvec3(0.0, -15.0, 0.0), center, 10.0, BoundingSphere{glm::vec3(0.0f), 1.0f}));

    const LevelRanges ranges = LevelRanges::Make(SceneScale::KmToScene(kMoonRadiusKm), kPixelsPerRadian, 8.0);
    const BodyFrame body = MoonUnderEye(kShackleton, 1000.0);
    FieldSource source(9);
    Selection selection;
    Select(body, ranges, SelectInput{}, source, selection);
    for (const DrawTile& draw : selection.tiles) {
        const glm::dvec3 direction = Direction(draw.tile->key.face, Extent(draw.tile->key).s0, Extent(draw.tile->key).t0);
        EXPECT_GT(glm::dot(direction, kShackleton), 0.0) << "a tile on the far side was kept";
    }
}

TEST(TerrainQuadtree, FrustumAndLightBoxDecideViewAndShadowTiles) {
    const LevelRanges ranges = LevelRanges::Make(SceneScale::KmToScene(kMoonRadiusKm), kPixelsPerRadian, 8.0);
    const BodyFrame body = MoonUnderEye(kShackleton, 5000.0);
    // Looking along the ground: half the cap is behind the camera.
    const glm::dvec3 up = glm::normalize(-body.center);
    const glm::dvec3 ahead = glm::normalize(glm::cross(up, glm::dvec3(1.0, 0.0, 0.0)));
    const glm::mat4 view(glm::lookAt(glm::dvec3(0.0), ahead, up));
    const glm::mat4 projection = glm::perspective(glm::radians(45.0f), 16.0f / 9.0f, 1e-9f, 1.0f);
    const std::vector<Frustum> views{Frustum(projection * view)};
    SelectInput input;
    input.viewDirection = ahead;
    input.pixelsPerRadian = kPixelsPerRadian;
    input.views = &views;
    FieldSource source(9);
    Selection inView;
    Select(body, ranges, input, source, inView);
    EXPECT_GT(inView.frustumCulled, 0);
    for (const DrawTile& draw : inView.tiles) {
        EXPECT_TRUE(draw.inView);
        EXPECT_FALSE(draw.inLight);
    }
    const Frustum light(glm::ortho(-1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f)); // everything near the eye
    input.light = &light;
    Selection withLight;
    Select(body, ranges, input, source, withLight);
    EXPECT_GT(withLight.tiles.size(), inView.tiles.size());
    bool shadowOnly = false;
    for (const DrawTile& draw : withLight.tiles) {
        shadowOnly = shadowOnly || (draw.inLight && !draw.inView);
    }
    EXPECT_TRUE(shadowOnly);
}

TEST(TerrainTile, KtxRoundTripKeepsHeightsAndRejectsWhatIsNotATile) {
    const HeightTile source = FieldTile(TileAt(FromDirection(kShackleton), 7), 0x5);
    const std::vector<uint8_t> file = EncodeTile(source);
    std::string error;
    const auto tile = ParseHeightTile(file.data(), file.size(), source.key, &error);
    ASSERT_TRUE(tile) << error;
    EXPECT_EQ(tile->childMask, 0x5);
    EXPECT_FALSE(tile->synthesized);
    const double step = (source.maxHeight - source.minHeight) / 65535.0;
    for (std::size_t i = 0; i < source.heights.size(); ++i) {
        ASSERT_NEAR(tile->heights[i], source.heights[i], step * 0.51 + 1e-3);
    }
    EXPECT_NEAR(tile->minHeight, source.minHeight, 1e-3);

    const std::string html = "<!doctype html><html></html>";
    EXPECT_FALSE(ParseHeightTile(reinterpret_cast<const uint8_t*>(html.data()), html.size(), source.key, &error));
    const std::vector<uint8_t> small = ktx2::WriteR16Unorm(33, 33, std::vector<uint16_t>(33 * 33, 0),
                                                           {{"SSterrain", TileMetadata(0, 1, 0)}});
    EXPECT_FALSE(ParseHeightTile(small.data(), small.size(), source.key, &error));
    const std::vector<uint8_t> unlabeled = ktx2::WriteR16Unorm(kTileStride, kTileStride,
                                                               std::vector<uint16_t>(kTileStride * kTileStride, 0), {});
    EXPECT_FALSE(ParseHeightTile(unlabeled.data(), unlabeled.size(), source.key, &error));
    EXPECT_NE(error.find("SSterrain"), std::string::npos);
    std::vector<uint8_t> truncated(file.begin(), file.begin() + static_cast<long>(file.size() / 2));
    EXPECT_FALSE(ParseHeightTile(truncated.data(), truncated.size(), source.key, &error));
}

TEST(TerrainTile, UpsampledChildrenKeepTheParentSamplesAndAgreeOnSharedEdges) {
    const TileKey left = TileAt(FromDirection(kShackleton), 9);
    const TileKey right{left.face, left.level, left.x + 1, left.y};
    const HeightTile a = FieldTile(left, 0), b = FieldTile(right, 0);
    for (int q = 0; q < 4; ++q) {
        const HeightTile child = UpsampleChild(a, q);
        EXPECT_TRUE(child.synthesized);
        EXPECT_EQ(child.childMask, 0);
        for (int gy = 0; gy <= kTileQuads; gy += 2) {
            for (int gx = 0; gx <= kTileQuads; gx += 2) {
                ASSERT_FLOAT_EQ(child.At(gx, gy), a.At(gx / 2 + 32 * (q & 1), gy / 2 + 32 * (q >> 1)));
                ASSERT_NEAR(MorphedHeight(child, &a, gx, gy, 1.0), child.At(gx, gy), 1e-3);
            }
        }
    }
    // a's right children and b's left children meet along a's column 64 = b's column 0.
    const HeightTile ar = UpsampleChild(a, 1), bl = UpsampleChild(b, 0);
    for (int gy = 0; gy <= kTileQuads; ++gy) {
        EXPECT_NEAR(ar.At(kTileQuads, gy), bl.At(0, gy), 1e-3);
    }
}

TEST(TerrainTile, SurfaceHeightIsTheIndexBuffersTriangles) {
    HeightTile tile = FlatTile(TileKey{2, 3, 1, 1});
    for (int gy = -1; gy <= kTileQuads + 1; ++gy) {
        for (int gx = -1; gx <= kTileQuads + 1; ++gx) {
            tile.heights[static_cast<std::size_t>((gy + 1) * kTileStride + gx + 1)] = static_cast<float>(gx * gx + 3 * gy);
        }
    }
    EXPECT_DOUBLE_EQ(SurfaceHeight(tile, 4.0, 6.0), 16.0 + 18.0);
    // Inside quad (4,6): split along (4,6)-(5,7). Below the diagonal: (4,6) (5,6) (5,7).
    EXPECT_DOUBLE_EQ(SurfaceHeight(tile, 4.75, 6.25), tile.At(4, 6) + 0.75 * (tile.At(5, 6) - tile.At(4, 6)) +
                                                          0.25 * (tile.At(5, 7) - tile.At(5, 6)));
    EXPECT_DOUBLE_EQ(SurfaceHeight(tile, 4.25, 6.75), tile.At(4, 6) + 0.75 * (tile.At(4, 7) - tile.At(4, 6)) +
                                                          0.25 * (tile.At(5, 7) - tile.At(4, 7)));
    EXPECT_DOUBLE_EQ(SurfaceHeight(tile, 64.0, 64.0), tile.At(64, 64));
    // A fully morphed odd vertex lies on the parent's triangles.
    const HeightTile parent = FieldTile(TileKey{2, 2, 0, 0}, 0xF);
    EXPECT_NEAR(ParentSurfaceHeight(parent, 3, 5, 9), SurfaceHeight(parent, (5 + 64) / 2.0, (9 + 64) / 2.0), 1e-6);
    EXPECT_NEAR(ParentSurfaceHeight(parent, 0, 5, 8), SurfaceHeight(parent, 2.5, 4.0), 1e-6);
}

TEST(TerrainTileSet, ParsesTheBakersTileSet) {
    const std::string json = R"json({
      "format": "ssterrain-1", "body": "moon", "radiusKm": 1737.4, "maxLevel": 8,
      "minHeightM": -9115.5, "maxHeightM": 10783, "faces": [0, 1, 2, 3, 4, 5],
      "attribution": "LRO LOLA (NASA/GSFC)", "sources": [{"name": "LDEM", "url": "https://pds"}],
      "tilesSha256": "ab12", "skirt": null, "ok": true
    })json";
    std::string error;
    const auto set = ParseTileSet(json, &error);
    ASSERT_TRUE(set) << error;
    EXPECT_EQ(set->body, "moon");
    EXPECT_EQ(set->maxLevel, 8);
    EXPECT_EQ(set->faces, 0x3F);
    EXPECT_DOUBLE_EQ(set->minHeightM, -9115.5);
    EXPECT_EQ(set->attribution, "LRO LOLA (NASA/GSFC)");
    EXPECT_FALSE(ParseTileSet(R"({"format": "ssterrain-2", "body": "moon", "radiusKm": 1, "maxLevel": 1})"));
    EXPECT_FALSE(ParseTileSet(R"({"format": "ssterrain-1", "body": "moon", "radiusKm": 1, "maxLevel": 1, "faces": [6]})"));
    EXPECT_FALSE(ParseTileSet("<!doctype html>"));
    EXPECT_FALSE(ParseTileSet(R"({"format": "ssterrain-1", "body": "moon", "radiusKm": 1, "maxLevel": 99})"));
    EXPECT_EQ(TilePath("mars", TileKey{3, 7, 12, 99}), "resource/terrain/mars/3/7/12/99.ktx2");
}

namespace {

/** Serves a baked moon from memory, answering at once or when told to. */
struct FakeServer {
    int maxLevel = 9;
    uint8_t faces = 0x3F;
    bool hasSet = true;
    bool deferred = false;
    std::vector<std::string> requested;
    std::vector<std::pair<std::string, FetchDone>> pending;

    FetchFn Fetch() {
        return [this](const std::string& path, FetchDone done) {
            requested.push_back(path);
            if (deferred) {
                pending.emplace_back(path, std::move(done));
            } else {
                done(Serve(path));
            }
        };
    }

    void Flush() {
        auto queued = std::move(pending);
        pending.clear();
        for (auto& [path, done] : queued) {
            done(Serve(path));
        }
    }

    std::optional<std::vector<uint8_t>> Serve(const std::string& path) const {
        if (path == "resource/terrain/moon/tileset.json") {
            if (!hasSet) {
                return std::nullopt;
            }
            std::string faceList;
            for (int f = 0; f < kFaceCount; ++f) {
                if ((faces >> f) & 1) {
                    faceList += (faceList.empty() ? "" : ",") + std::to_string(f);
                }
            }
            const std::string json = "{\"format\":\"ssterrain-1\",\"body\":\"moon\",\"radiusKm\":1737.4,\"maxLevel\":" +
                                     std::to_string(maxLevel) +
                                     ",\"minHeightM\":-4000,\"maxHeightM\":4000,\"faces\":[" + faceList + "]}";
            return std::vector<uint8_t>(json.begin(), json.end());
        }
        int face, level;
        unsigned x, y;
        if (std::sscanf(path.c_str(), "resource/terrain/moon/%d/%d/%u/%u.ktx2", &face, &level, &x, &y) != 4 ||
            level > maxLevel) {
            return std::nullopt;
        }
        const TileKey key{static_cast<uint8_t>(face), static_cast<uint8_t>(level), x, y};
        return EncodeTile(FieldTile(key, level < maxLevel ? 0xF : 0));
    }
};

} // namespace

TEST(TerrainTileCache, StartsTheBestRequestsUpToTheInFlightCap) {
    FakeServer server;
    server.deferred = true;
    TileCache cache(server.Fetch());
    cache.SetLimits(CacheLimits{64u << 20, 3, 128});
    EXPECT_EQ(cache.Prepare(0, "moon"), TileCache::SetState::Loading);
    server.Flush();
    cache.BeginFrame();
    EXPECT_EQ(cache.State(0), TileCache::SetState::Ready);
    EXPECT_FALSE(cache.Ready(0)); // roots requested, not here yet
    server.Flush();
    cache.BeginFrame();
    ASSERT_TRUE(cache.Ready(0));

    const HeightTile* root = cache.Source(0).Root(1);
    ASSERT_TRUE(root);
    server.requested.clear();
    for (int q = 0; q < 4; ++q) {
        EXPECT_EQ(cache.Source(0).Child(*root, q, RequestPriority{q, 0.0}), nullptr);
    }
    const HeightTile* other = cache.Source(0).Root(4);
    EXPECT_EQ(cache.Source(0).Child(*other, 0, RequestPriority{9, 0.5}), nullptr);
    cache.EndFrame();
    ASSERT_EQ(server.requested.size(), 3u);
    EXPECT_EQ(server.requested[0], "resource/terrain/moon/4/1/0/0.ktx2"); // error bucket 9
    EXPECT_EQ(server.requested[1], "resource/terrain/moon/1/1/1/1.ktx2"); // 3
    EXPECT_EQ(server.requested[2], "resource/terrain/moon/1/1/0/1.ktx2"); // 2
    EXPECT_EQ(cache.Stats().inFlight, 3);

    // Still in flight: not asked again; the one left out goes once a slot frees up.
    cache.BeginFrame();
    for (int q = 0; q < 4; ++q) {
        cache.Source(0).Child(*root, q, RequestPriority{q, 0.0});
    }
    cache.EndFrame();
    EXPECT_EQ(server.requested.size(), 3u);
    server.Flush();
    cache.BeginFrame();
    EXPECT_NE(cache.Find(0, TileKey{1, 1, 1, 1}), nullptr);
    for (int q = 0; q < 4; ++q) {
        cache.Source(0).Child(*root, q, RequestPriority{q, 0.0});
    }
    cache.EndFrame();
    ASSERT_EQ(server.requested.size(), 5u);
    EXPECT_EQ(server.requested[3], "resource/terrain/moon/1/1/1/0.ktx2");
    EXPECT_EQ(server.requested[4], "resource/terrain/moon/1/1/0/0.ktx2");
}

TEST(TerrainTileCache, EvictsTheLeastRecentlyUsedDownToTheBudgetAndKeepsRoots) {
    FakeServer server;
    server.maxLevel = 0; // every child is upsampled: no fetches past the roots
    TileCache cache(server.Fetch());
    cache.SetLimits(CacheLimits{10 * kTileGpuBytes, 4, 128});
    cache.Prepare(0, "moon");
    cache.BeginFrame();
    cache.BeginFrame();
    ASSERT_TRUE(cache.Ready(0));
    TileSource& source = cache.Source(0);
    const HeightTile* root = source.Root(0);
    std::vector<uint64_t> uids;
    for (int q = 0; q < 4; ++q) {
        const HeightTile* child = source.Child(*root, q, {});
        ASSERT_TRUE(child);
        EXPECT_TRUE(child->synthesized);
        uids.push_back(child->uid);
    }
    cache.EndFrame();
    EXPECT_EQ(cache.Stats().resident, 10);
    cache.BeginFrame();
    const HeightTile* kept = cache.Find(0, root->key.Child(2));
    source.Touch(*kept);
    const HeightTile* grand = source.Child(*kept, 0, {});
    ASSERT_TRUE(grand);
    source.Touch(*grand);
    cache.EndFrame();
    // 11 tiles over a budget of 10: the oldest untouched child goes, never a root or this frame's.
    EXPECT_EQ(cache.Stats().resident, 10);
    const std::vector<uint64_t> evicted = cache.TakeEvicted();
    ASSERT_EQ(evicted.size(), 1u);
    EXPECT_NE(evicted[0], kept->uid);
    EXPECT_TRUE(std::find(uids.begin(), uids.end(), evicted[0]) != uids.end());
    for (int face = 0; face < kFaceCount; ++face) {
        EXPECT_NE(cache.Find(0, TileKey{static_cast<uint8_t>(face), 0, 0, 0}), nullptr);
    }
    EXPECT_TRUE(cache.TakeEvicted().empty());
}

TEST(TerrainTileCache, MissingDataKeepsTheSphere) {
    FakeServer server;
    server.hasSet = false;
    TileCache cache(server.Fetch());
    cache.SetLimits(CacheLimits{1u << 20, 4, 128});
    cache.Prepare(0, "moon");
    cache.BeginFrame();
    EXPECT_EQ(cache.State(0), TileCache::SetState::Missing);
    EXPECT_FALSE(cache.Ready(0));
    cache.Prepare(0, "moon");
    EXPECT_EQ(server.requested.size(), 1u); // asked once

    // A face the data does not cover is the flat sphere, never fetched.
    FakeServer partial;
    partial.faces = 0x01;
    TileCache covered(partial.Fetch());
    covered.SetLimits(CacheLimits{1u << 20, 4, 128});
    covered.Prepare(0, "moon");
    covered.BeginFrame();
    covered.BeginFrame();
    ASSERT_TRUE(covered.Ready(0));
    EXPECT_EQ(partial.requested.size(), 2u); // tileset.json and face 0's root
    const HeightTile* bare = covered.Find(0, TileKey{3, 0, 0, 0});
    ASSERT_TRUE(bare);
    EXPECT_EQ(bare->minHeight, 0.0f);
    EXPECT_EQ(bare->maxHeight, 0.0f);
}

namespace {

TerrainBodyInput MoonInput(const BodyFrame& body, int slot) {
    TerrainBodyInput input;
    input.slot = slot;
    input.center = body.center;
    input.shape = body.shape;
    input.meshRadius = body.meshRadius;
    return input;
}

TerrainSettings OnSettings() {
    TerrainSettings settings;
    settings.enabled = true;
    settings.maxPixelError = 8.0;
    settings.limits = CacheLimits{64u << 20, 8, 128};
    return settings;
}

} // namespace

TEST(TerrainSystem, TheLowTierNeverRequestsATerrainFile) {
    FakeServer server;
    TerrainSystem terrain(server.Fetch());
    TerrainSettings low;
    low.enabled = false;
    terrain.Configure(low);
    const int slot = terrain.AddBody("moon", kMoonRadiusKm);
    for (const double metres : {1.0e6, 1000.0, 10.0}) {
        terrain.Update(TerrainViewInput{}, {MoonInput(MoonUnderEye(kShackleton, metres), slot)});
        EXPECT_EQ(terrain.Active(slot), nullptr);
    }
    EXPECT_TRUE(server.requested.empty());
    EXPECT_FALSE(terrain.SurfaceRadius(slot, kShackleton));
}

TEST(TerrainSystem, TilesReplaceTheSphereOnlyCloseToTheBody) {
    FakeServer server;
    TerrainSystem terrain(server.Fetch());
    terrain.Configure(OnSettings());
    const int slot = terrain.AddBody("moon", kMoonRadiusKm);
    const double radiusM = kMoonRadiusKm * 1000.0;
    TerrainViewInput view;
    view.pixelsPerRadian = kPixelsPerRadian;

    terrain.Update(view, {MoonInput(MoonUnderEye(kShackleton, 3.0 * radiusM), slot)});
    EXPECT_TRUE(server.requested.empty()); // beyond prefetch range
    for (int frame = 0; frame < 3; ++frame) {
        terrain.Update(view, {MoonInput(MoonUnderEye(kShackleton, 1.0 * radiusM), slot)});
        EXPECT_EQ(terrain.Active(slot), nullptr); // prefetched, still the sphere
    }
    EXPECT_FALSE(server.requested.empty());
    terrain.Update(view, {MoonInput(MoonUnderEye(kShackleton, 0.52 * radiusM), slot)});
    EXPECT_EQ(terrain.Active(slot), nullptr);
    terrain.Update(view, {MoonInput(MoonUnderEye(kShackleton, 0.48 * radiusM), slot)});
    ASSERT_NE(terrain.Active(slot), nullptr);
    EXPECT_EQ(terrain.Active(slot)->fade, 0.0); // the swap happens on the bare sphere
    terrain.Update(view, {MoonInput(MoonUnderEye(kShackleton, 0.52 * radiusM), slot)});
    EXPECT_NE(terrain.Active(slot), nullptr); // hysteresis
    terrain.Update(view, {MoonInput(MoonUnderEye(kShackleton, 0.56 * radiusM), slot)});
    EXPECT_EQ(terrain.Active(slot), nullptr);
    EXPECT_DOUBLE_EQ(HeightFade(0.1), 1.0);
    EXPECT_GT(HeightFade(0.3), 0.0);
    EXPECT_LT(HeightFade(0.3), 1.0);
    EXPECT_NE(terrain.StatsLine().find("budget 64.0 MiB"), std::string::npos);
}

TEST(TerrainSystem, TheFloorStandsOnTheTileUnderTheEye) {
    FakeServer server;
    TerrainSystem terrain(server.Fetch());
    terrain.Configure(OnSettings());
    const int slot = terrain.AddBody("moon", kMoonRadiusKm);
    TerrainViewInput view;
    view.pixelsPerRadian = kPixelsPerRadian;
    const BodyFrame body = MoonUnderEye(kShackleton, 1000.0);
    EXPECT_FALSE(terrain.SurfaceRadius(slot, kShackleton)); // not resident: the caller keeps the analytic radius
    for (int frame = 0; frame < 40; ++frame) {
        terrain.Update(view, {MoonInput(body, slot)});
    }
    ASSERT_NE(terrain.Active(slot), nullptr);
    const auto radius = terrain.SurfaceRadius(slot, kShackleton);
    ASSERT_TRUE(radius);
    const double metres = (*radius - kMeshRadius) / body.heightScale;
    // The data stops at level 9 (quads of ~80 m): its upsampled triangles, not the field itself.
    EXPECT_NEAR(metres, Field(kShackleton), 60.0);
    EXPECT_GE(terrain.Active(slot)->selection.deepest, 12);
    EXPECT_NE(terrain.StatsLine().find("moon"), std::string::npos);
}
