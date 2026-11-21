#!/usr/bin/env node
/**
 * Generate star-system catalogs from the NASA Exoplanet Archive.
 *
 * Pulls the Planetary Systems Composite Parameters table (PSCompPars) over TAP as CSV and
 * writes one resource/systems/<id>.catalog.json per curated system, in the same schema as
 * resource/systems/sol.catalog.json, plus resource/systems/index.json. Both outputs are
 * committed (like the star catalogue): run this to refresh them, then
 * `node scripts/generate-planet-metadata.mjs` to derive the runtime and web files.
 *
 *   node scripts/generate-exoplanet-systems.mjs            # fetch, write the snapshot and catalogs
 *   node scripts/generate-exoplanet-systems.mjs --offline  # rebuild from the committed snapshot
 *   node scripts/generate-exoplanet-systems.mjs --check    # exit 1 if outputs would change (offline)
 *
 * The archive rows for the curated systems are kept in resource/systems/archive/ so the
 * catalogs rebuild without the network (and CI can check them). How many archive systems
 * would qualify is recorded there too, so index.json can say how many more are available.
 *
 * Gaps are filled the way the archive's own composite table does, and flagged in each body's
 * `fit` block so the UI can say which numbers are measured:
 *   - radius from mass via Chen & Kipping (2017) "Forecaster" mean relation (and back);
 *   - a missing semi-major axis from Kepler's third law with the stellar mass (and period);
 *   - missing eccentricity = 0; missing inclination = 90° (edge-on);
 *   - a missing argument of periastron = 90° (the convention for circular orbits);
 *   - missing luminosity from R² (T / T☉)⁴.
 */
import { existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join, relative } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const systemsDir = join(root, 'resource/systems');
const archiveDir = join(systemsDir, 'archive');
const snapshotPath = join(archiveDir, 'pscomppars.curated.csv');
const summaryPath = join(archiveDir, 'summary.json');
const indexPath = join(systemsDir, 'index.json');

const TAP = 'https://exoplanetarchive.ipac.caltech.edu/TAP/sync';
const COLUMNS = [
  'hostname',
  'pl_name',
  'pl_letter',
  'sy_dist',
  'st_spectype',
  'st_teff',
  'st_rad',
  'st_mass',
  'st_lum',
  'pl_orbper',
  'pl_orbsmax',
  'pl_rade',
  'pl_bmasse',
  'pl_orbeccen',
  'pl_orbincl',
  'pl_orblper',
  'pl_eqt',
  'pl_tranmid',
  'disc_year',
  'discoverymethod',
];

/**
 * The curated systems, nearest-first is the picker's job (it sorts by distance). `host` is the
 * archive's hostname, which is not always the common name (Kepler-90 is filed as KOI-351).
 * `ru` is the Russian display name: catalogue designations are kept in Latin, as in Russian
 * astronomical usage; only the common names translate.
 */
const CURATED = [
  { id: 'trappist-1', host: 'TRAPPIST-1', en: 'TRAPPIST-1', ru: 'TRAPPIST-1' },
  { id: 'proxima-cen', host: 'Proxima Cen', en: 'Proxima Centauri', ru: 'Проксима Центавра' },
  { id: 'kepler-90', host: 'KOI-351', en: 'Kepler-90', ru: 'Kepler-90' },
  { id: '51-peg', host: '51 Peg', en: '51 Pegasi', ru: '51 Пегаса' },
  { id: '55-cnc', host: '55 Cnc', en: '55 Cancri', ru: '55 Рака' },
  { id: 'tau-cet', host: 'tau Cet', en: 'Tau Ceti', ru: 'Тау Кита' },
  { id: 'toi-700', host: 'TOI-700', en: 'TOI-700', ru: 'TOI-700' },
  { id: 'kepler-186', host: 'Kepler-186', en: 'Kepler-186', ru: 'Kepler-186' },
];

const args = new Set(process.argv.slice(2));
const checkOnly = args.has('--check');
const offline = args.has('--offline') || checkOnly;

