#!/usr/bin/env node
/**
 * Generate planet metadata artifacts from resource/planets.catalog.json.
 *
 * Outputs:
 *   - web/public/planet_facts.json
 *   - web/threejs/src/data/orbital-parameters.json
 *   - resource/planet_manifest.json
 *   - src/Solar_System/OrbitLayoutBodies.generated.inc
 *   - src/Solar_System/BodyCatalog.generated.h
 *
 * Usage:
 *   node scripts/generate-planet-metadata.mjs
 *   node scripts/generate-planet-metadata.mjs --check
 *   node scripts/generate-planet-metadata.mjs --write-checksums
 *
 * --check            Exit 1 if committed outputs would differ.
 * --write-checksums  Fill sha256 for existing files listed in asset-manifest.json.
 *
 * Index convention (generated into comments where applicable):
 *   Focus body indices: Sun=0 … Vesta=11 (matches OrbitLayout::Body / explorer panel).
 *   Satellites occupy 12+ and are catalog-built; they are not focusable.
 */
import { createHash } from 'node:crypto';
import {
  existsSync,
  mkdirSync,
  readFileSync,
  readdirSync,
  statSync,
  writeFileSync,
} from 'node:fs';
import { dirname, join, relative } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const catalogPath = join(root, 'resource/planets.catalog.json');
const schemaPath = join(root, 'resource/planets.catalog.schema.json');

const OUT = {
  facts: join(root, 'web/public/planet_facts.json'),
  orbital: join(root, 'web/threejs/src/data/orbital-parameters.json'),
  manifest: join(root, 'resource/planet_manifest.json'),
  assetManifest: join(root, 'resource/asset-manifest.json'),
  orbitLayoutBodies: join(root, 'src/Solar_System/OrbitLayoutBodies.generated.inc'),
  bodyCatalog: join(root, 'src/Solar_System/BodyCatalog.generated.h'),
};

const args = new Set(process.argv.slice(2));
const checkOnly = args.has('--check');
const writeChecksums = args.has('--write-checksums');

function readJson(path) {
  return JSON.parse(readFileSync(path, 'utf8'));
}

function stableStringify(value) {
  return `${JSON.stringify(value, null, 2)}\n`;
}

function normalizeJsonText(text) {
  return stableStringify(JSON.parse(text));
}

const FOCUS_KINDS = new Set(['star', 'planet', 'dwarf_planet']);

function isFocusBody(body) {
  return FOCUS_KINDS.has(body.kind);
}

