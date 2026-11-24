import { describe, expect, it } from 'vitest';
import {
    cellAt,
    cellValue,
    colorRange,
    flightLinkQuery,
    flightTimeline,
    parseFlightLink,
    parseFlightPlanJson,
    parsePorkchopJson,
    porkchopColor,
    timeScaleForFlight,
    type FlightPlanReport,
    type PorkchopGrid,
} from './flight';
import { parseTimeline } from './timeline';

const grid = (): PorkchopGrid => ({
    departureBody: 3,
    arrivalBody: 4,
    departureStartJd: 2461000.5,
    departureStepDays: 10,
    departureSteps: 3,
    tofMinDays: 100,
    tofStepDays: 50,
    tofSteps: 2,
    tiles: 1,
    best: { departureJd: 2461010.5, tofDays: 150, c3: 9, vInfArrive: 3 },
    c3: [20, 16, null, 12, 9, 30],
    vInfArrive: [4, 3.5, null, 3.2, 3, 5],
});

describe('porkchop JSON', () => {
    it('accepts a grid whose arrays match its size and rejects one that does not', () => {
        expect(parsePorkchopJson(JSON.stringify(grid()))?.best?.c3).toBe(9);
        expect(parsePorkchopJson(JSON.stringify({ ...grid(), c3: [1] }))).toBeNull();
        expect(parsePorkchopJson('not json')).toBeNull();
    });
});

describe('porkchop cells', () => {
    it('colours by C3, arrival v∞, or their energy sum, null where there is no transfer', () => {
        const g = grid();
        expect(cellValue(g, 'c3', 4)).toBe(9);
        expect(cellValue(g, 'vinf', 4)).toBe(3);
        expect(cellValue(g, 'total', 4)).toBe(18);
        expect(cellValue(g, 'total', 2)).toBeNull();
        expect(colorRange(g, 'c3').min).toBe(9);
    });

    it('maps the cheapest value to the brightest colour and no-solution to grey', () => {
        const cheap = porkchopColor(0, 0, 10);
        const dear = porkchopColor(10, 0, 10);
        expect(cheap[0] + cheap[1] + cheap[2]).toBeGreaterThan(dear[0] + dear[1] + dear[2]);
        expect(porkchopColor(null, 0, 10)).toEqual([40, 40, 48, 255]);
        expect(porkchopColor(1e9, 0, 10)).toEqual(dear); // clamped
    });

    it('finds the cell under a pixel with flight time running up', () => {
        const g = grid();
        const bottomLeft = cellAt(g, 1, 99, 300, 100);
        expect(bottomLeft).toMatchObject({ departureIndex: 0, tofIndex: 0, tofDays: 100, index: 0 });
        const topRight = cellAt(g, 299, 1, 300, 100);
        expect(topRight).toMatchObject({ departureIndex: 2, tofIndex: 1, departureJd: 2461020.5, index: 5 });
        expect(cellAt(g, -1, 5, 300, 100)).toBeNull();
    });
});

describe('?flight= links', () => {
    it('parses a transfer with its date and flight time, and round-trips it', () => {
        const link = parseFlightLink(new URLSearchParams('flight=earth-mars&dep=2026-11-01&tof=210'));
        expect(link).toEqual({ kind: 'transfer', from: 'earth', to: 'mars', departure: '2026-11-01', tofDays: 210 });
        expect(link && flightLinkQuery(link)).toBe('flight=earth-mars&dep=2026-11-01&tof=210');
    });

    it('keeps a bare body pair (the porkchop opens) and drops malformed parts', () => {
        expect(parseFlightLink(new URLSearchParams('flight=Earth-Venus&dep=soon&tof=-3'))).toEqual({
            kind: 'transfer',
            from: 'earth',
            to: 'venus',
            departure: undefined,
            tofDays: undefined,
        });
    });

    it('reads anything else id-shaped as a preset, and nothing for junk or a body to itself', () => {
        expect(parseFlightLink(new URLSearchParams('flight=voyager2'))).toEqual({ kind: 'preset', id: 'voyager2' });
        expect(parseFlightLink(new URLSearchParams('flight=earth-earth'))).toEqual({
            kind: 'preset',
            id: 'earth-earth',
        });
        expect(parseFlightLink(new URLSearchParams('flight=<script>'))).toBeUndefined();
        expect(parseFlightLink(new URLSearchParams(''))).toBeUndefined();
    });
});

describe('flight playback', () => {
    it('flies a transfer in about 40 seconds of orrery time', () => {
        // timeScale 1 is a year per 120 s, so 210 days in 40 s is ~1.72.
        expect(timeScaleForFlight(210)).toBeCloseTo(210 / 365.25 / (40 / 120), 6);
        expect(timeScaleForFlight(0.1)).toBe(0.05);
    });

    const plan: FlightPlanReport = parseFlightPlanJson(
        JSON.stringify({
            ok: true,
            id: 'voyager2',
            name: 'Voyager 2',
            startJd: 2443376.1,
            endJd: 2447763.7,
            encounters: [
                {
                    kind: 'launch',
                    body: 3,
                    bodyName: 'Earth',
                    jd: 2443376.1,
                    date: '1977-08-20',
                    vInfIn: 0,
                    vInfOut: 10,
                    deltaV: 7,
                },
                {
                    kind: 'flyby',
                    body: 5,
                    bodyName: 'Jupiter',
                    jd: 2444064.4,
                    date: '1979-07-09',
                    vInfIn: 8,
                    vInfOut: 8,
                    deltaV: 0,
                },
                {
                    kind: 'arrival',
                    body: 8,
                    bodyName: 'Neptune',
                    jd: 2447763.7,
                    date: '1989-08-25',
                    vInfIn: 17,
                    vInfOut: 0,
                    deltaV: 0,
                },
            ],
        }),
    );

    it('turns a plan into a valid timeline that frames each encounter on its date', () => {
        const timeline = flightTimeline(plan, 10);
        expect(timeline).not.toBeNull();
        if (!timeline) return;
        const parsed = parseTimeline(JSON.parse(JSON.stringify(timeline)));
        expect(parsed.ok).toBe(true);
        expect(timeline.durationSec).toBe(20);
        const encounterKeys = timeline.keyframes.filter((key) => key.focus !== 'flight');
        expect(encounterKeys.map((key) => [key.focus, key.jd, key.t])).toEqual([
            ['earth', 2443376.1, 0],
            ['jupiter', 2444064.4, 10],
            ['neptune', 2447763.7, 20],
        ]);
        // Between encounters it rides the craft, dates increasing.
        const dates = timeline.keyframes.map((key) => key.jd ?? 0);
        expect([...dates].sort((a, b) => a - b)).toEqual(dates);
        expect(timeline.keyframes.some((key) => key.focus === 'flight')).toBe(true);
    });

    it('has no timeline for a failed plan', () => {
        expect(flightTimeline({ ok: false })).toBeNull();
        expect(parseFlightPlanJson('{').ok).toBe(false);
    });
});