// --- Physical constants -----------------------------------------------------------------
const SUN_TEFF_K = 5772; // IAU 2015 B3; StarPhysics::kSunEffectiveTemperatureK
const SUN_RADIUS_KM = 695700;
const EARTH_RADIUS_KM = 6371;
const KM_PER_AU = 149597870.7;
const EARTH_MASSES_PER_SUN = 332946;
const J2000 = 2451545.0;
/** Chen & Kipping (2017), Forecaster's mean relation: R = C M^S in Earth units, per regime. */
const FORECASTER = {
  terranMaxMe: 2.04,
  neptunianMaxMe: 0.414 * 317.828, // 0.414 Jupiter masses
  jovianMaxMe: 0.08 * EARTH_MASSES_PER_SUN,
  terran: { c: 1.008, s: 0.279 },
  neptunian: { c: 0.80811, s: 0.589 },
  jovian: { c: 17.739, s: -0.044 },
};

// --- CSV ----------------------------------------------------------------------------------
function parseCsv(text) {
  const rows = [];
  let row = [];
  let field = '';
  let quoted = false;
  for (let i = 0; i < text.length; i++) {
    const c = text[i];
    if (quoted) {
      if (c === '"') {
        if (text[i + 1] === '"') {
          field += '"';
          i++;
        } else {
          quoted = false;
        }
      } else {
        field += c;
      }
    } else if (c === '"') {
      quoted = true;
    } else if (c === ',') {
      row.push(field);
      field = '';
    } else if (c === '\n' || c === '\r') {
      if (c === '\r' && text[i + 1] === '\n') i++;
      row.push(field);
      if (row.some((value) => value !== '')) rows.push(row);
      row = [];
      field = '';
    } else {
      field += c;
    }
  }
  if (field !== '' || row.length > 0) {
    row.push(field);
    rows.push(row);
  }
  const [header, ...body] = rows;
  return body.map((values) => Object.fromEntries(header.map((name, i) => [name, values[i] ?? ''])));
}

function num(value) {
  if (value === undefined || value === null || value === '') return null;
  const n = Number(value);
  return Number.isFinite(n) ? n : null;
}

async function tapQuery(adql) {
  const url = `${TAP}?query=${encodeURIComponent(adql)}&format=csv`;
  const response = await fetch(url);
  if (!response.ok) {
    throw new Error(`TAP query failed: HTTP ${response.status} for ${adql}`);
  }
  return response.text();
}

/** A planet the scene can place and draw: an orbit (period or distance) and a size (radius or mass). */
function isUsable(row) {
  return (num(row.pl_orbper) !== null || num(row.pl_orbsmax) !== null) &&
    (num(row.pl_rade) !== null || num(row.pl_bmasse) !== null);
}

async function fetchArchive() {
  const hosts = CURATED.map((s) => `'${s.host.replace(/'/g, "''")}'`).join(',');
  const curatedCsv = await tapQuery(
    `select ${COLUMNS.join(',')} from pscomppars where hostname in (${hosts}) order by hostname, pl_orbper`,
  );
  // How many systems qualify, by the same test isUsable applies per planet.
  const countCsv = await tapQuery(
    'select count(distinct hostname) as n from pscomppars where ' +
      '(pl_orbper is not null or pl_orbsmax is not null) and (pl_rade is not null or pl_bmasse is not null)',
  );
  const available = num(parseCsv(countCsv)[0]?.n);
  return { curatedCsv, available };
}

// --- Gap filling ----------------------------------------------------------------------------
function radiusFromMass(massMe) {
  const f = FORECASTER;
  const regime = massMe < f.terranMaxMe ? f.terran : massMe < f.neptunianMaxMe ? f.neptunian : f.jovian;
  return regime.c * Math.pow(massMe, regime.s);
}

/** Inverse of radiusFromMass where it is monotonic (terran and Neptunian); Jovian radii stay put. */
function massFromRadius(radiusRe) {
  const f = FORECASTER;
  const terranMaxR = radiusFromMass(f.terranMaxMe - 1e-9);
  if (radiusRe < terranMaxR) return Math.pow(radiusRe / f.terran.c, 1 / f.terran.s);
  const neptunianMaxR = radiusFromMass(f.neptunianMaxMe - 1e-9);
  if (radiusRe < neptunianMaxR) return Math.pow(radiusRe / f.neptunian.c, 1 / f.neptunian.s);
  return null; // degenerate for giants: radius barely depends on mass
}

function round(value, digits) {
  if (value === null || value === undefined) return value;
  const scale = 10 ** digits;
  return Math.round(value * scale) / scale;
}