function validateCatalog(catalog) {
  if (catalog.schemaVersion !== 1) {
    throw new Error(`Unsupported schemaVersion: ${catalog.schemaVersion}`);
  }
  if (!Array.isArray(catalog.bodies) || catalog.bodies.length === 0) {
    throw new Error('catalog.bodies must be a non-empty array');
  }

  const indices = new Map();
  const ids = new Set();
  for (const body of catalog.bodies) {
    if (!body.id || typeof body.id !== 'string') {
      throw new Error('Each body requires a string id');
    }
    if (ids.has(body.id)) {
      throw new Error(`Duplicate body id: ${body.id}`);
    }
    ids.add(body.id);

    if (typeof body.index !== 'number' || body.index < 0) {
      throw new Error(`Body ${body.id} requires a non-negative index`);
    }
    if (indices.has(body.index)) {
      throw new Error(
        `Duplicate index ${body.index}: ${indices.get(body.index)} and ${body.id}`,
      );
    }
    indices.set(body.index, body.id);

    if (body.kind === 'satellite') {
      if (!body.parent) {
        throw new Error(`Satellite ${body.id} requires a parent id`);
      }
      const k = body.orbit?.keplerian;
      if (!k || typeof k.aKm !== 'number' || typeof k.e !== 'number') {
        throw new Error(
          `Satellite ${body.id} requires orbit.keplerian {aKm, e, iDeg, OmegaDeg, omegaDeg, M0Deg, nDegPerDay}`,
        );
      }
    }

    if (body.system?.initTag && body.system.name) {
      const allow = catalog.initTagAllowlist;
      if (Array.isArray(allow) && allow.length > 0 && !allow.includes(body.system.initTag)) {
        throw new Error(
          `Body ${body.id} initTag "${body.system.initTag}" not in initTagAllowlist ` +
            '(must match PlanetSystemLoader::MakePlanetInitFunc generic InitCatalogSystem path)',
        );
      }
    }
  }

  for (const body of catalog.bodies) {
    if (body.parent && !ids.has(body.parent)) {
      throw new Error(`Body ${body.id} parent "${body.parent}" is not in the catalog`);
    }
  }

  // Focusable primary bodies (star/planet/dwarf_planet) must form a contiguous 0..N-1 range.
  const focusIndices = catalog.bodies
    .filter(isFocusBody)
    .map((b) => b.index)
    .sort((a, b) => a - b);
  for (let i = 0; i < focusIndices.length; i++) {
    if (focusIndices[i] !== i) {
      throw new Error(
        `Focus indices must be contiguous from 0 among star/planet/dwarf_planet; ` +
          `missing index ${i} (have ${focusIndices.join(',')})`,
      );
    }
  }

  if (indices.get(0) !== 'sun') {
    throw new Error('Index 0 must be id "sun" (canonical OrbitLayout / explorer convention)');
  }

  const vesta = catalog.bodies.find((b) => b.id === 'vesta');
  if (!vesta || vesta.index !== 11) {
    throw new Error('Focus index 11 must remain id "vesta" (Sun=0 … Vesta=11 frozen)');
  }

  // Lightweight schema presence check (full AJV optional).
  if (existsSync(schemaPath)) {
    const schema = readJson(schemaPath);
    if (!schema.required?.includes('bodies')) {
      throw new Error('planets.catalog.schema.json looks invalid (missing bodies requirement)');
    }
  }
}

function buildPlanetFacts(catalog) {
  const bodies = [...catalog.bodies]
    .filter(isFocusBody)
    .sort((a, b) => a.index - b.index)
    .map((body) => {
      const f = body.facts || {};
      const o = body.orbit || {};
      return {
        index: body.index,
        id: body.id,
        name: body.displayName?.en ?? body.id,
        nameRu: body.displayName?.ru,
        type: body.type,
        diameterKm: f.diameterKm,
        massEarths: f.massEarths,
        // Prefer fact sheet values when present; fall back to orbit block.
        siderealRotationDays: f.siderealRotationDays ?? o.siderealRotationDays,
        orbitalPeriodDays:
          f.orbitalPeriodDays === undefined
            ? (o.orbitalPeriodDays ?? null)
            : f.orbitalPeriodDays,
        distanceAu: f.distanceAu ?? o.distanceAu ?? 0,
        moons: f.moons ?? 0,
        summary: f.summary?.en ?? '',
        summaryRu: f.summary?.ru,
      };
    });

  const scaleModes = {};
  for (const [key, mode] of Object.entries(catalog.scaleModes || {})) {
    scaleModes[key] = {
      label: mode.label?.en ?? key,
      labelRu: mode.label?.ru,
      description: mode.description?.en ?? '',
      descriptionRu: mode.description?.ru,
    };
  }

  return {
    version: 2,
    generatedFrom: 'resource/planets.catalog.json',
    // Canonical focus indices: Sun=0 … Vesta=11. Satellites are omitted from explorer facts.
    indexConvention: 'Sun=0 … Vesta=11; matches OrbitLayout::Body and Module focus APIs',
    sceneNote: catalog.sceneNote?.en ?? '',
    bodies,
    scaleModes,
  };
}

function buildOrbitalParameters(catalog) {
  const bodies = catalog.bodies
    .filter((b) => b.includeInCompanion && isFocusBody(b) && b.kind !== 'star')
    .sort((a, b) => a.index - b.index)
    .map((body) => {
      const o = body.orbit || {};
      const a = body.assets || {};
      const pos = o.compressedOffset || body.system?.proxyPosition || [0, 0, 0];
      return {
        id: body.id,
        name: body.displayName?.en ?? body.id,
        position: pos,
        earthRadiusScale: o.earthRadiusScale ?? 1,
        axialTiltDegrees: o.axialTiltDegrees ?? 0,
        rotationRate: o.rotationRate ?? 0.005,
        texture: a.companionTexture ?? `${body.displayName?.en ?? body.id}_Diffuse.ktx2`,
        fallbackColor: a.fallbackColor ?? '#888888',
      };
    });

  return {
    source:
      'Generated from resource/planets.catalog.json — companion scale 1 unit = 1 C++ world unit',
    generatedFrom: 'resource/planets.catalog.json',
    indexConvention: 'Body order follows catalog focus index (Sun excluded from this list)',
    bodies,
  };
}

