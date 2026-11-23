import { afterEach, describe, expect, it, vi } from 'vitest';
import { installContextLossHandling, parseLoseContextSeconds, startContextLossCycle } from './contextLoss';

const quiet = { error: () => undefined, log: () => undefined };

function lose(canvas: EventTarget): Event {
    const event = new Event('webglcontextlost', { cancelable: true });
    canvas.dispatchEvent(event);
    return event;
}

function restore(canvas: EventTarget): void {
    canvas.dispatchEvent(new Event('webglcontextrestored'));
}

function hooks(restoreResult: () => boolean) {
    return {
        pause: vi.fn(),
        resume: vi.fn(),
        restore: vi.fn(restoreResult),
        onFatal: vi.fn(),
    };
}

describe('installContextLossHandling', () => {
    it('asks for the context back, pauses, and resumes once C++ has rebuilt the scene', () => {
        const canvas = new EventTarget();
        const h = hooks(() => true);
        const state = installContextLossHandling(canvas, h, quiet);

        expect(lose(canvas).defaultPrevented).toBe(true);
        expect(state.lost).toBe(true);
        expect(h.pause).toHaveBeenCalledTimes(1);
        expect(h.resume).not.toHaveBeenCalled();

        restore(canvas);
        expect(h.restore).toHaveBeenCalledTimes(1);
        expect(h.resume).toHaveBeenCalledTimes(1);
        expect(h.onFatal).not.toHaveBeenCalled();
        expect(state).toMatchObject({ lost: false, losses: 1, restores: 1 });
    });

    it('survives repeated cycles and ignores a duplicate loss or a stray restore', () => {
        const canvas = new EventTarget();
        const h = hooks(() => true);
        const state = installContextLossHandling(canvas, h, quiet);
        restore(canvas); // nothing was lost
        expect(h.restore).not.toHaveBeenCalled();
        for (let i = 0; i < 3; i += 1) {
            lose(canvas);
            lose(canvas);
            restore(canvas);
        }
        expect(h.pause).toHaveBeenCalledTimes(3);
        expect(h.restore).toHaveBeenCalledTimes(3);
        expect(state).toMatchObject({ lost: false, losses: 3, restores: 3 });
    });

    it('shows the fatal path, without resuming, when the restore fails or throws', () => {
        for (const failing of [
            () => false,
            () => {
                throw new Error('boom');
            },
        ]) {
            const canvas = new EventTarget();
            const h = hooks(failing);
            const state = installContextLossHandling(canvas, h, quiet);
            lose(canvas);
            restore(canvas);
            expect(h.resume).not.toHaveBeenCalled();
            expect(h.onFatal).toHaveBeenCalledTimes(1);
            expect(state.restores).toBe(0);
        }
    });
});

describe('parseLoseContextSeconds', () => {
    it('reads a positive period, at least one second', () => {
        expect(parseLoseContextSeconds('?losecontext=5')).toBe(5);
        expect(parseLoseContextSeconds('?perf=1&losecontext=2.5')).toBe(2.5);
        expect(parseLoseContextSeconds('?losecontext=0.1')).toBe(1);
    });

    it('is off when absent or not a positive number', () => {
        for (const search of ['', '?losecontext=', '?losecontext=0', '?losecontext=-3', '?losecontext=soon']) {
            expect(parseLoseContextSeconds(search), search).toBeNull();
        }
    });
});

describe('startContextLossCycle', () => {
    afterEach(() => {
        vi.useRealTimers();
    });

    it('drops the context every period and restores it after the delay', () => {
        vi.useFakeTimers();
        vi.spyOn(console, 'log').mockImplementation(() => undefined);
        const canvas = new EventTarget();
        let lost = false;
        const extension = {
            loseContext: vi.fn(() => {
                lost = true;
                lose(canvas);
            }),
            restoreContext: vi.fn(() => {
                lost = false;
            }),
        };
        const gl = { getExtension: () => extension, isContextLost: () => lost };
        const timers = {
            setInterval: (handler: () => void, ms: number) => setInterval(handler, ms),
            clearInterval: (id: number) => {
                clearInterval(id);
            },
            setTimeout: (handler: () => void, ms: number) => setTimeout(handler, ms),
            clearTimeout: (id: number) => {
                clearTimeout(id);
            },
        };
        const stop = startContextLossCycle(gl, canvas, 5000, 500, timers);

        vi.advanceTimersByTime(4999);
        expect(extension.loseContext).not.toHaveBeenCalled();
        vi.advanceTimersByTime(1);
        expect(extension.loseContext).toHaveBeenCalledTimes(1);
        vi.advanceTimersByTime(500);
        expect(extension.restoreContext).toHaveBeenCalledTimes(1);
        vi.advanceTimersByTime(4600); // t = 10.1 s: lost again at 10 s, its restore due at 10.5 s
        expect(extension.loseContext).toHaveBeenCalledTimes(2);

        stop();
        vi.advanceTimersByTime(20_000);
        expect(extension.loseContext).toHaveBeenCalledTimes(2);
        expect(extension.restoreContext).toHaveBeenCalledTimes(1); // the pending one was cancelled
    });

    it('does nothing without WEBGL_lose_context', () => {
        vi.spyOn(console, 'warn').mockImplementation(() => undefined);
        const stop = startContextLossCycle(
            { getExtension: () => null, isContextLost: () => false },
            new EventTarget(),
            1000,
        );
        expect(() => {
            stop();
        }).not.toThrow();
    });
});
