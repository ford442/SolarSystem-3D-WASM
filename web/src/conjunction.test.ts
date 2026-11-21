import { describe, expect, it, vi } from 'vitest';
import { bindConjunctionChip, bodyDisplayName, formatNextConjunction, type NextConjunction } from './conjunction.js';
import { PLANET_IDS } from './generated/planetIds.js';

const venusJupiter: NextConjunction = {
    valid: true,
    bodyA: 2,
    bodyB: 5,
    julianDate: 2457986.5, // 2017-08-21
    separationDeg: 0.4321,
};

describe('bodyDisplayName', () => {
    it('agrees with the generated focus indices for every body', () => {
        // BODY_NAMES is typed by hand; this fails the day a catalog row moves or is added.
        for (const [id, index] of Object.entries(PLANET_IDS)) {
            expect(bodyDisplayName(index).toLowerCase(), `index ${index}`).toContain(id);
        }
    });

    it('falls back to a numbered label outside the table', () => {
        expect(bodyDisplayName(99)).toBe('Body 99');
        expect(bodyDisplayName(-1)).toBe('Body -1');
    });
});

describe('formatNextConjunction', () => {
    it('is empty for an invalid result', () => {
        expect(formatNextConjunction({ ...venusJupiter, valid: false })).toBe('');
    });

    it('reads like the C++ SkyEvents::Format line', () => {
        expect(formatNextConjunction(venusJupiter)).toBe('Next conjunction: Venus-Jupiter, 2017-08-21 (0.4 deg apart)');
    });
});

/** Just enough of an element for bindConjunctionChip. */
function fakeElements() {
    const listeners: Partial<Record<string, (() => void)[]>> = {};
    const jump = {
        addEventListener: (type: string, fn: () => void) => {
            (listeners[type] ??= []).push(fn);
        },
    };
    const chip = { hidden: true };
    const text = { textContent: 'stale' };
    const click = () => {
        for (const fn of listeners.click ?? []) fn();
    };
    return { chip, text, jump, click };
}

describe('bindConjunctionChip', () => {
    function bind(next: () => NextConjunction) {
        const { chip, text, jump, click } = fakeElements();
        const setSimulationEpoch = vi.fn();
        const onJumped = vi.fn();
        const binding = bindConjunctionChip({
            chip: chip as unknown as HTMLElement,
            text: text as unknown as HTMLElement,
            jumpButton: jump as unknown as HTMLButtonElement,
            getNextConjunction: next,
            setSimulationEpoch,
            onJumped,
        });
        return { chip, text, click, setSimulationEpoch, onJumped, ...binding };
    }

    it('shows the chip with text when there is a conjunction', () => {
        const { chip, text } = bind(() => venusJupiter);
        expect(chip.hidden).toBe(false);
        expect(text.textContent).toContain('Venus-Jupiter');
    });

    it('hides the chip and clears the text when there is none', () => {
        const { chip, text } = bind(() => ({ ...venusJupiter, valid: false }));
        expect(chip.hidden).toBe(true);
        expect(text.textContent).toBe('');
    });

    it('jumps the simulation to the event date and notifies', () => {
        const { click, setSimulationEpoch, onJumped } = bind(() => venusJupiter);
        click();
        expect(setSimulationEpoch).toHaveBeenCalledExactlyOnceWith(venusJupiter.julianDate);
        expect(onJumped).toHaveBeenCalledExactlyOnceWith(venusJupiter);
    });

    it('does nothing on click while there is no valid conjunction', () => {
        const { click, setSimulationEpoch, onJumped } = bind(() => ({ ...venusJupiter, valid: false }));
        click();
        expect(setSimulationEpoch).not.toHaveBeenCalled();
        expect(onJumped).not.toHaveBeenCalled();
    });

    it('refresh follows the runtime as the answer changes', () => {
        let current: NextConjunction = { ...venusJupiter, valid: false };
        const { chip, refresh } = bind(() => current);
        expect(chip.hidden).toBe(true);
        current = venusJupiter;
        refresh();
        expect(chip.hidden).toBe(false);
    });
});
