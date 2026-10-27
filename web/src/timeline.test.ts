import { describe, expect, it } from 'vitest';
import {
    DAYS_PER_SECOND_AT_1X,
    KM_PER_SCENE_UNIT,
    createTimelineEvaluator,
    keyframeFromPose,
    monotoneSlopes,
    parseTimeline,
    quatFromFrontUp,
    quatFromYawPitchRoll,
    quatRotate,
    slerp,
    squad,
    yawPitchRollFromQuat,
    type BodyState,
    type Quat,
    type SceneSample,
    type Timeline,
    type TimelineKeyframe,
    type Vec3,
} from './timeline.js';

const METRE = 1e-3 / KM_PER_SCENE_UNIT;

const sub = (a: Vec3, b: Vec3): Vec3 => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
const norm = (a: Vec3): number => Math.hypot(a[0], a[1], a[2]);
const dist = (a: Vec3, b: Vec3): number => norm(sub(a, b));
const dot = (a: Vec3, b: Vec3): number => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const unit = (a: Vec3): Vec3 => [a[0] / norm(a), a[1] / norm(a), a[2] / norm(a)];

function scene(
    bodies: Record<string, BodyState>,
    surfaceRadius?: (focus: string, direction: Vec3) => number,
    facetNormal?: (focus: string, direction: Vec3) => Vec3,
): SceneSample {
    return {
        body: (focus) => bodies[focus] ?? null,
        surface: (focus, direction) => ({
            radius: surfaceRadius ? surfaceRadius(focus, direction) : bodies[focus]?.radius ?? 0,
            normal: facetNormal ? facetNormal(focus, direction) : unit(direction),
        }),
    };
}

const SUN: BodyState = { position: [0, 0, 0], radius: 1, radiusKm: 695700 };
const atSun = scene({ sun: SUN });

function timeline(keyframes: TimelineKeyframe[]): Timeline {
    return { id: 'test', title: 'Test', durationSec: keyframes[keyframes.length - 1].t, keyframes };
}

const key = (t: number, position: Vec3, extra: Partial<TimelineKeyframe> = {}): TimelineKeyframe =>
    ({ t, camera: { position, rotation: [0, 0, 0, 1] }, ...extra });

function frame(tl: Timeline, t: number, sample: SceneSample = atSun, startJd = 2451545) {
    const result = createTimelineEvaluator(tl, { startJd }).frameAt(t, sample);
    expect(result).not.toBeNull();
    return result!;
}