function wrap360(deg) {
  const w = deg % 360;
  return w < 0 ? w + 360 : w;
}

/** Mean anomaly (deg) at the transit: true anomaly f = 90° − ω puts the planet on the line of sight. */
function meanAnomalyAtTransitDeg(e, omegaDeg) {
  const f = ((90 - omegaDeg) * Math.PI) / 180;
  const E = 2 * Math.atan2(Math.sqrt(1 - e) * Math.sin(f / 2), Math.sqrt(1 + e) * Math.cos(f / 2));
  return ((E - e * Math.sin(E)) * 180) / Math.PI;
}

function blackbodyHex(kelvin) {
  // Krystek (1985) Planckian locus → linear sRGB → gamma, as StarPhysics::BlackbodyLinearRgb.
  const t = Math.min(Math.max(kelvin, 1000), 15000);
  const t2 = t * t;
  const u = (0.860117757 + 1.54118254e-4 * t + 1.28641212e-7 * t2) / (1 + 8.42420235e-4 * t + 7.08145163e-7 * t2);
  const v = (0.317398726 + 4.22806245e-5 * t + 4.20481691e-8 * t2) / (1 - 2.89741816e-5 * t + 1.61456053e-7 * t2);
  const d = 2 * u - 8 * v + 4;
  const x = (3 * u) / d;
  const y = (2 * v) / d;
  const X = x / y;
  const Z = (1 - x - y) / y;
  const lin = [
    3.2404542 * X - 1.5371385 - 0.4985314 * Z,
    -0.969266 * X + 1.8760108 + 0.041556 * Z,
    0.0556434 * X - 0.2040259 + 1.0572252 * Z,
  ].map((c) => Math.max(c, 0));
  const peak = Math.max(...lin);
  return `#${lin
    .map((c) => {
      const n = c / peak;
      const s = n <= 0.0031308 ? 12.92 * n : 1.055 * Math.pow(n, 1 / 2.4) - 0.055;
      return Math.round(Math.min(Math.max(s, 0), 1) * 255)
        .toString(16)
        .padStart(2, '0');
    })
    .join('')}`;
}

/**
 * Rocky bands by zero-albedo equilibrium temperature (Earth's is 278 K, TRAPPIST-1 b's 398 K):
 * lava ≥ 1000 K, scorched ≥ 330 K, ocean ≥ 240 K, tundra ≥ 190 K, ice below. They are looks,
 * not climate models: an "ocean" is a temperate world drawn the way a reader expects one.
 *
 * A planetLighting PROCEDURAL_SURFACE material for a body without a texture (render.procedural).
 * `mode`: 0 rocky continents, 1 gas bands, 2 lava, 3 ice. Colours are linear RGB.
 */
