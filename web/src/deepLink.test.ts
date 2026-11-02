import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import {
    buildShareableUrl,
    hasDeepLinkViewState,
    parseDeepLinkFromUrl,
    type DeepLinkRuntimeReaders,
} from './deepLink.js';
import { isoDateFromJulianDate } from './ephemeris.js';
import { MAX_PLANET_INDEX, PLANET_IDS } from './generated/planetIds.js';

const BASE = 'https://example.test/solar-system/';

/** parseDeepLinkFromUrl takes a `?query`; buildShareableUrl returns a whole URL. */
const parse = (query: string) => parseDeepLinkFromUrl(query);
const parseUrl = (url: string) => parseDeepLinkFromUrl(new URL(url).search);

describe('parseDeepLinkFromUrl', () => {
    it('is empty for no query', () => {
        const state = parse('');
        expect(hasDeepLinkViewState(state)).toBe(false);
        expect(Object.values(state).every((value) => value === undefined)).toBe(true);
    });

    describe('planet', () => {
        it('accepts every catalog id and its number, any case', () => {
            for (const [id, index] of Object.entries(PLANET_IDS)) {
                expect(parse(`?planet=${id}`).planet, id).toBe(index);
                expect(parse(`?planet=${id.toUpperCase()}`).planet, id.toUpperCase()).toBe(index);
                expect(parse(`?planet=${index}`).planet, String(index)).toBe(index);
            }
            expect(parse('?focus=mars').planet).toBe(PLANET_IDS.mars);
        });

        it('rejects out-of-range, fractional and unknown values', () => {
            for (const bad of ['-1', String(MAX_PLANET_INDEX + 1), '3.5', 'nibiru', '', 'earth2']) {
                expect(parse(`?planet=${bad}`).planet, bad).toBeUndefined();
            }
        });

        it('does not resolve names that only exist on Object.prototype', () => {
            for (const name of ['constructor', '__proto__', 'hasownproperty', 'valueof', 'tostring']) {
                expect(parse(`?planet=${name}`).planet, name).toBeUndefined();
            }
        });
    });

    describe('settings', () => {
        it('reads quality by name, alias or number', () => {
            expect(parse('?quality=low').quality).toBe(0);
            expect(parse('?q=med').quality).toBe(1);
            expect(parse('?quality=medium').quality).toBe(1);
            expect(parse('?quality=2').quality).toBe(2);
            expect(parse('?quality=3').quality).toBeUndefined();
            expect(parse('?quality=ultra').quality).toBeUndefined();
        });

        it('lets the long name win over its short alias', () => {
            expect(parse('?time=5&ts=9').timeScale).toBe(5);
            expect(parse('?ts=9').timeScale).toBe(9);
            expect(parse('?time=fast').timeScale).toBeUndefined();
            expect(parse('?time=Infinity').timeScale).toBeUndefined();
        });

        it('reads booleans in the spellings the UI and humans use', () => {
            for (const yes of ['1', 'true', 'YES', 'on']) expect(parse(`?paused=${yes}`).paused, yes).toBe(true);
            for (const no of ['0', 'false', 'No', 'off']) expect(parse(`?paused=${no}`).paused, no).toBe(false);
            expect(parse('?paused=maybe').paused).toBeUndefined();
            expect(parse('?p=1&shadows=0&orbits=1&fields=0')).toMatchObject({
                paused: true,
                shadows: false,
                orbitLines: true,
                magneticFields: false,
            });
        });

        it('reads orbit scale by name or number', () => {
            expect(parse('?orbit=compressed').orbitScale).toBe(0);
            expect(parse('?scale=realistic').orbitScale).toBe(1);
            expect(parse('?orbit=au').orbitScale).toBe(1);
            expect(parse('?orbit=2').orbitScale).toBeUndefined();
        });

        it('keeps a date only when it is YYYY-MM-DD, and a jd only when finite', () => {
            expect(parse('?date=2017-08-21').simulationDate).toBe('2017-08-21');
            expect(parse('?date=21-08-2017').simulationDate).toBeUndefined();
            expect(parse('?jd=2457986.75').julianDate).toBe(2457986.75);
            expect(parse('?jd=soon').julianDate).toBeUndefined();
        });
    });

    describe('tours, missions and timelines', () => {
        it('lowercases and validates mission and tour ids', () => {
            expect(parse('?mission=Voyager-1').mission).toBe('voyager-1');
            expect(parse('?probe=cassini').mission).toBe('cassini');
            expect(parse('?mission=1voyager').mission).toBeUndefined();
            expect(parse('?mission=a%20b').mission).toBeUndefined();
            expect(parse(`?tour=${'a'.repeat(33)}`).tour).toBeUndefined();
            expect(parse('?tour=grand-tour').tour).toBe('grand-tour');
        });

        it('keeps a tour step only as a non-negative integer', () => {
            expect(parse('?step=3').tourStep).toBe(3);
            expect(parse('?step=0').tourStep).toBe(0);
            expect(parse('?step=-1').tourStep).toBeUndefined();
            expect(parse('?step=1.5').tourStep).toBeUndefined();
        });

        describe('timeline', () => {
            beforeEach(() => {
                vi.stubGlobal('window', { location: { href: BASE } });
            });
            afterEach(() => {
                vi.unstubAllGlobals();
            });

            it('takes a shipped id or an http(s) URL, and ?tt= only alongside one', () => {
                expect(parse('?timeline=grand-tour&tt=12.5')).toMatchObject({
                    timeline: 'grand-tour',
                    timelineTime: 12.5,
                });
                expect(parse('?tt=12.5').timelineTime).toBeUndefined();
                const url = 'https://cdn.example/shots/one.json';
                expect(parse(`?timeline=${encodeURIComponent(url)}`).timeline).toBe(url);
            });

            it('refuses other schemes and negative times', () => {
                expect(parse(`?timeline=${encodeURIComponent('javascript:alert(1)')}`).timeline).toBeUndefined();
                expect(parse(`?timeline=${encodeURIComponent('data:text/plain,hi')}`).timeline).toBeUndefined();
                expect(parse('?timeline=grand-tour&tt=-3').timelineTime).toBeUndefined();
            });
        });
    });

    describe('observer and camera', () => {
        it('reads the Observe view and drops out-of-range fields individually', () => {
            expect(parse('?mode=observe&lat=37.77&lon=-122.42&az=180&el=45&fov=10&rate=60')).toMatchObject({
                mode: 'observe',
                observer: { lat: 37.77, lon: -122.42, az: 180, el: 45, fov: 10, rate: 60 },
            });
            const clipped = parse('?lat=91&lon=-361&el=-91&fov=0.1&rate=-1&az=10').observer;
            expect(clipped).toEqual({
                lat: undefined,
                lon: undefined,
                az: 10,
                el: undefined,
                fov: undefined,
                rate: undefined,
            });
            expect(parse('?lat=91').observer).toBeUndefined();
        });

        it('needs all five camera values', () => {
            expect(parse('?x=1&y=2&z=3&yaw=4&pitch=5').camera).toEqual({ x: 1, y: 2, z: 3, yaw: 4, pitch: 5 });
            expect(parse('?x=1&y=2&z=3&yaw=4').camera).toBeUndefined();
        });
    });
});

