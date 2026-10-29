#!/usr/bin/env python3
"""Bake a body's elevation model into the cube-sphere height tiles the app streams.

Run by hand, never in CI: the source rasters are gigabytes and stay out of git, and so do
the tiles. Publish the output directory with the other runtime assets (VITE_ASSET_BASE,
see README "Terrain tiles") and list its tileset.json in asset-manifest.json.

Output, read by src/render/terrain/ (docs/ARCHITECTURE.md §11.3):

  <out>/<body>/tileset.json             format ssterrain-1: levels, height range, faces
  <out>/<body>/<face>/<z>/<x>/<y>.ktx2  67x67 R16_UNORM: 65x65 samples plus a 1-sample apron

A tile's samples are heights in metres above the catalog radius of the body, quantised
between the tile's own min and max (the KTX2 "SSterrain" entry, with the mask of children
that exist). Sample (i, j), i and j in -1..65, sits at face coordinates
s = s0 + size*i/64, t = t0 + size*j/64 on the equal-angle cube (w = tan(pi/4 s)), face
bases as in CubeSphere.cpp; rows go up in t. Longitude follows the sphere mesh and the
diffuse maps: east = -atan2(z, x) in model space, +Y the north pole, 0 on +X.

Every tile of a level samples the same global grid (the source averaged over power-of-two
blocks, then bilinear), so two tiles that share an edge or a corner agree on it exactly.

Sources (the catalog row's render.terrain.attribution names them):
  moon     LRO LOLA LDEM (e.g. LDEM_118M / LDEM_512), heights above 1737.4 km  --datum-km 1737.4
  mars     MGS MOLA MEGDR (e.g. megt90n000fb), heights above the areoid         --datum-km 3396.0
  mercury  MESSENGER MLA/MDIS global DEM, above 2439.4 km                         --datum-km 2439.4
           faces the DEM leaves mostly empty are left out and stay the sphere
  earth    SRTM v3 first, then ETOPO 2022 (bathymetry and the rest of the land)   --datum-km 6371.0

Examples:
  python3 scripts/bake-terrain-tiles.py moon --source LDEM_118M.tif --datum-km 1737.4 \\
      --levels 6 --region -89.9 0 60 9 --out build/terrain
  python3 scripts/bake-terrain-tiles.py mars --source megt128.tif --datum-km 3396 \\
      --levels 6 --region 18.65 -133.8 400 9 --out build/terrain
  python3 scripts/bake-terrain-tiles.py earth --source srtm.vrt --source etopo2022.tif \\
      --datum-km 6371 --levels 7 --out build/terrain

Any raster GDAL reads in geographic coordinates works (GDAL is imported only for those); a
.npy file is taken as a global equirectangular grid, north row first, west column first.
"""
import argparse
import hashlib
import json
import math
import os
import struct
import sys
from pathlib import Path

import numpy as np

QUADS = 64
APRON = 1
STRIDE = QUADS + 1 + 2 * APRON  # 67
VK_FORMAT_R16_UNORM = 70
KTX_IDENTIFIER = bytes([0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A])
REPO = Path(__file__).resolve().parent.parent

# CubeSphere.cpp Basis(): (n, u, v) per face, +X -X +Y -Y +Z -Z.
FACES = [
    ((1, 0, 0), (0, 0, -1), (0, 1, 0)),
    ((-1, 0, 0), (0, 0, 1), (0, 1, 0)),
    ((0, 1, 0), (1, 0, 0), (0, 0, -1)),
    ((0, -1, 0), (1, 0, 0), (0, 0, 1)),
    ((0, 0, 1), (1, 0, 0), (0, 1, 0)),
    ((0, 0, -1), (-1, 0, 0), (0, 1, 0)),
]


# --- Sources ---------------------------------------------------------------------------

