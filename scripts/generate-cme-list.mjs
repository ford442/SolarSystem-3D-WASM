#!/usr/bin/env node
/**
 * Generate resource/spaceweather/cmes.json: the coronal mass ejections the space-weather layer
 * plays (src/astro/SpaceWeather.h, docs/ARCHITECTURE.md §13).
 *
 * Two sources, merged and sorted by launch:
 *   - CURATED below: the storms worth jumping to, from Carrington (1859) on, with the
 *     observed shock arrival at Earth and peak Kp the model is calibrated against;
 *   - NASA's DONKI (CCMC Space Weather Database Of Notifications, Knowledge, Information),
 *     2010 to now: the most accurate CME analysis of each fast, wide CME, the interplanetary
 *     shock it drove at Earth (IPS) and the geomagnetic storm (GST) that followed.
 *
 * The DONKI rows are snapshotted in resource/spaceweather/archive/donki.json so the list
 * rebuilds without the network, and CI can check it:
 *
 *   node scripts/generate-cme-list.mjs            # fetch DONKI, write the snapshot and cmes.json
 *   node scripts/generate-cme-list.mjs --offline  # rebuild cmes.json from the snapshot
 *   node scripts/generate-cme-list.mjs --check    # exit 1 if cmes.json would change (offline)
 *
 * A failed fetch keeps the snapshot it had rather than dropping its rows.
 *
 * Every row: id, name, launch (ISO UTC, when the front is at launchRadiusRsun), speedKmS,
 * halfAngleDeg, latitudeDeg and longitudeDeg (Stonyhurst: 0 is the Earth-facing central
 * meridian, + is west, so + longitudes head the way Earth orbits), earthArrival and kpMax when
 * observed, source. The C++ side fits the drag-based model's γ to earthArrival and the
 * coupling to kpMax (SpaceWeather::Calibrate).
 */
import { existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join, relative } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const outputPath = join(root, 'resource/spaceweather/cmes.json');
const snapshotPath = join(root, 'resource/spaceweather/archive/donki.json');

const DONKI = 'https://kauai.ccmc.gsfc.nasa.gov/DONKI/WS/get';
const FIRST_DONKI_YEAR = 2010;
/** DONKI rows kept: fast enough to drive a shock, wide enough to be seen across the system. */
const MIN_SPEED_KMS = 900;
const MIN_HALF_ANGLE_DEG = 20;
/** A DONKI CME this close to a curated launch is the same event: the curated row wins. */
const SAME_EVENT_DAYS = 1.0;

/**
 * Launch is the flare or first coronagraph sighting (launchRadiusRsun 1 unless given); speeds
 * are LASCO's linear plane-of-sky fits where there are any. Source regions in Stonyhurst
 * coordinates (E is negative longitude). Arrivals are the storm sudden commencements.
 */
