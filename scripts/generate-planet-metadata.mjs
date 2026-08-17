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
    { path: OUT.facts, data: buildPlanetFacts(catalog) },
    { path: OUT.orbital, data: buildOrbitalParameters(catalog) },
    { path: OUT.manifest, data: buildPlanetManifest(catalog) },
  ];

  let dirty = false;
  for (const { path, data } of outputs) {
    const { changed } = writeIfChanged(path, stableStringify(data), { checkOnly });
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
