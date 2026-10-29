/**
 * Plays a timeline (timeline.ts) through the engine: each frame it moves the date, samples
 * where the bodies are, and sets the camera pose, roll, field of view and overlays. While a
 * timeline holds the camera the orrery clock is paused, so nothing but the timeline moves
 * the date between frames; releasing the camera restores the clock, the field of view, the
 * HUD and the overlays as they were. Capture (capture.ts) drives renderAt() frame by frame
 * instead of the requestAnimationFrame loop here.
 */
import {
    createTimelineEvaluator,
    parseTimeline,
    referencedFoci,
    type BodyState,
    type SceneSample,
    type Timeline,
    type TimelineEvaluator,
    type TimelineFrame,
    type TimelineOverlays,
    type TimelineParseResult,
    type Vec3,
} from './timeline.js';
import { applyTourView } from './tourPlayer.js';
import type {
    BodyCatalogEntry,
    MissionInfo,
    SceneStateSnapshot,
    SolarSystemRuntime,
    SurfacePoint,
} from './wasmBridge.js';

/** Cinematics shipped in public/timelines/ (the issue #151 set; Olympus Mons waits for terrain). */
export const SHIPPED_TIMELINES: readonly { id: string; title: string }[] = [
    { id: 'voyager2-grand-tour', title: 'Voyager 2 Grand Tour' },
    { id: 'eclipse-2024-from-the-moon', title: '2024-04-08 eclipse from the Moon' },
    { id: 'descent-to-earth', title: 'Seamless descent to 1 m above Earth' },
];

const TIMELINE_ID = /^[a-z][a-z0-9-]{0,47}$/;
/** HUD mask with every text overlay on (SetHudOverlays bits). */
const HUD_ALL = 15;

/**
 * Where `?timeline=` points: a shipped id resolves under `<base>timelines/`, anything else
 * must be an http(s) URL (absolute, or relative to the page). Null when it is neither.
 */
export function timelineUrl(source: string, base: string, pageUrl: string): string | null {
    if (TIMELINE_ID.test(source)) {
        return new URL(`${base}timelines/${source}.json`, pageUrl).toString();
    }
    try {
        const url = new URL(source, pageUrl);
        return url.protocol === 'https:' || url.protocol === 'http:' ? url.toString() : null;
    } catch {
        return null;
    }
}

export async function fetchTimeline(source: string): Promise<TimelineParseResult> {
    const url = timelineUrl(source, import.meta.env.BASE_URL, window.location.href);
    if (!url) {
        return { ok: false, error: `not a timeline id or http(s) URL: ${source}` };
    }
    try {
        const response = await fetch(url);
        if (!response.ok) {
            return { ok: false, error: `${url}: HTTP ${response.status}` };
        }
        return parseTimeline(await response.json());
    } catch (error) {
        return { ok: false, error: `${url}: ${error instanceof Error ? error.message : String(error)}` };
    }
}

/** Focus ids → engine indices: catalog bodies by id, probes as `mission:<id>`. */
export interface FocusDirectory {
    bodies: BodyCatalogEntry[];
    missions: MissionInfo[];
    body(focus: string): BodyCatalogEntry | undefined;
    missionIndex(focus: string): number;
}

export function createFocusDirectory(runtime: SolarSystemRuntime): FocusDirectory {
    const bodies = runtime.getBodyCatalog();
    const missions = runtime.getMissionCatalog();
    const byId = new Map(bodies.map((entry) => [entry.id, entry]));
    return {
        bodies,
        missions,
        body: (focus) => byId.get(focus),
        missionIndex: (focus) =>
            focus.startsWith('mission:') ? missions.find((mission) => mission.id === focus.slice(8))?.index ?? -1 : -1,
    };
}

