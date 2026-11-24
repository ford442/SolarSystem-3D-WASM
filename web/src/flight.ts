/**
 * Flight planner, the pure half (docs/ARCHITECTURE.md §14): the shapes C++ returns
 * (GetPorkchopJson, GetFlightPlanJson, GetFlightStateJson, GetFlightPresetsJson), the
 * porkchop heat-map's colour scale and cell lookup, the `?flight=` deep link, and the cinematic
 * timeline a planned flight becomes so capture.ts can record it. No DOM and no engine here:
 * flightPlanner.ts is the panel, and flight.test.ts covers this file under Vitest.
 */
import { julianDateFromIsoDate } from './ephemeris.js';
import type { Timeline, TimelineKeyframe } from './timeline.js';

/** Bodies the planner flies between: OrbitLayout indices and catalog ids (Flight::FindBody). */
export const FLIGHT_BODIES = [
    { index: 1, id: 'mercury', name: 'Mercury' },
    { index: 2, id: 'venus', name: 'Venus' },
    { index: 3, id: 'earth', name: 'Earth' },
    { index: 4, id: 'mars', name: 'Mars' },
    { index: 5, id: 'jupiter', name: 'Jupiter' },
    { index: 6, id: 'saturn', name: 'Saturn' },
    { index: 7, id: 'uranus', name: 'Uranus' },
    { index: 8, id: 'neptune', name: 'Neptune' },
    { index: 9, id: 'pluto', name: 'Pluto' },
    { index: 10, id: 'ceres', name: 'Ceres' },
    { index: 11, id: 'vesta', name: 'Vesta' },
] as const;

export type FlightBodyId = (typeof FLIGHT_BODIES)[number]['id'];

export function flightBodyByIndex(index: number): (typeof FLIGHT_BODIES)[number] | undefined {
    return FLIGHT_BODIES.find((body) => body.index === index);
}

export function flightBodyById(id: string): (typeof FLIGHT_BODIES)[number] | undefined {
    const lower = id.toLowerCase();
    return FLIGHT_BODIES.find((body) => body.id === lower);
}

export interface PorkchopRequest {
    from: number;
    to: number;
    departureStartJd: number;
    departureSpanDays: number;
    tofMinDays: number;
    tofMaxDays: number;
    departureSteps: number;
    tofSteps: number;
}

export interface PorkchopGrid {
    departureBody: number;
    arrivalBody: number;
    departureStartJd: number;
    departureStepDays: number;
    departureSteps: number;
    tofMinDays: number;
    tofStepDays: number;
    tofSteps: number;
    tiles: number;
    best: { departureJd: number; tofDays: number; c3: number; vInfArrive: number } | null;
    /** Row-major [tof][departure], km²/s²; null where Lambert has no solution. */
    c3: (number | null)[];
    /** Row-major [tof][departure], km/s. */
    vInfArrive: (number | null)[];
}

export interface FlightEncounter {
    kind: 'launch' | 'flyby' | 'arrival';
    body: number;
    bodyName: string;
    jd: number;
    date: string;
    vInfIn: number;
    vInfOut: number;
    deltaV: number;
    turnDeg?: number;
    altitudeKm?: number;
    bDotT?: number;
    bDotR?: number;
    feasible?: boolean;
    historicAltitudeKm?: number;
}

export interface FlightPlanReport {
    ok: boolean;
    error?: string;
    id?: string;
    name?: string;
    startJd?: number;
    endJd?: number;
    c3?: number;
    totalDeltaV?: number;
    legs?: { from: number; to: number; jd0: number; jd1: number; revolutions: number }[];
    encounters?: FlightEncounter[];
}

export interface FlightState {
    valid: boolean;
    id?: string;
    phase?: 'before-launch' | 'cruise' | 'arrived';
    leg?: number;
    toBody?: number;
    jd?: number;
    progress?: number;
    speedKmS?: number;
    sunDistanceAu?: number;
    targetDistanceKm?: number;
    chasing?: boolean;
}

export interface FlightPreset {
    id: string;
    name: string;
    sequence: string;
    startJd: number;
    endJd: number;
}

function parseJson(json: string): unknown {
    try {
        return JSON.parse(json) as unknown;
    } catch {
        return null;
    }
}

const isRecord = (value: unknown): value is Record<string, unknown> => typeof value === 'object' && value !== null;

export function parsePorkchopJson(json: string): PorkchopGrid | null {
    const raw = parseJson(json);
    if (!isRecord(raw) || !Array.isArray(raw.c3) || !Array.isArray(raw.vInfArrive)) {
        return null;
    }
    const grid = raw as unknown as PorkchopGrid;
    if (grid.c3.length !== grid.departureSteps * grid.tofSteps || grid.vInfArrive.length !== grid.c3.length) {
        return null;
    }
    return grid;
}