function proceduralMaterial(radiusRe, massMe, teqK, seed) {
  const giant = radiusRe >= 6 || (massMe ?? 0) >= 50;
  const neptune = !giant && radiusRe >= 2;
  const base = { seed, equilibriumTemperatureK: round(teqK, 1) };
  if (giant) {
    if (teqK >= 1000) {
      return { ...base, style: 'hot-jupiter', mode: 1, colors: [[0.09, 0.05, 0.04], [0.42, 0.2, 0.09], [0.85, 0.45, 0.18]], bands: 9, emissive: 0.25 };
    }
    if (teqK >= 300) {
      return { ...base, style: 'gas-giant', mode: 1, colors: [[0.5, 0.38, 0.25], [0.82, 0.7, 0.52], [0.95, 0.9, 0.8]], bands: 12, emissive: 0 };
    }
    return { ...base, style: 'cold-giant', mode: 1, colors: [[0.55, 0.58, 0.6], [0.78, 0.76, 0.68], [0.92, 0.9, 0.85]], bands: 10, emissive: 0 };
  }
  if (neptune) {
    if (teqK >= 700) {
      return { ...base, style: 'warm-neptune', mode: 1, colors: [[0.3, 0.33, 0.3], [0.52, 0.55, 0.46], [0.75, 0.74, 0.62]], bands: 6, emissive: 0 };
    }
    return { ...base, style: 'ice-giant', mode: 1, colors: [[0.13, 0.32, 0.55], [0.3, 0.55, 0.72], [0.62, 0.82, 0.9]], bands: 5, emissive: 0 };
  }
  if (teqK >= 1000) {
    return { ...base, style: 'lava', mode: 2, colors: [[0.05, 0.04, 0.04], [0.2, 0.12, 0.09], [1.0, 0.38, 0.06]], seaLevel: 0.55, emissive: 1.4 };
  }
  if (teqK >= 330) {
    return { ...base, style: 'scorched', mode: 0, colors: [[0.32, 0.22, 0.15], [0.55, 0.42, 0.3], [0.72, 0.62, 0.5]], seaLevel: 0.0, polarCap: 0, emissive: 0 };
  }
  if (teqK >= 240) {
    return { ...base, style: 'ocean', mode: 0, colors: [[0.07, 0.22, 0.45], [0.42, 0.45, 0.28], [0.92, 0.94, 0.97]], seaLevel: 0.55, polarCap: 0.75, emissive: 0 };
  }
  if (teqK >= 190) {
    return { ...base, style: 'tundra', mode: 3, colors: [[0.45, 0.47, 0.5], [0.7, 0.74, 0.78], [0.95, 0.96, 0.98]], seaLevel: 0.35, polarCap: 0.45, emissive: 0 };
  }
  return { ...base, style: 'ice', mode: 3, colors: [[0.55, 0.6, 0.66], [0.82, 0.86, 0.9], [0.97, 0.98, 1.0]], seaLevel: 0.2, polarCap: 0.2, emissive: 0 };
}

function planetType(radiusRe, massMe, teqK) {
  if (radiusRe >= 6 || (massMe ?? 0) >= 50) return teqK >= 1000 ? 'hot jupiter' : 'gas giant';
  if (radiusRe >= 2) return 'sub-neptune';
  if (radiusRe >= 1.25) return 'super-earth';
  return 'terrestrial';
}

const RU_TYPE = {
  'hot jupiter': 'горячий юпитер',
  'gas giant': 'газовый гигант',
  'sub-neptune': 'мини-нептун',
  'super-earth': 'суперземля',
  terrestrial: 'планета земного типа',
};

const RU_METHOD = {
  Transit: 'транзитным методом',
  'Radial Velocity': 'методом лучевых скоростей',
  Imaging: 'прямым наблюдением',
  Microlensing: 'методом микролинзирования',
  'Transit Timing Variations': 'по вариациям времени транзитов',
};

/** The system's reference plane: the median observed inclination, so co-planar orbits draw flat. */
function median(values) {
  const sorted = [...values].sort((a, b) => a - b);
  const mid = Math.floor(sorted.length / 2);
  return sorted.length % 2 ? sorted[mid] : (sorted[mid - 1] + sorted[mid]) / 2;
}