describe('position splines', () => {
    it('pass through every key', () => {
        const tl = timeline([key(0, [0, 0, 0]), key(2, [10, 3, -1]), key(3, [12, -4, 6]), key(7, [-5, 0, 2])]);
        for (const k of tl.keyframes) {
            expect(dist(frame(tl, k.t).position, k.camera.position)).toBeLessThan(1e-12);
        }
    });

    it('reproduce uniform motion along a line even with uneven key spacing', () => {
        const velocity: Vec3 = [3, -1, 0.5];
        const at = (t: number): Vec3 => [velocity[0] * t, velocity[1] * t, velocity[2] * t];
        const tl = timeline([0, 0.5, 3, 3.25, 9].map((t) => key(t, at(t))));
        for (let t = 0; t <= 9; t += 0.37) {
            expect(dist(frame(tl, t).position, at(t))).toBeLessThan(1e-9);
        }
    });

    it('are C1 in time across a key with uneven spacing (Catmull-Rom default)', () => {
        const tl = timeline([key(0, [0, 0, 0]), key(1, [4, 2, 0]), key(5, [6, -3, 1]), key(6, [9, 0, 0])]);
        const h = 1e-5;
        for (const t of [1, 5]) {
            const before = sub(frame(tl, t).position, frame(tl, t - h).position).map((v) => v / h);
            const after = sub(frame(tl, t + h).position, frame(tl, t).position).map((v) => v / h);
            for (let axis = 0; axis < 3; ++axis) {
                expect(after[axis]).toBeCloseTo(before[axis], 3);
            }
        }
    });

    it('stop at a key with tension 1', () => {
        const tl = timeline([key(0, [0, 0, 0]), key(1, [5, 0, 0], { tension: 1 }), key(2, [5, 5, 0])]);
        const h = 1e-6;
        expect(dist(frame(tl, 1 + h).position, frame(tl, 1).position) / h).toBeLessThan(1e-3);
    });

    it('hold the last key after the end and the first before the start', () => {
        const tl = timeline([key(1, [1, 2, 3]), key(2, [4, 5, 6])]);
        expect(frame(tl, 0).position).toEqual([1, 2, 3]);
        expect(dist(frame(tl, 10).position, [4, 5, 6])).toBeLessThan(1e-12);
    });

    it('ride along with a moving focus body', () => {
        // Jupiter-like body moving along x as the date advances; keys are offsets from it.
        const tl = timeline([
            key(0, [0, 2, 0], { focus: 'jupiter', jd: 100, camera: { position: [0, 2, 0], units: 'radii' } }),
            key(10, [3, 0, 0], { focus: 'jupiter', jd: 200, camera: { position: [3, 0, 0], units: 'radii' } }),
        ]);
        const evaluator = createTimelineEvaluator(tl, { startJd: 0 });
        const bodyAt = (jd: number): BodyState => ({ position: [jd * 50, 7, -3], radius: 0.5, radiusKm: 69911 });
        for (const t of [0, 2.5, 5, 7.5, 10]) {
            const jd = evaluator.dateAt(t);
            const jupiter = bodyAt(jd);
            const result = evaluator.frameAt(t, scene({ sun: SUN, jupiter }));
            const offset = sub(result!.position, jupiter.position);
            // The offset is the radii-scaled spline between the keys, wherever Jupiter is.
            const reference = evaluator.frameAt(t, scene({ sun: SUN, jupiter: { ...jupiter, position: [0, 0, 0] } }));
            expect(dist(offset, reference!.position)).toBeLessThan(1e-9);
        }
        expect(dist(sub(evaluator.frameAt(10, scene({ sun: SUN, jupiter: bodyAt(200) }))!.position, bodyAt(200).position), [1.5, 0, 0]))
            .toBeLessThan(1e-12);
    });

    it('places offsets in a frame aimed at another body', () => {
        const earth: BodyState = { position: [100, 0, 0], radius: 2, radiusKm: 6371 };
        const moon: BodyState = { position: [100, 0, 30], radius: 0.5, radiusKm: 1737 };
        const sample = scene({ sun: SUN, earth, moon });
        // +Z of the frame points from the Moon to Earth (world -Z here), +Y is ecliptic north.
        const tl = timeline([{ t: 0, focus: 'moon', camera: { position: [0, 1, 2], units: 'radii', frame: 'earth', lookAt: 'earth' } }]);
        const f = frame(tl, 0, sample);
        expect(dist(f.position, [100, 0.5, 29])).toBeLessThan(1e-12);
        // Right-handed: x = y × z = (0,1,0) × (0,0,-1) = (-1,0,0).
        const side = timeline([{ t: 0, focus: 'moon', camera: { position: [1, 0, 0], units: 'radii', frame: 'earth' } }]);
        expect(dist(frame(side, 0, sample).position, [99.5, 0, 30])).toBeLessThan(1e-12);
    });

    it('waits (null) while a focus body is not loaded', () => {
        const tl = timeline([key(0, [0, 0, 0], { focus: 'moon' }), key(1, [1, 0, 0], { focus: 'moon' })]);
        expect(createTimelineEvaluator(tl, { startJd: 0 }).frameAt(0.5, atSun)).toBeNull();
    });
});

