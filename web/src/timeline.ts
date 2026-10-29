/**
 * Cinematic timelines (docs/ARCHITECTURE.md §11.2): keyframed camera paths, dates, fields of
 * view and overlays. This module is the schema and the maths only; it never touches the
 * engine. timelinePlayer.ts samples the scene through the runtime and applies the result,
 * and timeline.test.ts covers everything here under Vitest.
 *
 * A keyframe's camera is an offset from a focus body (or a mission probe), and every control
 * point is re-resolved at the date being evaluated. "Two radii off Jupiter" therefore stays
 * two radii off Jupiter while the date track carries Jupiter along its orbit, and a segment
 * that changes focus blends between two moving bodies. Positions are splined in world space
 * (Kochanek–Bartels, Catmull-Rom by default) relative to the segment's first point, so the
 * numbers stay small, or, on a `polar` segment, as a direction plus the log of the height
 * above the drawn surface: a descent from a million kilometres to a metre is one smooth
 * exponential and cannot dip below the ground. Rotations are squad between fixed
 * quaternions, or a slerp between look-at / horizon targets evaluated at the current point.
 * The date is a monotone cubic through the keys' dates, with per-segment easing.
 */
import type { TourStepView } from './tourPlayer.js';
import type { SurfacePoint } from './wasmBridge.js';

export type Vec3 = [number, number, number];
/** Unit quaternion as [x, y, z, w]. Camera-local −Z is the view direction, +Y is up, +X right. */
export type Quat = [number, number, number, number];

/** How an offset is measured: scene units, the focus body's drawn radii, or kilometres. */
export type LengthUnits = 'scene' | 'radii' | 'km';
/**
 * Date easing for the segment that starts at a key. `smooth` (default) is the monotone cubic
 * through every key's date, so the date rate is continuous across keys; `step` holds the
 * key's date until the next key.
 */
export type DateEase = 'smooth' | 'linear' | 'ease-in' | 'ease-out' | 'ease-in-out' | 'step';
/** `spline`: Kochanek–Bartels in world space. `polar`: direction + log height around the focus. */
export type PathMode = 'spline' | 'polar';

export interface TimelineCamera {
    /** Offset from the focus body's centre, in `units`. With `altitudeKm` only its direction counts. */
    position: Vec3;
    units?: LengthUnits;
    /**
     * Axes for `position` and a point `lookAt`: ecliptic scene axes by default, or, given a
     * body id / `mission:<id>`, +Z towards that body from the focus, +Y towards ecliptic north
     * and +X completing a right-handed set ("on the Moon's limb as seen from Earth").
     */
    frame?: string;
    /** Height above the drawn surface along `position`, in the focus body's kilometres. */
    altitudeKm?: number;
    /** Fixed world orientation. Without it the camera looks at `lookAt`, along `horizon`, or at the focus. */
    rotation?: Quat;
    /** A body id / `mission:<id>`, or a point as an offset from the focus in `units`. */
    lookAt?: string | Vec3;
    /**
     * Up for `lookAt`: ecliptic north (default), or `radial` for a level horizon: away from the
     * focus centre, turning to the normal of the drawn facet below within ~0.02 radii of it.
     */
    up?: 'ecliptic' | 'radial';
    /**
     * Look along the local horizon of the focus body: azimuth from the Sun's bearing, clockwise
     * seen from above, and elevation above the horizon, in degrees. Up is as for `up: 'radial'`.
     */
    horizon?: { azimuthDeg: number; elevationDeg: number };
}

export interface TimelineOverlays {
    orbitLines?: boolean;
    magneticFields?: boolean;
    /** The engine's text HUD (hints and distance labels). */
    hud?: boolean;
}

export interface TimelineKeyframe {
    /** Seconds from the start of the timeline; strictly increasing. */
    t: number;
    /**
     * Julian Date at this key. A key without one advances from the previous key at the
     * `timeScale` in force, or, when no key has set one yet, is interpolated in time between
     * the dated keys around it.
     */
    jd?: number;
    dateEase?: DateEase;
    /** Body id from the catalog ("earth", "moon", …), `mission:<id>`, or "sun" (default). */
    focus?: string;
    camera: TimelineCamera;
    /** Path of the segment that starts here; `polar` needs the next key on the same focus. */
    path?: PathMode;
    /** Kochanek–Bartels parameters at this key, each in [-1, 1]; 0, 0, 0 is Catmull-Rom. */
    tension?: number;
    continuity?: number;
    bias?: number;
    /** Vertical field of view in degrees (the engine clamps to 1–45); carried forward. */
    fov?: number;
    /** Date rate from this key on, in the orrery's units (1 = a year per 120 s); carried forward. */
    timeScale?: number;
    /** Merged onto the overlays in force; a step at the key. */
    overlays?: TimelineOverlays;
    /** Caption from this key until the next key with a caption ("" clears it). */
    caption?: string;
}

export interface Timeline {
    id: string;
    title: string;
    description?: string;
    durationSec: number;
    /** Applied once when the timeline takes the camera (the tour view schema: orbitScale, quality, …). */
    view?: TourStepView;
    keyframes: TimelineKeyframe[];
}

/** Where a focus is at the evaluated date. */
export interface BodyState {
    /** Heliocentric scene position, double. */
    position: Vec3;
    /** Drawn equatorial radius, scene units (0 for a probe). */
    radius: number;
    /** True radius in km (0 for a probe): `km` offsets are body-relative so they hold on both scales. */
    radiusKm: number;
}