function buildPlanetManifest(catalog) {
  const systems = catalog.bodies
    .filter((b) => b.system && b.system.name && b.assets)
    .sort((a, b) => a.index - b.index)
    .map((body) => {
      const sys = body.system;
      const assets = body.assets;
      const entry = {
        name: sys.name || body.displayName?.en || body.id,
        init: sys.initTag,
        proxyPosition: sys.proxyPosition || body.orbit?.compressedOffset || [0, 0, 0],
        activationRadius: sys.activationRadius ?? 800,
        required: assets.requiredLow || [],
        optional: assets.optionalLow || [],
      };
      if (assets.optionalHigh && assets.optionalHigh.length > 0) {
        entry.optionalHighRes = assets.optionalHigh;
      }
      if (assets.optionalMid && assets.optionalMid.length > 0) {
        entry.optionalMidRes = assets.optionalMid;
      }
      return entry;
    });

  return {
    version: 1,
    generatedFrom: 'resource/planets.catalog.json',
    // proxyPosition is heliocentric art offset (same as OrbitLayout compressedOffset).
    systems,
  };
}

/** Format a JSON number as an unambiguous C++ float literal. */
function cppFloat(n) {
  const value = n ?? 0;
  const text = String(value);
  return `${text.includes('.') ? text : `${text}.0`}f`;
}

/**
 * Generates the body-physics table OrbitLayout::kBodies[] (see OrbitLayout.cpp):
 * heliocentric compressed-art offset, AU distance, orbital period, inclination, and
 * sidereal rotation, one row per *focus* body in index order (Sun=0 … Vesta=11).
 * Satellites are excluded — their scene orbits live on BodyCatalog::Entry.
 *
 * Axial tilt is applied by CatalogBody from BodyCatalog::Entry.axialTiltDegrees
 * (catalog SSOT; see docs/ARCHITECTURE.md §2.1). This table stays tilt-free because
 * OrbitLayout only needs ephemeris radius/period/spin.
 */
function buildOrbitLayoutBodiesInc(catalog) {
  const bodies = [...catalog.bodies].filter(isFocusBody).sort((a, b) => a.index - b.index);
  const rows = bodies.map((body) => {
    const o = body.orbit || {};
    const [ox, oy, oz] = o.compressedOffset || [0, 0, 0];
    return (
      `    {glm::vec3(${cppFloat(ox)}, ${cppFloat(oy)}, ${cppFloat(oz)}), ` +
      `${cppFloat(o.distanceAu)}, ${cppFloat(o.orbitalPeriodDays)}, ` +
      `${cppFloat(o.inclinationDeg)}, ${cppFloat(o.siderealRotationDays)}}, // ${body.id}`
    );
  });
  return [
    '// AUTO-GENERATED by scripts/generate-planet-metadata.mjs from resource/planets.catalog.json.',
    '// Do not edit directly — run: node scripts/generate-planet-metadata.mjs',
    "// Included into OrbitLayout.cpp's kBodies[] initializer. Row order is focus index order",
    '// (Sun=0 … Vesta=11). Satellites (index 12+) are not rows here.',
    ...rows,
  ].join('\n');
}

/** Format a JSON string as a C++ string literal (double-quoted, no embedded quotes/newlines here). */
function cppString(s) {
  return `"${s}"`;
}

/** Format a JSON string as a C++ wide string literal (source files are UTF-8). */
function cppWideString(s) {
  return `L"${s}"`;
}

function cppNullableString(s) {
  return s ? cppString(s) : 'nullptr';
}

