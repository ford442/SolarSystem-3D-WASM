#include "TerrainQuadtree.h"

#include <algorithm>
#include <cmath>

namespace Terrain {

namespace {

/** Both diagonals of a tile on the unit sphere. */
double UnitDiagonal(const TileKey& key) {
    const TileExtent e = Extent(key);
    const double s1 = e.s0 + e.size, t1 = e.t0 + e.size;
    const double a = glm::distance(Direction(key.face, e.s0, e.t0), Direction(key.face, s1, t1));
    const double b = glm::distance(Direction(key.face, s1, e.t0), Direction(key.face, e.s0, t1));
    return std::max(a, b);
}

struct Visit {
    const BodyFrame& body;
    const LevelRanges& ranges;
    const SelectInput& input;
    TileSource& source;
    Selection& out;
    double maxScale;
    double occluderRadius;

    void Node(const HeightTile& tile, const HeightTile* parent) {
        const TileKey& key = tile.key;
        const int level = key.level;
        const double skirt = SkirtDepth(key, tile, body);
        const BoundingSphere model = TileBound(key, tile, body, skirt);
        const glm::dvec3 center = body.center + body.shape * glm::dvec3(model.center);
        const double radius = static_cast<double>(model.radius) * maxScale;
        const BoundingSphere bound{glm::vec3(center), static_cast<float>(radius)};

        if (BehindHorizon(input.eye, body.center, occluderRadius, BoundingSphere{bound})) {
            ++out.horizonCulled;
            return;
        }
        const bool inView = !input.views || std::any_of(input.views->begin(), input.views->end(),
                                                        [&](const Frustum& f) { return f.Intersects(bound); });
        const bool inLight = input.light && input.light->Intersects(bound);
        if (!inView && !inLight) {
            ++out.frustumCulled;
            return;
        }
        source.Touch(tile);

        const glm::dvec3 toTile = center - input.eye;
        const double centerDistance = glm::length(toTile);
        const double distance = std::max(0.0, centerDistance - radius);
        if (level < kMaxLevel && distance < ranges.split[level]) {
            RequestPriority priority;
            const double quadPixels = ranges.diagonal[level] / kTileQuads / std::max(distance, 1e-12) * input.pixelsPerRadian;
            priority.errorBucket = static_cast<int>(std::floor(std::log2(std::max(quadPixels, 1e-30))));
            priority.viewAngle = centerDistance > 0.0
                ? std::acos(std::clamp(glm::dot(toTile / centerDistance, input.viewDirection), -1.0, 1.0))
                : 0.0;
            const HeightTile* children[4];
            bool all = true;
            for (int q = 0; q < 4; ++q) {
                children[q] = source.Child(tile, q, priority);
                all = all && children[q];
            }
            if (all) {
                for (const HeightTile* child : children) {
                    Node(*child, &tile);
                }
                return;
            }
            ++out.waiting;
        }

        DrawTile draw;
        draw.tile = &tile;
        draw.parent = parent;
        draw.frame = MakeTileFrame(key, body.meshRadius);
        draw.center = body.center + body.shape * draw.frame.centerModel;
        draw.morph = parent ? ranges.Morph(level) : glm::dvec2(1.0, 0.0);
        draw.distance = distance;
        draw.skirtDepth = skirt;
        draw.inView = inView;
        draw.inLight = inLight;
        // At its own level a tile is at least split(L) = ratio × diagonal away, its quads a
        // few pixels; only a stand-in for children still loading (or the deepest level) comes
        // close enough for its triangles to need per-fragment depth.
        draw.nearSurface = distance < ranges.diagonal[level];
        out.tiles.push_back(draw);
        out.deepest = std::max(out.deepest, level);
    }
};

} // namespace

LevelRanges LevelRanges::Make(double radiusWorld, double pixelsPerRadian, double maxPixelError) {
    LevelRanges ranges;
    // A quad's edge is about diagonal / (64 √2); it spans maxPixelError pixels at the split distance.
    ranges.ratio = std::max(kMinSplitRatio, pixelsPerRadian / (kTileQuads * std::sqrt(2.0) * std::max(maxPixelError, 0.1)));
    for (int level = 0; level <= kMaxLevel; ++level) {
        // The face's centre, edge and corner tiles span the largest and smallest of the level.
        const uint32_t last = (1u << level) - 1u, middle = last / 2u;
        double longest = 0.0;
        for (const auto& [x, y] : {std::pair{middle, middle}, std::pair{0u, middle}, std::pair{0u, 0u}}) {
            longest = std::max(longest, UnitDiagonal(TileKey{0, static_cast<uint8_t>(level), x, y}));
        }
        ranges.diagonal[level] = longest * radiusWorld;
        ranges.split[level] = ranges.ratio * ranges.diagonal[level];
    }
    return ranges;
}

glm::dvec2 LevelRanges::Morph(int level) const {
    if (level <= 0 || level > kMaxLevel) {
        return {1.0, 0.0};
    }
    return {split[level] + diagonal[level], split[level - 1]};
}

double BodyFrame::MaxScale() const {
    return std::max({glm::length(shape[0]), glm::length(shape[1]), glm::length(shape[2])});
}

double BodyFrame::MinScale() const {
    return std::min({glm::length(shape[0]), glm::length(shape[1]), glm::length(shape[2])});
}

double SkirtDepth(const TileKey& key, const HeightTile& tile, const BodyFrame& body) {
    // One quad's edge, or a quarter of the relief: a level step's crack is no deeper.
    const double quad = 0.25 * 3.14159265358979323846 * Extent(key).size / kTileQuads * body.meshRadius;
    const double relief = static_cast<double>(tile.maxHeight - tile.minHeight) * body.heightScale;
    return std::max(quad, 0.25 * relief);
}

BoundingSphere TileBound(const TileKey& key, const HeightTile& tile, const BodyFrame& body, double skirtDepth) {
    const TileExtent e = Extent(key);
    const double low = body.meshRadius + tile.minHeight * body.heightScale - skirtDepth;
    const double high = body.meshRadius + tile.maxHeight * body.heightScale;
    const double sc = e.s0 + 0.5 * e.size, tc = e.t0 + 0.5 * e.size;
    const glm::dvec3 center = Direction(key.face, sc, tc) * (0.5 * (low + high));
    // Distance from the centre grows with the angle from it, so the corners bound the tile;
    // the edge midpoints are cheap insurance.
    double radius = 0.5 * (high - low);
    for (int i = 0; i <= 2; ++i) {
        for (int j = 0; j <= 2; ++j) {
            if (i == 1 && j == 1) {
                continue;
            }
            const glm::dvec3 direction = Direction(key.face, e.s0 + 0.5 * i * e.size, e.t0 + 0.5 * j * e.size);
            radius = std::max({radius, glm::distance(direction * low, center), glm::distance(direction * high, center)});
        }
    }
    return {glm::vec3(center), static_cast<float>(radius * (1.0 + 1e-6))};
}

bool BehindHorizon(const glm::dvec3& eye, const glm::dvec3& center, double occluderRadius, const BoundingSphere& bound) {
    const glm::dvec3 toEye = eye - center;
    const double eyeDistance = glm::length(toEye);
    if (occluderRadius <= 0.0 || eyeDistance <= occluderRadius) {
        return false;
    }
    // Behind the plane of the horizon circle, and inside the cone the body's sphere shadows
    // from the eye: every sight line to it crosses the disc the circle bounds, inside the body.
    const glm::dvec3 up = toEye / eyeDistance;
    const glm::dvec3 boundCenter(bound.center);
    const double radius = bound.radius;
    if (glm::dot(boundCenter - center, up) + radius >= occluderRadius * occluderRadius / eyeDistance) {
        return false;
    }
    const glm::dvec3 toBound = boundCenter - eye;
    const double boundDistance = glm::length(toBound);
    if (boundDistance <= radius) {
        return false;
    }
    const double coneHalfAngle = std::asin(occluderRadius / eyeDistance);
    const double offAxis = std::acos(std::clamp(glm::dot(toBound / boundDistance, -up), -1.0, 1.0));
    return offAxis + std::asin(radius / boundDistance) < coneHalfAngle;
}

void Select(const BodyFrame& body, const LevelRanges& ranges, const SelectInput& input, TileSource& source,
            Selection& out) {
    out = Selection{};
    const double occluder = (body.meshRadius + std::min(0.0, body.minHeightM) * body.heightScale) * body.MinScale();
    Visit visit{body, ranges, input, source, out, body.MaxScale(), occluder};
    for (int face = 0; face < kFaceCount; ++face) {
        if (const HeightTile* root = source.Root(face)) {
            visit.Node(*root, nullptr);
        }
    }
    std::sort(out.tiles.begin(), out.tiles.end(), [](const DrawTile& a, const DrawTile& b) {
        return a.distance != b.distance ? a.distance < b.distance : a.tile->key.Packed() < b.tile->key.Packed();
    });
}

double ParentSurfaceHeight(const HeightTile& parent, int quadrant, int gx, int gy) {
    // The child's grid point in the parent's, doubled: even lands on a parent sample, odd
    // halfway along the quad edge (or diagonal) the parent's triangles have there.
    const int cx = gx + kTileQuads * (quadrant & 1), cy = gy + kTileQuads * ((quadrant >> 1) & 1);
    const int px = cx >> 1, py = cy >> 1;
    return 0.5 * (static_cast<double>(parent.At(px, py)) + parent.At(px + (cx & 1), py + (cy & 1)));
}

double MorphedHeight(const HeightTile& tile, const HeightTile* parent, int gx, int gy, double k) {
    const double own = tile.At(gx, gy);
    if (!parent || k <= 0.0) {
        return own;
    }
    return own + k * (ParentSurfaceHeight(*parent, tile.key.Quadrant(), gx, gy) - own);
}

} // namespace Terrain
