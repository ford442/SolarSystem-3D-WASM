import { describe, expect, it } from 'vitest';
import { isoDateFromJulianDate } from './ephemeris.js';
import {
    clampLatitude,
    DEFAULT_OBSERVER_SITE,
    ECLIPSE_2017,
    eventVisibilitySuffix,
    eyepieceFieldDeg,
    formatFov,
    isTelescopeFov,
    normalizeLongitude,
    OBSERVER_SITES,
    parseObserveStateJson,
    parseTimeOfDayDays,
    TELESCOPE_MAX_FOV_DEG,
    timeOfDayFromJulianDate,
    type ObserveState,
} from './observe.js';

const complete = {
    active: true,
    latDeg: 42.8666,
    lonDeg: -106.3131,
    altM: 1600,
    julianDate: 2457987.2387,
    utc: '2017-08-21 17:43:45',
    azDeg: 190.5,
    elDeg: 62.1,
    fovDeg: 10,
    timeRate: 60,
    paused: false,
    lstDeg: 123.4,
    sunAltDeg: 62,
    sunAzDeg: 190,
    moonAltDeg: 62,
    moonAzDeg: 190,
    moonIllum: 0.001,
    sunCoverage: 1,
    starCount: 4096,
    constellations: true,
    constellationSegments: 600,
    moonLibLonDeg: 1.5,
    moonLibLatDeg: -2.5,
    eventVisibility: 'above',
};

describe('parseObserveStateJson', () => {
    it('round-trips a complete payload field for field', () => {
        expect(parseObserveStateJson(JSON.stringify(complete))).toEqual(complete);
    });

    it('returns the same inactive state for anything unparseable', () => {
        const inactive = parseObserveStateJson('');
        expect(inactive.active).toBe(false);
        for (const bad of ['not json', 'null', '7', '"x"', '{', 'undefined']) {
            expect(parseObserveStateJson(bad), JSON.stringify(bad)).toBe(inactive);
        }
    });

    it('treats an empty object as inactive with the defaults filled in', () => {
        const state = parseObserveStateJson('{}');
        expect(state.active).toBe(false);
        expect(state.timeRate).toBe(1);
        expect(state.constellations).toBe(true); // only an explicit false switches it off
        expect(state.eventVisibility).toBe('none');
        expect(Object.values(state).every((value) => value !== undefined && !Number.isNaN(value))).toBe(true);
    });

    it('replaces non-finite and mistyped numbers with the default instead of passing NaN on', () => {
        const state = parseObserveStateJson(JSON.stringify({ ...complete, azDeg: 'north', elDeg: null, altM: [] }));
        expect(Number.isFinite(state.azDeg)).toBe(true);
        expect(Number.isFinite(state.elDeg)).toBe(true);
        expect(state.altM).toBe(0);
    });

    it('requires strict true for active/paused and strict false to disable constellations', () => {
        expect(parseObserveStateJson('{"active":1}').active).toBe(false);
        expect(parseObserveStateJson('{"active":"true"}').active).toBe(false);
        expect(parseObserveStateJson('{"paused":true}').paused).toBe(true);
        expect(parseObserveStateJson('{"constellations":false}').constellations).toBe(false);
        expect(parseObserveStateJson('{"constellations":0}').constellations).toBe(true);
    });

    it('accepts only the known event-visibility values', () => {
        for (const value of ['above', 'below', 'unknown']) {
            expect(parseObserveStateJson(JSON.stringify({ eventVisibility: value })).eventVisibility).toBe(value);
        }
        for (const value of ['none', 'maybe', 3, null]) {
            expect(parseObserveStateJson(JSON.stringify({ eventVisibility: value })).eventVisibility).toBe('none');
        }
    });
});

describe('eventVisibilitySuffix', () => {
    const state = (overrides: Partial<ObserveState>): ObserveState => ({
        ...parseObserveStateJson(JSON.stringify(complete)),
        ...overrides,
    });

    it('speaks only while Observe is active and the answer is known', () => {
        expect(eventVisibilitySuffix(state({ eventVisibility: 'above' }))).toContain('above your horizon');
        expect(eventVisibilitySuffix(state({ eventVisibility: 'below' }))).toContain('below your horizon');
        expect(eventVisibilitySuffix(state({ eventVisibility: 'unknown' }))).toBe('');
        expect(eventVisibilitySuffix(state({ eventVisibility: 'none' }))).toBe('');
        expect(eventVisibilitySuffix(state({ active: false, eventVisibility: 'above' }))).toBe('');
        expect(eventVisibilitySuffix(null)).toBe('');
    });
});