function cppKind(kind) {
  switch (kind) {
    case 'star':
      return 'Kind::Star';
    case 'dwarf_planet':
      return 'Kind::DwarfPlanet';
    case 'satellite':
      return 'Kind::Satellite';
    default:
      return 'Kind::Planet';
  }
}

function cppOrbitPlane(plane) {
  return plane === 'xy' ? 'OrbitPlane::XY' : 'OrbitPlane::XZ';
}

function strEqHelper() {
  return [
    'inline constexpr bool StrEq(const char* a, const char* b) {',
    '    if (!a || !b) {',
    '        return a == b;',
    '    }',
    '    while (*a && *a == *b) {',
    '        ++a;',
    '        ++b;',
    '    }',
    '    return *a == *b;',
    '}',
  ];
}

/**
 * Render + orbit descriptors consumed by CatalogBody / CatalogSatellite / CatalogClouds.
 * Mercury–Pluto and all catalog moons are built from these rows. Axial tilt is SSOT:
 * catalog orbit.axialTiltDegrees, applied as Rotate(degrees, Z) then optional artTiltX.
 */
function buildBodyCatalogHeader(catalog) {
  const byId = new Map(catalog.bodies.map((b) => [b.id, b]));
  const bodies = catalog.bodies
    .filter((b) => b.render)
    .sort((a, b) => a.index - b.index);

  const entries = bodies.map((body) => {
    const r = body.render;
    const flags = r.shaderFlags || {};
    const lod = r.lod || {};
    const o = body.orbit || {};
    const k = o.keplerian || {};
    const cloud = r.cloudLayer || {};
    const parent = body.parent ? byId.get(body.parent) : null;
    const initTag = body.system?.initTag ?? parent?.system?.initTag ?? body.id;
    const kind = cppKind(body.kind);
    return (
      `    {${body.index}, ${cppString(body.id)}, ${cppNullableString(body.parent)}, ` +
      `${cppString(initTag)}, ${kind}, ` +
      `${cppWideString(body.displayName?.en ?? body.id)}, ` +
      `${cppWideString(body.displayName?.ru ?? body.displayName?.en ?? body.id)}, ` +
      `${cppFloat(o.distanceAu)}, ${cppFloat(o.orbitalPeriodDays)}, ` +
      `${cppFloat(o.earthRadiusScale ?? 1)}, ${cppFloat(o.axialTiltDegrees)}, ` +
      `${cppFloat(r.artTiltXDegrees)}, ${cppFloat(o.yawOffsetDegrees)}, ` +
      `${cppFloat(o.artRollDegrees)}, ` +
      `{${!!flags.hasNightTexture}, ${!!flags.hasSpecularMap}, ${!!flags.hasClouds}}, ` +
      `${!!(r.useSphereIntersect ?? body.kind === 'satellite')}, ` +
      `${cppFloat(r.ambientFactor)}, ` +
      `{${cppString(lod.diffuse ?? '')}, ${cppString(lod.normal ?? '')}, ` +
      `${cppNullableString(lod.specular)}, ${cppNullableString(lod.night)}, ` +
      `${cppNullableString(lod.clouds)}}, ${cppNullableString(r.mesh)}, ` +
      `${cppFloat(o.sceneOrbitRadius)}, ${cppOrbitPlane(o.orbitPlane)}, ` +
      `${cppFloat(o.initialAnomalyRad)}, ${cppFloat(o.spinDegPerSimSecond)}, ` +
      `{${cppFloat(k.aKm)}, ${cppFloat(k.e)}, ${cppFloat(k.iDeg)}, ${cppFloat(k.OmegaDeg)}, ` +
      `${cppFloat(k.omegaDeg)}, ${cppFloat(k.M0Deg)}, ${cppFloat(k.nDegPerDay)}}, ` +
      `{${cppNullableString(cloud.diffuse)}, ${cppNullableString(cloud.normal)}, ` +
      `${cppFloat(cloud.scaleFactor)}, ${cppFloat(cloud.spinDegPerSimSecond)}, ` +
      `${cppFloat(cloud.ambientFactor)}}, ` +
      `${!!body.system?.hasAtmosphere}, ${!!body.system?.hasCloudLayer}, ` +
      `${!!body.system?.hasRings}, ${!!body.system?.atmosphereToneMapping}, ` +
      `${!!body.system?.lightFarUsesPlanetDistance}}, // ${body.id}`
    );
  });

  return [
    '#pragma once',
    '// AUTO-GENERATED by scripts/generate-planet-metadata.mjs from resource/planets.catalog.json.',
    '// Do not edit directly — run: node scripts/generate-planet-metadata.mjs',
    '//',
    '// CatalogBody / CatalogSatellite / CatalogClouds consume these rows. Mercury–Pluto and',
    '// catalog moons are constructed from this table (plus SystemVisuals for atmospheres/rings).',
    '// Axial tilt SSOT is orbit.axialTiltDegrees, applied as Rotate(Z) then optional artTiltX.',
    '// See docs/ARCHITECTURE.md §2.1.',
    '',
    '#include <cstdint>',
    '',
    'namespace BodyCatalog {',
    '',
    'enum class Kind : uint8_t { Star = 0, Planet = 1, DwarfPlanet = 2, Satellite = 3 };',
    'enum class OrbitPlane : uint8_t { XZ = 0, XY = 1 };',
    '',
    'struct ShaderFlags {',
    '    bool hasNightTexture;',
    '    bool hasSpecularMap;',
    '    bool hasClouds;',
    '};',
    '',
    'struct TextureLodIds {',
    '    const char* diffuse;',
    '    const char* normal;',
    '    const char* specular; // nullptr if this body has no specular map.',
    '    const char* night;    // nullptr if no night map (Earth).',
    '    const char* clouds;   // nullptr if the surface shader has no cloud map.',
    '};',
    '',
    'struct Keplerian {',
    '    float aKm;',
    '    float e;',
    '    float iDeg;',
    '    float OmegaDeg;',
    '    float omegaDeg;',
    '    float M0Deg;',
    '    float nDegPerDay;',
    '};',
    '',
    'struct CloudLayer {',
    '    const char* diffuse; // nullptr => no cloud shell.',
    '    const char* normal;',
    '    float scaleFactor;',
    '    float spinDegPerSimSecond;',
    '    float ambientFactor;',
    '};',
    '',
    'struct Entry {',
    '    int index;',
    '    const char* id;',
    '    const char* parentId; // nullptr for heliocentric bodies.',
    '    const char* initTag;',
    '    Kind kind;',
    '    const wchar_t* displayNameEn;',
    '    const wchar_t* displayNameRu;',
    '    float auDistance;',
    '    float orbitalPeriodDays;',
    '    float earthRadiusScale;',
    '    float axialTiltDegrees; // catalog SSOT; Rotate(Z) in CatalogBody.',
    '    float artTiltXDegrees;  // Saturn ring-presentation overlay; else 0.',
    '    float yawOffsetDegrees;',
    '    float artRollDegrees;',
    '    ShaderFlags shaderFlags;',
    '    bool useSphereIntersect;',
    '    float ambientFactor;',
    '    TextureLodIds lod;',
    '    const char* meshPath; // nullptr => unit sphere.',
    '    float sceneOrbitRadius;',
    '    OrbitPlane orbitPlane;',
    '    float initialAnomalyRad;',
    '    float spinDegPerSimSecond;',
    '    Keplerian keplerian;',
    '    CloudLayer cloudLayer;',
    '    bool hasAtmosphere;',
    '    bool hasCloudLayer;',
    '    bool hasRings;',
    '    bool atmosphereToneMapping;',
    '    bool lightFarUsesPlanetDistance;',
    '};',
    '',
    'inline constexpr Entry kEntries[] = {',
    ...entries,
    '};',
    '',
    ...strEqHelper(),
    '',
    '/** Row for a catalog index, or nullptr when that body has no render descriptor. */',
    'inline constexpr const Entry* FindByIndex(int index) {',
    '    for (const Entry& entry : kEntries) {',
    '        if (entry.index == index) {',
    '            return &entry;',
    '        }',
    '    }',
    '    return nullptr;',
    '}',
    '',
    'inline constexpr const Entry* FindById(const char* id) {',
    '    for (const Entry& entry : kEntries) {',
    '        if (StrEq(entry.id, id)) {',
    '            return &entry;',
    '        }',
    '    }',
    '    return nullptr;',
    '}',
    '',
    '/** Primary (non-satellite) body for a staged-loading initTag, or nullptr. */',
    'inline constexpr const Entry* FindPrimaryByInitTag(const char* initTag) {',
    '    for (const Entry& entry : kEntries) {',
    '        if (entry.kind != Kind::Satellite && StrEq(entry.initTag, initTag)) {',
    '            return &entry;',
    '        }',
    '    }',
    '    return nullptr;',
    '}',
    '',
    '} // namespace BodyCatalog',
  ].join('\n');
}

