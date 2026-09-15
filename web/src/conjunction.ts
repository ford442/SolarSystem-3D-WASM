import { isoDateFromJulianDate } from './ephemeris';

export interface NextConjunction {
    valid: boolean;
    bodyA: number;
    bodyB: number;
    julianDate: number;
    separationDeg: number;
}

const BODY_NAMES = [
    'Sun',
    'Mercury',
    'Venus',
    'Earth',
    'Mars',
    'Jupiter',
    'Saturn',
    'Uranus',
    'Neptune',
    'Pluto',
    'Ceres',
    'Vesta',
] as const;

export function bodyDisplayName(index: number): string {
    return BODY_NAMES[index] ?? `Body ${index}`;
}

/** Match SkyEvents::Format — ASCII one-liner for HTML chips. */
export function formatNextConjunction(next: NextConjunction): string {
    if (!next.valid) {
        return '';
    }
    const date = isoDateFromJulianDate(next.julianDate);
    return `Next conjunction: ${bodyDisplayName(next.bodyA)}-${bodyDisplayName(next.bodyB)}, ${date} (${next.separationDeg.toFixed(1)} deg apart)`;
}

export function bindConjunctionChip(options: {
    chip: HTMLElement;
    text: HTMLElement;
    jumpButton: HTMLButtonElement;
    getNextConjunction: () => NextConjunction;
    setSimulationEpoch: (julianDate: number) => void;
    onJumped?: (next: NextConjunction) => void;
}): { refresh: () => void } {
    const refresh = (): void => {
        const next = options.getNextConjunction();
        if (!next.valid) {
            options.chip.hidden = true;
            options.text.textContent = '';
            return;
        }
        options.chip.hidden = false;
        options.text.textContent = formatNextConjunction(next);
    };

    options.jumpButton.addEventListener('click', () => {
        const next = options.getNextConjunction();
        if (!next.valid) {
            return;
        }
        options.setSimulationEpoch(next.julianDate);
        options.onJumped?.(next);
        refresh();
    });

    refresh();
    return { refresh };
}
