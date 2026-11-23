/**
 * WebGL context loss and restore (docs/ARCHITECTURE.md §7.8).
 *
 * A mobile GPU reset or a long-backgrounded tab can drop the context. `preventDefault()` on
 * webglcontextlost is what tells the browser we want it back; until webglcontextrestored the
 * main loop is paused (every GL call would be a no-op). On restore C++ rebuilds every GPU
 * resource in place from the CPU state it kept (the OnContextRestored export) and the loop
 * resumes where it was: same camera, same date, nothing downloaded again. The reload panel is
 * only for a restore that fails.
 */

export interface ContextLossHooks {
    /** Stop the frame loop (Module.pauseMainLoop). */
    pause(): void;
    /** Start it again (Module.resumeMainLoop). */
    resume(): void;
    /** Rebuild GPU state on the restored context; false or a throw means it cannot draw again. */
    restore(): boolean;
    /** The restore failed: offer a reload. */
    onFatal(error: unknown): void;
}

export interface ContextLossState {
    readonly lost: boolean;
    readonly losses: number;
    readonly restores: number;
}

interface Logger {
    error(...data: unknown[]): void;
    log(...data: unknown[]): void;
}

export function installContextLossHandling(
    canvas: EventTarget,
    hooks: ContextLossHooks,
    logger: Logger = console,
    now: () => number = () => performance.now(),
): ContextLossState {
    const state = { lost: false, losses: 0, restores: 0 };
    let lostAt = 0;

    canvas.addEventListener(
        'webglcontextlost',
        (event) => {
            event.preventDefault();
            if (state.lost) return;
            state.lost = true;
            state.losses += 1;
            lostAt = now();
            logger.error('[WebGL] Context lost; pausing the main loop until the browser restores it');
            hooks.pause();
        },
        false,
    );

    canvas.addEventListener(
        'webglcontextrestored',
        () => {
            if (!state.lost) return;
            state.lost = false;
            let ok = false;
            let failure: unknown = new Error('OnContextRestored reported failure');
            try {
                ok = hooks.restore();
            } catch (error: unknown) {
                failure = error;
            }
            if (!ok) {
                logger.error('[WebGL] Restoring the scene after a context loss failed:', failure);
                hooks.onFatal(failure);
                return;
            }
            state.restores += 1;
            logger.log(`[WebGL] Context restored after ${String(Math.round(now() - lostAt))} ms; resuming`);
            hooks.resume();
        },
        false,
    );

    return state;
}

/** `?losecontext=<seconds>`: the WEBGL_lose_context test cycle's period, or null when absent or invalid. */
export function parseLoseContextSeconds(search: string): number | null {
    const raw = new URLSearchParams(search).get('losecontext');
    if (raw === null || raw.trim() === '') return null;
    const seconds = Number(raw);
    if (!Number.isFinite(seconds) || seconds <= 0) return null;
    return Math.max(seconds, 1);
}

interface LoseContextExtension {
    loseContext(): void;
    restoreContext(): void;
}

interface LosableContext {
    getExtension(name: 'WEBGL_lose_context'): LoseContextExtension | null;
    isContextLost(): boolean;
}

interface Timers {
    setInterval(handler: () => void, ms: number): number;
    clearInterval(id: number): void;
    setTimeout(handler: () => void, ms: number): number;
    clearTimeout(id: number): void;
}

/**
 * Test hook for `?losecontext=<seconds>`: drop the context through WEBGL_lose_context every
 * `periodMs` and give it back `restoreDelayMs` after each loss, so the restore path runs over
 * and over (web/e2e/contextLoss.spec.ts). Returns a function that stops the cycle.
 */
export function startContextLossCycle(
    gl: LosableContext,
    canvas: EventTarget,
    periodMs: number,
    restoreDelayMs = 500,
    timerSource?: Timers,
): () => void {
    const extension = gl.getExtension('WEBGL_lose_context');
    if (!extension) {
        console.warn('[WebGL] ?losecontext ignored: WEBGL_lose_context is not available');
        return () => undefined;
    }
    const timers = timerSource ?? window;
    let pendingRestore: number | null = null;
    const onLost = (): void => {
        pendingRestore = timers.setTimeout(() => {
            pendingRestore = null;
            extension.restoreContext();
        }, restoreDelayMs);
    };
    canvas.addEventListener('webglcontextlost', onLost);
    const interval = timers.setInterval(() => {
        if (!gl.isContextLost()) {
            console.log('[WebGL] ?losecontext: dropping the context');
            extension.loseContext();
        }
    }, periodMs);
    return () => {
        timers.clearInterval(interval);
        if (pendingRestore !== null) timers.clearTimeout(pendingRestore);
        canvas.removeEventListener('webglcontextlost', onLost);
    };
}