describe('buildShareableUrl', () => {
    const readers: DeepLinkRuntimeReaders = {
        getQualityPreset: () => 1,
        getTimeScale: () => 2.5,
        getPaused: () => true,
        getSimulationEpoch: () => 2457986.75,
        getOrbitScaleMode: () => 1,
        getShadowQuality: () => 2,
        getOrbitLines: () => false,
        getMagneticFields: () => true,
        getFocusedPlanetIndex: () => PLANET_IDS.jupiter,
        getFocusedMission: () => ({ valid: true, id: 'voyager-1' }),
        getCameraPose: () => ({ x: 1.123456789123, y: -2, z: 3, yaw: 12.34, pitch: -5.67 }),
        isoDateFromJulianDate,
    };

    it('replaces the query and keeps path and hash', () => {
        const url = new URL(buildShareableUrl({ getQualityPreset: () => 2 }, `${BASE}?stale=1&quality=0#top`));
        expect(url.origin + url.pathname).toBe(BASE);
        expect(url.hash).toBe('#top');
        expect([...url.searchParams.keys()]).toEqual(['quality']);
        expect(url.searchParams.get('quality')).toBe('2');
    });

    it('round-trips through parseDeepLinkFromUrl', () => {
        const state = parseUrl(buildShareableUrl(readers, BASE));
        expect(state).toMatchObject({
            quality: 1,
            timeScale: 2.5,
            paused: true,
            julianDate: 2457986.75,
            simulationDate: '2017-08-21',
            orbitScale: 1,
            shadows: true,
            orbitLines: false,
            magneticFields: true,
            planet: PLANET_IDS.jupiter,
            mission: 'voyager-1',
        });
        // Realistic scale keeps 1e-9 of position (a metre above the ground); angles keep 0.1 degree.
        expect(state.camera).toEqual({ x: 1.123456789, y: -2, z: 3, yaw: 12.3, pitch: -5.7 });
    });

    it('rounds positions to 0.01 on the compressed scale', () => {
        const compressed = { ...readers, getOrbitScaleMode: () => 0 as const };
        expect(parseUrl(buildShareableUrl(compressed, BASE)).camera?.x).toBe(1.12);
    });

    it('names the focused body for every focus index, and omits it when nothing is focused', () => {
        for (const [id, index] of Object.entries(PLANET_IDS)) {
            const url = new URL(buildShareableUrl({ getFocusedPlanetIndex: () => index }, BASE));
            expect(url.searchParams.get('planet'), id).toBe(id);
        }
        for (const none of [-1, MAX_PLANET_INDEX + 1]) {
            const url = new URL(buildShareableUrl({ getFocusedPlanetIndex: () => none }, BASE));
            expect(url.searchParams.has('planet'), String(none)).toBe(false);
        }
    });

    it('writes only what the readers provide, and skips non-finite numbers', () => {
        expect(buildShareableUrl({}, BASE)).toBe(BASE);
        const url = new URL(buildShareableUrl({ getTimeScale: () => NaN, getSimulationEpoch: () => Infinity }, BASE));
        expect(url.search).toBe('');
    });

    it('in Observe mode describes the sky view and leaves out the fly-through pose and focus', () => {
        const observing: DeepLinkRuntimeReaders = {
            ...readers,
            getObserveState: () => ({
                active: true,
                latDeg: 37.774929,
                lonDeg: -122.419416,
                azDeg: 180.004,
                elDeg: 45,
                fovDeg: 10,
                timeRate: 60,
            }),
        };
        const url = new URL(buildShareableUrl(observing, BASE));
        for (const absent of ['planet', 'mission', 'x', 'y', 'z', 'yaw', 'pitch']) {
            expect(url.searchParams.has(absent), absent).toBe(false);
        }
        expect(parseUrl(url.toString())).toMatchObject({
            mode: 'observe',
            observer: { lat: 37.7749, lon: -122.4194, az: 180, el: 45, fov: 10, rate: 60 },
        });
    });

    it('an inactive Observe state is ignored', () => {
        const idle: DeepLinkRuntimeReaders = {
            ...readers,
            getObserveState: () => ({
                active: false,
                latDeg: 0,
                lonDeg: 0,
                azDeg: 0,
                elDeg: 0,
                fovDeg: 60,
                timeRate: 1,
            }),
        };
        expect(new URL(buildShareableUrl(idle, BASE)).searchParams.get('mode')).toBeNull();
    });
});
