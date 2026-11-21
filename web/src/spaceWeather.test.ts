import { describe, expect, it } from 'vitest';
import { isoDateFromJulianDate } from './ephemeris.js';
import {
    EMPTY_SPACE_WEATHER,
    formatKp,
    formatKpOverride,
    chipStorm,
    formatStormChip,
    parseSpaceWeatherJson,
    stormCard,
    type SpaceWeatherState,
} from './spaceWeather.js';

/** Shaped like Application::GetSpaceWeatherJson → SpaceWeather::State::ToJson (2003-10-29 ~09 UT). */
const HALLOWEEN = JSON.stringify({
    sol: true,
    solarWind: true,
    particles: 200000,
    auroraMode: 2,
    photosphereMode: 2,
    jd: 2452941.875,
    kp: 8.67,
    kpOverride: -1,
    sunspotNumber: 61.3,
    cycle: 23,
    cyclePhase: 0.6123,
    spotLatitudeDeg: 12.4,
    cme: {
        index: 3,
        launchJd: 2452940.958333,
        arrivalJd: 2452941.743056,
        speedKmS: 2459,
        frontAu: 1.3412,
        frontSpeedKmS: 1820,
        kpMax: 9,
        hitsEarth: true,
        id: '2003-10-28',
        name: 'Halloween storm (Oct 28)',
        source: 'curated',
    },
    nextStorm: {
        index: 4,
        launchJd: 2452941.354167,
        arrivalJd: 2452942.25,
        speedKmS: 2029,
        kpMax: 9,
        observedKp: true,
        id: '2003-10-29',
        name: 'Halloween storm (Oct 29)',
        source: 'curated',
    },
    lastStorm: null,
    bodies: [
        {
            index: 2,
            id: 'earth',
            name: 'Earth',
            rAu: 0.9934,
            standoffRadii: 5.61,
            quietStandoffRadii: 10.12,
            windSpeedKmS: 1650,
            densityCm3: 18.2,
            pressureNPa: 82.4,
            kp: 8.67,
            ovalLatDeg: 49.8,
            cmeStrength: 0.912,
            storm: true,
        },
        {
            index: 4,
            id: 'jupiter',
            name: 'Jupiter',
            rAu: 5.43,
            standoffRadii: 71.2,
            quietStandoffRadii: 71.2,
            windSpeedKmS: 410,
            densityCm3: 0.24,
            pressureNPa: 0.067,
            kp: 2,
            ovalLatDeg: 74.1,
            cmeStrength: 0,
            storm: false,
        },
    ],
});

function withEarth(state: SpaceWeatherState, earth: Partial<SpaceWeatherState['bodies'][number]>): SpaceWeatherState {
    return {
        ...state,
        bodies: state.bodies.map((body) => (body.id === 'earth' ? { ...body, ...earth } : body)),
    };
}

describe('parseSpaceWeatherJson', () => {
    it('reads every field C++ writes', () => {
        const state = parseSpaceWeatherJson(HALLOWEEN);
        expect(state).toMatchObject({
            sol: true,
            solarWind: true,
            particles: 200000,
            julianDate: 2452941.875,
            kp: 8.67,
            kpOverride: -1,
            cycle: 23,
        });
        expect(state.cme).toMatchObject({
            index: 3,
            hitsEarth: true,
            frontAu: 1.3412,
            name: 'Halloween storm (Oct 28)',
        });
        expect(state.nextStorm).toMatchObject({ index: 4, kpMax: 9, kpObserved: true, source: 'curated' });
        expect(state.lastStorm).toBeNull();
        expect(state.bodies.map((body) => body.id)).toEqual(['earth', 'jupiter']);
        expect(state.bodies[0]).toMatchObject({ standoffRadii: 5.61, ovalLatDeg: 49.8, storm: true });
    });

    it('is empty outside Sol, for garbage, and for the no-app placeholder', () => {
        expect(parseSpaceWeatherJson('{"sol":false}')).toBe(EMPTY_SPACE_WEATHER);
        expect(parseSpaceWeatherJson('not json')).toBe(EMPTY_SPACE_WEATHER);
        expect(parseSpaceWeatherJson('[1,2]')).toBe(EMPTY_SPACE_WEATHER);
        expect(parseSpaceWeatherJson('')).toBe(EMPTY_SPACE_WEATHER);
    });

    it('drops unreadable parts instead of throwing', () => {
        const state = parseSpaceWeatherJson(
            JSON.stringify({
                sol: true,
                kp: 'high',
                cme: 7,
                nextStorm: { index: -1 },
                bodies: [null, { id: '' }, { id: 'earth' }],
            }),
        );
        expect(state.kp).toBe(0);
        expect(state.kpOverride).toBe(-1);
        expect(state.cme).toBeNull();
        expect(state.nextStorm).toBeNull();
        expect(state.bodies.map((body) => body.id)).toEqual(['earth']);
    });
});