export function parseFlightPlanJson(json: string): FlightPlanReport {
    const raw = parseJson(json);
    return isRecord(raw) && typeof raw.ok === 'boolean' ? (raw as unknown as FlightPlanReport) : { ok: false };
}

export function parseFlightStateJson(json: string): FlightState {
    const raw = parseJson(json);
    return isRecord(raw) && typeof raw.valid === 'boolean' ? (raw as unknown as FlightState) : { valid: false };
}

export function parseFlightPresetsJson(json: string): FlightPreset[] {
    const raw = parseJson(json);
    return Array.isArray(raw)
        ? raw.filter((entry): entry is FlightPreset => isRecord(entry) && typeof entry.id === 'string')
        : [];
}

// --- Heat-map ------------------------------------------------------------------------------

/** The value a cell is coloured by: departure C3, arrival v∞, or their sum as excess energy. */
export type PorkchopMetric = 'c3' | 'vinf' | 'total';

export function cellValue(grid: PorkchopGrid, metric: PorkchopMetric, index: number): number | null {
    const c3 = grid.c3.at(index) ?? null;
    const vInf = grid.vInfArrive.at(index) ?? null;
    if (c3 === null || vInf === null) {
        return null;
    }
    if (metric === 'c3') return c3;
    if (metric === 'vinf') return vInf;
    return c3 + vInf * vInf;
}

/**
 * The colour range: from the grid's minimum up to a few times it (a porkchop's interesting
 * contours sit near the optimum; the far corners run to hundreds and would wash it out).
 */
export function colorRange(grid: PorkchopGrid, metric: PorkchopMetric): { min: number; max: number } {
    let min = Infinity;
    for (let i = 0; i < grid.c3.length; ++i) {
        const value = cellValue(grid, metric, i);
        if (value !== null && value < min) min = value;
    }
    if (!Number.isFinite(min)) return { min: 0, max: 1 };
    const max = metric === 'vinf' ? min + 4 : Math.max(min * 3, min + 20);
    return { min, max };
}

/**
 * Value → RGB on a perceptually ordered ramp (dark violet → teal → yellow, viridis-like), so
 * the cheap valley reads as the brightest region. `null` (no solution) is transparent grey.
 */
export function porkchopColor(value: number | null, min: number, max: number): [number, number, number, number] {
    if (value === null) return [40, 40, 48, 255];
    const stops: [number, number, number][] = [
        [253, 231, 37],
        [94, 201, 98],
        [33, 145, 140],
        [59, 82, 139],
        [68, 1, 84],
    ];
    const t = Math.min(1, Math.max(0, (value - min) / Math.max(1e-9, max - min)));
    const scaled = t * (stops.length - 1);
    const i = Math.min(stops.length - 2, Math.floor(scaled));
    const f = scaled - i;
    const a = stops[i];
    const b = stops[i + 1];
    return [
        Math.round(a[0] + (b[0] - a[0]) * f),
        Math.round(a[1] + (b[1] - a[1]) * f),
        Math.round(a[2] + (b[2] - a[2]) * f),
        255,
    ];
}

/** The cell under canvas pixel (x, y) of a width × height canvas: departure runs right, TOF up. */
export function cellAt(
    grid: PorkchopGrid,
    x: number,
    y: number,
    width: number,
    height: number,
): { departureIndex: number; tofIndex: number; departureJd: number; tofDays: number; index: number } | null {
    if (x < 0 || y < 0 || x >= width || y >= height) return null;
    const departureIndex = Math.min(grid.departureSteps - 1, Math.floor((x / width) * grid.departureSteps));
    const tofIndex = Math.min(grid.tofSteps - 1, Math.floor(((height - y) / height) * grid.tofSteps));
    return {
        departureIndex,
        tofIndex,
        departureJd: grid.departureStartJd + departureIndex * grid.departureStepDays,
        tofDays: grid.tofMinDays + tofIndex * grid.tofStepDays,
        index: tofIndex * grid.departureSteps + departureIndex,
    };
}

// --- Deep link -----------------------------------------------------------------------------

export type FlightLink =
    | { kind: 'transfer'; from: FlightBodyId; to: FlightBodyId; departure?: string; tofDays?: number }
    | { kind: 'preset'; id: string };

/**
 * `?flight=earth-mars&dep=2026-11-01&tof=210` (a transfer; without dep/tof the planner opens
 * on the porkchop instead of flying) or `?flight=voyager2` (a preset; C++ knows which ids
 * exist and ignores an unknown one).
 */
