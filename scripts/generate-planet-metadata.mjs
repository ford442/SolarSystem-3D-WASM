#!/usr/bin/env node
/**
 * Generate planet metadata artifacts from resource/planets.catalog.json.
 *
 * Outputs:
 *   - web/public/planet_facts.json
 *   - web/threejs/src/data/orbital-parameters.json
 *   - resource/planet_manifest.json
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
 *   Focus body indices: Sun=0 … Pluto=9 (matches OrbitLayout::Body / explorer panel).
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
        `Duplicate focus index ${body.index}: ${indices.get(body.index)} and ${body.id}`,
      );
    }
    indices.set(body.index, body.id);

    if (body.system?.initTag) {
      const allow = catalog.initTagAllowlist;
      if (Array.isArray(allow) && allow.length > 0 && !allow.includes(body.system.initTag)) {
        throw new Error(
          `Body ${body.id} initTag "${body.system.initTag}" not in initTagAllowlist ` +
            '(must match PlanetSystemLoader::MakePlanetInitFunc)',
        );
      }
    }
  }

  // Focusable primary bodies must form a contiguous 0..N-1 range (Sun=0).
  const sorted = [...indices.keys()].sort((a, b) => a - b);
  for (let i = 0; i < sorted.length; i++) {
    if (sorted[i] !== i) {
      throw new Error(
        `Focus indices must be contiguous from 0; missing index ${i} (have ${sorted.join(',')})`,
      );
    }
  }

  if (indices.get(0) !== 'sun') {
    throw new Error('Index 0 must be id "sun" (canonical OrbitLayout / explorer convention)');
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
    // Canonical focus indices: Sun=0 … last body (Pluto=9 today).
    indexConvention: 'Sun=0; matches OrbitLayout::Body and Module focus APIs',
    sceneNote: catalog.sceneNote?.en ?? '',
    bodies,
    scaleModes,
  };
}

function buildOrbitalParameters(catalog) {
  const bodies = catalog.bodies
    .filter((b) => b.includeInCompanion && b.kind !== 'star')
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
    .filter((b) => b.system && b.assets)
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
 * Generates the body-physics table OrbitLayout::kBodies[] used to hand-maintain (see
 * OrbitLayout.cpp): heliocentric compressed-art offset, AU distance, orbital period,
 * inclination, and sidereal rotation, one row per body in focus-index order (Sun=0).
 * Deliberately excludes axialTiltDegrees/earthRadiusScale — those per-body catalog fields
 * currently only feed the Three.js companion and do not match the hand-tuned art tilts each
 * planet's Render()/AdjustToParent() applies in C++ (e.g. Uranus/Neptune/Pluto differ in
 * sign, axis, or presence). Reconciling that is a separate, deliberate follow-up — see
 * docs/plans/PORTING_GUIDE.md-style notes in the planet-render-architecture writeup.
 */
function buildOrbitLayoutBodiesInc(catalog) {
  const bodies = [...catalog.bodies].sort((a, b) => a.index - b.index);
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
    '// (Sun=0), matching OrbitLayout::Body and the bodyIndex() lookup.',
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

/**
 * Generates a render-descriptor table for catalog bodies that carry a `render` block (today:
 * planets/dwarf planets with no unique shader needs). CatalogBody consumes it to build a body
 * with no hand-written C++ class — that is the path Ceres and Vesta take. The eight planets
 * plus Pluto still have their own classes and hardcode the same values; their rows exist so
 * they can migrate one at a time. The Sun, ring systems, and moons stay hand-maintained;
 * moons are not in the catalog at all yet (orbits hardcoded per-file — see Moon.cpp et al.).
 *
 * Deliberately excludes axial tilt: catalog `orbit.axialTiltDegrees` only feeds the Three.js
 * companion today and does not match the hand-tuned art rotations C++ applies for
 * Uranus/Neptune/Pluto (different sign, axis, or presence entirely) — reconciling that is a
 * separate, deliberate decision, not something this table should paper over. `earthRadiusScale`
 * has the same caveat: the existing planet classes pass their own hand-tuned coefficient to
 * PlanetInfo (Pluto 0.18651 vs catalog 0.18), so this field is authoritative only for bodies
 * actually built through CatalogBody.
 */
function buildBodyCatalogHeader(catalog) {
  const bodies = catalog.bodies
    .filter((b) => b.render)
    .sort((a, b) => a.index - b.index);

  const entries = bodies.map((body) => {
    const r = body.render;
    const flags = r.shaderFlags || {};
    const lod = r.lod || {};
    return (
      `    {${body.index}, ${cppString(body.id)}, ${cppString(body.system?.initTag ?? body.id)}, ` +
      `${cppWideString(body.displayName?.en ?? body.id)}, ` +
      `${cppWideString(body.displayName?.ru ?? body.displayName?.en ?? body.id)}, ` +
      `${cppFloat(body.orbit?.distanceAu)}, ${cppFloat(body.orbit?.orbitalPeriodDays)}, ` +
      `${cppFloat(body.orbit?.earthRadiusScale ?? 1)}, ` +
      `{${!!flags.hasNightTexture}, ${!!flags.hasSpecularMap}, ${!!flags.hasClouds}}, ` +
      `${cppFloat(r.ambientFactor)}, ` +
      `{${cppString(lod.diffuse ?? '')}, ${cppString(lod.normal ?? '')}, ` +
      `${lod.specular ? cppString(lod.specular) : 'nullptr'}}}, // ${body.id}`
    );
  });

  return [
    '#pragma once',
    '// AUTO-GENERATED by scripts/generate-planet-metadata.mjs from resource/planets.catalog.json.',
    '// Do not edit directly — run: node scripts/generate-planet-metadata.mjs',
    '//',
    '// Render descriptors for catalog bodies with no unique shader needs (see the `render`',
    '// block in planets.catalog.json). Consumed by Solar_System/CatalogBody, which builds a',
    '// renderable body straight from a row — that is how Ceres and Vesta exist without a C++',
    '// class of their own. Rows for the eight planets and Pluto are not yet used: those still',
    '// have hand-written classes that hardcode the same values, and can migrate one at a time.',
    '// The Sun, ring systems, and moons remain hand-maintained; moons are not in the catalog.',
    '//',
    '// Deliberately excludes axial tilt — see OrbitLayoutBodies.generated.inc header comment',
    '// for why catalog orbit.axialTiltDegrees cannot drive this without a reconciliation pass.',
    '// earthRadiusScale carries the same caveat: authoritative only for CatalogBody bodies,',
    '// because each hand-written planet class passes its own coefficient to PlanetInfo.',
    '',
    '#include <cstdint>',
    '',
    'namespace BodyCatalog {',
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
    '};',
    '',
    'struct Entry {',
    '    int index;',
    '    const char* id;',
    '    const char* initTag;',
    '    const wchar_t* displayNameEn;',
    '    const wchar_t* displayNameRu;',
    '    float auDistance;',
    '    float orbitalPeriodDays;',
    '    float earthRadiusScale;',
    '    ShaderFlags shaderFlags;',
    '    float ambientFactor;',
    '    TextureLodIds lod;',
    '};',
    '',
    'inline constexpr Entry kEntries[] = {',
    ...entries,
    '};',
    '',
    '/** Row for a focus index, or nullptr when that body has no render descriptor. */',
    'inline constexpr const Entry* FindByIndex(int index) {',
    '    for (const Entry& entry : kEntries) {',
    '        if (entry.index == index) {',
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
