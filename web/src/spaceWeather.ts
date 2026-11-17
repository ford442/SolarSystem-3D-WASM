import { isoDateFromJulianDate } from './ephemeris.js';

/**
 * Space weather as C++ reports it (GetSpaceWeatherJson, src/astro/SpaceWeather.cpp,
 * docs/ARCHITECTURE.md §13). Everything here is read from that payload: TypeScript formats
 * the numbers, it never derives one.
 */
export interface StormEvent {
    /** Index into the CME catalog (resource/spaceweather/cmes.json), for PlaySpaceWeatherEvent. */
    index: number;
    id: string;
    name: string;
    source: string;
    launchJd: number;
    /** Shock at Earth: observed when the catalog has it, else modelled. */
    arrivalJd: number;
    speedKmS: number;
    /** Peak Kp: observed when known, else modelled. */
    kpMax: number;
    /** kpMax is the observed peak (catalog `kpMax`), not the model's. */
    kpObserved: boolean;
}

export interface ActiveCme {
    /** -1 for a CME launched from the panel. */
    index: number;
    id: string;
    name: string;
    launchJd: number;
    arrivalJd: number;
    speedKmS: number;
    frontAu: number;
    frontSpeedKmS: number;
    kpMax: number;
    hitsEarth: boolean;
}

export interface BodyWeather {
    /** Focus index (OrbitLayout::Body). */
    index: number;
    id: string;
    name: string;
    rAu: number;
    /** Magnetopause standoff now, and in the quiet wind, in planet radii. */
    standoffRadii: number;
    quietStandoffRadii: number;
    windSpeedKmS: number;
    densityCm3: number;
    pressureNPa: number;
    kp: number;
    /** The auroral oval's equatorward edge at magnetic midnight, degrees of magnetic latitude. */
    ovalLatDeg: number;
    cmeStrength: number;
    storm: boolean;
}

export interface SpaceWeatherState {
    /** False outside the Solar System: the magnetospheres and CMEs are Sol's. */
    sol: boolean;
    solarWind: boolean;
    particles: number;
    julianDate: number;
    kp: number;
    /** < 0: Kp follows the wind. */
    kpOverride: number;
    sunspotNumber: number;
    cycle: number;
    cyclePhase: number;
    cme: ActiveCme | null;
    nextStorm: StormEvent | null;
    lastStorm: StormEvent | null;
    bodies: BodyWeather[];
}

export const EMPTY_SPACE_WEATHER: SpaceWeatherState = {
    sol: false,
    solarWind: false,
    particles: 0,
    julianDate: 0,
    kp: 0,
    kpOverride: -1,
    sunspotNumber: 0,
    cycle: 0,
    cyclePhase: 0,
    cme: null,
    nextStorm: null,
    lastStorm: null,
    bodies: [],
};

type JsonRecord = Record<string, unknown>;

function isRecord(value: unknown): value is JsonRecord {
    return typeof value === 'object' && value !== null && !Array.isArray(value);
}

function num(record: JsonRecord, key: string, fallback = 0): number {
    const value = record[key];
    return typeof value === 'number' && Number.isFinite(value) ? value : fallback;
}

function str(record: JsonRecord, key: string): string {
    const value = record[key];
    return typeof value === 'string' ? value : '';
}

function parseStorm(value: unknown): StormEvent | null {
    if (!isRecord(value)) {
        return null;
    }
    const index = num(value, 'index', -1);
    const launchJd = num(value, 'launchJd');
    if (index < 0 || launchJd <= 0) {
        return null;
    }
    return {
        index,
        id: str(value, 'id'),
        name: str(value, 'name'),
        source: str(value, 'source'),
        launchJd,
        arrivalJd: num(value, 'arrivalJd'),
        speedKmS: num(value, 'speedKmS'),
        kpMax: num(value, 'kpMax', -1),
        kpObserved: value.observedKp === true,
    };
}

function parseCme(value: unknown): ActiveCme | null {
    if (!isRecord(value)) {
        return null;
    }
    const launchJd = num(value, 'launchJd');
    if (launchJd <= 0) {
        return null;
    }
    return {
        index: num(value, 'index', -1),
        id: str(value, 'id'),
        name: str(value, 'name'),
        launchJd,
        arrivalJd: num(value, 'arrivalJd'),
        speedKmS: num(value, 'speedKmS'),
        frontAu: num(value, 'frontAu'),
        frontSpeedKmS: num(value, 'frontSpeedKmS'),
        kpMax: num(value, 'kpMax', -1),
        hitsEarth: value.hitsEarth === true,
    };
}

function parseBody(value: unknown): BodyWeather | null {
    if (!isRecord(value)) {
        return null;
    }
    const id = str(value, 'id');
    if (!id) {
        return null;
    }
    return {
        index: num(value, 'index', -1),
        id,
        name: str(value, 'name'),
        rAu: num(value, 'rAu'),
        standoffRadii: num(value, 'standoffRadii'),
        quietStandoffRadii: num(value, 'quietStandoffRadii'),
        windSpeedKmS: num(value, 'windSpeedKmS'),
        densityCm3: num(value, 'densityCm3'),
        pressureNPa: num(value, 'pressureNPa'),
        kp: num(value, 'kp'),
        ovalLatDeg: num(value, 'ovalLatDeg'),
        cmeStrength: num(value, 'cmeStrength'),
        storm: value.storm === true,
    };
}

