#!/usr/bin/env node
/**
 * Generate planet metadata artifacts from the star-system catalogs in resource/systems/.
 *
 * resource/systems/index.json lists the systems (Sol plus the exoplanet systems that
 * scripts/generate-exoplanet-systems.mjs writes). Sol's catalog, sol.catalog.json, is the
 * one the engine is compiled with:
 *   - web/public/planet_facts.json
 *   - web/threejs/src/data/orbital-parameters.json
 *   - resource/planet_manifest.json
 *   - src/Solar_System/OrbitLayoutBodies.generated.inc
 *   - src/Solar_System/BodyCatalog.generated.h
 *   - web/src/generated/planetIds.ts
 * and every system, Sol included, gets
 *   - resource/systems/generated/<id>.runtime.json  (what StarSystemCatalog loads at run time;
 *     Sol's is the test that the loader reproduces the compiled table)
 *   - web/public/systems/<id>/planet_facts.json     (the explorer panel; Sol keeps the old path too)
 *   - web/public/systems/<id>/thumbnail.svg         (the system picker)
 *   - web/public/systems/index.json
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
 *   Focus body indices: Sun=0 … Vesta=11 are frozen ABI; outer-system bodies follow
 *   (Eris=12, Haumea=13, Makemake=14, 1P/Halley=15). Matches OrbitLayout::Body / explorer panel.
 *   Satellites come after the last focus index and are catalog-built; they are not focusable.
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
const systemsDir = join(root, 'resource/systems');
const systemsIndexPath = join(systemsDir, 'index.json');
const schemaPath = join(systemsDir, 'catalog.schema.json');
const SOL_ID = 'sol';
const SOL_CATALOG_REL = 'resource/systems/sol.catalog.json';

const OUT = {
  facts: join(root, 'web/public/planet_facts.json'),
  orbital: join(root, 'web/threejs/src/data/orbital-parameters.json'),
  manifest: join(root, 'resource/planet_manifest.json'),
  assetManifest: join(root, 'resource/asset-manifest.json'),
  orbitLayoutBodies: join(root, 'src/Solar_System/OrbitLayoutBodies.generated.inc'),
  bodyCatalog: join(root, 'src/Solar_System/BodyCatalog.generated.h'),
  planetIds: join(root, 'web/src/generated/planetIds.ts'),
  runtime: (id) => join(root, `resource/systems/generated/${id}.runtime.json`),
  systemFacts: (id) => join(root, `web/public/systems/${id}/planet_facts.json`),
  thumbnail: (id) => join(root, `web/public/systems/${id}/thumbnail.svg`),
  webIndex: join(root, 'web/public/systems/index.json'),
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

/**
 * IAU prime-meridian elements of a body: W at J2000 and its rate in degrees per day
 * (WGCCRE, Archinal et al. 2018). Without a catalog entry the rate follows the sidereal day.
 */
function primeMeridian(body) {
  const o = body.orbit || {};
  const rate = o.primeMeridianRateDegPerDay ?? (o.siderealRotationDays ? 360 / o.siderealRotationDays : 0);
  return { w0Deg: o.primeMeridianDegJ2000 ?? 0, rateDegPerDay: rate };
}

const FOCUS_KINDS = new Set(['star', 'planet', 'dwarf_planet', 'comet']);

function isFocusBody(body) {
  return FOCUS_KINDS.has(body.kind);
}

function isRgb(value) {
  return Array.isArray(value) && value.length === 3 && value.every((v) => Number.isFinite(v) && v >= 0);
}

function isFinitePositive(value) {
  return Number.isFinite(value) && value > 0;
}

/**
 * render.atmosphere = { oneil: {...}, physical?: {...} }. `oneil` drives the cheap shell
 * every preset can draw; `physical` is baked into transmittance/multi-scattering LUTs by
 * tools/atmosphere_lut_baker and drawn on the Medium/Full presets. See ARCHITECTURE §9.2.
 */
function validateAtmosphere(body) {
  const where = `Body ${body.id} render.atmosphere`;
  const a = body.render.atmosphere;
  const o = a.oneil;
  if (!o) {
    throw new Error(`${where} requires an oneil block (the Low-preset shell)`);
  }
  for (const key of ['shellScale', 'outerRadius', 'hScaleFactor']) {
    if (!isFinitePositive(o[key])) throw new Error(`${where}.oneil.${key} must be a positive number`);
  }
  for (const key of ['color', 'mieTint']) {
    if (!isRgb(o[key])) throw new Error(`${where}.oneil.${key} must be [r, g, b]`);
  }
  for (const key of ['innerRadiusMinusEpsilon', 'toneMapping']) {
    if (typeof o[key] !== 'boolean') throw new Error(`${where}.oneil.${key} must be a boolean`);
  }

  const p = a.physical;
  if (!p) return;
  for (const key of ['groundRadiusKm', 'topRadiusKm', 'thicknessScale', 'exposure']) {
    if (!isFinitePositive(p[key])) throw new Error(`${where}.physical.${key} must be a positive number`);
  }
  if (!(p.topRadiusKm > p.groundRadiusKm)) {
    throw new Error(`${where}.physical.topRadiusKm must exceed groundRadiusKm`);
  }
  const radiusKm = (body.facts?.diameterKm ?? 0) / 2;
  if (radiusKm > 0 && Math.abs(p.groundRadiusKm - radiusKm) > 0.05 * radiusKm) {
    throw new Error(
      `${where}.physical.groundRadiusKm=${p.groundRadiusKm} is not within 5% of the body radius ${radiusKm}`,
    );
  }
  if (!isRgb(p.rayleigh?.scatteringPerKm) || !isFinitePositive(p.rayleigh?.scaleHeightKm)) {
    throw new Error(`${where}.physical.rayleigh needs scatteringPerKm [r, g, b] and scaleHeightKm > 0`);
  }
  const m = p.mie;
  if (!isRgb(m?.scatteringPerKm) || !isRgb(m?.extinctionPerKm) || !isFinitePositive(m?.scaleHeightKm)) {
    throw new Error(`${where}.physical.mie needs scatteringPerKm, extinctionPerKm and scaleHeightKm`);
  }
  if (m.scatteringPerKm.some((s, i) => s > m.extinctionPerKm[i])) {
    throw new Error(`${where}.physical.mie scattering cannot exceed extinction (albedo > 1)`);
  }
  if (!Number.isFinite(m.phaseG) || Math.abs(m.phaseG) >= 1) {
    throw new Error(`${where}.physical.mie.phaseG must be in (-1, 1)`);
  }
  const ab = p.absorption;
  if (!isRgb(ab?.extinctionPerKm) || !Number.isFinite(ab?.layerCenterKm) || !isFinitePositive(ab?.layerHalfWidthKm)) {
    throw new Error(`${where}.physical.absorption needs extinctionPerKm, layerCenterKm and layerHalfWidthKm > 0`);
  }
  if (!isRgb(p.groundAlbedo) || p.groundAlbedo.some((v) => v > 1)) {
    throw new Error(`${where}.physical.groundAlbedo must be [r, g, b] in [0, 1]`);
  }
}

