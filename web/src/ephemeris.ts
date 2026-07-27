/** Civil UTC date YYYY-MM-DD → Julian Date at 0h (matches C++ Ephemeris::JulianDateFromYmd). */
export function julianDateFromIsoDate(isoDate: string): number | undefined {
    const match = /^(\d{4})-(\d{2})-(\d{2})$/.exec(isoDate);
    if (!match) return undefined;
    const year = Number(match[1]);
    const month = Number(match[2]);
    const day = Number(match[3]);
    const a = Math.floor((14 - month) / 12);
    const y = year + 4800 - a;
    const m = month + 12 * a - 3;
    const jdn = day + Math.floor((153 * m + 2) / 5) + 365 * y + Math.floor(y / 4)
        - Math.floor(y / 100) + Math.floor(y / 400) - 32045;
    return jdn - 0.5;
}

export function isoDateFromJulianDate(jd: number): string {
    const J = Math.floor(jd + 0.5);
    const f = J + 1401 + Math.floor((Math.floor((4 * J + 274277) / 146097) * 3) / 4) - 38;
    const e = 4 * f + 3;
    const g = Math.floor((e % 1461) / 4);
    const h = 5 * g + 2;
    const day = Math.floor((h % 153) / 5) + 1;
    const month = (Math.floor(h / 153) + 2) % 12 + 1;
    const year = Math.floor(e / 1461) - 4716 + Math.floor((12 + 2 - month) / 12);
    const mm = String(month).padStart(2, '0');
    const dd = String(day).padStart(2, '0');
    return `${year}-${mm}-${dd}`;
}

export function isoDateUtcNow(): string {
    const now = new Date();
    const y = now.getUTCFullYear();
    const m = String(now.getUTCMonth() + 1).padStart(2, '0');
    const d = String(now.getUTCDate()).padStart(2, '0');
    return `${y}-${m}-${d}`;
}

export function julianDateUtcNow(): number {
    return Date.now() / 86400000 + 2440587.5;
}
