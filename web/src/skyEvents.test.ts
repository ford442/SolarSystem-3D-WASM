import { describe, expect, it } from 'vitest';
// The writer of the slugs, read as text (vite/client types `?raw`; no Node types in this project).
import skyEventsCpp from '../../src/astro/SkyEvents.cpp?raw';
import { isoDateFromJulianDate } from './ephemeris.js';
import {
    formatSkyEvent,
    INVALID_SKY_EVENT,
    parseSkyEventJson,
    SKY_EVENT_LANDMARKS,
    type NextSkyEvent,
} from './skyEvents.js';

const payload = (overrides: Record<string, unknown> = {}) =>
    JSON.stringify({
        valid: true,
        kind: 'solarEclipse',
        bodyA: 0,
        bodyB: 3,
        bodyAName: 'Sun',
        bodyBName: 'Earth',
        julianDate: 2457986.5,
        missDeg: 0.25,
        limitDeg: 0.5,
        ...overrides,
    });

describe('parseSkyEventJson', () => {
    it('reads a complete payload', () => {
        expect(parseSkyEventJson(payload())).toEqual({
            valid: true,
            kind: 'solarEclipse',
            bodyA: 0,
            bodyB: 3,
            bodyAName: 'Sun',
            bodyBName: 'Earth',
            julianDate: 2457986.5,
            missDeg: 0.25,
            limitDeg: 0.5,
        });
    });

    it('degrades to INVALID_SKY_EVENT instead of throwing', () => {
        for (const bad of ['', 'not json', 'null', '42', '"str"', '[]', '{"valid":false}', '{"valid":"yes"}']) {
            expect(parseSkyEventJson(bad), JSON.stringify(bad)).toBe(INVALID_SKY_EVENT);
        }
    });

    it('rejects a kind this bundle does not know and a missing or non-positive date', () => {
        expect(parseSkyEventJson(payload({ kind: 'occultation' }))).toBe(INVALID_SKY_EVENT);
        expect(parseSkyEventJson(payload({ kind: 5 }))).toBe(INVALID_SKY_EVENT);
        expect(parseSkyEventJson(payload({ julianDate: 0 }))).toBe(INVALID_SKY_EVENT);
        expect(parseSkyEventJson(payload({ julianDate: -1 }))).toBe(INVALID_SKY_EVENT);
        expect(parseSkyEventJson(payload({ julianDate: 'tomorrow' }))).toBe(INVALID_SKY_EVENT);
        expect(parseSkyEventJson(payload({ julianDate: null }))).toBe(INVALID_SKY_EVENT);
    });

    it('substitutes defaults for unreadable optional fields', () => {
        const event = parseSkyEventJson(payload({ bodyA: 'x', bodyAName: 7, missDeg: null, limitDeg: Infinity }));
        expect(event).toMatchObject({ valid: true, bodyA: -1, bodyAName: '', missDeg: 0, limitDeg: 0 });
    });

    it('accepts exactly the kind slugs the C++ side emits', () => {
        // SkyEvents::EventKindSlug is the writer; "keep the two lists in step" is a comment
        // in skyEvents.ts, so enforce it.
        const body = /const char\* EventKindSlug\([^)]*\)\s*\{([\s\S]*?)\n\}/.exec(skyEventsCpp)?.[1] ?? '';
        const slugs = [...body.matchAll(/return "(\w+)";/g)].map((match) => match[1]).filter((slug) => slug !== 'none');
        expect(slugs.length).toBeGreaterThanOrEqual(5);
        for (const slug of slugs) {
            expect(parseSkyEventJson(payload({ kind: slug })).valid, slug).toBe(true);
        }
    });
});

describe('formatSkyEvent', () => {
    const event = (kind: NextSkyEvent['kind']): NextSkyEvent =>
        parseSkyEventJson(payload({ kind, bodyAName: 'Venus', bodyBName: 'Jupiter', missDeg: 1.26 }));

    it('is empty for an invalid event', () => {
        expect(formatSkyEvent(INVALID_SKY_EVENT)).toBe('');
    });

    it('has a one-line caption per kind', () => {
        expect(formatSkyEvent(event('conjunction'))).toBe(
            'Next conjunction: Venus-Jupiter, 2017-08-21 (1.3 deg apart)',
        );
        expect(formatSkyEvent(event('solarEclipse'))).toBe('Next solar eclipse: 2017-08-21');
        expect(formatSkyEvent(event('lunarEclipse'))).toBe('Next lunar eclipse: 2017-08-21');
        expect(formatSkyEvent(event('transit'))).toBe('Next transit: Venus across the Sun, 2017-08-21');
        expect(formatSkyEvent(event('shadowTransit'))).toBe('Next shadow transit: Venus on Jupiter, 2017-08-21');
    });

    it('keeps the date for a hand-built event of unknown kind', () => {
        expect(formatSkyEvent({ ...event('conjunction'), kind: 'none' })).toBe('Next event: 2017-08-21');
    });
});

describe('SKY_EVENT_LANDMARKS', () => {
    it('have unique ids and a Julian date that falls on the date the label shows', () => {
        const ids = SKY_EVENT_LANDMARKS.map((landmark) => landmark.id);
        expect(new Set(ids).size).toBe(ids.length);
        for (const landmark of SKY_EVENT_LANDMARKS) {
            expect(isoDateFromJulianDate(landmark.julianDate), landmark.id).toBe(landmark.isoDate);
        }
    });
});