function writeIfChanged(path, content, { checkOnly: check }) {
  mkdirSync(dirname(path), { recursive: true });
  const next = content.endsWith('\n') ? content : `${content}\n`;
  if (existsSync(path)) {
    const prev = readFileSync(path, 'utf8');
    // Compare normalized JSON when both parse as JSON.
    try {
      if (normalizeJsonText(prev) === normalizeJsonText(next)) {
        return { path, changed: false };
      }
    } catch {
      if (prev === next) {
        return { path, changed: false };
      }
    }
  }
  if (check) {
    return { path, changed: true };
  }
  writeFileSync(path, next, 'utf8');
  return { path, changed: true };
}

function sha256File(path) {
  const hash = createHash('sha256');
  hash.update(readFileSync(path));
  return hash.digest('hex');
}

function updateAssetManifestChecksums() {
  if (!existsSync(OUT.assetManifest)) {
    throw new Error(`Missing ${OUT.assetManifest}`);
  }
  const manifest = readJson(OUT.assetManifest);
  const assetRoot = join(root, 'resource');
  let updated = 0;
  let missing = 0;
  for (const file of manifest.files || []) {
    const local = join(assetRoot, file.path);
    if (!existsSync(local) || !statSync(local).isFile()) {
      missing += 1;
      continue;
    }
    const digest = sha256File(local);
    if (file.sha256 !== digest) {
      file.sha256 = digest;
      updated += 1;
    }
  }
  const text = stableStringify(manifest);
  const result = writeIfChanged(OUT.assetManifest, text, { checkOnly });
  return { updated, missing, result };
}