describe('site arithmetic', () => {
    it('clamps latitude to the poles', () => {
        expect(clampLatitude(91)).toBe(90);
        expect(clampLatitude(-200)).toBe(-90);
        expect(clampLatitude(12.5)).toBe(12.5);
    });

    it('folds longitude into [-180, 180)', () => {
        expect(normalizeLongitude(0)).toBe(0);
        expect(normalizeLongitude(180)).toBe(-180);
        expect(normalizeLongitude(-180)).toBe(-180);
        expect(normalizeLongitude(190)).toBe(-170);
        expect(normalizeLongitude(-190)).toBe(170);
        expect(normalizeLongitude(540)).toBe(-180);
        expect(normalizeLongitude(721)).toBe(1);
    });

    it('has unique, in-range sites and a default that is one of them', () => {
        const ids = OBSERVER_SITES.map((site) => site.id);
        expect(new Set(ids).size).toBe(ids.length);
        expect(OBSERVER_SITES).toContain(DEFAULT_OBSERVER_SITE);
        for (const site of OBSERVER_SITES) {
            expect(Math.abs(site.latDeg), site.id).toBeLessThanOrEqual(90);
            expect(site.lonDeg, site.id).toBeGreaterThanOrEqual(-180);
            expect(site.lonDeg, site.id).toBeLessThan(180);
        }
    });
});

describe('time of day', () => {
    it('parses HH:MM and HH:MM:SS to a fraction of a day', () => {
        expect(parseTimeOfDayDays('00:00')).toBe(0);
        expect(parseTimeOfDayDays('12:00')).toBe(0.5);
        expect(parseTimeOfDayDays('06:30:00')).toBeCloseTo(6.5 / 24, 12);
        expect(parseTimeOfDayDays('23:59:59')).toBeCloseTo(86399 / 86400, 12);
    });

    it('rejects malformed and out-of-range times', () => {
        for (const bad of ['', '1:00', '24:00', '12:60', '12:00:60', '12', '12:00:', 'noon', '12:00 PM']) {
            expect(parseTimeOfDayDays(bad), JSON.stringify(bad)).toBeUndefined();
        }
    });

    it('formats a Julian date as UTC HH:MM:SS, never reaching 24:00:00', () => {
        expect(timeOfDayFromJulianDate(2451544.5)).toBe('00:00:00');
        expect(timeOfDayFromJulianDate(2451545.0)).toBe('12:00:00');
        expect(timeOfDayFromJulianDate(2451544.5 + 0.75)).toBe('18:00:00');
        expect(timeOfDayFromJulianDate(2451544.5 + 1 - 1e-9)).toBe('23:59:59');
    });

    it('is the inverse of parseTimeOfDayDays', () => {
        for (const time of ['00:00:00', '03:14:15', '17:43:45', '23:59:59']) {
            const days = parseTimeOfDayDays(time) ?? NaN;
            expect(timeOfDayFromJulianDate(2451544.5 + days), time).toBe(time);
        }
    });
});

describe('ECLIPSE_2017', () => {
    it('is mid-totality on the 2017-08-21 eclipse day at a listed site', () => {
        expect(isoDateFromJulianDate(ECLIPSE_2017.julianDate)).toBe('2017-08-21');
        expect(timeOfDayFromJulianDate(ECLIPSE_2017.julianDate)).toBe('17:43:45');
        expect(OBSERVER_SITES.map((site) => site.id)).toContain(ECLIPSE_2017.siteId);
    });
});

describe('field of view', () => {
    it('shows two decimals only below 10 degrees', () => {
        expect(formatFov(0.25)).toBe('0.25');
        expect(formatFov(1.5)).toBe('1.50');
        expect(formatFov(9.99)).toBe('9.99');
        expect(formatFov(10)).toBe('10');
        expect(formatFov(45.4)).toBe('45');
    });

    it('is a telescope at or just past the 2 degree limit', () => {
        expect(isTelescopeFov(0.25)).toBe(true);
        expect(isTelescopeFov(TELESCOPE_MAX_FOV_DEG)).toBe(true);
        expect(isTelescopeFov(TELESCOPE_MAX_FOV_DEG + 0.0005)).toBe(true);
        expect(isTelescopeFov(TELESCOPE_MAX_FOV_DEG + 0.01)).toBe(false);
        expect(isTelescopeFov(60)).toBe(false);
    });

    it('limits the eyepiece to the short side of the viewport', () => {
        expect(eyepieceFieldDeg(1, 1920, 1080)).toBe(1); // landscape sees the full vertical field
        expect(eyepieceFieldDeg(1, 540, 1080)).toBe(0.5); // portrait sees a share of it
        expect(eyepieceFieldDeg(1, 100, 0)).toBe(1); // no viewport yet
    });
});