export const CURATED = [
  {
    id: '1859-09-01-carrington',
    name: 'Carrington Event',
    launch: '1859-09-01T11:18Z',
    speedKmS: 2380,
    halfAngleDeg: 60,
    latitudeDeg: 20,
    longitudeDeg: 12,
    earthArrival: '1859-09-02T04:50Z',
    kpMax: 9,
    notes: 'White-light flare seen by Carrington and Hodgson; 17.6 h transit. Speed is the mean transit speed; source position approximate.',
  },
  {
    id: '1989-03-10-quebec',
    name: 'March 1989 storm',
    launch: '1989-03-10T19:12Z',
    speedKmS: 1500,
    halfAngleDeg: 60,
    latitudeDeg: 31,
    longitudeDeg: -22,
    earthArrival: '1989-03-13T01:27Z',
    kpMax: 9,
    notes: 'X4.5 flare in NOAA 5395; the storm that blacked out Hydro-Québec. No coronagraph speed: estimated.',
  },
  {
    id: '2000-07-14-bastille',
    name: 'Bastille Day event',
    launch: '2000-07-14T10:54Z',
    speedKmS: 1674,
    halfAngleDeg: 70,
    latitudeDeg: 22,
    longitudeDeg: 7,
    earthArrival: '2000-07-15T14:37Z',
    kpMax: 8.67,
    notes: 'X5.7 flare in NOAA 9077; full halo.',
  },
  {
    id: '2003-10-28-halloween',
    name: 'Halloween storms (I)',
    launch: '2003-10-28T11:30Z',
    speedKmS: 2459,
    halfAngleDeg: 70,
    latitudeDeg: -16,
    longitudeDeg: -8,
    earthArrival: '2003-10-29T06:11Z',
    kpMax: 9,
    notes: 'X17.2 flare in NOAA 10486; 19 h transit.',
  },
  {
    id: '2003-10-29-halloween',
    name: 'Halloween storms (II)',
    launch: '2003-10-29T20:54Z',
    speedKmS: 2029,
    halfAngleDeg: 70,
    latitudeDeg: -15,
    longitudeDeg: 2,
    earthArrival: '2003-10-30T16:19Z',
    kpMax: 9,
    notes: 'X10 flare in NOAA 10486.',
  },
  {
    id: '2003-11-18-superstorm',
    name: 'November 2003 superstorm',
    launch: '2003-11-18T08:50Z',
    speedKmS: 1660,
    halfAngleDeg: 60,
    latitudeDeg: 0,
    longitudeDeg: -18,
    earthArrival: '2003-11-20T08:03Z',
    kpMax: 8.67,
    notes: 'M3.9 flare in NOAA 10501; the deepest Dst (−422 nT) of solar cycle 23.',
  },
  {
    id: '2012-07-23-stereo-a',
    name: 'July 2012 near miss',
    launch: '2012-07-23T02:08Z',
    speedKmS: 3050,
    halfAngleDeg: 45,
    latitudeDeg: 0,
    longitudeDeg: 125,
    notes: 'Carrington-class CME that hit STEREO-A, 121° west of Earth; it missed Earth by about a week of solar rotation.',
  },
  {
    id: '2015-03-15-st-patricks',
    name: "St. Patrick's Day storm",
    launch: '2015-03-15T01:48Z',
    speedKmS: 719,
    halfAngleDeg: 60,
    latitudeDeg: -22,
    longitudeDeg: 25,
    earthArrival: '2015-03-17T04:45Z',
    kpMax: 7.67,
    notes: 'C9 flare in NOAA 12297; the strongest storm of cycle 24.',
  },
  {
    id: '2017-09-06-x9',
    name: 'September 2017 storm',
    launch: '2017-09-06T12:24Z',
    speedKmS: 1571,
    halfAngleDeg: 60,
    latitudeDeg: -9,
    longitudeDeg: 34,
    earthArrival: '2017-09-07T23:00Z',
    kpMax: 8.33,
    notes: 'X9.3 flare in NOAA 12673, the largest of cycle 24.',
  },
  {
    id: '2024-05-08-gannon',
    name: 'Gannon storm (May 2024)',
    launch: '2024-05-08T12:24Z',
    speedKmS: 1100,
    halfAngleDeg: 60,
    latitudeDeg: -18,
    longitudeDeg: -5,
    earthArrival: '2024-05-10T16:36Z',
    kpMax: 9,
    notes: 'Several CMEs from NOAA 13664 (8–9 May) merged on the way; this row stands for the first arrival. Launch, speed and source are representative.',
  },
  {
    id: '2024-10-09-x18',
    name: 'October 2024 storm',
    launch: '2024-10-09T02:10Z',
    speedKmS: 1400,
    halfAngleDeg: 60,
    latitudeDeg: -13,
    longitudeDeg: 8,
    earthArrival: '2024-10-10T14:40Z',
    kpMax: 8.67,
    notes: 'X1.8 flare in NOAA 13848. Speed and source approximate.',
  },
];

const args = new Set(process.argv.slice(2));
const checkOnly = args.has('--check');
const offline = args.has('--offline') || checkOnly;
let dirty = false;

function stableStringify(value) {
  return `${JSON.stringify(value, null, 2)}\n`;
}

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

