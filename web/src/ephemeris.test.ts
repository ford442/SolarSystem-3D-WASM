import { afterEach, describe, expect, it, vi } from 'vitest';
import { isoDateFromJulianDate, isoDateUtcNow, julianDateFromIsoDate, julianDateUtcNow } from './ephemeris.js';

describe('julianDateFromIsoDate', () => {
    it.each([
        ['1858-11-17', 2400000.5], // Modified Julian Date epoch
        ['1970-01-01', 2440587.5], // Unix epoch
        ['2000-01-01', 2451544.5], // midnight before J2000.0 (2451545.0 is noon)
        ['2017-08-21', 2457986.5], // the Observe-mode eclipse landmark
    ])('%s is JD %f at 0h UTC', (iso, jd) => {
        expect(julianDateFromIsoDate(iso)).toBe(jd);
    });

    it('rejects anything that is not YYYY-MM-DD', () => {
        for (const bad of ['', '2000-1-1', '20000101', '2000/01/01', '2000-01-01T00:00', ' 2000-01-01', 'today']) {
            expect(julianDateFromIsoDate(bad), JSON.stringify(bad)).toBeUndefined();
        }
    });

    it('counts Gregorian leap days: 2000 and 2024 yes, 1900 and 2100 no', () => {
        const days = (a: string, b: string) => (julianDateFromIsoDate(b) ?? NaN) - (julianDateFromIsoDate(a) ?? NaN);
        expect(days('2000-02-28', '2000-03-01')).toBe(2);
        expect(days('2024-02-28', '2024-03-01')).toBe(2);
        expect(days('1900-02-28', '1900-03-01')).toBe(1);
        expect(days('2100-02-28', '2100-03-01')).toBe(1);
    });
});

describe('isoDateFromJulianDate', () => {
    it('is the inverse of julianDateFromIsoDate for every day across 80 years', () => {
        const start = julianDateFromIsoDate('1950-01-01') ?? NaN;
        for (let day = 0; day < 80 * 366; day += 1) {
            const jd = start + day;
            const iso = isoDateFromJulianDate(jd);
            expect(julianDateFromIsoDate(iso), iso).toBe(jd);
        }
    });

    it('changes day at 0h UTC (JD .5), not at noon', () => {
        const midnight = 2451544.5; // 2000-01-01 00:00
        expect(isoDateFromJulianDate(midnight)).toBe('2000-01-01');
        expect(isoDateFromJulianDate(midnight + 0.4999)).toBe('2000-01-01');
        expect(isoDateFromJulianDate(midnight + 0.5)).toBe('2000-01-01'); // noon is J2000.0 itself
        expect(isoDateFromJulianDate(midnight + 0.9999)).toBe('2000-01-01');
        expect(isoDateFromJulianDate(midnight + 1)).toBe('2000-01-02');
        expect(isoDateFromJulianDate(midnight - 0.0001)).toBe('1999-12-31');
    });

    it('zero-pads month and day', () => {
        expect(isoDateFromJulianDate(julianDateFromIsoDate('2009-03-04') ?? NaN)).toBe('2009-03-04');
    });
});

describe('now helpers', () => {
    afterEach(() => {
        vi.useRealTimers();
    });

    it('julianDateUtcNow is the Unix epoch offset', () => {
        vi.useFakeTimers();
        vi.setSystemTime(new Date('1970-01-01T00:00:00Z'));
        expect(julianDateUtcNow()).toBe(2440587.5);
        vi.setSystemTime(new Date('2000-01-01T12:00:00Z'));
        expect(julianDateUtcNow()).toBeCloseTo(2451545.0, 9);
    });

    it('isoDateUtcNow uses the UTC date, not the local one', () => {
        vi.useFakeTimers();
        vi.setSystemTime(new Date('2024-12-31T23:59:59Z'));
        expect(isoDateUtcNow()).toBe('2024-12-31');
        vi.setSystemTime(new Date('2025-01-01T00:00:00Z'));
        expect(isoDateUtcNow()).toBe('2025-01-01');
    });
});