/**
 * render.star on the index-0 body: what StarPhysics derives the light, corona and flare tint,
 * irradiance and angular size from. glowTemperatureK is the glow spectrum's art calibration
 * (5778 K, what the Sun was tuned at), the default for every star: its colour is the tint.
 */
function validateStar(body) {
  const st = body.render?.star;
  const where = `Star ${body.id} render.star`;
  if (!st) throw new Error(`${where} is required (effectiveTemperatureK, radiusSolar, luminositySolar)`);
  for (const key of ['effectiveTemperatureK', 'radiusSolar', 'luminositySolar']) {
    if (!isFinitePositive(st[key])) throw new Error(`${where}.${key} must be a positive number`);
  }
  if (st.effectiveTemperatureK < 1000 || st.effectiveTemperatureK > 50000) {
    throw new Error(`${where}.effectiveTemperatureK must be within 1000–50000 K`);
  }
  const activity = st.activity;
  if (activity !== undefined && typeof activity !== 'object') {
    if (!(activity >= 0 && activity <= 1)) throw new Error(`${where}.activity must be in [0, 1]`);
  } else if (activity !== undefined) {
    if (activity.flare !== undefined && !(activity.flare >= 0 && activity.flare <= 1)) {
      throw new Error(`${where}.activity.flare must be in [0, 1]`);
    }
    const c = activity.cycle;
    if (c !== undefined) {
      if (!isFinitePositive(c.periodYears)) throw new Error(`${where}.activity.cycle.periodYears must be positive`);
      if (!Number.isFinite(c.minimumYear)) throw new Error(`${where}.activity.cycle.minimumYear must be a year`);
      if (!isFinitePositive(c.meanPeakSunspotNumber)) {
        throw new Error(`${where}.activity.cycle.meanPeakSunspotNumber must be positive`);
      }
      if (c.cycleAtMinimum !== undefined && !(Number.isInteger(c.cycleAtMinimum) && c.cycleAtMinimum > 0)) {
        throw new Error(`${where}.activity.cycle.cycleAtMinimum must be a positive integer`);
      }
    }
  }
}

/**
 * render.aurora: the auroral shell (AuroraShell, aurora.fs) on a body with a dipole field.
 * The oval itself (where, how wide, how far it moves with Kp) is physics and lives in
 * src/astro/SpaceWeather.cpp; this is the look. Colours are linear RGB.
 */
function validateAurora(body) {
  const a = body.render.aurora;
  const where = `Body ${body.id} render.aurora`;
  if (!Array.isArray(a.altitudeKm) || a.altitudeKm.length !== 2 || !a.altitudeKm.every(isFinitePositive) ||
      a.altitudeKm[0] >= a.altitudeKm[1]) {
    throw new Error(`${where}.altitudeKm must be [bottom, top] km, bottom < top`);
  }
  for (const key of ['colorLow', 'colorHigh', 'colorEdge']) {
    if (!isRgb(a[key])) throw new Error(`${where}.${key} must be [r, g, b] (linear)`);
  }
  if (!isFinitePositive(a.brightness)) throw new Error(`${where}.brightness must be positive`);
  if (body.kind === 'satellite') throw new Error(`${where}: only primaries carry an aurora`);
}

const PROCEDURAL_MODES = new Set([0, 1, 2, 3]);

/** render.procedural: the PROCEDURAL_SURFACE permutation's material (CatalogMaterial). */
function validateProcedural(body) {
  const pr = body.render.procedural;
  const where = `Body ${body.id} render.procedural`;
  if (!PROCEDURAL_MODES.has(pr.mode)) throw new Error(`${where}.mode must be 0 (rocky), 1 (banded), 2 (lava) or 3 (ice)`);
  if (!Array.isArray(pr.colors) || pr.colors.length !== 3 || !pr.colors.every(isRgb)) {
    throw new Error(`${where}.colors must be three [r, g, b] (linear)`);
  }
  if (!Number.isFinite(pr.seed)) throw new Error(`${where}.seed must be a number`);
  const flags = body.render.shaderFlags || {};
  if (flags.hasNightTexture || flags.hasSpecularMap || flags.hasClouds) {
    throw new Error(`${where}: a procedural surface has no maps, so no shaderFlags`);
  }
}