class Source:
    """A geographic raster: width x height cells, cell (0, 0) at (lon0, lat0) top-left."""

    width = height = 0
    lon0 = -180.0
    lat0 = 90.0
    dlon = dlat = 0.0  # degrees per cell; dlat is negative (north row first)

    def global_lon(self):
        return abs(self.width * self.dlon - 360.0) < 1e-6 * 360.0

    def block(self, col0, row0, cols, rows, factor):
        """Mean of factor x factor cells from (col0, row0), cols x rows of them; NaN = no data."""
        raise NotImplementedError


class NpySource(Source):
    def __init__(self, path):
        self.data = np.load(path).astype(np.float32)
        self.height, self.width = self.data.shape
        self.dlon = 360.0 / self.width
        self.dlat = -180.0 / self.height

    def block(self, col0, row0, cols, rows, factor):
        r = np.clip(np.arange(row0 * factor, (row0 + rows) * factor), 0, self.height - 1)
        c = np.mod(np.arange(col0 * factor, (col0 + cols) * factor), self.width)
        cells = self.data[np.ix_(r, c)]
        return cells.reshape(rows, factor, cols, factor).mean(axis=(1, 3))


class GdalSource(Source):
    def __init__(self, path, scale=None, offset=None):
        from osgeo import gdal  # only for real rasters; the .npy path needs none
        gdal.UseExceptions()
        self.gdal = gdal
        self.dataset = gdal.Open(str(path))
        self.band = self.dataset.GetRasterBand(1)
        gt = self.dataset.GetGeoTransform()
        if gt[2] != 0 or gt[4] != 0:
            raise SystemExit(f"{path}: rotated rasters are not supported; warp it to plain lon/lat first")
        self.lon0, self.dlon, self.lat0, self.dlat = gt[0], gt[1], gt[3], gt[5]
        srs = self.dataset.GetSpatialRef()
        if srs is not None and srs.IsProjected():
            # Degrees per metre at the equator would be a guess; a projected DEM has to be
            # warped (gdalwarp -t_srs "+proj=longlat +R=<radius>") before it comes here.
            raise SystemExit(f"{path}: projected raster; warp it to geographic coordinates first")
        self.width, self.height = self.dataset.RasterXSize, self.dataset.RasterYSize
        self.nodata = self.band.GetNoDataValue()
        self.scale = scale if scale is not None else (self.band.GetScale() or 1.0)
        self.offset = offset if offset is not None else (self.band.GetOffset() or 0.0)

    def _read(self, col0, row0, cols, rows, factor):
        out = np.full((rows, cols), np.nan, dtype=np.float64)
        r0, r1 = max(row0 * factor, 0), min((row0 + rows) * factor, self.height)
        if r1 <= r0:
            return out
        # Columns in at most two runs when the window wraps round a global raster.
        runs = []
        start = col0 * factor
        end = (col0 + cols) * factor
        if self.global_lon():
            while start < end:
                wrapped = start % self.width
                take = min(end - start, self.width - wrapped)
                runs.append((wrapped, take, (start - col0 * factor)))
                start += take
        else:
            lo, hi = max(start, 0), min(end, self.width)
            if hi > lo:
                runs.append((lo, hi - lo, lo - col0 * factor))
        for src_col, count, dst in runs:
            # Whole blocks only, so every tile averages the same cells (GRIORA_Average).
            first, last = dst // factor, (dst + count + factor - 1) // factor
            c0 = src_col - (dst - first * factor)
            c0, c1 = max(c0, 0), min(c0 + (last - first) * factor, self.width)
            if c1 <= c0:
                continue
            raw = self.band.ReadAsArray(c0, r0, c1 - c0, r1 - r0,
                                        buf_xsize=max(1, (c1 - c0) // factor), buf_ysize=max(1, (r1 - r0) // factor),
                                        resample_alg=self.gdal.GRIORA_Average).astype(np.float64)
            if self.nodata is not None:
                raw[raw == self.nodata] = np.nan
            rr = (r0 // factor) - row0
            out[rr:rr + raw.shape[0], first:first + raw.shape[1]] = raw[:, :last - first] * self.scale + self.offset
        return out

    def block(self, col0, row0, cols, rows, factor):
        return self._read(col0, row0, cols, rows, factor)


def open_source(path, args):
    path = Path(path)
    if path.suffix == ".npy":
        return NpySource(path)
    return GdalSource(path, args.scale, args.offset)


# --- Geometry --------------------------------------------------------------------------

def warp(s):
    return np.tan(0.25 * math.pi * s)


def tile_lonlat(face, level, x, y):
    """Longitude east and latitude, degrees, of the tile's 67x67 samples (apron included)."""
    size = 2.0 / (1 << level)
    i = np.arange(-APRON, QUADS + 1 + APRON, dtype=np.float64)
    s = -1.0 + size * x + size * i / QUADS
    t = -1.0 + size * y + size * i / QUADS
    ws, wt = np.meshgrid(warp(s), warp(t))  # rows in t, columns in s
    n, u, v = (np.array(a, dtype=np.float64) for a in FACES[face])
    d = n[None, None, :] + ws[..., None] * u[None, None, :] + wt[..., None] * v[None, None, :]
    d /= np.linalg.norm(d, axis=-1, keepdims=True)
    lon = np.degrees(-np.arctan2(d[..., 2], d[..., 0]))
    lat = np.degrees(np.arcsin(np.clip(d[..., 1], -1.0, 1.0)))
    return lon, lat


def tile_spacing_deg(level):
    """A level's sample spacing at a face centre, degrees of arc."""
    return math.degrees(0.5 * math.pi / ((1 << level) * QUADS))


def sample_level(source, lon, lat, level, lon_offset):
    """Bilinear heights at (lon, lat) from the source averaged to about this level's spacing."""
    native = abs(source.dlon)
    factor = 1
    while factor * 2 * native <= tile_spacing_deg(level):
        factor *= 2
    dlon, dlat = source.dlon * factor, source.dlat * factor
    lon = lon + lon_offset
    # Cell centres of the block grid: column c at lon0 + (c + 0.5) dlon.
    fc = (lon - source.lon0) / dlon - 0.5
    fr = (lat - source.lat0) / dlat - 0.5
    if source.global_lon():
        period = source.width / factor
        # Unwrap round the tile's middle so a window never spans the whole raster for nothing.
        middle = fc[fc.shape[0] // 2, fc.shape[1] // 2]
        fc = middle + np.mod(fc - middle + period / 2, period) - period / 2
    c0 = int(math.floor(fc.min())) - 1
    r0 = int(math.floor(fr.min())) - 1
    cols = int(math.floor(fc.max())) - c0 + 3
    rows = int(math.floor(fr.max())) - r0 + 3
    rows_total = int(math.ceil(source.height / factor))
    grid = np.full((rows, cols), np.nan)
    rr0, rr1 = max(r0, 0), min(r0 + rows, rows_total)
    if rr1 > rr0:
        grid[rr0 - r0:rr1 - r0] = source.block(c0, rr0, cols, rr1 - rr0, factor)
    fc -= c0
    fr = np.clip(fr - r0, 0.0, rows - 1.000001)
    ic, ir = np.floor(fc).astype(int), np.floor(fr).astype(int)
    ic = np.clip(ic, 0, cols - 2)
    ir = np.clip(ir, 0, rows - 2)
    ac, ar = fc - ic, fr - ir
    g00, g10 = grid[ir, ic], grid[ir, ic + 1]
    g01, g11 = grid[ir + 1, ic], grid[ir + 1, ic + 1]
    return (g00 * (1 - ac) + g10 * ac) * (1 - ar) + (g01 * (1 - ac) + g11 * ac) * ar


def sample_tile(sources, face, level, x, y, args):
    lon, lat = tile_lonlat(face, level, x, y)
    heights = np.full(lon.shape, np.nan)
    for source in sources:  # first valid sample wins (SRTM before ETOPO)
        missing = np.isnan(heights)
        if not missing.any():
            break
        values = sample_level(source, lon, lat, level, args.lon_offset_deg)
        heights[missing] = values[missing]
    valid = ~np.isnan(heights)
    heights += (args.datum_km - args.radius_km) * 1000.0
    if args.min_height is not None:
        heights = np.maximum(heights, args.min_height)
    return np.where(valid, heights, 0.0), valid.mean()


# --- Which tiles -----------------------------------------------------------------------

def angular_distance_deg(lat1, lon1, lat2, lon2):
    p1, p2, dl = math.radians(lat1), math.radians(lat2), math.radians(lon2 - lon1)
    c = math.sin(p1) * math.sin(p2) + math.cos(p1) * math.cos(p2) * math.cos(dl)
    return math.degrees(math.acos(max(-1.0, min(1.0, c))))


def wanted(face, level, x, y, args):
    if level <= args.levels:
        return True
    if not args.region:
        return False
    lon, lat = tile_lonlat(face, level, x, y)
    centre_lat, centre_lon = lat[STRIDE // 2, STRIDE // 2], lon[STRIDE // 2, STRIDE // 2]
    half = max(angular_distance_deg(centre_lat, centre_lon, lat[a, b], lon[a, b])
               for a in (APRON, STRIDE - 1 - APRON) for b in (APRON, STRIDE - 1 - APRON))
    for region_lat, region_lon, radius_km, max_level in args.region:
        reach = math.degrees(radius_km / args.radius_km)
        if level <= max_level and angular_distance_deg(centre_lat, centre_lon, region_lat, region_lon) < reach + half:
            return True
    return False


# --- Output ----------------------------------------------------------------------------

def ktx2_r16(texels, key_values):
    """A single-level KTX2, the same bytes ktx2::WriteR16Unorm writes."""
    dfd = struct.pack("<11I", 44, 0, 2 | (40 << 16), 1 | (1 << 8) | (1 << 16), 0, 2, 0, 15 << 16, 0, 0, 0xFFFF)
    kvd = b""
    for key, value in sorted(key_values.items()):
        entry = key.encode() + b"\0" + value.encode() + b"\0"
        kvd += struct.pack("<I", len(entry)) + entry + b"\0" * (-len(entry) % 4)
    header_size = 12 + 9 * 4 + 4 * 4 + 2 * 8 + 3 * 8  # identifier, header, index, sgd, one level
    dfd_at = header_size
    kvd_at = dfd_at + len(dfd)
    level_at = kvd_at + len(kvd)
    level_at += -level_at % 4
    payload = texels.astype("<u2").tobytes()
    out = KTX_IDENTIFIER + struct.pack("<9I", VK_FORMAT_R16_UNORM, 2, STRIDE, STRIDE, 0, 0, 1, 1, 0)
    out += struct.pack("<4I", dfd_at, len(dfd), kvd_at if kvd else 0, len(kvd))
    out += struct.pack("<2Q", 0, 0) + struct.pack("<3Q", level_at, len(payload), len(payload))
    out += dfd + kvd
    out += b"\0" * (level_at - len(out))
    return out + payload


def float9(value):
    # TileMetadata() writes 9 significant digits; float32 then round-trips exactly.
    return f"{float(np.float32(value)):.9g}"


def encode(heights, children):
    low, high = np.float32(heights.min()), np.float32(heights.max())
    span = float(high) - float(low)
    texels = np.zeros(heights.shape) if span <= 0 else np.round((heights - float(low)) / span * 65535.0)
    meta = f"minM={float9(low)} maxM={float9(high)} children={children}"
    return ktx2_r16(np.clip(texels, 0, 65535), {"KTXorientation": "ru", "SSterrain": meta}), float(low), float(high)


def bake(args):
    sources = [open_source(path, args) for path in args.source]
    out = Path(args.out) / args.body
    deepest = max([args.levels] + [int(r[3]) for r in args.region])
    faces, lowest, highest, count, digest = [], math.inf, -math.inf, 0, hashlib.sha256()

    def visit(face, level, x, y, heights):
        nonlocal lowest, highest, count
        children = [q for q in range(4) if level < deepest and
                    wanted(face, level + 1, (x << 1) | (q & 1), (y << 1) | (q >> 1), args)]
        mask = sum(1 << q for q in children)
        data, low, high = encode(heights, mask)
        path = out / str(face) / str(level) / str(x) / f"{y}.ktx2"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        digest.update(f"{face}/{level}/{x}/{y}".encode() + hashlib.sha256(data).digest())
        lowest, highest, count = min(lowest, low), max(highest, high), count + 1
        if count % 500 == 0:
            print(f"  {count} tiles (level {level})", flush=True)
        for q in children:
            cx, cy = (x << 1) | (q & 1), (y << 1) | (q >> 1)
            child, _ = sample_tile(sources, face, level + 1, cx, cy, args)
            visit(face, level + 1, cx, cy, child)

    for face in range(6):
        root, coverage = sample_tile(sources, face, 0, 0, 0, args)
        if coverage < args.min_face_coverage:
            print(f"face {face}: {coverage:.1%} covered, left out (the sphere draws there)")
            continue
        faces.append(face)
        print(f"face {face}: {coverage:.1%} covered")
        visit(face, 0, 0, 0, root)

    tileset = {
        "format": "ssterrain-1",
        "body": args.body,
        "radiusKm": args.radius_km,
        "maxLevel": deepest,
        "minHeightM": lowest if count else 0.0,
        "maxHeightM": highest if count else 0.0,
        "faces": faces,
        "attribution": args.attribution,
        "sources": [os.path.basename(p) for p in args.source],
        "tiles": count,
        "tilesSha256": digest.hexdigest(),
    }
    (out / "tileset.json").write_text(json.dumps(tileset, indent=2) + "\n")
    print(f"{count} tiles, levels 0-{deepest}, heights {tileset['minHeightM']:.0f}..{tileset['maxHeightM']:.0f} m "
          f"-> {out}")


def catalog_row(body):
    catalog = json.loads((REPO / "resource" / "planets.catalog.json").read_text())
    rows = catalog["bodies"] if isinstance(catalog, dict) else catalog
    for row in rows:
        if row.get("id") == body:
            return row
    raise SystemExit(f"{body}: not in resource/planets.catalog.json")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("body", help="catalog id with render.terrain: moon, mars, mercury, earth")
    parser.add_argument("--source", action="append", required=True,
                        help="elevation raster (GDAL, geographic) or global .npy; repeat in priority order")
    parser.add_argument("--datum-km", type=float, required=True, help="radius the source's heights are measured from")
    parser.add_argument("--levels", type=int, default=5, help="bake every tile down to this level (default 5)")
    parser.add_argument("--region", nargs=4, type=float, action="append", default=[],
                        metavar=("LAT", "LON", "RADIUS_KM", "MAX_LEVEL"),
                        help="also bake down to MAX_LEVEL within RADIUS_KM of LAT, LON (repeatable)")
    parser.add_argument("--out", default="build/terrain", help="output root (default build/terrain)")
    parser.add_argument("--scale", type=float, help="override the band's scale (metres per stored unit)")
    parser.add_argument("--offset", type=float, help="override the band's offset (metres)")
    parser.add_argument("--lon-offset-deg", type=float, default=0.0,
                        help="added to the mesh longitude before sampling, if the diffuse map is not centred on 0")
    parser.add_argument("--min-height", type=float, help="clamp heights below this (metres above the catalog radius)")
    parser.add_argument("--min-face-coverage", type=float, default=0.98,
                        help="leave out a face with less valid data than this at level 0 (default 0.98)")
    args = parser.parse_args()

    row = catalog_row(args.body)
    terrain = row.get("render", {}).get("terrain")
    if not terrain:
        raise SystemExit(f"{args.body}: the catalog row has no render.terrain; add it (with the attribution) first")
    # As the generator derives BodyCatalog::Entry::radiusKm, which the runtime scales heights by.
    args.radius_km = float(row["facts"]["diameterKm"]) / 2.0
    args.attribution = terrain["attribution"]
    args.region = [(lat, lon, radius, int(level)) for lat, lon, radius, level in args.region]
    bake(args)
    return 0


if __name__ == "__main__":
    sys.exit(main())