/** The scene a timeline reads, from one GetSceneStateJson snapshot plus surface queries. */
export function sampleScene(runtime: SolarSystemRuntime, directory: FocusDirectory, state: SceneStateSnapshot): SceneSample {
    const body = (focus: string): BodyState | null => {
        const mission = directory.missionIndex(focus);
        if (mission >= 0) {
            const probe = state.missions[mission];
            return probe ? { position: [probe[0], probe[1], probe[2]], radius: 0, radiusKm: 0 } : null;
        }
        const entry = directory.body(focus);
        const row = entry ? state.bodies[entry.index] : null;
        return entry && row ? { position: [row[0], row[1], row[2]], radius: row[3], radiusKm: entry.radiusKm } : null;
    };
    return {
        body,
        surface: (focus: string, direction: Vec3): SurfacePoint => {
            const entry = directory.body(focus);
            return entry
                ? runtime.getSurface(entry.index, direction[0], direction[1], direction[2])
                : { radius: 0, normal: direction };
        },
    };
}

export type TimelinePlayerState = 'idle' | 'playing' | 'paused' | 'ended';

export interface TimelinePlayer {
    load(timeline: Timeline): void;
    /** Swap in an edited version of the loaded timeline, keeping the camera and the start date. */
    update(timeline: Timeline): void;
    current(): Timeline | null;
    play(fromSec?: number): void;
    pause(): void;
    /** End playback and give the camera back. */
    stop(): void;
    /** Show the frame at t (scrub preview); takes the camera if it does not hold it yet. */
    seek(t: number): TimelineFrame | null;
    time(): number;
    state(): TimelinePlayerState;
    /** Put the scene at t without drawing (capture steps the engine itself). */
    renderAt(t: number): TimelineFrame | null;
    /** Take the camera: pause the clock, leave Observe and mission follow, apply the timeline's view. */
    acquire(): void;
    /** Give the camera back (stop() does this too). */
    release(): void;
    holdsCamera(): boolean;
}

