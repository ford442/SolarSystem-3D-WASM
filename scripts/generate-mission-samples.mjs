#!/usr/bin/env node
/**
 * Bake sampled heliocentric mission trajectories (AU, scene-axis Cartesian) from
 * Standish planet positions at published encounter dates, then Catmull-Rom through
 * those waypoints. No SPICE, no network. Run:
 *   node scripts/generate-mission-samples.mjs
 */
import { writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const kPi = Math.PI;
const kTwoPi = 2 * kPi;
const kDegToRad = kPi / 180;
const kJ2000 = 2451545.0;
const kDaysPerCentury = 36525.0;

// JPL SSD Table 1 (1800–2050), same numbers as src/Auxiliary_Modules/Ephemeris.cpp.
const kStandish = {
  earth: [1.00000261, 0.00000562, 0.01671123, -0.00004392, -0.00001531, -0.01294668,
    100.46457166, 35999.37244981, 102.93768193, 0.32327364, 0.0, 0.0],
  jupiter: [5.20288700, -0.00011607, 0.04838624, -0.00013253, 1.30439695, -0.00183714,
    34.39644051, 3034.74612775, 14.72847983, 0.21252668, 100.47390909, 0.20469106],
  saturn: [9.53667594, -0.00125060, 0.05386179, -0.00050991, 2.48599187, 0.00193609,
    49.95424423, 1222.49362201, 92.59887831, -0.41897216, 113.66242448, -0.28867794],
  uranus: [19.18916464, -0.00196176, 0.04725744, -0.00004397, 0.77263783, -0.00242939,
    313.23810451, 428.48202785, 170.95427630, 0.40805281, 74.01692503, 0.04240589],
  neptune: [30.06992276, 0.00026291, 0.00859048, 0.00005105, 1.77004347, 0.00035372,
    -55.12002969, 218.45945325, 44.96476227, -0.32241464, 131.78422574, -0.00508664],
  pluto: null,
};

const kPluto = { epochJd: kJ2000, a: 39.482, e: 0.2488, IDeg: 17.16, OmDeg: 110.299, wDeg: 113.834, M0Deg: 14.53, nDegPerDay: 360.0 / 90465.0 };

function julianDateFromYmd(year, month, day, hour = 0) {
  const a = Math.floor((14 - month) / 12);
  const y = year + 4800 - a;
  const m = month + 12 * a - 3;
  const jdn = day + Math.floor((153 * m + 2) / 5) + 365 * y + Math.floor(y / 4) - Math.floor(y / 100) + Math.floor(y / 400) - 32045;
  return jdn - 0.5 + hour / 24;
}

function wrapDeg180(deg) {
  deg = ((deg + 180) % 360 + 360) % 360;
  return deg - 180;
}

function wrapRadTwoPi(rad) {
  rad = rad % kTwoPi;
  if (rad < 0) rad += kTwoPi;
  return rad;
}

function solveKepler(MRad, e) {
  MRad = wrapRadTwoPi(MRad);
  if (MRad > kPi) MRad -= kTwoPi;
  let E = MRad + e * Math.sin(MRad);
  for (let i = 0; i < 12; ++i) {
    const dM = MRad - (E - e * Math.sin(E));
    const dE = dM / (1.0 - e * Math.cos(E));
    E += dE;
    if (Math.abs(dE) < 1e-10) break;
  }
  return E;
}

function fromOrbitalPlane(a, e, IDeg, OmDeg, wDeg, MDeg) {
  const I = IDeg * kDegToRad;
  const Om = OmDeg * kDegToRad;
  const w = wDeg * kDegToRad;
  const E = solveKepler(MDeg * kDegToRad, e);
  const xp = a * (Math.cos(E) - e);
  const yp = a * Math.sqrt(Math.max(0, 1 - e * e)) * Math.sin(E);
  const cosW = Math.cos(w);
  const sinW = Math.sin(w);
  const cosOm = Math.cos(Om);
  const sinOm = Math.sin(Om);
  const cosI = Math.cos(I);
  const sinI = Math.sin(I);
  const xe = (cosW * cosOm - sinW * sinOm * cosI) * xp + (-sinW * cosOm - cosW * sinOm * cosI) * yp;
  const ye = (cosW * sinOm + sinW * cosOm * cosI) * xp + (-sinW * sinOm + cosW * cosOm * cosI) * yp;
  const ze = (sinW * sinI) * xp + (cosW * sinI) * yp;
  return { xe, ye, ze, r: Math.hypot(xe, ye, ze) };
}

function standishXyz(el, jd) {
  const T = (jd - kJ2000) / kDaysPerCentury;
  const a = el[0] + el[1] * T;
  const e = el[2] + el[3] * T;
  const I = el[4] + el[5] * T;
  const L = el[6] + el[7] * T;
  const wBar = el[8] + el[9] * T;
  const Om = el[10] + el[11] * T;
  const w = wBar - Om;
  const M = wrapDeg180(L - wBar);
  return fromOrbitalPlane(a, e, I, Om, w, M);
}

function keplerXyz(body, jd) {
  const M = wrapDeg180(body.M0Deg + body.nDegPerDay * (jd - body.epochJd));
  return fromOrbitalPlane(body.a, body.e, body.IDeg, body.OmDeg, body.wDeg, M);
}

function planetSceneAu(name, jd) {
  const xyz = name === 'pluto' ? keplerXyz(kPluto, jd) : standishXyz(kStandish[name], jd);
  // Scene Y-up: ecliptic x→X, ecliptic z→Y, ecliptic y→Z (OrbitLayout::GetOffset).
  return [xyz.xe, xyz.ze, xyz.ye];
}

function scaleFromSun(p, factor) {
  return p.map((c) => c * factor);
}

function lerpVec(a, b, t) {
  return [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t];
}

function catmullRom(p0, p1, p2, p3, t) {
  const t2 = t * t;
  const t3 = t2 * t;
  const out = [0, 0, 0];
  for (let i = 0; i < 3; ++i) {
    out[i] = 0.5 * (
      (2 * p1[i]) +
      (-p0[i] + p2[i]) * t +
      (2 * p0[i] - 5 * p1[i] + 4 * p2[i] - p3[i]) * t2 +
      (-p0[i] + 3 * p1[i] - 3 * p2[i] + p3[i]) * t3
    );
  }
  return out;
}

function sampleSpline(waypoints, count) {
  if (waypoints.length < 2) return waypoints.map((w) => [w.jd, ...w.p]);
  const samples = [];
  const nSeg = waypoints.length - 1;
  for (let i = 0; i < count; ++i) {
    const u = i / (count - 1);
    const f = u * nSeg;
    const seg = Math.min(nSeg - 1, Math.floor(f));
    const t = f - seg;
    const p0 = waypoints[Math.max(0, seg - 1)].p;
    const p1 = waypoints[seg].p;
    const p2 = waypoints[seg + 1].p;
    const p3 = waypoints[Math.min(waypoints.length - 1, seg + 2)].p;
    const jd = waypoints[seg].jd + t * (waypoints[seg + 1].jd - waypoints[seg].jd);
    const p = catmullRom(p0, p1, p2, p3, t);
    samples.push([
      Math.round(jd * 10000) / 10000,
      Math.round(p[0] * 1e5) / 1e5,
      Math.round(p[1] * 1e5) / 1e5,
      Math.round(p[2] * 1e5) / 1e5,
    ]);
  }
  return samples;
}

function outboundAt(jd, saturnJd, saturnP, lonDeg, latDeg, r0, rDotPerDay) {
  const dt = jd - saturnJd;
  const r = r0 + rDotPerDay * dt;
  const lon = lonDeg * kDegToRad;
  const lat = latDeg * kDegToRad;
  const cosLat = Math.cos(lat);
  // Blend outbound direction from Saturn's flyby heading toward the asymptotic ray.
  const asymptote = [
    r * Math.cos(lon) * cosLat,
    r * Math.sin(lat),
    r * Math.sin(lon) * cosLat,
  ];
  const heading = saturnP.map((c) => c / Math.hypot(...saturnP));
  const blend = Math.min(1, Math.max(0, dt / (20 * 365.25)));
  const dir = lerpVec(heading, asymptote.map((c) => c / Math.hypot(...asymptote)), blend);
  const len = Math.hypot(...dir) || 1;
  return dir.map((c) => (c / len) * r);
}

function buildVoyager1() {
  const launch = julianDateFromYmd(1977, 9, 5, 12.83);
  const jupiter = julianDateFromYmd(1979, 3, 5, 12);
  const saturn = julianDateFromYmd(1980, 11, 12, 23.93);
  const earthP = planetSceneAu('earth', launch);
  const jupP = scaleFromSun(planetSceneAu('jupiter', jupiter), 1.04);
  const satP = scaleFromSun(planetSceneAu('saturn', saturn), 1.06);
  const cruise1 = julianDateFromYmd(1978, 6, 1);
  const cruise2 = julianDateFromYmd(1980, 1, 1);
  const waypoints = [
    { jd: launch, p: scaleFromSun(earthP, 1.01) },
    { jd: cruise1, p: lerpVec(earthP, jupP, 0.42) },
    { jd: jupiter, p: jupP },
    { jd: cruise2, p: lerpVec(jupP, satP, 0.45) },
    { jd: saturn, p: satP },
  ];
  const outboundDates = [
    julianDateFromYmd(1983, 1, 1),
    julianDateFromYmd(1990, 2, 14),
    julianDateFromYmd(2004, 12, 16),
    julianDateFromYmd(2012, 8, 25),
    julianDateFromYmd(2026, 9, 15),
  ];
  const rSat = Math.hypot(...satP);
  for (const jd of outboundDates) {
    waypoints.push({
      jd,
      p: outboundAt(jd, saturn, satP, 254.5, 12.0, rSat, 0.0093),
    });
  }
  return sampleSpline(waypoints, 720);
}

function buildVoyager2() {
  const launch = julianDateFromYmd(1977, 8, 20, 14.48);
  const jupiter = julianDateFromYmd(1979, 7, 9, 22.29);
  const saturn = julianDateFromYmd(1981, 8, 26, 3.4);
  const uranus = julianDateFromYmd(1986, 1, 24, 17.99);
  const neptune = julianDateFromYmd(1989, 8, 25, 4);
  const earthP = planetSceneAu('earth', launch);
  const jupP = scaleFromSun(planetSceneAu('jupiter', jupiter), 1.04);
  const satP = scaleFromSun(planetSceneAu('saturn', saturn), 1.05);
  const uraP = scaleFromSun(planetSceneAu('uranus', uranus), 1.04);
  const nepP = scaleFromSun(planetSceneAu('neptune', neptune), 1.05);
  const waypoints = [
    { jd: launch, p: scaleFromSun(earthP, 1.01) },
    { jd: julianDateFromYmd(1978, 8, 1), p: lerpVec(earthP, jupP, 0.5) },
    { jd: jupiter, p: jupP },
    { jd: julianDateFromYmd(1980, 8, 1), p: lerpVec(jupP, satP, 0.5) },
    { jd: saturn, p: satP },
    { jd: julianDateFromYmd(1984, 1, 1), p: lerpVec(satP, uraP, 0.5) },
    { jd: uranus, p: uraP },
    { jd: julianDateFromYmd(1987, 10, 1), p: lerpVec(uraP, nepP, 0.5) },
    { jd: neptune, p: nepP },
    { jd: julianDateFromYmd(2000, 1, 1), p: outboundAt(julianDateFromYmd(2000, 1, 1), neptune, nepP, 298, -58, Math.hypot(...nepP), 0.0081) },
    { jd: julianDateFromYmd(2026, 9, 15), p: outboundAt(julianDateFromYmd(2026, 9, 15), neptune, nepP, 300, -59, Math.hypot(...nepP), 0.0081) },
  ];
  return sampleSpline(waypoints, 640);
}

const catalog = {
  version: 1,
  units: 'au',
  note: 'Sampled heliocentric positions in scene-axis AU (ecliptic x, z, y). Baked offline; not SPICE.',
  missions: [
    {
      id: 'voyager1',
      name: 'Voyager 1',
      color: [1.0, 0.78, 0.22],
      samples: buildVoyager1(),
    },
    {
      id: 'voyager2',
      name: 'Voyager 2',
      color: [0.45, 0.82, 1.0],
      samples: buildVoyager2(),
    },
  ],
};

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const outPath = join(root, 'resource/missions/catalog.json');
writeFileSync(outPath, `${JSON.stringify(catalog)}\n`);
const bytes = Buffer.byteLength(JSON.stringify(catalog));
console.log(`Wrote ${catalog.missions.length} missions, ${catalog.missions.map((m) => m.samples.length).join('+')} samples (${bytes} bytes) → ${outPath}`);