/**
 * Parse one GetSpaceWeatherJson payload. A stale or partial build degrades to
 * EMPTY_SPACE_WEATHER (or drops the unreadable part) rather than throwing into a frame.
 */
export function parseSpaceWeatherJson(json: string): SpaceWeatherState {
    let raw: unknown;
    try {
        raw = JSON.parse(json);
    } catch {
        return EMPTY_SPACE_WEATHER;
    }
    if (!isRecord(raw) || raw.sol !== true) {
        return EMPTY_SPACE_WEATHER;
    }
    const bodies = Array.isArray(raw.bodies)
        ? raw.bodies.map(parseBody).filter((body): body is BodyWeather => body !== null)
        : [];
    return {
        sol: true,
        solarWind: raw.solarWind === true,
        particles: num(raw, 'particles'),
        julianDate: num(raw, 'jd'),
        kp: num(raw, 'kp'),
        kpOverride: num(raw, 'kpOverride', -1),
        sunspotNumber: num(raw, 'sunspotNumber'),
        cycle: num(raw, 'cycle'),
        cyclePhase: num(raw, 'cyclePhase'),
        cme: parseCme(raw.cme),
        nextStorm: parseStorm(raw.nextStorm),
        lastStorm: parseStorm(raw.lastStorm),
        bodies,
    };
}

/** Kp to one decimal, or the NOAA G-scale storm level when it is one (Kp 5 = G1 … 9 = G5). */
export function formatKp(kp: number): string {
    const value = Math.round(kp * 10) / 10;
    const g = Math.floor(value) - 4;
    return g >= 1 ? `Kp ${value.toFixed(1)} (G${Math.min(g, 5)})` : `Kp ${value.toFixed(1)}`;
}

/** The Kp override control's label. */
export function formatKpOverride(kpOverride: number): string {
    return kpOverride < 0 ? 'Auto' : `Kp ${kpOverride.toFixed(1)}`;
}

/**
 * The storm the chip offers: the next catalogued one, or past the catalog's end (today's date,
 * by default) the latest, so "Play storm" is there whatever the date.
 */
export function chipStorm(state: SpaceWeatherState): { storm: StormEvent; upcoming: boolean } | null {
    if (!state.sol) {
        return null;
    }
    if (state.nextStorm) {
        return { storm: state.nextStorm, upcoming: true };
    }
    return state.lastStorm ? { storm: state.lastStorm, upcoming: false } : null;
}

/** The chip's storm line: "Next geomagnetic storm: Halloween storms (I), 2003-10-29 (Kp 9.0 (G5))". */
export function formatStormChip(state: SpaceWeatherState): string {
    const offered = chipStorm(state);
    if (!offered) {
        return '';
    }
    const { storm, upcoming } = offered;
    const date = isoDateFromJulianDate(storm.arrivalJd > 0 ? storm.arrivalJd : storm.launchJd);
    const kp = storm.kpMax >= 0 ? ` (${formatKp(storm.kpMax)})` : '';
    return `${upcoming ? 'Next geomagnetic storm' : 'Latest catalogued storm'}: ${storm.name || storm.id}, ${date}${kp}`;
}

export interface StormCard {
    title: string;
    lines: string[];
}

/**
 * The educational card while a storm is on at Earth (Kp ≥ 5) or a CME is on its way there:
 * what the wind is doing to the magnetosphere, in C++'s numbers.
 */
export function stormCard(state: SpaceWeatherState): StormCard | null {
    if (!state.sol) {
        return null;
    }
    const earth = state.bodies.find((body) => body.id === 'earth');
    if (!earth) {
        return null;
    }
    const cme = state.cme;
    const incoming = cme !== null && cme.hitsEarth && cme.frontAu < earth.rAu && cme.frontAu > 0;
    if (!earth.storm && !incoming) {
        return null;
    }
    const lines: string[] = [];
    if (earth.storm) {
        lines.push(
            `${formatKp(earth.kp)}${state.kpOverride >= 0 ? ' (held by the Kp slider)' : ''}: the auroral oval reaches ${earth.ovalLatDeg.toFixed(0)}° magnetic latitude at midnight.`,
        );
        lines.push(
            `The magnetopause is ${earth.standoffRadii.toFixed(1)} Earth radii toward the Sun (${earth.quietStandoffRadii.toFixed(1)} in a quiet wind).`,
        );
        lines.push(
            `Solar wind at Earth: ${earth.windSpeedKmS.toFixed(0)} km/s, ${earth.densityCm3.toFixed(1)} protons/cm³, ${earth.pressureNPa.toFixed(1)} nPa.`,
        );
    }
    if (cme) {
        const name = cme.name || cme.id || 'A CME';
        if (incoming) {
            lines.push(
                `${name}: front at ${cme.frontAu.toFixed(2)} AU, ${cme.frontSpeedKmS.toFixed(0)} km/s; at Earth ${isoDateFromJulianDate(cme.arrivalJd)}.`,
            );
        } else {
            lines.push(`${name}, launched ${isoDateFromJulianDate(cme.launchJd)} at ${cme.speedKmS.toFixed(0)} km/s.`);
        }
    }
    return { title: earth.storm ? 'Geomagnetic storm' : 'CME on its way', lines };
}