describe('polar descent', () => {
    const EARTH: BodyState = { position: [1900, 0, 0], radius: 6371 / KM_PER_SCENE_UNIT, radiusKm: 6371 };
    const sample = scene({ sun: SUN, earth: EARTH });
    const down = (t: number, altitudeKm: number, extra: Partial<TimelineKeyframe> = {}): TimelineKeyframe => ({
        t,
        focus: 'earth',
        path: 'polar',
        camera: { position: [0.2, 1, 0], altitudeKm, lookAt: 'earth' },
        ...extra,
    });
    const altitude = (position: Vec3): number => dist(position, EARTH.position) - EARTH.radius;
    const tl = timeline([down(0, 1e6), down(4, 1e3), down(8, 1), down(12, 1e-3)]);

    it('lands exactly a metre above the surface and never below it', () => {
        expect(altitude(frame(tl, 12, sample).position) / METRE).toBeCloseTo(1, 6);
        let previous = Infinity;
        for (let t = 0; t <= 12; t += 0.05) {
            const h = altitude(frame(tl, t, sample).position);
            expect(h).toBeGreaterThanOrEqual(METRE * (1 - 1e-9));
            expect(h).toBeLessThanOrEqual(previous * (1 + 1e-12));
            previous = h;
        }
    });

    it('is exponential: equal times take equal factors off the height', () => {
        // Keys a factor of 1000 apart every 4 s: 2 s in, the height is ~1000^0.5 down.
        const ratio = altitude(frame(tl, 0, sample).position) / altitude(frame(tl, 2, sample).position);
        expect(Math.log10(ratio)).toBeGreaterThan(1.2);
        expect(Math.log10(ratio)).toBeLessThan(1.8);
    });

    it('measures height above the facets where the surface is lower than the radius', () => {
        const faceted = scene({ sun: SUN, earth: EARTH }, () => EARTH.radius - 7 / KM_PER_SCENE_UNIT);
        const end = frame(tl, 12, faceted).position;
        expect((dist(end, EARTH.position) - (EARTH.radius - 7 / KM_PER_SCENE_UNIT)) / METRE).toBeCloseTo(1, 4);
    });

    it('keeps looking at the focus centre along the way', () => {
        for (const t of [0, 3, 6.5, 11]) {
            const f = frame(tl, t, sample);
            const front = quatRotate(f.rotation, [0, 0, -1]);
            expect(dot(front, unit(sub(EARTH.position, f.position)))).toBeGreaterThan(1 - 1e-9);
        }
    });

    it('levels the horizon with radial up', () => {
        const horizon = timeline([
            down(0, 1, { camera: { position: [0.2, 1, 0.4], altitudeKm: 0.001, horizon: { azimuthDeg: 30, elevationDeg: 0 } } }),
        ]);
        const f = frame(horizon, 0, sample);
        const radial = unit(sub(f.position, EARTH.position));
        expect(Math.abs(dot(quatRotate(f.rotation, [0, 0, -1]), radial))).toBeLessThan(1e-9);
        expect(dot(quatRotate(f.rotation, [0, 1, 0]), radial)).toBeGreaterThan(1 - 1e-9);
        // The engine reproduces the same frame from yaw, pitch and roll.
        const engine = quatFromYawPitchRoll(f.yaw, f.pitch, f.roll);
        expect(dot(quatRotate(engine, [0, 1, 0]), radial)).toBeGreaterThan(1 - 1e-6);
    });

    it('levels it on the facet below near the ground, on the radial direction high up', () => {
        // A facet tilted 3° from the radial direction, as sphere.obj's are between vertices.
        const tilt = 3 * (Math.PI / 180);
        const tilted = (direction: Vec3): Vec3 => {
            const d = unit(direction);
            const side = unit([-d[2], 0, d[0]]);
            return unit([
                d[0] * Math.cos(tilt) + side[0] * Math.sin(tilt),
                d[1] * Math.cos(tilt) + side[1] * Math.sin(tilt),
                d[2] * Math.cos(tilt) + side[2] * Math.sin(tilt),
            ]);
        };
        const faceted = scene({ sun: SUN, earth: EARTH }, undefined, (_focus, direction) => tilted(direction));
        const at = (altitudeKm: number) => frame(timeline([down(0, altitudeKm, {
            camera: { position: [0.2, 1, 0.4], altitudeKm, horizon: { azimuthDeg: 30, elevationDeg: 0 } },
        })]), 0, faceted);

        const ground = at(0.001);
        const normal = tilted(sub(ground.position, EARTH.position));
        expect(dot(quatRotate(ground.rotation, [0, 1, 0]), normal)).toBeGreaterThan(1 - 1e-9);
        expect(Math.abs(dot(quatRotate(ground.rotation, [0, 0, -1]), normal))).toBeLessThan(1e-9);

        const high = at(400); // 0.06 radii up
        const radial = unit(sub(high.position, EARTH.position));
        expect(dot(quatRotate(high.rotation, [0, 1, 0]), radial)).toBeGreaterThan(1 - 1e-9);
    });
});