function validateCatalog(catalog, systemId) {
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
      const keplerianFields = [
        'aKm',
        'e',
        'iDeg',
        'OmegaDeg',
        'omegaDeg',
        'M0Deg',
        'nDegPerDay',
        // Secular rates; 0 for a row with no measured precession.
        'OmegaDotDegPerDay',
        'omegaDotDegPerDay',
      ];
      if (!k || keplerianFields.some((field) => typeof k[field] !== 'number')) {
        throw new Error(
          `Satellite ${body.id} requires orbit.keplerian {${keplerianFields.join(', ')}}`,
        );
      }
      if (typeof body.orbit?.sceneOrbitRadius !== 'number' || body.orbit.sceneOrbitRadius <= 0) {
        throw new Error(`Satellite ${body.id} requires orbit.sceneOrbitRadius > 0`);
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

  // render.shaderFlags is the single source CatalogMaterial reads; the lod ids decide which
  // textures MakePlanetInfo loads. They must agree, or a flag samples an unbound unit.
  const flagToLod = [
    ['hasSpecularMap', 'specular'],
    ['hasNightTexture', 'night'],
    ['hasClouds', 'clouds'],
  ];
  for (const body of catalog.bodies) {
    const r = body.render;
    if (!r) continue;
    const flags = r.shaderFlags || {};
    const lod = r.lod || {};
    for (const [flag, lodKey] of flagToLod) {
      if (Boolean(flags[flag]) !== (lod[lodKey] != null)) {
        throw new Error(
          `Body ${body.id} render.shaderFlags.${flag}=${Boolean(flags[flag])} ` +
            `disagrees with render.lod.${lodKey}=${JSON.stringify(lod[lodKey] ?? null)}`,
        );
      }
    }
    if (body.kind === 'satellite' && (flags.hasNightTexture || flags.hasClouds)) {
      // SatelliteInfo carries only diffuse/normal/specular.
      throw new Error(`Satellite ${body.id} cannot use night or cloud maps`);
    }
  }

  for (const body of catalog.bodies) {
    if (body.render?.atmosphere) {
      validateAtmosphere(body);
    }
    if (body.render?.aurora) {
      validateAurora(body);
    }
    // Spin is a function of the date: W = W0 + Wdot * (days since J2000) (OrbitLayout::
    // GetAxialSpinDegrees), so a focus body's rate must agree with its sidereal day. Bodies
    // with no IAU frame (Eris, Haumea, Makemake, Halley) omit both and spin from W0 = 0 at
    // 360 / siderealRotationDays.
    if (isFocusBody(body)) {
      const spin = primeMeridian(body);
      if (!Number.isFinite(spin.w0Deg) || spin.w0Deg < 0 || spin.w0Deg >= 360) {
        throw new Error(`Body ${body.id} orbit.primeMeridianDegJ2000 must be in [0, 360)`);
      }
      const sidereal = body.orbit?.siderealRotationDays;
      if (Number.isFinite(sidereal) && sidereal !== 0 && body.orbit?.primeMeridianRateDegPerDay !== undefined) {
        const impliedDays = 360 / spin.rateDegPerDay;
        // Within 1.5%: the fact-sheet day and the IAU rate differ by that much for Neptune.
        if (Math.sign(impliedDays) !== Math.sign(sidereal) || Math.abs(impliedDays / sidereal - 1) > 0.015) {
          throw new Error(
            `Body ${body.id} primeMeridianRateDegPerDay (${spin.rateDegPerDay}) disagrees with siderealRotationDays (${sidereal})`,
          );
        }
      }
    }
    // render.oblateness flattens the surface, cloud and atmosphere meshes along the pole
    // (CatalogBody / CatalogClouds / Atmosphere via BodyShape.h); satellites stay spheres.
    const oblateness = body.render?.oblateness;
    if (oblateness !== undefined) {
      if (!Number.isFinite(oblateness) || oblateness < 0 || oblateness >= 0.5) {
        throw new Error(`Body ${body.id} render.oblateness must be a flattening in [0, 0.5)`);
      }
      if (body.kind === 'satellite') {
        throw new Error(`Satellite ${body.id} cannot set render.oblateness`);
      }
    }
    // render.terrain: height tiles close up (src/render/terrain/). They tile the sphere mesh,
    // so a body drawn with its own mesh (Phobos) cannot have them.
    const terrain = body.render?.terrain;
    if (terrain !== undefined) {
      if (typeof terrain !== 'object' || terrain === null || typeof terrain.attribution !== 'string' ||
          terrain.attribution.trim() === '') {
        throw new Error(`Body ${body.id} render.terrain needs an attribution string`);
      }
      if (body.render.mesh) {
        throw new Error(`Body ${body.id} has its own mesh and cannot set render.terrain`);
      }
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

  // Satellites sit after every focus index so adding a focus body never aliases a moon id.
  const lastFocus = focusIndices[focusIndices.length - 1];
  for (const body of catalog.bodies) {
    if (body.kind === 'satellite' && body.index <= lastFocus) {
      throw new Error(`Satellite ${body.id} index ${body.index} must be above the last focus index ${lastFocus}`);
    }
  }

  const star = catalog.bodies.find((b) => b.index === 0);
  if (!star || star.kind !== 'star') {
    throw new Error('Index 0 must be the system\'s star (kind "star")');
  }
  validateStar(star);
  if (catalog.bodies.some((b) => b.kind === 'star' && b.index !== 0)) {
    throw new Error('One star per system: only index 0 may be kind "star"');
  }

  if (systemId === SOL_ID) {
    // Sol's indices are frozen ABI: deep links, the FocusPlanet export and OrbitLayout::Body.
    if (indices.get(0) !== 'sun') {
      throw new Error('Index 0 must be id "sun" (canonical OrbitLayout / explorer convention)');
    }
    const vesta = catalog.bodies.find((b) => b.id === 'vesta');
    if (!vesta || vesta.index !== 11) {
      throw new Error('Focus index 11 must remain id "vesta" (Sun=0 … Vesta=11 frozen)');
    }
  } else {
    if (catalog.system?.id !== systemId) {
      throw new Error(`catalog.system.id "${catalog.system?.id}" must be the index id "${systemId}"`);
    }
    for (const body of catalog.bodies) {
      if (body.kind === 'satellite' || !body.render || body.kind === 'star') continue;
      // No exoplanet has a texture: their surfaces are render.procedural.
      if (!body.render.procedural && !body.render.lod?.diffuse) {
        throw new Error(`Body ${body.id} needs render.procedural or render.lod textures`);
      }
    }
  }
  for (const body of catalog.bodies) {
    if (body.render?.procedural) validateProcedural(body);
  }

  // Lightweight schema presence check (full AJV optional).
  if (existsSync(schemaPath)) {
    const schema = readJson(schemaPath);
    if (!schema.required?.includes('bodies')) {
      throw new Error('resource/systems/catalog.schema.json looks invalid (missing bodies requirement)');
    }
  }
}

function buildPlanetFacts(catalog, generatedFrom) {
  const sol = catalog.system?.id === undefined || catalog.system.id === SOL_ID;
  const bodies = [...catalog.bodies]
    .filter(isFocusBody)
    .sort((a, b) => a.index - b.index)
    .map((body) => {
      const f = body.facts || {};
      const o = body.orbit || {};
      const row = {
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
      // Exoplanet systems: what the archive measured, and which numbers are estimates.
      for (const key of [
        'equilibriumTemperatureK',
        'irradianceEarth',
        'discoveryYear',
        'discoveryMethod',
        'spectralType',
        'effectiveTemperatureK',
        'luminositySolar',
        'distancePc',
      ]) {
        if (f[key] !== undefined && f[key] !== null) row[key] = f[key];
      }
      if (body.fit && Object.keys(body.fit).length > 0) row.fit = body.fit;
      if (body.render?.procedural) row.surface = body.render.procedural.style;
      return row;
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

  const facts = {
    version: 2,
    generatedFrom,
    // Canonical focus indices: Sun=0 … Vesta=11 (frozen), then Eris=12 … Halley=15. Satellites are omitted from explorer facts.
    indexConvention: sol
      ? 'Sun=0 … Vesta=11 (frozen), Eris=12 … Halley=15; matches OrbitLayout::Body and Module focus APIs'
      : 'Star=0, planets 1… by semi-major axis; matches the active system\'s OrbitLayout table',
    sceneNote: catalog.sceneNote?.en ?? '',
    bodies,
    scaleModes,
  };
  if (!sol) {
    facts.system = {
      id: catalog.system.id,
      name: catalog.system.name?.en,
      nameRu: catalog.system.name?.ru,
      distancePc: catalog.system.distancePc,
      habitableZoneAu: catalog.system.habitableZoneAu,
      source: catalog.system.source,
      sceneNoteRu: catalog.sceneNote?.ru,
    };
  }
  return facts;
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
      `Generated from ${SOL_CATALOG_REL} — companion scale 1 unit = 1 C++ world unit`,
    generatedFrom: `${SOL_CATALOG_REL}`,
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
    generatedFrom: `${SOL_CATALOG_REL}`,
    // proxyPosition is heliocentric art offset (same as OrbitLayout compressedOffset).
    systems,
  };
}

/** Format a JSON number as an unambiguous C++ float literal. */
function cppFloat(n) {
  const value = Number(n ?? 0);
  if (!Number.isFinite(value)) {
    throw new Error(`cppFloat expected a finite number, got ${n}`);
  }
  const text = String(value);
  if (/[eE]/.test(text)) {
    return `${text}f`;
  }
  return `${text.includes('.') ? text : `${text}.0`}f`;
}

/** A C++ double literal: no suffix, enough digits to round-trip. */
function cppDouble(n) {
  const value = Number(n);
  if (!Number.isFinite(value)) {
    throw new Error(`Cannot emit non-finite double: ${n}`);
  }
  const text = String(value);
  return /[.eE]/.test(text) ? text : `${text}.0`;
}

/** facts.diameterKm / 2: the radius a body is drawn at on realistic (true) scale. */
function trueRadiusKm(body) {
  const radiusKm = (body.facts?.diameterKm ?? 0) / 2;
  if (!(radiusKm > 0)) {
    throw new Error(`${body.id} needs facts.diameterKm > 0: realistic scale draws every body at its true radius`);
  }
  return radiusKm;
}

/**
 * Generates the body-physics table OrbitLayout::kBodies[] (see OrbitLayout.cpp):
 * heliocentric compressed-art offset, AU distance, orbital period, inclination, and
 * sidereal rotation, and the IAU prime-meridian W0 / Wdot (doubles: Wdot times a century of
 * days is millions of degrees), one row per *focus* body in index order (Sun=0 … Halley=15).
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
    const spin = primeMeridian(body);
    return (
      `    {glm::vec3(${cppFloat(ox)}, ${cppFloat(oy)}, ${cppFloat(oz)}), ` +
      `${cppFloat(o.distanceAu)}, ${cppFloat(o.orbitalPeriodDays)}, ` +
      `${cppFloat(o.inclinationDeg)}, ${cppFloat(o.siderealRotationDays)}, ` +
      `${cppDouble(spin.w0Deg)}, ${cppDouble(spin.rateDegPerDay)}}, // ${body.id}`
    );
  });
  return [
    `// AUTO-GENERATED by scripts/generate-planet-metadata.mjs from ${SOL_CATALOG_REL}.`,
    '// Do not edit directly — run: node scripts/generate-planet-metadata.mjs',
    "// Included into OrbitLayout.cpp's kBodies[] initializer. Row order is focus index order",
    '// (Sun=0 … Halley=15). Satellites (index 16+) are not rows here.',
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
    case 'comet':
      return 'Kind::Comet';
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
/**
 * web/src/generated/planetIds.ts: the focus-index table the web UI shares with C++
 * (OrbitLayout::Body, FocusPlanet, ?planet= deep links). Derived from the same catalog rows
 * as OrbitLayoutBodies.generated.inc, so a new focus body never needs a parallel TS edit.
 */
function buildPlanetIdsTs(catalog) {
  const focus = [...catalog.bodies].filter(isFocusBody).sort((a, b) => a.index - b.index);
  const last = focus[focus.length - 1].index;
  return [
    `// AUTO-GENERATED by scripts/generate-planet-metadata.mjs from ${SOL_CATALOG_REL}`,
    '// — do not edit. Focus indices match OrbitLayout::Body and the FocusPlanet export.',
    '',
    '/** Catalog id → focus index. Sun=0 … Vesta=11 are frozen; satellites (16+) are not focusable. */',
    'export const PLANET_IDS = {',
    ...focus.map((b) => `    ${/^[A-Za-z_$][\w$]*$/.test(b.id) ? b.id : JSON.stringify(b.id)}: ${b.index},`),
    '} as const;',
    '',
    'export type PlanetId = keyof typeof PLANET_IDS;',
    '',
    "/** A focusable body's index (Sun … 1P/Halley). */",
    'export type PlanetIndex = (typeof PLANET_IDS)[PlanetId];',
    '',
    '/** Highest focus index (OrbitLayout::kBodyCount - 1). */',
    `export const MAX_PLANET_INDEX = ${last};`,
    '',
    'export const PLANET_ID_BY_INDEX: Readonly<Record<PlanetIndex, PlanetId>> = {',
    ...focus.map((b) => `    ${b.index}: '${b.id}',`),
    '};',
    '',
  ].join('\n');
}

/**
 * One catalog row as BodyCatalog::Entry's fields, by their C++ names. The header prints it as
 * an aggregate initializer (cppEntry) and the runtime JSON writes it as is, so the compiled
 * Sol table and the files StarSystemCatalog loads cannot disagree on a field.
 */
function entryRow(body, byId) {
  const r = body.render;
  const flags = r.shaderFlags || {};
  const lod = r.lod || {};
  const o = body.orbit || {};
  const k = o.keplerian || {};
  const cloud = r.cloudLayer || {};
  const parent = body.parent ? byId.get(body.parent) : null;
  const pr = r.procedural;
  const num = (v) => Number(v ?? 0);
  return {
    index: body.index,
    id: body.id,
    // A planet of an exoplanet system orbits the star; the C++ side keys heliocentric bodies
    // by parentId == nullptr / the star, exactly as Sol's "sun".
    parentId: body.parent ?? null,
    initTag: body.system?.initTag ?? parent?.system?.initTag ?? body.id,
    kind: body.kind,
    displayNameEn: body.displayName?.en ?? body.id,
    displayNameRu: body.displayName?.ru ?? body.displayName?.en ?? body.id,
    auDistance: num(o.distanceAu),
    orbitalPeriodDays: num(o.orbitalPeriodDays),
    earthRadiusScale: Number(o.earthRadiusScale ?? 1),
    radiusKm: trueRadiusKm(body),
    axialTiltDegrees: num(o.axialTiltDegrees),
    artTiltXDegrees: num(r.artTiltXDegrees),
    oblateness: num(r.oblateness),
    yawOffsetDegrees: num(o.yawOffsetDegrees),
    artRollDegrees: num(o.artRollDegrees),
    shaderFlags: {
      hasNightTexture: !!flags.hasNightTexture,
      hasSpecularMap: !!flags.hasSpecularMap,
      hasClouds: !!flags.hasClouds,
    },
    useSphereIntersect: !!(r.useSphereIntersect ?? body.kind === 'satellite'),
    ambientFactor: num(r.ambientFactor),
    lod: {
      diffuse: lod.diffuse ?? '',
      normal: lod.normal ?? '',
      specular: lod.specular ?? null,
      night: lod.night ?? null,
      clouds: lod.clouds ?? null,
    },
    meshPath: r.mesh ?? null,
    sceneOrbitRadius: num(o.sceneOrbitRadius),
    orbitPlane: o.orbitPlane === 'xy' ? 'xy' : 'xz',
    initialAnomalyRad: num(o.initialAnomalyRad),
    spinDegPerSimSecond: num(o.spinDegPerSimSecond),
    keplerian: {
      aKm: num(k.aKm),
      e: num(k.e),
      iDeg: num(k.iDeg),
      OmegaDeg: num(k.OmegaDeg),
      omegaDeg: num(k.omegaDeg),
      M0Deg: num(k.M0Deg),
      nDegPerDay: num(k.nDegPerDay),
      OmegaDotDegPerDay: num(k.OmegaDotDegPerDay),
      omegaDotDegPerDay: num(k.omegaDotDegPerDay),
    },
    cloudLayer: {
      diffuse: cloud.diffuse ?? null,
      normal: cloud.normal ?? null,
      scaleFactor: num(cloud.scaleFactor),
      spinDegPerSimSecond: num(cloud.spinDegPerSimSecond),
      ambientFactor: num(cloud.ambientFactor),
    },
    hasCloudLayer: !!body.system?.hasCloudLayer,
    hasRings: !!body.system?.hasRings,
    hasTerrain: !!r.terrain,
    terrainAttribution: r.terrain?.attribution ?? null,
    procedural: {
      enabled: !!pr,
      mode: pr ? pr.mode : 0,
      seed: pr ? num(pr.seed) : 0,
      colors: pr ? pr.colors : [[0, 0, 0], [0, 0, 0], [0, 0, 0]],
      bands: pr ? num(pr.bands) : 0,
      seaLevel: pr ? num(pr.seaLevel) : 0,
      polarCap: pr ? num(pr.polarCap) : 0,
      emissive: pr ? num(pr.emissive) : 0,
      equilibriumTemperatureK: pr ? num(pr.equilibriumTemperatureK) : 0,
    },
    placeFromElements: !!o.placeFromElements,
  };
}

function cppRgb(v) {
  return `{${cppFloat(v[0])}, ${cppFloat(v[1])}, ${cppFloat(v[2])}}`;
}

function cppEntry(e) {
  const k = e.keplerian;
  const c = e.cloudLayer;
  const p = e.procedural;
  return (
    `    {${e.index}, ${cppString(e.id)}, ${cppNullableString(e.parentId)}, ` +
    `${cppString(e.initTag)}, ${cppKind(e.kind)}, ` +
    `${cppWideString(e.displayNameEn)}, ${cppWideString(e.displayNameRu)}, ` +
    `${cppFloat(e.auDistance)}, ${cppFloat(e.orbitalPeriodDays)}, ` +
    `${cppFloat(e.earthRadiusScale)}, ${cppFloat(e.radiusKm)}, ${cppFloat(e.axialTiltDegrees)}, ` +
    `${cppFloat(e.artTiltXDegrees)}, ${cppFloat(e.oblateness)}, ${cppFloat(e.yawOffsetDegrees)}, ` +
    `${cppFloat(e.artRollDegrees)}, ` +
    `{${e.shaderFlags.hasNightTexture}, ${e.shaderFlags.hasSpecularMap}, ${e.shaderFlags.hasClouds}}, ` +
    `${e.useSphereIntersect}, ${cppFloat(e.ambientFactor)}, ` +
    `{${cppString(e.lod.diffuse)}, ${cppString(e.lod.normal)}, ` +
    `${cppNullableString(e.lod.specular)}, ${cppNullableString(e.lod.night)}, ` +
    `${cppNullableString(e.lod.clouds)}}, ${cppNullableString(e.meshPath)}, ` +
    `${cppFloat(e.sceneOrbitRadius)}, ${cppOrbitPlane(e.orbitPlane)}, ` +
    `${cppFloat(e.initialAnomalyRad)}, ${cppFloat(e.spinDegPerSimSecond)}, ` +
    `{${cppFloat(k.aKm)}, ${cppFloat(k.e)}, ${cppFloat(k.iDeg)}, ${cppFloat(k.OmegaDeg)}, ` +
    `${cppFloat(k.omegaDeg)}, ${cppFloat(k.M0Deg)}, ${cppFloat(k.nDegPerDay)}, ` +
    `${cppFloat(k.OmegaDotDegPerDay)}, ${cppFloat(k.omegaDotDegPerDay)}}, ` +
    `{${cppNullableString(c.diffuse)}, ${cppNullableString(c.normal)}, ` +
    `${cppFloat(c.scaleFactor)}, ${cppFloat(c.spinDegPerSimSecond)}, ` +
    `${cppFloat(c.ambientFactor)}}, ` +
    `${e.hasCloudLayer}, ${e.hasRings}, ` +
    `${e.hasTerrain}, ${cppNullableString(e.terrainAttribution)}, ` +
    `{${p.enabled}, ${p.mode}, ${cppFloat(p.seed)}, {${p.colors.map(cppRgb).join(', ')}}, ` +
    `${cppFloat(p.bands)}, ${cppFloat(p.seaLevel)}, ${cppFloat(p.polarCap)}, ${cppFloat(p.emissive)}, ` +
    `${cppFloat(p.equilibriumTemperatureK)}}, ` +
    `${e.placeFromElements}}, // ${e.id}`
  );
}

/** render.atmosphere as BodyCatalog::AtmosphereRow's fields (same split as entryRow). */
function atmosphereRow(body) {
  const o = body.render.atmosphere.oneil;
  const p = body.render.atmosphere.physical;
  return {
    bodyId: body.id,
    oneil: {
      shellScale: o.shellScale,
      color: o.color,
      innerRadiusMinusEpsilon: !!o.innerRadiusMinusEpsilon,
      outerRadius: o.outerRadius,
      mieTint: o.mieTint,
      hScaleFactor: o.hScaleFactor,
      toneMapping: !!o.toneMapping,
    },
    physical: p
      ? {
          enabled: true,
          groundRadiusKm: p.groundRadiusKm,
          topRadiusKm: p.topRadiusKm,
          thicknessScale: p.thicknessScale,
          rayleighScattering: p.rayleigh.scatteringPerKm,
          rayleighScaleHeightKm: p.rayleigh.scaleHeightKm,
          mieScattering: p.mie.scatteringPerKm,
          mieExtinction: p.mie.extinctionPerKm,
          mieScaleHeightKm: p.mie.scaleHeightKm,
          miePhaseG: p.mie.phaseG,
          absorptionExtinction: p.absorption.extinctionPerKm,
          absorptionCenterKm: p.absorption.layerCenterKm,
          absorptionHalfWidthKm: p.absorption.layerHalfWidthKm,
          groundAlbedo: p.groundAlbedo,
          exposure: p.exposure,
        }
      : null,
  };
}

function cppAtmosphere(a) {
  const o = a.oneil;
  const p = a.physical;
  const oneil =
    `{${cppFloat(o.shellScale)}, ${cppRgb(o.color)}, ${o.innerRadiusMinusEpsilon}, ` +
    `${cppFloat(o.outerRadius)}, ${cppRgb(o.mieTint)}, ${cppFloat(o.hScaleFactor)}, ${o.toneMapping}}`;
  const physical = p
    ? `{true, ${cppFloat(p.groundRadiusKm)}, ${cppFloat(p.topRadiusKm)}, ${cppFloat(p.thicknessScale)}, ` +
      `${cppRgb(p.rayleighScattering)}, ${cppFloat(p.rayleighScaleHeightKm)}, ` +
      `${cppRgb(p.mieScattering)}, ${cppRgb(p.mieExtinction)}, ` +
      `${cppFloat(p.mieScaleHeightKm)}, ${cppFloat(p.miePhaseG)}, ` +
      `${cppRgb(p.absorptionExtinction)}, ${cppFloat(p.absorptionCenterKm)}, ` +
      `${cppFloat(p.absorptionHalfWidthKm)}, ${cppRgb(p.groundAlbedo)}, ${cppFloat(p.exposure)}}`
    : '{}';
  return `    {${cppString(a.bodyId)}, ${oneil}, ${physical}},`;
}

/** The index-0 body's render.star as BodyCatalog::StarRow. */
function starRow(catalog) {
  const body = catalog.bodies.find((b) => b.index === 0);
  const st = body.render.star;
  return {
    id: body.id,
    displayNameEn: body.displayName?.en ?? body.id,
    displayNameRu: body.displayName?.ru ?? body.displayName?.en ?? body.id,
    spectralType: st.spectralType ?? '',
    effectiveTemperatureK: st.effectiveTemperatureK,
    // Every star's glow ramp stays at the Sun's art calibration: colour comes from its Krystek
    // tint (StarPhysics::RelativeTint), so the spectrum ramp must not redden it a second time.
    glowTemperatureK: st.glowTemperatureK ?? 5778,
    radiusKm: st.radiusSolar * 695700,
    radiusSolar: st.radiusSolar,
    luminositySolar: st.luminositySolar,
    massSolar: st.massSolar ?? 0,
    // render.star.activity is a flare level, or {flare, cycle} for a star with a sunspot cycle.
    activity: (typeof st.activity === 'object' ? st.activity.flare : st.activity) ?? 0,
    cyclePeriodYears: st.activity?.cycle?.periodYears ?? 0,
    cycleMinimumYear: st.activity?.cycle?.minimumYear ?? 0,
    cycleAtMinimum: st.activity?.cycle?.cycleAtMinimum ?? 0,
    cycleMeanPeakSunspots: st.activity?.cycle?.meanPeakSunspotNumber ?? 0,
  };
}

/** render.aurora as BodyCatalog::AuroraRow's fields. */
function auroraRow(body) {
  const a = body.render.aurora;
  return {
    bodyId: body.id,
    bottomKm: a.altitudeKm[0],
    topKm: a.altitudeKm[1],
    colorLow: a.colorLow,
    colorHigh: a.colorHigh,
    colorEdge: a.colorEdge,
    brightness: a.brightness,
  };
}

function cppAurora(a) {
  return (
    `    {${cppString(a.bodyId)}, ${cppFloat(a.bottomKm)}, ${cppFloat(a.topKm)}, ${cppRgb(a.colorLow)}, ` +
    `${cppRgb(a.colorHigh)}, ${cppRgb(a.colorEdge)}, ${cppFloat(a.brightness)}},`
  );
}

function cppStar(s) {
  return (
    `{${cppString(s.id)}, ${cppWideString(s.displayNameEn)}, ${cppWideString(s.displayNameRu)}, ` +
    `${cppString(s.spectralType)}, ${cppFloat(s.effectiveTemperatureK)}, ${cppFloat(s.glowTemperatureK)}, ` +
    `${cppDouble(s.radiusKm)}, ${cppFloat(s.radiusSolar)}, ${cppFloat(s.luminositySolar)}, ` +
    `${cppFloat(s.massSolar)}, ${cppFloat(s.activity)}, ${cppFloat(s.cyclePeriodYears)}, ` +
    `${cppFloat(s.cycleMinimumYear)}, ${s.cycleAtMinimum}, ${cppFloat(s.cycleMeanPeakSunspots)}}`
  );
}

/** The bodies BodyCatalog::Entry has rows for: everything with a render block but the star. */
function entryBodies(catalog) {
  return catalog.bodies.filter((b) => b.render && b.kind !== 'star').sort((a, b) => a.index - b.index);
}

function buildBodyCatalogHeader(catalog) {
  const byId = new Map(catalog.bodies.map((b) => [b.id, b]));
  const bodies = entryBodies(catalog);
  const entries = bodies.map((body) => cppEntry(entryRow(body, byId)));
  const atmospheres = bodies.filter((body) => body.render.atmosphere).map((body) => cppAtmosphere(atmosphereRow(body)));
  const auroras = bodies.filter((body) => body.render.aurora).map((body) => cppAurora(auroraRow(body)));

  return [
    '#pragma once',
    `// AUTO-GENERATED by scripts/generate-planet-metadata.mjs from ${SOL_CATALOG_REL}.`,
    '// Do not edit directly — run: node scripts/generate-planet-metadata.mjs',
    '//',
    '// The row types every star system uses, and Sol\'s rows compiled in. Code reads the',
    '// *active* system through BodyCatalog.h (Entries(), FindById, …), which is these tables',
    '// for Sol and a StarSystemCatalog-loaded copy of the same structs for any other system.',
    '// Axial tilt SSOT is orbit.axialTiltDegrees, applied as Rotate(Z) then optional artTiltX.',
    '// See docs/ARCHITECTURE.md §2.1 and §12.',
    '',
    '#include <cstdint>',
    '',
    'namespace BodyCatalog {',
    '',
    'enum class Kind : uint8_t { Star = 0, Planet = 1, DwarfPlanet = 2, Satellite = 3, Comet = 4 };',
    'enum class OrbitPlane : uint8_t { XZ = 0, XY = 1 };',
    '',
    'struct Rgb {',
    '    float r, g, b;',
    '};',
    '',
    'struct ShaderFlags {',
    '    bool hasNightTexture;',
    '    bool hasSpecularMap;',
    '    bool hasClouds;',
    '};',
    '',
    'struct TextureLodIds {',
    '    const char* diffuse; // "" for a procedural surface',
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
    '    float OmegaDeg;   // longitude of ascending node at J2000',
    '    float omegaDeg;   // argument of periapsis at J2000',
    '    float M0Deg;      // mean anomaly at J2000',
    '    float nDegPerDay; // mean-anomaly rate; anomalistic where the periapsis precesses',
    '    float OmegaDotDegPerDay;',
    '    float omegaDotDegPerDay;',
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
    '/**',
    ' * render.procedural: a surface drawn from noise (planetLighting PROCEDURAL_SURFACE) for a',
    ' * body with no texture — every exoplanet. The generator picks the palette from the',
    ' * equilibrium temperature and size (scripts/generate-exoplanet-systems.mjs).',
    ' */',
    'struct ProceduralSurface {',
    '    bool enabled;',
    '    uint8_t mode; // 0 rocky continents, 1 banded gas, 2 lava, 3 ice',
    '    float seed;',
    '    Rgb colors[3];  // low / mid / high (linear); for gas: belts, zones, highlights',
    '    float bands;    // gas: zonal band count',
    '    float seaLevel; // rocky: noise level below which colors[0] (ocean) shows',
    '    float polarCap; // |sin latitude| above which colors[2] (ice) caps the poles; 0 none',
    '    float emissive; // lava / hot giants: self-lit strength of colors[2]',
    '    float equilibriumTemperatureK;',
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
    '    float radiusKm;         // true equatorial radius (facts.diameterKm / 2): the drawn radius on realistic scale.',
    '    float axialTiltDegrees; // catalog SSOT; Rotate(Z) in CatalogBody.',
    '    float artTiltXDegrees;  // Saturn ring-presentation overlay; else 0.',
    '    float oblateness;       // flattening 1 - polar/equatorial; the mesh radius is equatorial.',
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
    '    bool hasCloudLayer;',
    '    bool hasRings;',
    '    bool hasTerrain;                // render.terrain: height tiles close up (render/terrain/)',
    '    const char* terrainAttribution; // their elevation data\'s credit; nullptr without',
    '    ProceduralSurface procedural;',
    '    bool placeFromElements; // orbit.placeFromElements: the Keplerian row places it (Ephemeris)',
    '};',
    '',
    '// One row per body; generated data, so clang-format leaves the long rows alone.',
    '// clang-format off',
    'inline constexpr Entry kSolEntries[] = {',
    ...entries,
    '};',
    '// clang-format on',
    '',
    ...strEqHelper(),
    '',
    '// ---------------------------------------------------------------------------------',
    '// Atmospheres (render.atmosphere). `oneil` is the cheap shell every preset can draw;',
    '// `physical` (enabled == true) is baked into LUTs under resource/atmosphere/ by',
    '// tools/atmosphere_lut_baker and drawn by atmospherePbr.fs. See ARCHITECTURE §9.2.',
    '',
    'struct AtmosphereOneil {',
    '    float shellScale;             // shell mesh scale (sphere.obj has radius 2)',
    '    Rgb color;                    // C_R, the dominant scattered colour',
    '    bool innerRadiusMinusEpsilon; // pull the inner radius just under the surface',
    '    float outerRadius;            // scene units',
    '    Rgb mieTint;                  // back-lit tint',
    '    float hScaleFactor;           // SCALE_H_FACTOR',
    '    bool toneMapping;             // ACES on the result (gas giants)',
    '};',
    '',
    'struct AtmospherePhysical {',
    '    bool enabled;',
    '    float groundRadiusKm;',
    '    float topRadiusKm;',
    '    float thicknessScale; // widens the shell, keeping vertical optical depth',
    '    Rgb rayleighScattering; // per km at the ground',
    '    float rayleighScaleHeightKm;',
    '    Rgb mieScattering;',
    '    Rgb mieExtinction;',
    '    float mieScaleHeightKm;',
    '    float miePhaseG;',
    '    Rgb absorptionExtinction; // tent-shaped layer (ozone, CH4, Venus UV absorber)',
    '    float absorptionCenterKm;',
    '    float absorptionHalfWidthKm;',
    '    Rgb groundAlbedo;',
    '    float exposure;',
    '};',
    '',
    'struct AtmosphereRow {',
    '    const char* bodyId;',
    '    AtmosphereOneil oneil;',
    '    AtmospherePhysical physical;',
    '};',
    '',
    '// clang-format off',
    'inline constexpr AtmosphereRow kSolAtmospheres[] = {',
    ...atmospheres,
    '};',
    '// clang-format on',
    '',
    '// ---------------------------------------------------------------------------------',
    '// The star (index 0, render.star). StarPhysics derives the light tint, corona and flare',
    '// colour, irradiance and angular size from it; docs/ARCHITECTURE.md §12.2.',
    '',
    'struct StarRow {',
    '    const char* id;',
    '    const wchar_t* displayNameEn;',
    '    const wchar_t* displayNameRu;',
    '    const char* spectralType;',
    '    float effectiveTemperatureK;',
    '    float glowTemperatureK; // the glow spectrum\'s art calibration (Sol: 5778 K)',
    '    double radiusKm;',
    '    float radiusSolar;',
    '    float luminositySolar;',
    '    float massSolar;',
    '    float activity; // 0–1 flare/activity look of the corona (a visual, not a model)',
    '    // render.star.activity.cycle: a sunspot cycle (SpaceWeather::EvaluateCycle); period 0 = none.',
    '    float cyclePeriodYears;',
    '    float cycleMinimumYear;      // decimal year of a minimum (Sol: 2008.9)',
    '    int cycleAtMinimum;          // number of the cycle that starts there; > 0 uses the Sun\'s observed maxima',
    '    float cycleMeanPeakSunspots; // peak smoothed sunspot number of an unnumbered cycle',
    '};',
    '',
    '// clang-format off',
    `inline constexpr StarRow kSolStar = ${cppStar(starRow(catalog))};`,
    '// clang-format on',
    '',
    '// ---------------------------------------------------------------------------------',
    '// Auroras (render.aurora): the look of the auroral shell (AuroraShell, aurora.fs). The',
    '// oval and its response to the wind are physics in src/astro/SpaceWeather.cpp; §13.',
    '',
    'struct AuroraRow {',
    '    const char* bodyId;',
    '    float bottomKm; // emission altitudes above the surface (giants: above 1 bar)',
    '    float topKm;',
    '    Rgb colorLow;  // linear: the lower curtain (Earth: O 557.7 nm)',
    '    Rgb colorHigh; // the upper glow (Earth: O 630.0 nm)',
    '    Rgb colorEdge; // the storm-time lower border (Earth: N2+ 427.8 nm)',
    '    float brightness;',
    '};',
    '',
    '// clang-format off',
    'inline constexpr AuroraRow kSolAuroras[] = {',
    ...auroras,
    '};',
    '// clang-format on',
    '',
    '} // namespace BodyCatalog',
  ].join('\n');
}

/**
 * OrbitLayout's per-body table for one system (OrbitLayout::BodyRow), focus bodies in index
 * order: the same numbers OrbitLayoutBodies.generated.inc compiles for Sol, plus the Keplerian
 * elements that place a body whose system has no analytic ephemeris (every system but Sol).
 */
function layoutRows(catalog) {
  return [...catalog.bodies]
    .filter(isFocusBody)
    .sort((a, b) => a.index - b.index)
    .map((body) => {
      const o = body.orbit || {};
      const spin = primeMeridian(body);
      const k = o.keplerian;
      return {
        index: body.index,
        id: body.id,
        compressedOffset: o.compressedOffset || [0, 0, 0],
        auDistance: Number(o.distanceAu ?? 0),
        orbitalPeriodDays: Number(o.orbitalPeriodDays ?? 0),
        inclinationDeg: Number(o.inclinationDeg ?? 0),
        siderealRotationDays: Number(o.siderealRotationDays ?? 0),
        primeMeridianDegJ2000: spin.w0Deg,
        primeMeridianRateDegPerDay: spin.rateDegPerDay,
        trueRadius: !!o.trueRadius,
        kepler:
          o.placeFromElements && k
            ? {
                aAu: k.aKm / 149597870.7,
                e: k.e,
                iDeg: k.iDeg,
                OmegaDeg: k.OmegaDeg,
                omegaDeg: k.omegaDeg,
                M0Deg: k.M0Deg,
                nDegPerDay: k.nDegPerDay,
              }
            : null,
      };
    });
}

function catalogRel(entry) {
  return `resource/systems/${entry.catalog}`;
}

/** resource/systems/generated/<id>.runtime.json: everything StarSystemCatalog needs, flat. */
function buildRuntimeJson(entry, catalog) {
  const byId = new Map(catalog.bodies.map((b) => [b.id, b]));
  const bodies = entryBodies(catalog);
  return {
    format: 'solarsystem-runtime-catalog',
    version: 1,
    generatedFrom: catalogRel(entry),
    id: entry.id,
    name: catalog.system?.name ?? entry.name,
    distancePc: catalog.system?.distancePc ?? entry.distancePc ?? 0,
    spectralType: catalog.system?.spectralType ?? null,
    habitableZoneAu: catalog.system?.habitableZoneAu ?? null,
    star: starRow(catalog),
    layout: layoutRows(catalog),
    entries: bodies.map((body) => entryRow(body, byId)),
    atmospheres: bodies.filter((body) => body.render.atmosphere).map(atmosphereRow),
    auroras: bodies.filter((body) => body.render.aurora).map(auroraRow),
  };
}

function srgbHex(linear) {
  return `#${linear
    .map((c) => {
      const n = Math.min(Math.max(c, 0), 1);
      const s = n <= 0.0031308 ? 12.92 * n : 1.055 * Math.pow(n, 1 / 2.4) - 0.055;
      return Math.round(s * 255)
        .toString(16)
        .padStart(2, '0');
    })
    .join('')}`;
}

/**
 * web/public/systems/<id>/thumbnail.svg for the picker: the star in its blackbody colour on
 * the left, the planets along a log-distance axis, sized by radius. No image files needed.
 */
function buildThumbnailSvg(entry, catalog) {
  const width = 160;
  const height = 72;
  const cy = height / 2;
  const star = catalog.bodies.find((b) => b.index === 0);
  const planets = catalog.bodies
    .filter((b) => isFocusBody(b) && b.index > 0 && (b.kind === 'planet' || b.kind === 'dwarf_planet'))
    .filter((b) => Number(b.orbit?.distanceAu) > 0)
    .sort((a, b) => a.orbit.distanceAu - b.orbit.distanceAu);
  const starColor = entry.starColor ?? catalog.system?.starColor ?? '#fff4e0';
  const starRadius = Math.max(5, Math.min(14, 9 + 3 * Math.log10(star.render?.star?.radiusSolar ?? 1)));
  const logs = planets.map((p) => Math.log10(p.orbit.distanceAu));
  const lo = Math.min(...logs);
  const hi = Math.max(...logs);
  const x0 = 26 + starRadius;
  const x1 = width - 10;
  const color = (body) => {
    if (body.render?.procedural) return srgbHex(body.render.procedural.colors[1]);
    return body.assets?.fallbackColor ?? '#9aa4b0';
  };
  const dots = planets.map((p, i) => {
    const t = hi > lo ? (logs[i] - lo) / (hi - lo) : 0.5;
    const x = x0 + t * (x1 - x0);
    const r = Math.max(1.4, Math.min(5.5, 1.6 * Math.sqrt(p.orbit.earthRadiusScale ?? 1)));
    return `  <circle cx="${x.toFixed(1)}" cy="${cy}" r="${r.toFixed(2)}" fill="${color(p)}"/>`;
  });
  const name = (catalog.system?.name?.en ?? entry.name?.en ?? entry.id).replace(/&/g, '&amp;').replace(/</g, '&lt;');
  return [
    `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${width} ${height}" width="${width}" height="${height}" role="img" aria-label="${name}">`,
    '  <defs>',
    `    <radialGradient id="glow"><stop offset="0" stop-color="${starColor}" stop-opacity="0.9"/><stop offset="1" stop-color="${starColor}" stop-opacity="0"/></radialGradient>`,
    '  </defs>',
    `  <rect width="${width}" height="${height}" fill="#05070d"/>`,
    `  <line x1="${x0 - 4}" y1="${cy}" x2="${x1 + 4}" y2="${cy}" stroke="#ffffff" stroke-opacity="0.12" stroke-width="0.6"/>`,
    `  <circle cx="16" cy="${cy}" r="${(starRadius * 2.2).toFixed(1)}" fill="url(#glow)"/>`,
    `  <circle cx="16" cy="${cy}" r="${starRadius.toFixed(1)}" fill="${starColor}"/>`,
    ...dots,
    '</svg>',
    '',
  ].join('\n');
}

/** web/public/systems/index.json: index.json with each thumbnail confirmed and the body count checked. */
function buildWebIndex(index, catalogs) {
  return {
    version: index.version,
    archive: index.archive,
    systems: index.systems.map((entry) => {
      const catalog = catalogs.get(entry.id);
      const focus = catalog.bodies.filter((b) => isFocusBody(b) && b.index > 0);
      if (focus.length !== entry.bodyCount) {
        throw new Error(
          `resource/systems/index.json: ${entry.id}.bodyCount is ${entry.bodyCount} but its catalog has ${focus.length} focus bodies`,
        );
      }
      const star = catalog.bodies.find((b) => b.index === 0);
      return {
        id: entry.id,
        name: entry.name,
        distancePc: entry.distancePc,
        starClass: entry.starClass,
        starTemperatureK: star.render.star.effectiveTemperatureK,
        starColor: entry.starColor,
        bodyCount: entry.bodyCount,
        thumbnail: entry.thumbnail,
        observe: !!entry.observe,
        facts: entry.id === SOL_ID ? 'planet_facts.json' : `systems/${entry.id}/planet_facts.json`,
        // Focus ids by index, for ?system=…&planet=<id or letter> deep links before the facts load.
        bodies: [...catalog.bodies]
          .filter(isFocusBody)
          .sort((a, b) => a.index - b.index)
          .map((b) => b.id),
      };
    }),
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

function loadSystems() {
  if (!existsSync(systemsIndexPath)) {
    console.error(`System index not found: ${systemsIndexPath}`);
    process.exit(1);
  }
  const index = readJson(systemsIndexPath);
  if (!Array.isArray(index.systems) || index.systems[0]?.id !== SOL_ID) {
    throw new Error('resource/systems/index.json must list "sol" first');
  }
  const catalogs = new Map();
  for (const entry of index.systems) {
    const path = join(systemsDir, entry.catalog);
    if (!existsSync(path)) throw new Error(`${entry.id}: catalog ${entry.catalog} not found in resource/systems/`);
    const catalog = readJson(path);
    try {
      validateCatalog(catalog, entry.id);
    } catch (error) {
      throw new Error(`${entry.catalog}: ${error.message}`);
    }
    catalogs.set(entry.id, catalog);
  }
  return { index, catalogs };
}

function main() {
  const { index, catalogs } = loadSystems();
  const catalog = catalogs.get(SOL_ID);

  const outputs = [
    { path: OUT.facts, text: stableStringify(buildPlanetFacts(catalog, SOL_CATALOG_REL)) },
    { path: OUT.orbital, text: stableStringify(buildOrbitalParameters(catalog)) },
    { path: OUT.manifest, text: stableStringify(buildPlanetManifest(catalog)) },
    { path: OUT.orbitLayoutBodies, text: buildOrbitLayoutBodiesInc(catalog) },
    { path: OUT.bodyCatalog, text: buildBodyCatalogHeader(catalog) },
    { path: OUT.planetIds, text: buildPlanetIdsTs(catalog) },
    { path: OUT.webIndex, text: stableStringify(buildWebIndex(index, catalogs)) },
  ];
  for (const entry of index.systems) {
    const systemCatalog = catalogs.get(entry.id);
    outputs.push({ path: OUT.runtime(entry.id), text: stableStringify(buildRuntimeJson(entry, systemCatalog)) });
    outputs.push({ path: OUT.thumbnail(entry.id), text: buildThumbnailSvg(entry, systemCatalog) });
    if (entry.id !== SOL_ID) {
      outputs.push({
        path: OUT.systemFacts(entry.id),
        text: stableStringify(buildPlanetFacts(systemCatalog, catalogRel(entry))),
      });
    }
  }
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
