import { isoDateFromJulianDate } from './ephemeris';

/**
 * Kinds mirror SkyEvents::EventKind in C++. The slug is what GetNextSkyEventJson emits;
 * keep the two lists in step or the chip silently falls back to the generic label.
 */
export type SkyEventKind =
    | 'conjunction'
    | 'solarEclipse'
    | 'lunarEclipse'
    | 'transit'
    | 'shadowTransit';

/**
 * One ephemeris-derived alignment, as assembled by SkyEvents::SkyEvent in C++.
 *
 * `missDeg` is how far the two centres miss at greatest alignment and `limitDeg` the
 * separation below which the event happens at all, so `1 - missDeg / limitDeg` reads as a
 * rough magnitude. Times come from a ~arcmin planet series and J2000 mean lunar elements:
 * the date is dependable, the time of day is not. Never present these as contact times.
 */
export interface NextSkyEvent {
    valid: boolean;
    kind: SkyEventKind | 'none';
    bodyA: number;
    bodyB: number;
    bodyAName: string;
    bodyBName: string;
    julianDate: number;
    missDeg: number;
    limitDeg: number;
}

const KNOWN_KINDS: readonly SkyEventKind[] = [
    'conjunction',
    'solarEclipse',
    'lunarEclipse',
    'transit',
    'shadowTransit',
];

export const INVALID_SKY_EVENT: NextSkyEvent = {
    valid: false,
    kind: 'none',
    bodyA: -1,
    bodyB: -1,
    bodyAName: '',
    bodyBName: '',
    julianDate: 0,
    missDeg: 0,
    limitDeg: 0,
};

function asFiniteNumber(value: unknown, fallback: number): number {
    return typeof value === 'number' && Number.isFinite(value) ? value : fallback;
}

function asString(value: unknown): string {
    return typeof value === 'string' ? value : '';
}

/**
 * Parse one GetNextSkyEventJson payload. The wasm module is the only writer, but a stale
 * build can still hand back a shape this version does not know, so every field is checked
 * and anything unreadable degrades to INVALID_SKY_EVENT rather than throwing into a frame.
 */
export function parseSkyEventJson(json: string): NextSkyEvent {
    let raw: unknown;
    try {
        raw = JSON.parse(json);
    } catch {
        return INVALID_SKY_EVENT;
    }
    if (typeof raw !== 'object' || raw === null) {
        return INVALID_SKY_EVENT;
    }

    const record = raw as Record<string, unknown>;
    if (record.valid !== true) {
        return INVALID_SKY_EVENT;
    }

    // A kind this build does not know, or a missing date, means the payload came from a
    // wasm module out of step with this bundle. Treat it as "no event" rather than render a
    // chip captioned with a year 4713 BC date.
    const kind = asString(record.kind) as SkyEventKind;
    if (!KNOWN_KINDS.includes(kind)) {
        return INVALID_SKY_EVENT;
    }
    const julianDate = asFiniteNumber(record.julianDate, 0);
    if (julianDate <= 0) {
        return INVALID_SKY_EVENT;
    }

    return {
        valid: true,
        kind,
        bodyA: asFiniteNumber(record.bodyA, -1),
        bodyB: asFiniteNumber(record.bodyB, -1),
        bodyAName: asString(record.bodyAName),
        bodyBName: asString(record.bodyBName),
        julianDate,
        missDeg: asFiniteNumber(record.missDeg, 0),
        limitDeg: asFiniteNumber(record.limitDeg, 0),
    };
}

/** Match SkyEvents::FormatEvent — ASCII one-liner for the HTML chip and the GL overlay. */
export function formatSkyEvent(event: NextSkyEvent): string {
    if (!event.valid) {
        return '';
    }
    const date = isoDateFromJulianDate(event.julianDate);
    switch (event.kind) {
        case 'conjunction':
            return `Next conjunction: ${event.bodyAName}-${event.bodyBName}, ${date} (${event.missDeg.toFixed(1)} deg apart)`;
        case 'solarEclipse':
            return `Next solar eclipse: ${date}`;
        case 'lunarEclipse':
            return `Next lunar eclipse: ${date}`;
        case 'transit':
            return `Next transit: ${event.bodyAName} across the Sun, ${date}`;
        case 'shadowTransit':
            return `Next shadow transit: ${event.bodyAName} on ${event.bodyBName}, ${date}`;
        default:
            // parseSkyEventJson rejects unknown kinds, so this is only reachable if a caller
            // hand-builds an event; keep it harmless rather than silently dropping the date.
            return `Next event: ${date}`;
    }
}

/**
 * Dates worth jumping to that the search window would not reach from "now". These are
 * documented historical events, not computed ones: the label is the published UTC date and
 * the Julian date is that date at the published instant of greatest eclipse, rounded to
 * the minute. Jumping here sets the epoch; what the renderer then shows comes back out of
 * the ephemeris, so the two can disagree by minutes.
 */
export interface SkyEventLandmark {
    id: string;
    label: string;
    isoDate: string;
    julianDate: number;
}

export const SKY_EVENT_LANDMARKS: readonly SkyEventLandmark[] = [
    // 2017-08-21 18:26 UTC — the "Great American Eclipse", total across the United States.
    { id: 'gae2017', label: 'Total solar eclipse', isoDate: '2017-08-21', julianDate: 2457987.268 },
    // 2024-04-08 18:17 UTC — total, Mexico through eastern Canada.
    { id: 'tse2024', label: 'Total solar eclipse', isoDate: '2024-04-08', julianDate: 2460409.262 },
    // 2012-06-06 01:29 UTC — the last Venus transit until 2117.
    { id: 'venus2012', label: 'Venus transit', isoDate: '2012-06-06', julianDate: 2456084.562 },
] as const;

/**
 * Wire the sky-event chip: a one-line hint, a button that jumps the simulation epoch to
 * the event, and one that jumps to a documented landmark date.
 */
export function bindSkyEventChip(options: {
    chip: HTMLElement;
    text: HTMLElement;
    jumpButton: HTMLButtonElement;
    landmarkButton?: HTMLButtonElement;
    landmark?: SkyEventLandmark;
    getNextSkyEvent: () => NextSkyEvent;
    setSimulationEpoch: (julianDate: number) => void;
    onJumped?: (julianDate: number) => void;
}): { refresh: () => void } {
    const landmark = options.landmark ?? SKY_EVENT_LANDMARKS[0];

    const refresh = (): void => {
        const next = options.getNextSkyEvent();
        options.chip.hidden = !next.valid;
        options.text.textContent = next.valid ? formatSkyEvent(next) : '';
        options.jumpButton.disabled = !next.valid;
    };

    options.jumpButton.addEventListener('click', () => {
        const next = options.getNextSkyEvent();
        if (!next.valid) {
            return;
        }
        options.setSimulationEpoch(next.julianDate);
        options.onJumped?.(next.julianDate);
        refresh();
    });

    if (options.landmarkButton) {
        options.landmarkButton.textContent = `Play ${landmark.isoDate}`;
        options.landmarkButton.title = `${landmark.label}, ${landmark.isoDate}`;
        options.landmarkButton.addEventListener('click', () => {
            options.setSimulationEpoch(landmark.julianDate);
            options.onJumped?.(landmark.julianDate);
            refresh();
        });
    }

    refresh();
    return { refresh };
}