describe('rotations', () => {
    const about = (axis: Vec3, degrees: number): Quat => {
        const a = unit(axis);
        const s = Math.sin((degrees * Math.PI) / 360);
        return [a[0] * s, a[1] * s, a[2] * s, Math.cos((degrees * Math.PI) / 360)];
    };
    // Rotation angle between two orientations, from the chord on S³ (acos loses ~1e-8 rad near 1).
    const angleBetween = (a: Quat, b: Quat): number => {
        const minus = Math.hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2], a[3] - b[3]);
        const plus = Math.hypot(a[0] + b[0], a[1] + b[1], a[2] + b[2], a[3] + b[3]);
        return 4 * Math.asin(Math.min(1, Math.min(minus, plus) / 2));
    };

    it('squad hits its keys and stays unit length', () => {
        const qs = [about([0, 1, 0], 0), about([1, 1, 0], 40), about([0, 0, 1], 90), about([1, 0, 1], 170)];
        expect(angleBetween(squad(qs[0], qs[1], qs[2], qs[3], 0), qs[1])).toBeLessThan(1e-9);
        expect(angleBetween(squad(qs[0], qs[1], qs[2], qs[3], 1), qs[2])).toBeLessThan(1e-9);
        for (let u = 0; u <= 1; u += 0.1) {
            const q = squad(qs[0], qs[1], qs[2], qs[3], u);
            expect(Math.hypot(...q)).toBeCloseTo(1, 12);
        }
    });

    it('squad is a slerp for a steady turn, and ignores which hemisphere a key is on', () => {
        const qs = [0, 30, 60, 90].map((deg) => about([0.3, 1, 0.2], deg));
        for (let u = 0; u <= 1; u += 0.125) {
            expect(angleBetween(squad(qs[0], qs[1], qs[2], qs[3], u), slerp(qs[1], qs[2], u))).toBeLessThan(1e-9);
            const flipped = qs.map((q, i) => (i % 2 ? q.map((v) => -v) as Quat : q));
            expect(angleBetween(squad(flipped[0], flipped[1], flipped[2], flipped[3], u), slerp(qs[1], qs[2], u)))
                .toBeLessThan(1e-9);
        }
    });

    it('fixed-quaternion keys turn smoothly through a key (squad, not a slerp chain)', () => {
        const tl = timeline([
            key(0, [0, 0, 0], { camera: { position: [0, 0, 0], rotation: about([0, 1, 0], 0) } }),
            key(1, [1, 0, 0], { camera: { position: [1, 0, 0], rotation: about([0, 1, 0], 45) } }),
            key(2, [2, 0, 0], { camera: { position: [2, 0, 0], rotation: about([1, 1, 0], 120) } }),
        ]);
        const h = 1e-4;
        const rate = (t0: number, t1: number) => angleBetween(frame(tl, t0).rotation, frame(tl, t1).rotation) / (t1 - t0);
        expect(rate(1, 1 + h)).toBeCloseTo(rate(1 - h, 1), 2);
    });

    it('converts to engine yaw/pitch/roll and back', () => {
        for (const yaw of [-170, -90, 0, 37, 145]) {
            for (const pitch of [-80, -20, 0, 45, 85]) {
                for (const roll of [-120, 0, 15, 179]) {
                    const q = quatFromYawPitchRoll(yaw, pitch, roll);
                    const back = yawPitchRollFromQuat(q);
                    expect(angleBetween(quatFromYawPitchRoll(back.yaw, back.pitch, back.roll), q)).toBeLessThan(1e-9);
                    expect(back.pitch).toBeCloseTo(pitch, 9);
                }
            }
        }
    });

    it('keeps a defined yaw looking straight down', () => {
        const q = quatFromFrontUp([0, -1, 0], [1, 0, 0]);
        const angles = yawPitchRollFromQuat(q);
        expect(angles.pitch).toBeCloseTo(-89.9, 9);
        expect(angles.yaw).toBeCloseTo(0, 6); // heading = the camera's up, +X
        expect(Number.isFinite(angles.roll)).toBe(true);
    });

    it('slerps between different look-at targets from where the camera is', () => {
        const bodies = scene({
            sun: SUN,
            mars: { position: [10, 0, 0], radius: 0.1, radiusKm: 3389.5 },
            venus: { position: [0, 0, 10], radius: 0.1, radiusKm: 6052 },
        });
        const tl = timeline([
            { t: 0, camera: { position: [0, 1, 0], lookAt: 'mars' } },
            { t: 2, camera: { position: [0, 1, 0], lookAt: 'venus' } },
        ]);
        const front = (t: number) => quatRotate(frame(tl, t, bodies).rotation, [0, 0, -1]);
        expect(dot(front(0), unit([10, -1, 0]))).toBeGreaterThan(1 - 1e-9);
        expect(dot(front(2), unit([0, -1, 10]))).toBeGreaterThan(1 - 1e-9);
        const mid = front(1);
        expect(dot(mid, unit([10, -1, 0]))).toBeCloseTo(dot(mid, unit([0, -1, 10])), 9);
    });
});