function main() {
  if (!existsSync(catalogPath)) {
    console.error(`Catalog not found: ${catalogPath}`);
    process.exit(1);
  }

  const catalog = readJson(catalogPath);
  validateCatalog(catalog);

  const outputs = [
    { path: OUT.facts, text: stableStringify(buildPlanetFacts(catalog)) },
    { path: OUT.orbital, text: stableStringify(buildOrbitalParameters(catalog)) },
    { path: OUT.manifest, text: stableStringify(buildPlanetManifest(catalog)) },
    { path: OUT.orbitLayoutBodies, text: buildOrbitLayoutBodiesInc(catalog) },
    { path: OUT.bodyCatalog, text: buildBodyCatalogHeader(catalog) },
  ];

  let dirty = false;
  for (const { path, text } of outputs) {
    const { changed } = writeIfChanged(path, text, { checkOnly });
    const rel = relative(root, path);
    if (changed) {
      dirty = true;
      console.log(`${checkOnly ? 'WOULD UPDATE' : 'updated'} ${rel}`);
    } else {
      console.log(`up-to-date ${rel}`);
    }
  }

  if (writeChecksums) {
    const { updated, missing, result } = updateAssetManifestChecksums();
    const rel = relative(root, OUT.assetManifest);
    if (result.changed) {
      dirty = true;
      console.log(
        `${checkOnly ? 'WOULD UPDATE' : 'updated'} ${rel} (${updated} checksums, ${missing} missing files left unchanged)`,
      );
    } else {
      console.log(`up-to-date ${rel} (${updated} checksums already current, ${missing} missing)`);
    }
  }

  if (checkOnly && dirty) {
    console.error(
      '\nGenerated planet metadata is out of date. Run:\n' +
        '  node scripts/generate-planet-metadata.mjs\n' +
        'and commit the results.',
    );
    process.exit(1);
  }

  console.log(checkOnly ? 'Planet metadata check passed.' : 'Planet metadata generation complete.');
}

main();
