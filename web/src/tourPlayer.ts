import type { DeepLinkViewState } from './deepLink.js';
import { SKY_EVENT_LANDMARKS } from './skyEvents.js';
import type { SolarSystemRuntime } from './wasmBridge.js';
import { shadowQualityForPreset } from './wasmBridge.js';

export interface TourStepView extends DeepLinkViewState {
    /** Catalog mission slug, e.g. "voyager1". */
    mission?: string;
    /** Jump to a documented sky-event landmark from skyEvents.ts. */
    landmark?: string;
    /** Jump to whatever GetNextSkyEvent returns at apply time. */
    skyEvent?: boolean;
}

export interface TourStep {
    atSec: number;
    label: string;
    narrationCue?: string;
    view: TourStepView;
}

export interface GuidedTour {
    id: string;
    title: string;
    durationSec: number;
    steps: TourStep[];
}

export function parseTourJson(raw: unknown): GuidedTour | null {
    if (typeof raw !== 'object' || raw === null) {
        return null;
    }
    const record = raw as Record<string, unknown>;
    const id = typeof record.id === 'string' ? record.id : '';
    const title = typeof record.title === 'string' ? record.title : id;
    const durationSec = typeof record.durationSec === 'number' && Number.isFinite(record.durationSec)
        ? record.durationSec
        : 0;
    if (!id || !Array.isArray(record.steps) || record.steps.length === 0) {
        return null;
    }
    const steps: TourStep[] = [];
    for (const entry of record.steps) {
        if (typeof entry !== 'object' || entry === null) {
            continue;
        }
        const step = entry as Record<string, unknown>;
        const atSec = typeof step.atSec === 'number' && Number.isFinite(step.atSec) ? step.atSec : NaN;
        const label = typeof step.label === 'string' ? step.label : '';
        if (!Number.isFinite(atSec) || !label) {
            continue;
        }
        const view = (typeof step.view === 'object' && step.view !== null)
            ? step.view as TourStepView
            : {};
        steps.push({
            atSec,
            label,
            narrationCue: typeof step.narrationCue === 'string' ? step.narrationCue : undefined,
            view,
        });
    }
    if (steps.length === 0) {
        return null;
    }
    steps.sort((a, b) => a.atSec - b.atSec);
    return { id, title, durationSec: durationSec || steps[steps.length - 1].atSec, steps };
}

export function stepIndexAtTime(tour: GuidedTour, elapsedSec: number): number {
    let index = 0;
    for (let i = 0; i < tour.steps.length; ++i) {
        if (tour.steps[i].atSec <= elapsedSec) {
            index = i;
        }
    }
    return index;
}

export function applyTourView(runtime: SolarSystemRuntime, view: TourStepView): void {
    if (view.quality !== undefined) {
        runtime.setQualityPreset(view.quality);
        if (view.shadows !== undefined) {
            runtime.setShadowQuality(shadowQualityForPreset(view.quality, view.shadows));
        }
    } else if (view.shadows !== undefined) {
        runtime.setShadowQuality(shadowQualityForPreset(runtime.getQualityPreset(), view.shadows));
    }
    if (view.timeScale !== undefined && Number.isFinite(view.timeScale)) {
        runtime.setTimeScale(view.timeScale);
    }
    if (view.paused !== undefined) {
        runtime.setPaused(view.paused);
    }
    if (view.orbitScale === 0 || view.orbitScale === 1) {
        runtime.setOrbitScaleMode(view.orbitScale);
    }
    if (view.orbitLines !== undefined) {
        runtime.setOrbitLines(view.orbitLines);
    }
    if (view.magneticFields !== undefined) {
        runtime.setMagneticFields(view.magneticFields);
    }

    if (view.landmark) {
        const landmark = SKY_EVENT_LANDMARKS.find((entry) => entry.id === view.landmark);
        if (landmark) {
            runtime.setSimulationEpoch(landmark.julianDate);
        }
    } else if (view.skyEvent) {
        const next = runtime.getNextSkyEvent();
        if (next.valid) {
            runtime.setSimulationEpoch(next.julianDate);
        }
    } else if (view.julianDate !== undefined && Number.isFinite(view.julianDate)) {
        runtime.setSimulationEpoch(view.julianDate);
    }

    if (view.camera) {
        runtime.setCameraPose(view.camera.x, view.camera.y, view.camera.z, view.camera.yaw, view.camera.pitch);
    } else if (view.mission) {
        const catalog = runtime.getMissionCatalog();
        const match = catalog.find((mission) => mission.id === view.mission);
        if (match) {
            runtime.focusMission(match.index);
        }
    } else if (view.planet !== undefined) {
        runtime.focusPlanet(view.planet);
    }
}

export interface TourPlayer {
    play(tour: GuidedTour, startStep?: number): void;
    stop(): void;
    isPlaying(): boolean;
    currentTour(): GuidedTour | null;
    currentStepIndex(): number;
    dispose(): void;
}

export function createTourPlayer(options: {
    runtime: SolarSystemRuntime;
    onCaption?: (text: string) => void;
    onStatus?: (text: string) => void;
    onStep?: (tour: GuidedTour, stepIndex: number) => void;
}): TourPlayer {
    let tour: GuidedTour | null = null;
    let startMs = 0;
    let startOffsetSec = 0;
    let lastStep = -1;
    let raf = 0;

    const tick = (): void => {
        if (!tour) {
            return;
        }
        const elapsed = (performance.now() - startMs) / 1000 + startOffsetSec;
        if (elapsed >= tour.durationSec) {
            const last = tour.steps.length - 1;
            if (lastStep !== last) {
                applyTourView(options.runtime, tour.steps[last].view);
                lastStep = last;
                options.onCaption?.(tour.steps[last].narrationCue ?? tour.steps[last].label);
                options.onStep?.(tour, last);
            }
            options.onStatus?.(`${tour.title} complete`);
            tour = null;
            raf = 0;
            return;
        }
        const index = stepIndexAtTime(tour, elapsed);
        if (index !== lastStep) {
            applyTourView(options.runtime, tour.steps[index].view);
            lastStep = index;
            options.onCaption?.(tour.steps[index].narrationCue ?? tour.steps[index].label);
            options.onStatus?.(`${tour.title}: ${tour.steps[index].label}`);
            options.onStep?.(tour, index);
        }
        raf = window.requestAnimationFrame(tick);
    };

    return {
        play(next, startStep = 0) {
            if (raf) {
                window.cancelAnimationFrame(raf);
            }
            tour = next;
            lastStep = -1;
            const clamped = Math.max(0, Math.min(startStep, next.steps.length - 1));
            startOffsetSec = next.steps[clamped].atSec;
            startMs = performance.now();
            options.onStatus?.(`${next.title}: ${next.steps[clamped].label}`);
            raf = window.requestAnimationFrame(tick);
        },
        stop() {
            if (raf) {
                window.cancelAnimationFrame(raf);
                raf = 0;
            }
            tour = null;
            lastStep = -1;
            options.onCaption?.('');
            options.onStatus?.('Tour stopped');
        },
        isPlaying: () => tour !== null,
        currentTour: () => tour,
        currentStepIndex: () => lastStep,
        dispose() {
            if (raf) {
                window.cancelAnimationFrame(raf);
            }
            tour = null;
        },
    };
}

export async function loadTour(id: string, baseUrl = window.location.href): Promise<GuidedTour | null> {
    const url = new URL(`tours/${id}.json`, new URL(import.meta.env.BASE_URL, baseUrl));
    const response = await fetch(url);
    if (!response.ok) {
        return null;
    }
    return parseTourJson(await response.json());
}