describe('date track', () => {
    it('hits anchored dates and never runs backwards between increasing ones', () => {
        const tl = timeline([key(0, [0, 0, 0], { jd: 100 }), key(1, [1, 0, 0], { jd: 101 }), key(5, [2, 0, 0], { jd: 3000 }),
            key(6, [3, 0, 0], { jd: 3000.5 })]);
        const evaluator = createTimelineEvaluator(tl, { startJd: 0 });
        expect(evaluator.dateAt(0)).toBe(100);
        expect(evaluator.dateAt(5)).toBeCloseTo(3000, 9);
        let previous = -Infinity;
        for (let t = 0; t <= 6; t += 0.01) {
            const jd = evaluator.dateAt(t);
            expect(jd).toBeGreaterThanOrEqual(previous);
            expect(jd).toBeLessThanOrEqual(3000.5 + 1e-9);
            previous = jd;
        }
    });

    it('starts at the current date and advances at timeScale where keys give none', () => {
        const tl = timeline([key(0, [0, 0, 0], { timeScale: 10 }), key(3, [1, 0, 0]), key(4, [2, 0, 0], { timeScale: 0 }),
            key(9, [3, 0, 0])]);
        const evaluator = createTimelineEvaluator(tl, { startJd: 2460000 });
        expect(evaluator.dateAt(0)).toBe(2460000);
        expect(evaluator.dateAt(3)).toBeCloseTo(2460000 + 30 * DAYS_PER_SECOND_AT_1X, 9);
        expect(evaluator.dateAt(1.5)).toBeCloseTo(2460000 + 15 * DAYS_PER_SECOND_AT_1X, 9); // steady, so linear
        expect(evaluator.dateAt(9)).toBeCloseTo(evaluator.dateAt(4), 9); // timeScale 0 holds the date
    });

    it('interpolates undated keys between dated ones until a timeScale is set', () => {
        const tl = timeline([key(0, [0, 0, 0], { jd: 1000 }), key(1, [1, 0, 0]), key(3, [2, 0, 0]), key(4, [3, 0, 0], { jd: 1400 }),
            key(5, [4, 0, 0], { timeScale: 1 }), key(7, [5, 0, 0])]);
        const evaluator = createTimelineEvaluator(tl, { startJd: 0 });
        expect(evaluator.dateAt(1)).toBeCloseTo(1100, 9);
        expect(evaluator.dateAt(3)).toBeCloseTo(1300, 9);
        expect(evaluator.dateAt(5)).toBeCloseTo(1400, 9); // held: nothing dated after, no rate yet
        expect(evaluator.dateAt(7)).toBeCloseTo(1400 + 2 * DAYS_PER_SECOND_AT_1X, 9);
    });

    it('eases or steps a segment on request', () => {
        const tl = timeline([key(0, [0, 0, 0], { jd: 0, dateEase: 'ease-in-out' }), key(2, [1, 0, 0], { jd: 100, dateEase: 'step' }),
            key(4, [2, 0, 0], { jd: 200, dateEase: 'linear' }), key(6, [3, 0, 0], { jd: 300 })]);
        const evaluator = createTimelineEvaluator(tl, { startJd: 0 });
        expect(evaluator.dateAt(1)).toBeCloseTo(50, 9);
        expect(evaluator.dateAt(0.2)).toBeLessThan(10 * 0.2 * 0.5);
        expect(evaluator.dateAt(3.9)).toBe(100);
        expect(evaluator.dateAt(4.5)).toBeCloseTo(225, 9);
    });

    it('monotone slopes are zero at a turning point', () => {
        expect(monotoneSlopes([0, 1, 2], [0, 5, 1])[1]).toBe(0);
        expect(monotoneSlopes([0, 1, 3], [0, 2, 6])).toEqual([2, 2, 2]);
    });
});