/** "2024-05-08T12:24Z" / "2024-05-08T12:24:00Z" / DONKI's "2024-05-08T12:24:00-CME-001" → ms. */
export function parseDonkiTime(text) {
  if (typeof text !== 'string') return Number.NaN;
  const match = /^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2})(?::(\d{2}))?/.exec(text);
  if (!match) return Number.NaN;
  const [, y, mo, d, h, mi, s] = match;
  return Date.UTC(Number(y), Number(mo) - 1, Number(d), Number(h), Number(mi), Number(s ?? 0));
}

export function isoMinutes(ms) {
  return `${new Date(ms).toISOString().slice(0, 16)}Z`;
}

const round = (value, digits) => {
  const scale = 10 ** digits;
  return Math.round(value * scale) / scale;
};

/** The CME activity ID an analysis or linked event refers to. */
function cmeIdOf(analysis) {
  return analysis.associatedCMEID ?? analysis.activityID ?? null;
}

/**
 * Turn the snapshot's DONKI rows into cmes.json rows: each CME's analysis, its Earth arrival
 * from the IPS that links it, and the peak Kp of the storm that links that IPS (or the CME).
 */
export function donkiEvents(snapshot) {
  const arrivals = new Map();
  for (const ips of snapshot.ips ?? []) {
    if (ips.location !== 'Earth') continue;
    const time = parseDonkiTime(ips.eventTime);
    for (const link of ips.linkedEvents ?? []) {
      if (!/-CME-/.test(link.activityID ?? '')) continue;
      const previous = arrivals.get(link.activityID);
      if (previous === undefined || time < previous.time) {
        arrivals.set(link.activityID, { time, ipsId: ips.activityID });
      }
    }
  }
  const storms = [];
  for (const gst of snapshot.gst ?? []) {
    const kp = Math.max(0, ...(gst.allKpIndex ?? []).map((k) => Number(k.kpIndex) || 0));
    storms.push({
      start: parseDonkiTime(gst.startTime),
      kp,
      links: new Set((gst.linkedEvents ?? []).map((l) => l.activityID)),
    });
  }

  const events = [];
  const seen = new Set();
  for (const analysis of snapshot.analyses ?? []) {
    const cmeId = cmeIdOf(analysis);
    if (!cmeId || seen.has(cmeId)) continue;
    const speed = Number(analysis.speed);
    const halfAngle = Number(analysis.halfAngle);
    const launch = parseDonkiTime(analysis.time21_5);
    if (!(speed >= MIN_SPEED_KMS) || !(halfAngle >= MIN_HALF_ANGLE_DEG) || Number.isNaN(launch)) continue;
    seen.add(cmeId);

    const row = {
      id: `donki-${cmeId}`,
      name: `CME ${isoMinutes(parseDonkiTime(cmeId)).slice(0, 10)}`,
      launch: isoMinutes(launch),
      launchRadiusRsun: 21.5,
      speedKmS: Math.round(speed),
      halfAngleDeg: round(halfAngle, 1),
      latitudeDeg: round(Number(analysis.latitude) || 0, 1),
      longitudeDeg: round(Number(analysis.longitude) || 0, 1),
    };
    const arrival = arrivals.get(cmeId);
    if (arrival && arrival.time > launch) {
      row.earthArrival = isoMinutes(arrival.time);
      const storm = storms
        .filter((s) => s.links.has(arrival.ipsId) || s.links.has(cmeId) ||
          (s.start >= arrival.time - 6 * 3600e3 && s.start <= arrival.time + 2 * 86400e3))
        .sort((a, b) => b.kp - a.kp)[0];
      if (storm && storm.kp > 0) row.kpMax = round(storm.kp, 2);
    }
    row.source = 'donki';
    events.push(row);
  }
  return events;
}

/** Curated rows plus the DONKI rows that are not one of them, sorted by launch. */
export function mergeEvents(curated, donki) {
  const curatedLaunches = curated.map((row) => parseDonkiTime(row.launch));
  const merged = curated.map((row) => {
    const { notes, ...rest } = row;
    return { ...rest, source: 'curated', ...(notes ? { notes } : {}) };
  });
  for (const row of donki) {
    const launch = parseDonkiTime(row.launch);
    const duplicate = curatedLaunches.some((t) => Math.abs(t - launch) < SAME_EVENT_DAYS * 86400e3);
    if (!duplicate) merged.push(row);
  }
  merged.sort((a, b) => parseDonkiTime(a.launch) - parseDonkiTime(b.launch) || a.id.localeCompare(b.id));
  return merged;
}