export function parseFlightLink(params: URLSearchParams): FlightLink | undefined {
    const value = params.get('flight')?.trim().toLowerCase();
    if (!value) return undefined;
    const pair = /^([a-z]+)-([a-z]+)$/.exec(value);
    const from = pair ? flightBodyById(pair[1]) : undefined;
    const to = pair ? flightBodyById(pair[2]) : undefined;
    if (from && to && from.id !== to.id) {
        const dep = params.get('dep');
        const tof = Number(params.get('tof'));
        return {
            kind: 'transfer',
            from: from.id,
            to: to.id,
            departure: dep && /^\d{4}-\d{2}-\d{2}$/.test(dep) ? dep : undefined,
            tofDays: Number.isFinite(tof) && tof >= 1 && tof <= 20000 ? tof : undefined,
        };
    }
    return /^[a-z][a-z0-9-]{0,31}$/.test(value) ? { kind: 'preset', id: value } : undefined;
}

/** The query a planned transfer shares as. */
export function flightLinkQuery(link: FlightLink): string {
    if (link.kind === 'preset') return `flight=${encodeURIComponent(link.id)}`;
    const parts = [`flight=${link.from}-${link.to}`];
    if (link.departure) parts.push(`dep=${link.departure}`);
    if (link.tofDays !== undefined) parts.push(`tof=${String(Math.round(link.tofDays * 10) / 10)}`);
    return parts.join('&');
}

export function departureJdFromLink(link: FlightLink): number | undefined {
    return link.kind === 'transfer' && link.departure ? julianDateFromIsoDate(link.departure) : undefined;
}

/**
 * The orrery time scale (1 = a year per 120 s) that flies `days` in about `seconds` of wall
 * time, so a 210-day transfer and a 12-year tour both play in under a minute.
 */
export function timeScaleForFlight(days: number, seconds = 40): number {
    const yearsPerSecondAt1 = 1 / 120;
    return Math.max(0.05, days / 365.25 / (seconds * yearsPerSecondAt1));
}

// --- Timeline ------------------------------------------------------------------------------

const ENCOUNTER_CAMERA: TimelineKeyframe['camera'] = { position: [1.2, 0.8, 3.5], units: 'radii' };
const CHASE_CAMERA: TimelineKeyframe['camera'] = { position: [-6, 4, 14], units: 'scene' };

/**
 * A planned flight as a cinematic timeline: at each encounter the camera frames the body (a few
 * radii off), and between them it rides with the craft (`focus: "flight"`, which the scene
 * state resolves to the craft's position at the evaluated date). Each leg gets `secondsPerLeg`
 * of the video, so a five-leg tour does not race through its later legs. Dates are linear in
 * time within a leg, so the arrival key lands on the arrival date exactly.
 */
export function flightTimeline(plan: FlightPlanReport, secondsPerLeg = 12): Timeline | null {
    if (!plan.ok || !plan.encounters || plan.encounters.length < 2) return null;
    const encounters = plan.encounters;
    const keyframes: TimelineKeyframe[] = [];
    const approach = Math.min(2.5, secondsPerLeg * 0.2);
    let t = 0;
    for (let i = 0; i < encounters.length; ++i) {
        const here = encounters[i];
        const bodyId = flightBodyByIndex(here.body)?.id ?? 'sun';
        keyframes.push({
            t,
            jd: here.jd,
            dateEase: 'linear',
            focus: bodyId,
            camera: ENCOUNTER_CAMERA,
            caption: `${here.bodyName} · ${here.kind} · ${here.date}`,
        });
        const next = encounters.at(i + 1);
        if (!next) break;
        const span = next.jd - here.jd;
        const leave = approach / secondsPerLeg;
        keyframes.push({
            t: t + approach,
            jd: here.jd + span * leave,
            dateEase: 'linear',
            focus: 'flight',
            camera: CHASE_CAMERA,
            caption: `${plan.name ?? 'Flight'} · cruise to ${next.bodyName}`,
        });
        keyframes.push({
            t: t + secondsPerLeg - approach,
            jd: next.jd - span * leave,
            dateEase: 'linear',
            focus: 'flight',
            camera: CHASE_CAMERA,
        });
        t += secondsPerLeg;
    }
    return {
        id: `flight-${plan.id ?? 'transfer'}`,
        title: plan.name ?? 'Flight',
        description: `Planned flight, ${encounters.map((e) => e.bodyName).join(' → ')}`,
        durationSec: t,
        keyframes,
    };
}