/** The scene at one date: what a timeline needs to resolve its keyframes. */
export interface SceneSample {
    body(focus: string): BodyState | null;
    /** The drawn surface along a world direction from the centre (the facets once loaded). */
    surface(focus: string, direction: Vec3): SurfacePoint;
}

export interface TimelineFrame {
    t: number;
    jd: number;
    /** Heliocentric camera position. */
    position: Vec3;
    rotation: Quat;
    yaw: number;
    pitch: number;
    roll: number;
    fov: number;
    overlays: Required<TimelineOverlays>;
    caption: string;
    /** Index of the key that starts the segment `t` is in. */
    segment: number;
}

/** Orrery date rate at timeScale 1 (OrbitLayout: kEarthYearDays per kEarthOrbitSecondsAt1x). */
export const DAYS_PER_SECOND_AT_1X = 365.25 / 120;
/** Kilometres per scene unit on realistic scale (SceneScale::kKmPerSceneUnit). */
export const KM_PER_SCENE_UNIT = 149597870.7 / 1900;
export const DEFAULT_FOV = 45;
export const DEFAULT_OVERLAYS: Required<TimelineOverlays> = { orbitLines: false, magneticFields: false, hud: false };
/** Lowest height a polar segment flies at, scene units (a hundredth of a millimetre on realistic). */
const MIN_ALTITUDE = 1e-13;
/** Heights (in drawn radii) between which `radial` up turns from the radial to the facet normal. */
const FACET_UP_BELOW = 0.002;
const RADIAL_UP_ABOVE = 0.02;
/** The engine camera has no defined yaw looking straight up or down; stay this far off. */
const MAX_PITCH_DEG = 89.9;
const ID_PATTERN = /^[a-z][a-z0-9-]{0,47}$/;

// --- Vectors and quaternions -----------------------------------------------------------------

const add = (a: Vec3, b: Vec3): Vec3 => [a[0] + b[0], a[1] + b[1], a[2] + b[2]];
const sub = (a: Vec3, b: Vec3): Vec3 => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
const scale = (a: Vec3, s: number): Vec3 => [a[0] * s, a[1] * s, a[2] * s];
const dot = (a: Vec3, b: Vec3): number => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const cross = (a: Vec3, b: Vec3): Vec3 => [
    a[1] * b[2] - a[2] * b[1],
    a[2] * b[0] - a[0] * b[2],
    a[0] * b[1] - a[1] * b[0],
];
const length = (a: Vec3): number => Math.hypot(a[0], a[1], a[2]);
const DEG = Math.PI / 180;

function normalizeOr(a: Vec3, fallback: Vec3): Vec3 {
    const l = length(a);
    return l > 0 ? scale(a, 1 / l) : fallback;
}

function quatMultiply(a: Quat, b: Quat): Quat {
    return [
        a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1],
        a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
        a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3],
        a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2],
    ];
}

const quatConjugate = (q: Quat): Quat => [-q[0], -q[1], -q[2], q[3]];
const quatDot = (a: Quat, b: Quat): number => a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
const quatNegate = (q: Quat): Quat => [-q[0], -q[1], -q[2], -q[3]];

export function quatNormalize(q: Quat): Quat {
    const l = Math.hypot(q[0], q[1], q[2], q[3]);
    return l > 0 ? [q[0] / l, q[1] / l, q[2] / l, q[3] / l] : [0, 0, 0, 1];
}

export function quatRotate(q: Quat, v: Vec3): Vec3 {
    const u: Vec3 = [q[0], q[1], q[2]];
    const t = scale(cross(u, v), 2);
    return add(add(v, scale(t, q[3])), cross(u, t));
}

/** Slerp along the arc from a to b as given (no hemisphere flip): squad relies on that. */
function slerpRaw(a: Quat, b: Quat, u: number): Quat {
    const cosine = Math.max(-1, Math.min(1, quatDot(a, b)));
    if (cosine > 0.9995) {
        return quatNormalize([
            a[0] + (b[0] - a[0]) * u,
            a[1] + (b[1] - a[1]) * u,
            a[2] + (b[2] - a[2]) * u,
            a[3] + (b[3] - a[3]) * u,
        ]);
    }
    const angle = Math.acos(cosine);
    const sine = Math.sin(angle);
    const wa = Math.sin((1 - u) * angle) / sine;
    const wb = Math.sin(u * angle) / sine;
    return [a[0] * wa + b[0] * wb, a[1] * wa + b[1] * wb, a[2] * wa + b[2] * wb, a[3] * wa + b[3] * wb];
}

/** Shortest-arc slerp. */
export function slerp(a: Quat, b: Quat, u: number): Quat {
    return slerpRaw(a, quatDot(a, b) < 0 ? quatNegate(b) : b, u);
}

function quatLog(q: Quat): Vec3 {
    const v: Vec3 = [q[0], q[1], q[2]];
    const s = length(v);
    if (s < 1e-12) {
        return [0, 0, 0];
    }
    return scale(v, Math.atan2(s, q[3]) / s);
}

function quatExp(v: Vec3): Quat {
    const angle = length(v);
    if (angle < 1e-12) {
        return [v[0], v[1], v[2], 1];
    }
    const s = Math.sin(angle) / angle;
    return [v[0] * s, v[1] * s, v[2] * s, Math.cos(angle)];
}

/** Squad inner control point for q between its neighbours (all on one hemisphere). */
function squadControl(previous: Quat, q: Quat, next: Quat): Quat {
    const inverse = quatConjugate(q);
    const a = quatLog(quatMultiply(inverse, next));
    const b = quatLog(quatMultiply(inverse, previous));
    return quatMultiply(q, quatExp(scale(add(a, b), -0.25)));
}