async function getJson(endpoint, query) {
  const url = `${DONKI}/${endpoint}?${new URLSearchParams(query)}`;
  const response = await fetch(url, { signal: AbortSignal.timeout(60_000) });
  if (!response.ok) {
    throw new Error(`DONKI ${endpoint} failed: HTTP ${response.status}`);
  }
  const text = await response.text();
  return text.trim() === '' ? [] : JSON.parse(text);
}

const pickAnalysis = (a) => ({
  associatedCMEID: a.associatedCMEID,
  time21_5: a.time21_5,
  latitude: a.latitude,
  longitude: a.longitude,
  halfAngle: a.halfAngle,
  speed: a.speed,
});
const pickLinks = (links) => (links ?? []).map((l) => ({ activityID: l.activityID }));

async function fetchDonki() {
  const snapshot = { fetched: new Date().toISOString().slice(0, 10), analyses: [], ips: [], gst: [] };
  const lastYear = new Date().getUTCFullYear();
  for (let year = FIRST_DONKI_YEAR; year <= lastYear; ++year) {
    const range = { startDate: `${year}-01-01`, endDate: `${year}-12-31` };
    const analyses = await getJson('CMEAnalysis', {
      ...range,
      mostAccurateOnly: 'true',
      speed: String(MIN_SPEED_KMS),
      halfAngle: String(MIN_HALF_ANGLE_DEG),
      catalog: 'ALL',
    });
    const ips = await getJson('IPS', { ...range, location: 'Earth', catalog: 'ALL' });
    const gst = await getJson('GST', range);
    snapshot.analyses.push(...analyses.map(pickAnalysis));
    snapshot.ips.push(...ips.map((e) => ({
      activityID: e.activityID,
      location: e.location,
      eventTime: e.eventTime,
      linkedEvents: pickLinks(e.linkedEvents),
    })));
    snapshot.gst.push(...gst.map((e) => ({
      gstID: e.gstID,
      startTime: e.startTime,
      allKpIndex: (e.allKpIndex ?? []).map((k) => ({ observedTime: k.observedTime, kpIndex: k.kpIndex })),
      linkedEvents: pickLinks(e.linkedEvents),
    })));
    console.log(`DONKI ${year}: ${analyses.length} CME analyses, ${ips.length} Earth shocks, ${gst.length} storms`);
  }
  return snapshot;
}

function readSnapshot() {
  if (!existsSync(snapshotPath)) {
    return { fetched: null, analyses: [], ips: [], gst: [] };
  }
  return JSON.parse(readFileSync(snapshotPath, 'utf8'));
}

async function main() {
  let snapshot = readSnapshot();
  if (!offline) {
    try {
      snapshot = await fetchDonki();
      write(snapshotPath, stableStringify(snapshot));
    } catch (error) {
      console.warn(`DONKI fetch failed (${error.message}); keeping the snapshot from ${snapshot.fetched ?? 'never'}`);
    }
  } else if (!existsSync(snapshotPath)) {
    write(snapshotPath, stableStringify(snapshot));
  }

  const donki = donkiEvents(snapshot);
  const events = mergeEvents(CURATED, donki);
  const document = {
    version: 1,
    note: 'Generated by scripts/generate-cme-list.mjs (curated storms + NASA DONKI). Do not edit by hand.',
    donkiFetched: snapshot.fetched,
    events,
  };
  write(outputPath, stableStringify(document));
  console.log(`${events.length} CMEs (${CURATED.length} curated, ${events.length - CURATED.length} from DONKI)`);

  if (checkOnly && dirty) {
    console.error('\nresource/spaceweather/cmes.json is out of date. Run:\n  node scripts/generate-cme-list.mjs --offline');
    process.exit(1);
  }
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  main().catch((error) => {
    console.error(error);
    process.exit(1);
  });
}