describe('fov, overlays and captions', () => {
    it('interpolates the field of view without overshoot and steps overlays', () => {
        const tl = timeline([
            key(0, [0, 0, 0], { fov: 45, overlays: { orbitLines: true }, caption: 'Hello' }),
            key(2, [1, 0, 0], { fov: 20 }),
            key(4, [2, 0, 0], { fov: 20, overlays: { orbitLines: false, magneticFields: true }, caption: '' }),
        ]);
        for (let t = 0; t <= 4; t += 0.1) {
            const fov = frame(tl, t).fov;
            expect(fov).toBeLessThanOrEqual(45 + 1e-9);
            expect(fov).toBeGreaterThanOrEqual(20 - 1e-9);
        }
        expect(frame(tl, 3).overlays).toEqual({ orbitLines: true, magneticFields: false, hud: false });
        expect(frame(tl, 3).caption).toBe('Hello');
        expect(frame(tl, 4).overlays).toEqual({ orbitLines: false, magneticFields: true, hud: false });
        expect(frame(tl, 4).caption).toBe('');
    });
});

describe('keyframes from poses', () => {
    it('reproduce the pose they were taken from', () => {
        const earth: BodyState = { position: [1234.5, 6.7, -890.1], radius: 0.0809, radiusKm: 6371 };
        const position: Vec3 = [1234.5 + 0.2, 6.7 + 0.01, -890.1 - 0.05];
        const key = keyframeFromPose({
            t: 0, jd: 2460409.26, focus: 'earth', units: 'radii', focusState: earth,
            position, yaw: 123, pitch: -12, roll: 7, fov: 30,
        });
        const f = frame(timeline([key]), 0, scene({ sun: SUN, earth }));
        expect(dist(f.position, position)).toBeLessThan(1e-10);
        expect(f.yaw).toBeCloseTo(123, 6);
        expect(f.pitch).toBeCloseTo(-12, 6);
        expect(f.roll).toBeCloseTo(7, 6);
        expect(f.fov).toBe(30);
    });
});

describe('parsing', () => {
    const valid = { id: 'demo', title: 'Demo', keyframes: [{ t: 0, camera: { position: [1, 2, 3] } }] };

    it('accepts a minimal timeline and fills defaults', () => {
        const result = parseTimeline(valid);
        expect(result.ok).toBe(true);
        if (result.ok) {
            expect(result.timeline.durationSec).toBe(0);
        }
    });

    it.each([
        [{ ...valid, id: 'Bad Id' }, /id/],
        [{ ...valid, keyframes: [] }, /non-empty/],
        [{ ...valid, keyframes: [{ t: 0, camera: { position: [1, 2] } }] }, /position/],
        [{ ...valid, keyframes: [{ t: 0, camera: { position: [0, 0, 0] } }, { t: 0, camera: { position: [0, 0, 0] } }] }, /later/],
        [{ ...valid, keyframes: [{ t: 0, focus: 'Earth!', camera: { position: [0, 0, 0] } }] }, /focus/],
        [{ ...valid, keyframes: [{ t: 0, camera: { position: [0, 0, 0], rotation: [0, 0, 0, 0] } }] }, /rotation/],
        [{ ...valid, keyframes: [{ t: 0, tension: 2, camera: { position: [0, 0, 0] } }] }, /tension/],
    ])('rejects %j', (raw, message) => {
        const result = parseTimeline(raw);
        expect(result.ok).toBe(false);
        if (!result.ok) {
            expect(result.error).toMatch(message);
        }
    });

    it('parses every shipped timeline', () => {
        const shipped = import.meta.glob<unknown>('../public/timelines/*.json', { eager: true, import: 'default' });
        const files = Object.keys(shipped);
        expect(files.length).toBeGreaterThanOrEqual(3);
        for (const path of files) {
            const result = parseTimeline(shipped[path]);
            expect(result.ok, `${path}: ${result.ok ? '' : result.error}`).toBe(true);
            if (result.ok) {
                expect(path.endsWith(`/${result.timeline.id}.json`)).toBe(true);
            }
        }
    });
});