export function createTimelinePlayer(options: {
    runtime: SolarSystemRuntime;
    onTime?: (t: number, frame: TimelineFrame | null) => void;
    onCaption?: (text: string) => void;
    onState?: (state: TimelinePlayerState) => void;
}): TimelinePlayer {
    const { runtime } = options;
    let timeline: Timeline | null = null;
    let evaluator: TimelineEvaluator | null = null;
    let directory: FocusDirectory | null = null;
    let saved: { paused: boolean; fov: number; hud: number; orbitLines: boolean; magneticFields: boolean } | null = null;
    let startJd = 0;
    let applied: Required<TimelineOverlays> | null = null;
    let caption = '';
    let state: TimelinePlayerState = 'idle';
    let time = 0;
    let raf = 0;
    let playStartTime = 0;
    let playStartWall = 0;

    const setState = (next: TimelinePlayerState): void => {
        if (next !== state) {
            state = next;
            options.onState?.(next);
        }
    };

    const setCaption = (text: string): void => {
        if (text !== caption) {
            caption = text;
            options.onCaption?.(text);
        }
    };

    const applyOverlays = (overlays: Required<TimelineOverlays>): void => {
        // The setters notify the settings panel, so only on a change, not every frame.
        if (applied?.orbitLines !== overlays.orbitLines) {
            runtime.setOrbitLines(overlays.orbitLines);
        }
        if (applied?.magneticFields !== overlays.magneticFields) {
            runtime.setMagneticFields(overlays.magneticFields);
        }
        if (applied?.hud !== overlays.hud) {
            runtime.setHudOverlays(overlays.hud ? HUD_ALL : 0);
        }
        applied = overlays;
    };

    const acquire = (): void => {
        if (saved || !timeline) {
            return;
        }
        saved = {
            paused: runtime.getPaused(),
            fov: runtime.getCameraFov(),
            hud: runtime.getHudOverlays(),
            orbitLines: runtime.getOrbitLines(),
            magneticFields: runtime.getMagneticFields(),
        };
        if (runtime.getObserveMode()) {
            runtime.setObserveMode(false);
        }
        if (runtime.getFocusedMissionIndex() >= 0) {
            runtime.focusMission(-1);
        }
        if (timeline.view) {
            // Settings and a start date only: the camera is the keyframes' business.
            const { camera: _camera, planet: _planet, mission: _mission, ...view } = timeline.view;
            applyTourView(runtime, view);
        }
        runtime.setPaused(true);
        directory = createFocusDirectory(runtime);
        startJd = runtime.getSimulationEpoch();
        evaluator = createTimelineEvaluator(timeline, { startJd });
        for (const focus of referencedFoci(timeline)) {
            const entry = directory.body(focus);
            if (entry) {
                runtime.preloadBodySystem(entry.index);
            }
        }
        applied = null;
    };

    const release = (): void => {
        cancelAnimationFrame(raf);
        if (!saved) {
            setState('idle');
            return;
        }
        runtime.setCameraRoll(0);
        runtime.setCameraFov(saved.fov);
        runtime.setHudOverlays(saved.hud);
        if (applied && applied.orbitLines !== saved.orbitLines) {
            runtime.setOrbitLines(saved.orbitLines);
        }
        if (applied && applied.magneticFields !== saved.magneticFields) {
            runtime.setMagneticFields(saved.magneticFields);
        }
        runtime.setPaused(saved.paused);
        // advanceSimulationTo does not notify; tell the settings panel where the date ended up.
        runtime.setSimulationEpoch(runtime.getSimulationEpoch());
        saved = null;
        evaluator = null;
        applied = null;
        setCaption('');
        setState('idle');
    };

    const renderAt = (t: number): TimelineFrame | null => {
        acquire();
        if (!evaluator || !directory || !timeline) {
            return null;
        }
        time = Math.max(0, Math.min(t, timeline.durationSec));
        runtime.advanceSimulationTo(evaluator.dateAt(time));
        const frame = evaluator.frameAt(time, sampleScene(runtime, directory, runtime.getSceneState()));
        if (!frame) {
            return null; // a focus system is still loading: hold the last pose
        }
        runtime.setCameraPose(frame.position[0], frame.position[1], frame.position[2], frame.yaw, frame.pitch);
        runtime.setCameraRoll(frame.roll);
        runtime.setCameraFov(frame.fov);
        applyOverlays(frame.overlays);
        setCaption(frame.caption);
        return frame;
    };

    const tick = (now: number): void => {
        if (state !== 'playing' || !timeline) {
            return;
        }
        const t = playStartTime + (now - playStartWall) / 1000;
        const frame = renderAt(Math.min(t, timeline.durationSec));
        options.onTime?.(time, frame);
        if (t >= timeline.durationSec) {
            setState('ended'); // hold the last frame until stop()
            return;
        }
        raf = requestAnimationFrame(tick);
    };

    return {
        load(next) {
            release();
            timeline = next;
            time = 0;
        },
        update(next) {
            timeline = next;
            if (evaluator) {
                evaluator = createTimelineEvaluator(next, { startJd });
            }
            time = Math.min(time, next.durationSec);
        },
        current: () => timeline,
        play(fromSec) {
            if (!timeline) {
                return;
            }
            acquire();
            cancelAnimationFrame(raf);
            const start = fromSec ?? (state === 'ended' ? 0 : time);
            playStartTime = start >= timeline.durationSec ? 0 : start;
            playStartWall = performance.now();
            setState('playing');
            raf = requestAnimationFrame(tick);
        },
        pause() {
            if (state === 'playing') {
                cancelAnimationFrame(raf);
                setState('paused');
            }
        },
        stop: release,
        seek(t) {
            if (!timeline) {
                return null;
            }
            if (state === 'playing') {
                cancelAnimationFrame(raf);
            }
            const frame = renderAt(t);
            setState('paused');
            options.onTime?.(time, frame);
            return frame;
        },
        time: () => time,
        state: () => state,
        renderAt,
        acquire,
        release,
        holdsCamera: () => saved !== null,
    };
}