describe('formatKp', () => {
    it('adds the NOAA G level from Kp 5', () => {
        expect(formatKp(2)).toBe('Kp 2.0');
        expect(formatKp(4.96)).toBe('Kp 5.0 (G1)');
        expect(formatKp(7.33)).toBe('Kp 7.3 (G3)');
        expect(formatKp(9)).toBe('Kp 9.0 (G5)');
    });

    it('labels the override slider', () => {
        expect(formatKpOverride(-1)).toBe('Auto');
        expect(formatKpOverride(7)).toBe('Kp 7.0');
    });
});

describe('formatStormChip', () => {
    it('names the next storm and the day it reaches Earth', () => {
        const state = parseSpaceWeatherJson(HALLOWEEN);
        expect(chipStorm(state)).toMatchObject({ upcoming: true, storm: { index: 4 } });
        expect(formatStormChip(state)).toBe(
            `Next geomagnetic storm: Halloween storm (Oct 29), ${isoDateFromJulianDate(2452942.25)} (Kp 9.0 (G5))`,
        );
    });

    it('offers the latest storm once the date is past the catalog', () => {
        const base = parseSpaceWeatherJson(HALLOWEEN);
        const latest = base.nextStorm && {
            ...base.nextStorm,
            index: 10,
            name: 'October 2024 storm',
            arrivalJd: 2460593.1,
        };
        const state = { ...base, nextStorm: null, lastStorm: latest };
        expect(chipStorm(state)).toMatchObject({ upcoming: false, storm: { index: 10 } });
        expect(formatStormChip(state)).toMatch(/^Latest catalogued storm: October 2024 storm, /);
    });

    it('is empty with no storm either side, or outside Sol', () => {
        expect(formatStormChip({ ...parseSpaceWeatherJson(HALLOWEEN), nextStorm: null })).toBe('');
        expect(chipStorm(EMPTY_SPACE_WEATHER)).toBeNull();
        expect(formatStormChip(EMPTY_SPACE_WEATHER)).toBe('');
    });
});

describe('stormCard', () => {
    it('shows the storm in C++ numbers', () => {
        const card = stormCard(parseSpaceWeatherJson(HALLOWEEN));
        expect(card?.title).toBe('Geomagnetic storm');
        expect(card?.lines).toHaveLength(4);
        expect(card?.lines[0]).toContain('Kp 8.7 (G4)');
        expect(card?.lines[0]).toContain('50° magnetic latitude');
        expect(card?.lines[1]).toContain('5.6 Earth radii');
        expect(card?.lines[1]).toContain('10.1 in a quiet wind');
        // The front is past Earth: the CME line says where it came from.
        expect(card?.lines[3]).toContain('launched');
    });

    it('says when the slider holds Kp', () => {
        const state = { ...parseSpaceWeatherJson(HALLOWEEN), kpOverride: 7 };
        expect(stormCard(state)?.lines[0]).toContain('held by the Kp slider');
    });

    it('announces a CME still on its way', () => {
        const base = parseSpaceWeatherJson(HALLOWEEN);
        const state = withEarth({ ...base, cme: base.cme && { ...base.cme, frontAu: 0.4 } }, { storm: false, kp: 2 });
        const card = stormCard(state);
        expect(card?.title).toBe('CME on its way');
        expect(card?.lines).toEqual([expect.stringContaining('front at 0.40 AU')]);
    });

    it('is hidden in quiet conditions, for a CME that misses Earth, and outside Sol', () => {
        const base = parseSpaceWeatherJson(HALLOWEEN);
        const quiet = withEarth({ ...base, cme: null }, { storm: false, kp: 2 });
        expect(stormCard(quiet)).toBeNull();
        const miss = withEarth(
            { ...base, cme: base.cme && { ...base.cme, frontAu: 0.4, hitsEarth: false } },
            { storm: false },
        );
        expect(stormCard(miss)).toBeNull();
        expect(stormCard(EMPTY_SPACE_WEATHER)).toBeNull();
    });
});