function buildSystem(spec, rows) {
  const usable = rows.filter(isUsable);
  if (usable.length === 0) {
    throw new Error(`${spec.id}: no usable planets in the archive rows for host ${spec.host}`);
  }
  const host = usable[0];
  const teff = num(host.st_teff);
  const rStar = num(host.st_rad);
  const mStar = num(host.st_mass);
  if (teff === null || rStar === null) {
    throw new Error(`${spec.id}: the archive has no st_teff / st_rad for ${spec.host}`);
  }
  const logLum = num(host.st_lum);
  const lum = logLum !== null ? 10 ** logLum : rStar * rStar * (teff / SUN_TEFF_K) ** 4;
  const distancePc = num(host.sy_dist);
  const spectralType = host.st_spectype || null;
  const idPrefix = spec.id.replace(/-/g, '_');
  const starId = idPrefix;

  const planets = usable.map((row) => {
    const fit = {};
    let period = num(row.pl_orbper);
    let a = num(row.pl_orbsmax);
    const mass = mStar ?? 1;
    if (a === null) {
      a = Math.cbrt(mass * (period / 365.25) ** 2);
      fit.semiMajorAxis = 'kepler-third-law';
    }
    if (period === null) {
      period = 365.25 * Math.sqrt(a ** 3 / mass);
      fit.period = 'kepler-third-law';
    }
    let radius = num(row.pl_rade);
    let massMe = num(row.pl_bmasse);
    if (radius === null) {
      radius = radiusFromMass(massMe);
      fit.radius = 'chen-kipping-2017';
    }
    if (massMe === null) {
      massMe = massFromRadius(radius);
      if (massMe !== null) fit.mass = 'chen-kipping-2017';
    }
    let e = num(row.pl_orbeccen);
    if (e === null) {
      e = 0;
      fit.eccentricity = true;
    }
    let incl = num(row.pl_orbincl);
    if (incl === null) {
      incl = 90;
      fit.inclination = true;
    }
    let omega = num(row.pl_orblper);
    if (omega === null) {
      omega = 90;
      fit.argumentOfPeriastron = true;
    }
    const tranmid = num(row.pl_tranmid);
    const teqArchive = num(row.pl_eqt);
    const teq = teqArchive ?? 278.6 * lum ** 0.25 / Math.sqrt(a);
    if (teqArchive === null) fit.equilibriumTemperature = 'zero-albedo';
    return { row, period, a, radius, massMe, e, incl, omega, tranmid, teq, fit };
  });
  planets.sort((p, q) => p.a - q.a);

  const referenceIncl = median(planets.map((p) => p.incl));
  const bodies = [];
  bodies.push({
    id: starId,
    index: 0,
    kind: 'star',
    parent: null,
    displayName: { en: spec.en, ru: spec.ru },
    type: 'star',
    facts: {
      diameterKm: round(2 * rStar * SUN_RADIUS_KM, 0),
      massEarths: mStar !== null ? round(mStar * EARTH_MASSES_PER_SUN, 0) : null,
      siderealRotationDays: null,
      orbitalPeriodDays: null,
      distanceAu: 0,
      moons: 0,
      distancePc: round(distancePc, 3),
      spectralType,
      effectiveTemperatureK: teff,
      luminositySolar: round(lum, 7),
      summary: {
        en: `${spectralType ?? 'Main-sequence'} star ${round(distancePc * 3.26156, 1)} light-years away; ${planets.length} known planet${planets.length === 1 ? '' : 's'} in the archive's composite table.`,
        ru: `Звезда класса ${spectralType ?? 'главной последовательности'} на расстоянии ${round(distancePc * 3.26156, 1)} св. лет; известных планет: ${planets.length}.`,
      },
    },
    orbit: {
      compressedOffset: [0, 0, 0],
      inclinationDeg: 0,
      siderealRotationDays: null,
      orbitalPeriodDays: null,
      distanceAu: 0,
      axialTiltDegrees: 0,
      earthRadiusScale: 1,
      rotationRate: 0,
    },
    render: {
      star: {
        effectiveTemperatureK: teff,
        radiusSolar: rStar,
        luminositySolar: round(lum, 7),
        massSolar: mStar,
        spectralType,
        // Flare/activity: a visual modulation of the corona noise, not a model (the UI says so).
        // M dwarfs are the active ones; scaled up as the star cools.
        activity: teff < 3900 ? round(Math.min(1, (3900 - teff) / 1300), 2) : 0,
      },
    },
    includeInCompanion: false,
  });

  planets.forEach((p, i) => {
    const letter = p.row.pl_letter || String.fromCharCode(98 + i);
    const id = `${idPrefix}_${letter}`;
    const name = `${spec.en} ${letter}`;
    const nameRu = `${spec.ru} ${letter}`;
    const nDegPerDay = 360 / p.period;
    let M0Deg = 0;
    if (p.tranmid !== null) {
      M0Deg = wrap360(meanAnomalyAtTransitDeg(p.e, p.omega) + nDegPerDay * (J2000 - p.tranmid));
    } else {
      p.fit.meanAnomaly = true;
    }
    // Close-in planets are taken as tidally locked (rotation = orbit); others get a placeholder day.
    const locked = p.a < 0.2;
    const siderealDays = locked ? p.period : 1.0;
    p.fit.rotation = locked ? 'tidally-locked' : 'unknown';
    const compressedRadius = 1500 + 250 * i;
    const theta = (i * 137.50776405 * Math.PI) / 180;
    const type = planetType(p.radius, p.massMe, p.teq);
    const discovered = num(p.row.disc_year);
    const method = p.row.discoverymethod || null;
    const irradiance = lum / (p.a * p.a);
    bodies.push({
      id,
      index: i + 1,
      kind: 'planet',
      parent: starId,
      displayName: { en: name, ru: nameRu },
      type,
      facts: {
        diameterKm: round(2 * p.radius * EARTH_RADIUS_KM, 0),
        massEarths: p.massMe !== null ? round(p.massMe, 3) : null,
        siderealRotationDays: round(siderealDays, 6),
        orbitalPeriodDays: round(p.period, 8),
        distanceAu: round(p.a, 6),
        moons: 0,
        equilibriumTemperatureK: round(p.teq, 1),
        irradianceEarth: round(irradiance, 4),
        discoveryYear: discovered,
        discoveryMethod: method,
        summary: {
          en: `${type[0].toUpperCase()}${type.slice(1)}${discovered ? ` discovered in ${discovered}` : ''}${method ? ` (${method.toLowerCase()})` : ''}; ${round(irradiance, 2)}× Earth's insolation, equilibrium temperature ≈ ${Math.round(p.teq)} K.`,
          ru: `${RU_TYPE[type][0].toUpperCase()}${RU_TYPE[type].slice(1)}${discovered ? `, открыта в ${discovered} г.` : ''}${method && RU_METHOD[method] ? ` ${RU_METHOD[method]}` : ''}; освещённость ${round(irradiance, 2)} от земной, равновесная температура ≈ ${Math.round(p.teq)} K.`,
        },
      },
      orbit: {
        compressedOffset: [round(compressedRadius * Math.cos(theta), 2), 0, round(compressedRadius * Math.sin(theta), 2)],
        inclinationDeg: round(p.incl - referenceIncl, 4),
        siderealRotationDays: round(siderealDays, 6),
        orbitalPeriodDays: round(p.period, 8),
        distanceAu: round(p.a, 6),
        axialTiltDegrees: 0,
        earthRadiusScale: round(p.radius, 4),
        rotationRate: 0,
        // IAU sense: W = W0 + Wdot·d, positive prograde (OrbitLayout::AxialSpinDegreesAt draws
        // it prograde outside Sol). A locked planet turns at the mean motion, and its W0 puts
        // the sub-stellar point at map longitude 0 (u = 0.5): W = λ − 180° with the mean
        // longitude λ = Ω + ω + M (Ω = 0).
        primeMeridianDegJ2000: locked ? round(wrap360(wrap360(p.omega) + M0Deg - 180), 4) : 0,
        primeMeridianRateDegPerDay: round(360 / siderealDays, 9),
        // Star-centric elements in the system's reference plane (the median observed
        // inclination), epoch J2000. Ω is unknown for exoplanets: 0 for all, so the transit
        // geometry (longitude 90° at mid-transit) is shared and the planets' relative phases
        // are the measured ones.
        keplerian: {
          aKm: round(p.a * KM_PER_AU, 1),
          e: p.e,
          iDeg: round(p.incl - referenceIncl, 4),
          OmegaDeg: 0,
          omegaDeg: round(wrap360(p.omega), 4),
          M0Deg: round(M0Deg, 4),
          nDegPerDay: round(nDegPerDay, 9),
          OmegaDotDegPerDay: 0,
          omegaDotDegPerDay: 0,
        },
        placeFromElements: true,
      },
      system: {
        name,
        initTag: id,
        activationRadius: 800,
        lodThreshold: 20,
      },
      render: {
        shaderFlags: { hasNightTexture: false, hasSpecularMap: false, hasClouds: false },
        ambientFactor: 0,
        procedural: proceduralMaterial(p.radius, p.massMe, p.teq, i + 1),
      },
      fit: p.fit,
      archive: {
        plName: p.row.pl_name,
        observedInclinationDeg: num(p.row.pl_orbincl),
      },
      includeInCompanion: false,
    });
  });

  const hz = { innerAu: round(Math.sqrt(lum / 1.1), 5), outerAu: round(Math.sqrt(lum / 0.53), 5) };
  return {
    schemaVersion: 1,
    system: {
      id: spec.id,
      name: { en: spec.en, ru: spec.ru },
      distancePc: round(distancePc, 3),
      spectralType,
      starColor: blackbodyHex(teff),
      habitableZoneAu: hz,
      source: {
        table: 'NASA Exoplanet Archive, Planetary Systems Composite Parameters (pscomppars)',
        url: 'https://exoplanetarchive.ipac.caltech.edu/',
        archiveHostname: spec.host,
        referencePlaneInclinationDeg: round(referenceIncl, 4),
      },
    },
    sceneNote: {
      en: 'Measured orbits and sizes; surfaces are procedural (no exoplanet has been imaged). Values marked fit are estimates.',
      ru: 'Орбиты и размеры измерены; поверхности процедурные (ни одна экзопланета не сфотографирована). Значения с пометкой fit — оценки.',
    },
    initTagAllowlist: bodies.filter((b) => b.system).map((b) => b.system.initTag),
    bodies,
  };
}