/**
 * Spherical cubic through q1 → q2 with q0 and q3 as neighbours: C1 across keys, unlike a
 * chain of slerps.
 */
export function squad(q0: Quat, q1: Quat, q2: Quat, q3: Quat, u: number): Quat {
    // Put the whole run on one hemisphere first; the inner slerps then must not flip.
    const b = quatDot(q0, q1) < 0 ? quatNegate(q1) : q1;
    const c = quatDot(b, q2) < 0 ? quatNegate(q2) : q2;
    const d = quatDot(c, q3) < 0 ? quatNegate(q3) : q3;
    const s1 = squadControl(q0, b, c);
    const s2 = squadControl(b, c, d);
    return quatNormalize(slerpRaw(slerpRaw(b, c, u), slerpRaw(s1, s2, u), 2 * u * (1 - u)));
}

/** Rotation whose −Z is `front` and whose +Y is as close to `upHint` as `front` allows. */
export function quatFromFrontUp(front: Vec3, upHint: Vec3): Quat {
    const f = normalizeOr(front, [0, 0, -1]);
    let right = cross(f, upHint);
    if (length(right) < 1e-9) {
        // Looking along the hint: any up will do, so take the one the engine would.
        right = cross(f, Math.abs(f[1]) < 0.9 ? [0, 1, 0] : [0, 0, -1]);
    }
    right = normalizeOr(right, [1, 0, 0]);
    const up = cross(right, f);
    const back = scale(f, -1);
    // Columns right, up, back: camera-local axes in world coordinates.
    const [m00, m10, m20] = right;
    const [m01, m11, m21] = up;
    const [m02, m12, m22] = back;
    const trace = m00 + m11 + m22;
    let q: Quat;
    if (trace > 0) {
        const s = Math.sqrt(trace + 1) * 2;
        q = [(m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25 * s];
    } else if (m00 > m11 && m00 > m22) {
        const s = Math.sqrt(1 + m00 - m11 - m22) * 2;
        q = [0.25 * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s];
    } else if (m11 > m22) {
        const s = Math.sqrt(1 + m11 - m00 - m22) * 2;
        q = [(m01 + m10) / s, 0.25 * s, (m12 + m21) / s, (m02 - m20) / s];
    } else {
        const s = Math.sqrt(1 + m22 - m00 - m11) * 2;
        q = [(m02 + m20) / s, (m12 + m21) / s, 0.25 * s, (m10 - m01) / s];
    }
    return quatNormalize(q);
}

/** The engine camera's front for a yaw/pitch (Camera::UpdateCameraVectors). */
function engineFront(yawDeg: number, pitchDeg: number): Vec3 {
    const yaw = yawDeg * DEG;
    const pitch = pitchDeg * DEG;
    return [Math.cos(yaw) * Math.cos(pitch), Math.sin(pitch), Math.sin(yaw) * Math.cos(pitch)];
}

/** The engine's up at roll 0: in the plane of front and ecliptic north. */
function engineLevelUp(front: Vec3): Vec3 {
    const right = normalizeOr(cross(front, [0, 1, 0]), [0, 0, 1]);
    return cross(right, front);
}

/** Orientation of the engine camera at yaw/pitch/roll (degrees). */
export function quatFromYawPitchRoll(yawDeg: number, pitchDeg: number, rollDeg = 0): Quat {
    const front = engineFront(yawDeg, pitchDeg);
    const level = engineLevelUp(front);
    // Camera::SetRoll turns up right-handed about front.
    const up = add(scale(level, Math.cos(rollDeg * DEG)), scale(cross(front, level), Math.sin(rollDeg * DEG)));
    return quatFromFrontUp(front, up);
}

/** The engine yaw/pitch/roll (degrees) that reproduce a camera orientation. */
export function yawPitchRollFromQuat(q: Quat): { yaw: number; pitch: number; roll: number } {
    const front = quatRotate(q, [0, 0, -1]);
    const up = quatRotate(q, [0, 1, 0]);
    const limit = Math.sin(MAX_PITCH_DEG * DEG);
    if (Math.abs(front[1]) > limit) {
        // Straight up or down: the heading is in the camera's up vector instead. Tilt to the
        // pitch limit along it so the engine's frame stays defined.
        const sign = Math.sign(front[1]);
        const heading = scale(up, -sign);
        return { yaw: Math.atan2(heading[2], heading[0]) / DEG, pitch: sign * MAX_PITCH_DEG, roll: 0 };
    }
    const pitch = Math.asin(front[1]) / DEG;
    const yaw = Math.atan2(front[2], front[0]) / DEG;
    const level = engineLevelUp(front);
    const roll = Math.atan2(dot(cross(level, up), front), dot(level, up)) / DEG;
    return { yaw, pitch, roll };
}

// --- Curves ----------------------------------------------------------------------------------

function hermite(p0: number, m0: number, p1: number, m1: number, u: number): number {
    const u2 = u * u;
    const u3 = u2 * u;
    return (2 * u3 - 3 * u2 + 1) * p0 + (u3 - 2 * u2 + u) * m0 + (-2 * u3 + 3 * u2) * p1 + (u3 - u2) * m1;
}

function hermiteVec(p0: Vec3, m0: Vec3, p1: Vec3, m1: Vec3, u: number): Vec3 {
    return [
        hermite(p0[0], m0[0], p1[0], m1[0], u),
        hermite(p0[1], m0[1], p1[1], m1[1], u),
        hermite(p0[2], m0[2], p1[2], m1[2], u),
    ];
}

export interface KbParams {
    tension?: number;
    continuity?: number;
    bias?: number;
}

/**
 * Kochanek–Bartels tangents at `current`, scaled for uneven key spacing so the curve is C1
 * in time, not just in the segment parameter: `outgoing` is per unit of the following
 * segment's parameter, `incoming` per unit of the preceding one's. A missing neighbour is
 * the reflection of the other one (a straight run-out at the ends).
 */
export function kbTangents(
    previous: Vec3 | null, current: Vec3, next: Vec3 | null,
    tPrevious: number, tCurrent: number, tNext: number,
    params: KbParams = {},
): { incoming: Vec3; outgoing: Vec3 } {
    if (!previous && !next) {
        return { incoming: [0, 0, 0], outgoing: [0, 0, 0] };
    }
    const prev = previous ?? sub(scale(current, 2), next as Vec3);
    const nxt = next ?? sub(scale(current, 2), prev);
    const h0 = previous ? tCurrent - tPrevious : tNext - tCurrent;
    const h1 = next ? tNext - tCurrent : tCurrent - tPrevious;
    const t = params.tension ?? 0;
    const c = params.continuity ?? 0;
    const b = params.bias ?? 0;
    const d0 = sub(current, prev);
    const d1 = sub(nxt, current);
    const outgoing = add(scale(d0, (1 - t) * (1 + c) * (1 + b) / 2), scale(d1, (1 - t) * (1 - c) * (1 - b) / 2));
    const incoming = add(scale(d0, (1 - t) * (1 - c) * (1 + b) / 2), scale(d1, (1 - t) * (1 + c) * (1 - b) / 2));
    const sum = h0 + h1;
    return {
        outgoing: scale(outgoing, sum > 0 ? 2 * h1 / sum : 1),
        incoming: scale(incoming, sum > 0 ? 2 * h0 / sum : 1),
    };
}

/**
 * Fritsch–Carlson slopes (per unit time) of the monotone cubic through (times, values): it
 * never overshoots between keys, so a date track never runs backwards and a descent never
 * dips under its lowest key.
 */
export function monotoneSlopes(times: readonly number[], values: readonly number[]): number[] {
    const n = values.length;
    if (n < 2) {
        return n === 1 ? [0] : [];
    }
    const secants: number[] = [];
    for (let k = 0; k < n - 1; ++k) {
        const h = times[k + 1] - times[k];
        secants.push(h > 0 ? (values[k + 1] - values[k]) / h : 0);
    }
    const slopes = new Array<number>(n);
    slopes[0] = secants[0];
    slopes[n - 1] = secants[n - 2];
    for (let k = 1; k < n - 1; ++k) {
        const s0 = secants[k - 1];
        const s1 = secants[k];
        if (s0 * s1 <= 0) {
            slopes[k] = 0;
            continue;
        }
        const h0 = times[k] - times[k - 1];
        const h1 = times[k + 1] - times[k];
        const w0 = 2 * h1 + h0;
        const w1 = h1 + 2 * h0;
        slopes[k] = (w0 + w1) / (w0 / s0 + w1 / s1);
    }
    return slopes;
}

function ease(kind: DateEase, u: number): number {
    switch (kind) {
    case 'linear': return u;
    case 'ease-in': return u * u * u;
    case 'ease-out': return 1 - (1 - u) ** 3;
    case 'ease-in-out': return u < 0.5 ? 4 * u * u * u : 1 - (-2 * u + 2) ** 3 / 2;
    case 'step': return 0;
    case 'smooth': return u;
    }
}

const smoothstep = (u: number): number => u * u * (3 - 2 * u);

// --- Parsing ---------------------------------------------------------------------------------

export type TimelineParseResult = { ok: true; timeline: Timeline } | { ok: false; error: string };

const isFiniteNumber = (value: unknown): value is number => typeof value === 'number' && Number.isFinite(value);

function isVec3(value: unknown): value is Vec3 {
    return Array.isArray(value) && value.length === 3 && value.every(isFiniteNumber);
}

function isQuat(value: unknown): value is Quat {
    return Array.isArray(value) && value.length === 4 && value.every(isFiniteNumber)
        && Math.hypot(...(value as number[])) > 1e-9;
}

const isFocus = (value: unknown): value is string =>
    typeof value === 'string' && (value.startsWith('mission:') ? ID_PATTERN.test(value.slice(8)) : ID_PATTERN.test(value));

function parseKeyframe(raw: unknown, index: number): TimelineKeyframe | string {
    if (typeof raw !== 'object' || raw === null) {
        return `keyframe ${index} is not an object`;
    }
    const record = raw as Record<string, unknown>;
    const where = `keyframe ${index}`;
    if (!isFiniteNumber(record.t) || record.t < 0) {
        return `${where}: t must be a number ≥ 0`;
    }
    if (typeof record.camera !== 'object' || record.camera === null) {
        return `${where}: camera is required`;
    }
    const rawCamera = record.camera as Record<string, unknown>;
    if (!isVec3(rawCamera.position)) {
        return `${where}: camera.position must be [x, y, z]`;
    }
    const camera: TimelineCamera = { position: rawCamera.position };
    if (rawCamera.units !== undefined) {
        if (rawCamera.units !== 'scene' && rawCamera.units !== 'radii' && rawCamera.units !== 'km') {
            return `${where}: camera.units must be scene, radii or km`;
        }
        camera.units = rawCamera.units;
    }
    if (rawCamera.frame !== undefined) {
        if (!isFocus(rawCamera.frame)) {
            return `${where}: camera.frame must be a body id or mission:<id>`;
        }
        camera.frame = rawCamera.frame;
    }
    if (rawCamera.altitudeKm !== undefined) {
        if (!isFiniteNumber(rawCamera.altitudeKm) || rawCamera.altitudeKm <= 0) {
            return `${where}: camera.altitudeKm must be a number > 0`;
        }
        camera.altitudeKm = rawCamera.altitudeKm;
    }
    if (rawCamera.rotation !== undefined) {
        if (!isQuat(rawCamera.rotation)) {
            return `${where}: camera.rotation must be a non-zero [x, y, z, w]`;
        }
        camera.rotation = quatNormalize(rawCamera.rotation);
    }
    if (rawCamera.lookAt !== undefined) {
        if (!isFocus(rawCamera.lookAt) && !isVec3(rawCamera.lookAt)) {
            return `${where}: camera.lookAt must be a body id, mission:<id> or [x, y, z]`;
        }
        camera.lookAt = rawCamera.lookAt;
    }
    if (rawCamera.up !== undefined) {
        if (rawCamera.up !== 'ecliptic' && rawCamera.up !== 'radial') {
            return `${where}: camera.up must be ecliptic or radial`;
        }
        camera.up = rawCamera.up;
    }
    if (rawCamera.horizon !== undefined) {
        const horizon = rawCamera.horizon as Record<string, unknown> | null;
        if (!horizon || !isFiniteNumber(horizon.azimuthDeg) || !isFiniteNumber(horizon.elevationDeg)) {
            return `${where}: camera.horizon needs azimuthDeg and elevationDeg`;
        }
        camera.horizon = { azimuthDeg: horizon.azimuthDeg, elevationDeg: horizon.elevationDeg };
    }
    const key: TimelineKeyframe = { t: record.t, camera };
    if (record.jd !== undefined) {
        if (!isFiniteNumber(record.jd)) {
            return `${where}: jd must be a number`;
        }
        key.jd = record.jd;
    }
    if (record.dateEase !== undefined) {
        if (!['smooth', 'linear', 'ease-in', 'ease-out', 'ease-in-out', 'step'].includes(record.dateEase as string)) {
            return `${where}: dateEase must be smooth, linear, ease-in, ease-out, ease-in-out or step`;
        }
        key.dateEase = record.dateEase as DateEase;
    }
    if (record.focus !== undefined) {
        if (!isFocus(record.focus)) {
            return `${where}: focus must be a body id or mission:<id>`;
        }
        key.focus = record.focus;
    }
    if (record.path !== undefined) {
        if (record.path !== 'spline' && record.path !== 'polar') {
            return `${where}: path must be spline or polar`;
        }
        key.path = record.path;
    }
    for (const name of ['tension', 'continuity', 'bias'] as const) {
        const value = record[name];
        if (value !== undefined) {
            if (!isFiniteNumber(value) || value < -1 || value > 1) {
                return `${where}: ${name} must be in [-1, 1]`;
            }
            key[name] = value;
        }
    }
    if (record.fov !== undefined) {
        if (!isFiniteNumber(record.fov) || record.fov <= 0 || record.fov >= 180) {
            return `${where}: fov must be in (0, 180) degrees`;
        }
        key.fov = record.fov;
    }
    if (record.timeScale !== undefined) {
        if (!isFiniteNumber(record.timeScale)) {
            return `${where}: timeScale must be a number`;
        }
        key.timeScale = record.timeScale;
    }
    if (record.overlays !== undefined) {
        const overlays = record.overlays as Record<string, unknown> | null;
        if (!overlays || typeof overlays !== 'object') {
            return `${where}: overlays must be an object`;
        }
        key.overlays = {};
        for (const name of ['orbitLines', 'magneticFields', 'hud'] as const) {
            if (overlays[name] !== undefined) {
                if (typeof overlays[name] !== 'boolean') {
                    return `${where}: overlays.${name} must be true or false`;
                }
                key.overlays[name] = overlays[name];
            }
        }
    }
    if (record.caption !== undefined) {
        if (typeof record.caption !== 'string') {
            return `${where}: caption must be a string`;
        }
        key.caption = record.caption;
    }
    return key;
}

/** Validate a timeline from JSON. Errors name the first problem found. */
export function parseTimeline(raw: unknown): TimelineParseResult {
    if (typeof raw !== 'object' || raw === null) {
        return { ok: false, error: 'timeline is not a JSON object' };
    }
    const record = raw as Record<string, unknown>;
    if (typeof record.id !== 'string' || !ID_PATTERN.test(record.id)) {
        return { ok: false, error: 'id must be lower-case letters, digits and dashes' };
    }
    if (!Array.isArray(record.keyframes) || record.keyframes.length === 0) {
        return { ok: false, error: 'keyframes must be a non-empty array' };
    }
    const keyframes: TimelineKeyframe[] = [];
    for (let i = 0; i < record.keyframes.length; ++i) {
        const key = parseKeyframe(record.keyframes[i], i);
        if (typeof key === 'string') {
            return { ok: false, error: key };
        }
        if (keyframes.length > 0 && key.t <= keyframes[keyframes.length - 1].t) {
            return { ok: false, error: `keyframe ${i}: t must be later than the previous key's` };
        }
        keyframes.push(key);
    }
    const last = keyframes[keyframes.length - 1].t;
    const durationSec = isFiniteNumber(record.durationSec) && record.durationSec >= last ? record.durationSec : last;
    const view = typeof record.view === 'object' && record.view !== null ? record.view as TourStepView : undefined;
    return {
        ok: true,
        timeline: {
            id: record.id,
            title: typeof record.title === 'string' && record.title ? record.title : record.id,
            description: typeof record.description === 'string' ? record.description : undefined,
            durationSec,
            view,
            keyframes,
        },
    };
}

// --- Evaluation ------------------------------------------------------------------------------

export interface TimelineEvaluator {
    readonly timeline: Timeline;
    /** Julian Date at `t`: set the engine to it before sampling the scene for frameAt. */
    dateAt(t: number): number;
    /** The camera at `t` in a scene sampled at dateAt(t); null while a focus is unknown (not loaded). */
    frameAt(t: number, scene: SceneSample): TimelineFrame | null;
}

interface Control {
    key: TimelineKeyframe;
    focus: string;
    center: Vec3;
    state: BodyState;
    /** The key's `frame` axes (x, y, z as world vectors) at this date. */
    axes: [Vec3, Vec3, Vec3];
    /** World position of the key at this date. */
    world: Vec3;
}

const ECLIPTIC_AXES: [Vec3, Vec3, Vec3] = [[1, 0, 0], [0, 1, 0], [0, 0, 1]];

/** Axes with +Z from `center` towards `target` and +Y as near ecliptic north as that allows. */
function frameAxes(center: Vec3, target: Vec3): [Vec3, Vec3, Vec3] {
    const z = normalizeOr(sub(target, center), [0, 0, 1]);
    let y = sub([0, 1, 0], scale(z, z[1]));
    if (length(y) < 1e-9) {
        y = sub([1, 0, 0], scale(z, z[0]));
    }
    y = normalizeOr(y, [1, 0, 0]);
    return [cross(y, z), y, z];
}

const inAxes = (axes: [Vec3, Vec3, Vec3], v: Vec3): Vec3 =>
    add(add(scale(axes[0], v[0]), scale(axes[1], v[1])), scale(axes[2], v[2]));

const focusOf = (key: TimelineKeyframe): string => key.focus ?? 'sun';

/** Scene units per `km` of offset from a body: body-relative, so it holds on compressed scale too. */
function kmScale(state: BodyState): number {
    return state.radius > 0 && state.radiusKm > 0 ? state.radius / state.radiusKm : 1 / KM_PER_SCENE_UNIT;
}

function unitScale(units: LengthUnits | undefined, state: BodyState): number {
    switch (units ?? 'scene') {
    case 'radii': return state.radius > 0 ? state.radius : 1;
    case 'km': return kmScale(state);
    default: return 1;
    }
}

function resolveControl(key: TimelineKeyframe, scene: SceneSample): Control | null {
    const focus = focusOf(key);
    const state = scene.body(focus);
    if (!state) {
        return null;
    }
    const camera = key.camera;
    let axes = ECLIPTIC_AXES;
    if (camera.frame) {
        const target = scene.body(camera.frame);
        if (!target) {
            return null;
        }
        axes = frameAxes(state.position, target.position);
    }
    let offset: Vec3;
    if (camera.altitudeKm !== undefined) {
        const direction = normalizeOr(inAxes(axes, camera.position), [0, 1, 0]);
        offset = scale(direction, scene.surface(focus, direction).radius + camera.altitudeKm * kmScale(state));
    } else {
        offset = scale(inAxes(axes, camera.position), unitScale(camera.units, state));
    }
    return { key, focus, center: state.position, state, axes, world: add(state.position, offset) };
}

/**
 * Up for a level horizon at `eye`: away from the focus centre high up, turning to the normal
 * of the facet below near the ground. The drawn surface there is that flat facet (hundreds
 * of km across on realistic Earth), which tilts a few degrees from the radial direction.
 */
function localUp(control: Control, eye: Vec3, scene: SceneSample): Vec3 {
    const offset = sub(eye, control.center);
    const radial = normalizeOr(offset, [0, 1, 0]);
    if (control.state.radius <= 0) {
        return radial;
    }
    const surface = scene.surface(control.focus, radial);
    const height = Math.max((length(offset) - surface.radius) / control.state.radius, Number.MIN_VALUE);
    const u = Math.log(height / FACET_UP_BELOW) / Math.log(RADIAL_UP_ABOVE / FACET_UP_BELOW);
    const toFacet = 1 - smoothstep(Math.min(1, Math.max(0, u)));
    return toFacet > 0 ? normalizeOr(add(scale(radial, 1 - toFacet), scale(surface.normal, toFacet)), radial) : radial;
}

/** A key's orientation for a camera standing at `eye`. */
function keyRotation(control: Control, eye: Vec3, scene: SceneSample): Quat {
    const camera = control.key.camera;
    if (camera.rotation) {
        return camera.rotation;
    }
    const radialUp = localUp(control, eye, scene);
    if (camera.horizon) {
        const sun = scene.body('sun')?.position ?? [0, 0, 0];
        // Azimuth 0 is the Sun's bearing on the local horizon; with the Sun overhead (or
        // underfoot), ecliptic north's, then +X's.
        const onHorizon = (direction: Vec3): Vec3 => sub(direction, scale(radialUp, dot(direction, radialUp)));
        let north = onHorizon(normalizeOr(sub(sun, eye), [0, 1, 0]));
        for (const fallback of [[0, 1, 0], [1, 0, 0]] as Vec3[]) {
            if (length(north) < 1e-6) {
                north = onHorizon(fallback);
            }
        }
        north = normalizeOr(north, [1, 0, 0]);
        const east = cross(north, radialUp);
        const azimuth = camera.horizon.azimuthDeg * DEG;
        const elevation = camera.horizon.elevationDeg * DEG;
        const level = add(scale(north, Math.cos(azimuth)), scale(east, Math.sin(azimuth)));
        const front = add(scale(level, Math.cos(elevation)), scale(radialUp, Math.sin(elevation)));
        return quatFromFrontUp(front, radialUp);
    }
    let target = control.center;
    if (typeof camera.lookAt === 'string') {
        target = scene.body(camera.lookAt)?.position ?? control.center;
    } else if (camera.lookAt) {
        target = add(control.center, scale(inAxes(control.axes, camera.lookAt), unitScale(camera.units, control.state)));
    }
    const up: Vec3 = camera.up === 'radial' ? radialUp : [0, 1, 0];
    return quatFromFrontUp(sub(target, eye), up);
}

/** Overlays and caption in force at key `index` (steps, carried forward). */
function stepState(keys: readonly TimelineKeyframe[], index: number): { overlays: Required<TimelineOverlays>; caption: string } {
    const overlays = { ...DEFAULT_OVERLAYS };
    let caption = '';
    for (let i = 0; i <= index; ++i) {
        Object.assign(overlays, keys[i].overlays);
        if (keys[i].caption !== undefined) {
            caption = keys[i].caption as string;
        }
    }
    return { overlays, caption };
}

export interface EvaluatorOptions {
    /** Date of the first key when it has no `jd`: usually the engine's date when playback starts. */
    startJd: number;
}

export function createTimelineEvaluator(timeline: Timeline, options: EvaluatorOptions): TimelineEvaluator {
    const keys = timeline.keyframes;
    const times = keys.map((key) => key.t);

    // Date track: every key gets a date (its own; the previous one advanced at the rate in
    // force; or, before any rate is set, its share of the way between the dated keys around
    // it), then a monotone cubic runs through them.
    const dates: number[] = new Array<number>(keys.length);
    let rate: number | null = null;
    let pending = -1; // first undated key still waiting for a later date or a rate
    const settle = (upTo: number, target: number | null): void => {
        // Undated keys pending..upTo: their share of the way to `target`, or held without one.
        const before = dates[pending - 1];
        for (let k = pending; k <= upTo; ++k) {
            dates[k] = target === null
                ? before
                : before + (target - before) * (times[k] - times[pending - 1]) / (times[upTo + 1] - times[pending - 1]);
        }
        pending = -1;
    };
    for (let i = 0; i < keys.length; ++i) {
        const jd = keys[i].jd;
        if (jd !== undefined) {
            dates[i] = jd;
            if (pending >= 0) {
                settle(i - 1, jd);
            }
        } else if (i === 0) {
            dates[0] = options.startJd;
        } else if (rate !== null) {
            dates[i] = dates[i - 1] + rate * DAYS_PER_SECOND_AT_1X * (times[i] - times[i - 1]);
        } else if (pending < 0) {
            pending = i;
        }
        if (keys[i].timeScale !== undefined) {
            if (pending >= 0) {
                settle(i, null);
            }
            rate = keys[i].timeScale as number;
        }
    }
    if (pending >= 0) {
        settle(keys.length - 1, null);
    }
    const jdSlopes = monotoneSlopes(times, dates);

    const fovs: number[] = [];
    for (let i = 0; i < keys.length; ++i) {
        fovs.push(keys[i].fov ?? (i > 0 ? fovs[i - 1] : DEFAULT_FOV));
    }
    const fovSlopes = monotoneSlopes(times, fovs);

    const steps = keys.map((_, index) => stepState(keys, index));

    /** Segment index and parameter for t; the last key holds after the end. */
    const locate = (t: number): { i: number; u: number } => {
        if (keys.length === 1 || t <= times[0]) {
            return { i: 0, u: 0 };
        }
        if (t >= times[keys.length - 1]) {
            return { i: keys.length - 2, u: 1 };
        }
        let i = 0;
        while (i < keys.length - 2 && times[i + 1] <= t) {
            ++i;
        }
        return { i, u: (t - times[i]) / (times[i + 1] - times[i]) };
    };

    const curve = (values: number[], slopes: number[], i: number, u: number, kind: DateEase): number => {
        if (keys.length === 1) {
            return values[0];
        }
        const h = times[i + 1] - times[i];
        if (kind === 'smooth') {
            return hermite(values[i], slopes[i] * h, values[i + 1], slopes[i + 1] * h, u);
        }
        return u >= 1 ? values[i + 1] : values[i] + (values[i + 1] - values[i]) * ease(kind, u);
    };

    const dateAt = (t: number): number => {
        const { i, u } = locate(t);
        return curve(dates, jdSlopes, i, u, keys[i].dateEase ?? 'smooth');
    };

    /** World position on segment i at u, from the controls resolved at this date. */
    const positionAt = (controls: Map<number, Control | null>, i: number, u: number, scene: SceneSample): Vec3 | null => {
        const c1 = controls.get(i) ?? null;
        const c2 = controls.get(i + 1) ?? null;
        if (!c1) {
            return null;
        }
        if (!c2) {
            return c1.world;
        }
        const c0 = controls.get(i - 1) ?? null;
        const c3 = controls.get(i + 2) ?? null;
        const t0 = times[i - 1] ?? 0;
        const t3 = times[i + 2] ?? 0;
        const [t1, t2] = [times[i], times[i + 1]];

        if (c1.key.path === 'polar' && c1.focus === c2.focus) {
            const center = c1.center;
            const sameFocus = (c: Control | null): Control | null => (c && c.focus === c1.focus ? c : null);
            const n0 = sameFocus(c0);
            const n3 = sameFocus(c3);
            const direction = (c: Control): Vec3 => normalizeOr(sub(c.world, center), [0, 1, 0]);
            const logHeight = (c: Control): number => {
                const d = direction(c);
                return Math.log(Math.max(length(sub(c.world, center)) - scene.surface(c.focus, d).radius, MIN_ALTITUDE));
            };
            const d1 = direction(c1);
            const d2 = direction(c2);
            const out1 = kbTangents(n0 ? direction(n0) : null, d1, d2, t0, t1, t2, c1.key).outgoing;
            const in2 = kbTangents(d1, d2, n3 ? direction(n3) : null, t1, t2, t3, c2.key).incoming;
            let d = hermiteVec(d1, out1, d2, in2, u);
            if (length(d) < 1e-6) {
                d = quatRotate(slerp(quatFromFrontUp(d1, [0, 1, 0]), quatFromFrontUp(d2, [0, 1, 0]), u), [0, 0, -1]);
            }
            d = normalizeOr(d, d1);
            // Height: monotone cubic in log space through the keys on this focus.
            const window = [n0, c1, c2, n3].filter((c): c is Control => c !== null);
            const windowTimes = [n0 ? t0 : null, t1, t2, n3 ? t3 : null].filter((v): v is number => v !== null);
            const heights = window.map(logHeight);
            const slopes = monotoneSlopes(windowTimes, heights);
            const k = n0 ? 1 : 0;
            const h = t2 - t1;
            const height = Math.exp(hermite(heights[k], slopes[k] * h, heights[k + 1], slopes[k + 1] * h, u));
            return add(center, scale(d, scene.surface(c1.focus, d).radius + height));
        }

        // Spline: relative to c1 so the cubic works on small numbers.
        const origin = c1.world;
        const p0 = c0 ? sub(c0.world, origin) : null;
        const p1: Vec3 = [0, 0, 0];
        const p2 = sub(c2.world, origin);
        const p3 = c3 ? sub(c3.world, origin) : null;
        const out1 = kbTangents(p0, p1, p2, t0, t1, t2, c1.key).outgoing;
        const in2 = kbTangents(p1, p2, p3, t1, t2, t3, c2.key).incoming;
        return add(origin, hermiteVec(p1, out1, p2, in2, u));
    };

    const frameAt = (t: number, scene: SceneSample): TimelineFrame | null => {
        const { i, u } = locate(t);
        const controls = new Map<number, Control | null>();
        for (let index = Math.max(0, i - 1); index <= Math.min(keys.length - 1, i + 2); ++index) {
            controls.set(index, resolveControl(keys[index], scene));
        }
        const position = positionAt(controls, i, u, scene);
        const c1 = controls.get(i) ?? null;
        if (!position || !c1 || (i + 1 < keys.length && !controls.get(i + 1))) {
            return null;
        }
        const c2 = controls.get(i + 1) ?? c1;

        let rotation: Quat;
        if (keys.length === 1) {
            rotation = keyRotation(c1, position, scene);
        } else if (c1.key.camera.rotation && c2.key.camera.rotation) {
            const neighbour = (c: Control | null | undefined, fallback: Quat): Quat =>
                c ? keyRotation(c, c.world, scene) : fallback;
            const q1 = c1.key.camera.rotation;
            const q2 = c2.key.camera.rotation;
            rotation = squad(neighbour(controls.get(i - 1), q1), q1, q2, neighbour(controls.get(i + 2), q2), u);
        } else {
            // Look-at and horizon keys are re-aimed from where the camera is now.
            rotation = slerp(keyRotation(c1, position, scene), keyRotation(c2, position, scene), smoothstep(u));
        }
        const angles = yawPitchRollFromQuat(rotation);
        const step = steps[u >= 1 ? Math.min(i + 1, keys.length - 1) : i];
        return {
            t,
            jd: dateAt(t),
            position,
            rotation,
            ...angles,
            fov: curve(fovs, fovSlopes, i, u, 'smooth'),
            overlays: step.overlays,
            caption: step.caption,
            segment: i,
        };
    };

    return { timeline, dateAt, frameAt };
}

/** Bodies and probes a timeline refers to (focus, frame and look-at targets), to preload before playing. */
export function referencedFoci(timeline: Timeline): string[] {
    const ids = new Set<string>();
    for (const key of timeline.keyframes) {
        ids.add(focusOf(key));
        if (typeof key.camera.lookAt === 'string') {
            ids.add(key.camera.lookAt);
        }
        if (key.camera.frame) {
            ids.add(key.camera.frame);
        }
    }
    return [...ids];
}

/**
 * A keyframe that reproduces a camera pose: offset from `focus` in `units`, the pose's
 * orientation as a quaternion, and the date. The editor's "add keyframe at current pose".
 */
export function keyframeFromPose(options: {
    t: number;
    jd: number;
    focus: string;
    units: LengthUnits;
    focusState: BodyState;
    position: Vec3;
    yaw: number;
    pitch: number;
    roll: number;
    fov: number;
}): TimelineKeyframe {
    const offset = scale(sub(options.position, options.focusState.position), 1 / unitScale(options.units, options.focusState));
    // Twelve significant digits keep a metre on realistic scale and the JSON readable.
    const tidy = (value: number): number => Number(value.toPrecision(12));
    const rotation = quatFromYawPitchRoll(options.yaw, options.pitch, options.roll).map(tidy) as Quat;
    const key: TimelineKeyframe = {
        t: Math.round(options.t * 100) / 100,
        jd: Math.round(options.jd * 1e6) / 1e6,
        focus: options.focus,
        camera: { position: offset.map(tidy) as Vec3, rotation },
        fov: Math.round(options.fov * 100) / 100,
    };
    if (options.units !== 'scene') {
        key.camera.units = options.units;
    }
    return key;
}