function stableStringify(value) {
  return `${JSON.stringify(value, null, 2)}\n`;
}

let dirty = false;
function write(path, text) {
  mkdirSync(dirname(path), { recursive: true });
  const prev = existsSync(path) ? readFileSync(path, 'utf8') : null;
  const rel = relative(root, path);
  if (prev === text) {
    console.log(`up-to-date ${rel}`);
    return;
  }
  dirty = true;
  if (checkOnly) {
    console.log(`WOULD UPDATE ${rel}`);
    return;
  }
  writeFileSync(path, text, 'utf8');
  console.log(`updated ${rel}`);
}

async function main() {
  let csvText;
  let summary;
  if (offline) {
    if (!existsSync(snapshotPath) || !existsSync(summaryPath)) {
      throw new Error(`--offline needs ${relative(root, snapshotPath)} and ${relative(root, summaryPath)}; run once online first`);
    }
    csvText = readFileSync(snapshotPath, 'utf8');
    summary = JSON.parse(readFileSync(summaryPath, 'utf8'));
  } else {
    const fetched = await fetchArchive();
    csvText = fetched.curatedCsv.endsWith('\n') ? fetched.curatedCsv : `${fetched.curatedCsv}\n`;
    summary = {
      table: 'pscomppars',
      fetched: new Date().toISOString().slice(0, 10),
      columns: COLUMNS,
      curatedHosts: CURATED.map((s) => s.host),
      archiveSystemsAvailable: fetched.available,
      criterion: 'at least one planet with (pl_orbper or pl_orbsmax) and (pl_rade or pl_bmasse)',
    };
    write(snapshotPath, csvText);
    write(summaryPath, stableStringify(summary));
  }

  const rows = parseCsv(csvText);
  const systems = [];
  for (const spec of CURATED) {
    const catalog = buildSystem(spec, rows.filter((row) => row.hostname === spec.host));
    write(join(systemsDir, `${spec.id}.catalog.json`), stableStringify(catalog));
    const star = catalog.bodies[0];
    systems.push({
      id: spec.id,
      catalog: `${spec.id}.catalog.json`,
      name: catalog.system.name,
      distancePc: catalog.system.distancePc,
      starClass: catalog.system.spectralType,
      starTemperatureK: star.render.star.effectiveTemperatureK,
      starColor: catalog.system.starColor,
      bodyCount: catalog.bodies.length - 1,
      thumbnail: `systems/${spec.id}/thumbnail.svg`,
      observe: false,
    });
  }

  const index = {
    version: 1,
    note: 'Generated by scripts/generate-exoplanet-systems.mjs (exoplanet rows) — Sol is fixed. generate-planet-metadata.mjs reads this list.',
    archive: {
      table: summary.table,
      fetched: summary.fetched,
      systemsAvailable: summary.archiveSystemsAvailable,
      criterion: summary.criterion,
    },
    systems: [
      {
        id: 'sol',
        catalog: 'sol.catalog.json',
        name: { en: 'Solar System', ru: 'Солнечная система' },
        distancePc: 0,
        starClass: 'G2V',
        starTemperatureK: SUN_TEFF_K,
        starColor: blackbodyHex(SUN_TEFF_K),
        bodyCount: 15,
        thumbnail: 'systems/sol/thumbnail.svg',
        observe: true,
      },
      ...systems,
    ],
  };
  write(indexPath, stableStringify(index));

  if (checkOnly && dirty) {
    console.error('\nExoplanet system catalogs are out of date. Run:\n  node scripts/generate-exoplanet-systems.mjs --offline');
    process.exit(1);
  }
}

main().catch((error) => {
  console.error(error);
  process.exit(1);
});
