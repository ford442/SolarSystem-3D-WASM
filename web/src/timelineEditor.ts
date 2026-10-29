/**
 * The cinematic dock: pick a shipped timeline or build one, scrub and play it, add keyframes
 * at the current camera pose, export or import JSON, share `?timeline=` links, and record a
 * video (capture.ts). Playback and the engine side live in timelinePlayer.ts.
 */
import { captureVideo, downloadBlob, type CaptureSettings } from './capture.js';
import type { DeepLinkViewState } from './deepLink.js';
import { isoDateFromJulianDate } from './ephemeris.js';
import { keyframeFromPose, parseTimeline, type LengthUnits, type Timeline, type TimelineKeyframe } from './timeline.js';
import {
    SHIPPED_TIMELINES,
    createFocusDirectory,
    createTimelinePlayer,
    fetchTimeline,
    sampleScene,
    type TimelinePlayer,
    type TimelinePlayerState,
} from './timelinePlayer.js';
import type { TourPlayer } from './tourPlayer.js';
import type { SolarSystemRuntime } from './wasmBridge.js';

const DRAFT_KEY = 'solarSystem.timelineDraft';
const CUSTOM = '__custom';
/** Gap after the last keyframe for a new one, seconds. */
const KEY_SPACING = 4;

function byId<T extends HTMLElement>(id: string): T {
    const element = document.getElementById(id);
    if (!element) {
        throw new Error(`#${id} missing from index.html`);
    }
    return element as T;
}

function readDraft(): Timeline | null {
    try {
        const raw = localStorage.getItem(DRAFT_KEY);
        const parsed = raw ? parseTimeline(JSON.parse(raw)) : null;
        return parsed?.ok ? parsed.timeline : null;
    } catch {
        return null;
    }
}

function writeDraft(timeline: Timeline): void {
    try {
        if (timeline.keyframes.length > 0) {
            localStorage.setItem(DRAFT_KEY, JSON.stringify(timeline));
        }
    } catch {
        // Private window or storage blocked: the draft just does not survive a reload.
    }
}

const emptyTimeline = (): Timeline => ({ id: 'my-timeline', title: 'My timeline', durationSec: 0, keyframes: [] });

export function initTimelineEditor(options: {
    runtime: SolarSystemRuntime;
    canvas: HTMLCanvasElement;
    deepLink: DeepLinkViewState;
    tourPlayer?: TourPlayer;
    caption: HTMLElement;
}): TimelinePlayer {
    const { runtime, canvas, deepLink, tourPlayer, caption } = options;
    const panel = byId<HTMLElement>('timeline-panel');
    const openButton = byId<HTMLButtonElement>('timeline-open');
    const select = byId<HTMLSelectElement>('timeline-select');
    const playButton = byId<HTMLButtonElement>('timeline-play');
    const stopButton = byId<HTMLButtonElement>('timeline-stop');
    const closeButton = byId<HTMLButtonElement>('timeline-close');
    const scrub = byId<HTMLInputElement>('timeline-scrub');
    const timeOutput = byId<HTMLOutputElement>('timeline-time');
    const focusSelect = byId<HTMLSelectElement>('timeline-focus');
    const unitsSelect = byId<HTMLSelectElement>('timeline-units');
    const addKeyButton = byId<HTMLButtonElement>('timeline-add-key');
    const keyList = byId<HTMLOListElement>('timeline-keys');
    const newButton = byId<HTMLButtonElement>('timeline-new');
    const exportButton = byId<HTMLButtonElement>('timeline-export');
    const importInput = byId<HTMLInputElement>('timeline-import');
    const copyLinkButton = byId<HTMLButtonElement>('timeline-copy-link');
    const captureSize = byId<HTMLSelectElement>('capture-size');
    const captureFps = byId<HTMLSelectElement>('capture-fps');
    const captureStart = byId<HTMLButtonElement>('capture-start');
    const captureCancel = byId<HTMLButtonElement>('capture-cancel');
    const captureProgress = byId<HTMLProgressElement>('capture-progress');
    const status = byId<HTMLElement>('timeline-status');

    let working: Timeline = emptyTimeline();
    /** The shipped id `working` was loaded from while unedited (its `?timeline=` link still holds). */
    let shippedId: string | null = null;
    let scrubbing = false;
    let capturing: AbortController | null = null;
    const stateListeners = new Set<(state: TimelinePlayerState) => void>();

    const setStatus = (text: string): void => {
        status.textContent = text;
    };

    const player = createTimelinePlayer({
        runtime,
        onTime: (t) => {
            if (!scrubbing) {
                scrub.value = String(t);
            }
            showTime(t);
        },
        onCaption: (text) => {
            caption.hidden = !text;
            caption.textContent = text;
        },
        onState: (state) => {
            playButton.textContent = state === 'playing' ? 'Pause' : 'Play';
            if (state === 'ended') {
                setStatus('End of the timeline. Stop gives the camera back.');
            }
            stateListeners.forEach((listener) => listener(state));
        },
    });

    const showTime = (t: number): void => {
        const jd = runtime.getSimulationEpoch();
        timeOutput.textContent = `${t.toFixed(1)} / ${working.durationSec.toFixed(1)} s`;
        timeOutput.title = player.holdsCamera() ? isoDateFromJulianDate(jd) : '';
    };

    const refreshControls = (): void => {
        const hasKeys = working.keyframes.length > 0;
        playButton.disabled = !hasKeys || capturing !== null;
        stopButton.disabled = capturing !== null;
        scrub.disabled = !hasKeys || capturing !== null;
        captureStart.disabled = !hasKeys || capturing !== null;
        exportButton.disabled = !hasKeys;
        scrub.max = String(Math.max(working.durationSec, 0.01));
        showTime(player.time());
    };

    const describeKey = (key: TimelineKeyframe): string => {
        const date = key.jd !== undefined ? isoDateFromJulianDate(key.jd) : 'date follows';
        const look = key.camera.rotation ? '' : key.camera.lookAt ? ` → ${String(key.camera.lookAt)}` : '';
        return `${key.focus ?? 'sun'}${look} · ${date}${key.caption ? ` · “${key.caption}”` : ''}`;
    };

    const renderKeys = (): void => {
        keyList.replaceChildren();
        working.keyframes.forEach((key, index) => {
            const item = document.createElement('li');
            const time = document.createElement('input');
            time.type = 'number';
            time.min = '0';
            time.step = '0.1';
            time.value = String(key.t);
            time.setAttribute('aria-label', `Keyframe ${index + 1} time in seconds`);
            time.addEventListener('change', () => {
                const t = Number(time.value);
                if (Number.isFinite(t) && t >= 0) {
                    key.t = t;
                    edited();
                }
            });
            const label = document.createElement('span');
            label.className = 'timeline-key-label';
            label.textContent = describeKey(key);
            label.title = label.textContent;
            const go = document.createElement('button');
            go.type = 'button';
            go.className = 'timeline-btn';
            go.textContent = 'Go';
            go.title = 'Show this keyframe';
            go.addEventListener('click', () => {
                player.seek(key.t);
            });
            const replace = document.createElement('button');
            replace.type = 'button';
            replace.className = 'timeline-btn';
            replace.textContent = 'Set';
            replace.title = 'Replace with the current camera pose';
            replace.addEventListener('click', () => {
                const fresh = keyFromCurrentPose(key.t, key.focus ?? 'sun', key.camera.units ?? 'scene');
                if (fresh) {
                    working.keyframes[index] = { ...key, ...fresh, camera: fresh.camera };
                    edited();
                }
            });
            const remove = document.createElement('button');
            remove.type = 'button';
            remove.className = 'timeline-btn';
            remove.textContent = '×';
            remove.setAttribute('aria-label', `Delete keyframe ${index + 1}`);
            remove.addEventListener('click', () => {
                working.keyframes.splice(index, 1);
                edited();
            });
            item.append(time, label, go, replace, remove);
            keyList.appendChild(item);
        });
    };

    /** Keys sorted with distinct times, duration to the last key, then everything redrawn. */
    const edited = (): void => {
        working.keyframes.sort((a, b) => a.t - b.t);
        for (let i = 1; i < working.keyframes.length; ++i) {
            if (working.keyframes[i].t <= working.keyframes[i - 1].t) {
                working.keyframes[i].t = Math.round((working.keyframes[i - 1].t + 0.1) * 100) / 100;
            }
        }
        const last = working.keyframes[working.keyframes.length - 1];
        working.durationSec = last ? last.t : 0;
        shippedId = null;
        select.value = CUSTOM;
        writeDraft(working);
        if (working.keyframes.length > 0) {
            if (player.current()) {
                player.update(working);
            } else {
                player.load(working);
            }
        }
        renderKeys();
        refreshControls();
    };

    const use = (timeline: Timeline, fromShipped: string | null): void => {
        working = timeline;
        shippedId = fromShipped;
        select.value = fromShipped ?? CUSTOM;
        player.load(timeline);
        scrub.value = '0';
        renderKeys();
        refreshControls();
    };

    const keyFromCurrentPose = (t: number, focus: string, units: LengthUnits): TimelineKeyframe | null => {
        const directory = createFocusDirectory(runtime);
        const focusState = sampleScene(runtime, directory, runtime.getSceneState()).body(focus);
        if (!focusState) {
            setStatus(`${focus} is not loaded yet; fly closer or pick another focus.`);
            return null;
        }
        const pose = runtime.getCameraPose();
        return keyframeFromPose({
            t,
            jd: runtime.getSimulationEpoch(),
            focus,
            units,
            focusState,
            position: [pose.x, pose.y, pose.z],
            yaw: pose.yaw,
            pitch: pose.pitch,
            roll: runtime.getCameraRoll(),
            fov: runtime.getCameraFov(),
        });
    };

    const fillSelects = (): void => {
        select.replaceChildren();
        for (const shipped of SHIPPED_TIMELINES) {
            select.add(new Option(shipped.title, shipped.id));
        }
        select.add(new Option('My timeline (editor)', CUSTOM));

        focusSelect.replaceChildren();
        const directory = createFocusDirectory(runtime);
        for (const body of directory.bodies) {
            focusSelect.add(new Option(body.name, body.id));
        }
        for (const mission of directory.missions) {
            focusSelect.add(new Option(`${mission.name} (probe)`, `mission:${mission.id}`));
        }
    };

    const selectNearestFocus = (): void => {
        // GetNearestPlanetIndex is 1–15, the catalog index of a primary.
        const nearest = runtime.getNearestPlanetIndex();
        const entry = createFocusDirectory(runtime).bodies.find((body) => body.index === nearest);
        focusSelect.value = entry?.id ?? 'sun';
    };

    const loadShipped = async (id: string): Promise<boolean> => {
        setStatus('Loading…');
        const result = await fetchTimeline(id);
        if (!result.ok) {
            setStatus(`Could not load ${id}: ${result.error}`);
            return false;
        }
        use(result.timeline, SHIPPED_TIMELINES.some((shipped) => shipped.id === result.timeline.id) ? result.timeline.id : null);
        setStatus(result.timeline.description ?? result.timeline.title);
        return true;
    };

    const open = (): void => {
        panel.hidden = false;
        document.body.classList.add('timeline-open');
        if (select.options.length === 0) {
            fillSelects();
        }
        selectNearestFocus();
    };

    const close = (): void => {
        if (capturing) {
            return;
        }
        player.stop();
        panel.hidden = true;
        document.body.classList.remove('timeline-open');
    };

    /** Show the first frame as soon as its focus bodies have loaded, then play. */
    const playWhenReady = async (fromSec: number): Promise<void> => {
        const deadline = performance.now() + 30_000;
        while (!player.seek(fromSec)) {
            if (performance.now() > deadline) {
                setStatus('The bodies this timeline follows did not load.');
                return;
            }
            setStatus('Waiting for the scene to load…');
            await new Promise((resolve) => setTimeout(resolve, 250));
        }
        setStatus(working.description ?? working.title);
        player.play(fromSec);
    };

    for (const eventName of ['pointerdown', 'pointerup', 'click', 'keydown', 'keyup']) {
        panel.addEventListener(eventName, (event) => event.stopPropagation());
    }

    openButton.addEventListener('click', open);
    closeButton.addEventListener('click', close);

    select.addEventListener('change', () => {
        if (select.value === CUSTOM) {
            use(readDraft() ?? emptyTimeline(), null);
            setStatus(working.keyframes.length ? 'Your draft timeline.' : 'Fly somewhere and add a keyframe.');
        } else {
            void loadShipped(select.value);
        }
    });

    playButton.addEventListener('click', () => {
        if (player.state() === 'playing') {
            player.pause();
            return;
        }
        tourPlayer?.stop();
        void playWhenReady(player.state() === 'ended' ? 0 : player.time());
    });

    stopButton.addEventListener('click', () => {
        player.stop();
        scrub.value = '0';
        setStatus('Camera released.');
        showTime(0);
    });

    scrub.addEventListener('input', () => {
        scrubbing = true;
        tourPlayer?.stop();
        player.seek(Number(scrub.value));
    });
    scrub.addEventListener('change', () => {
        scrubbing = false;
    });

    addKeyButton.addEventListener('click', () => {
        if (select.value !== CUSTOM && working.keyframes.length > 0) {
            // Editing a shipped timeline makes it the draft.
            working = { ...working, id: `${working.id}-edit`, keyframes: [...working.keyframes] };
        }
        const last = working.keyframes[working.keyframes.length - 1];
        // While the timeline holds the camera, the new key goes where the scrubber is.
        const t = player.holdsCamera() && player.time() > (last?.t ?? -1) ? player.time() : last ? last.t + KEY_SPACING : 0;
        const key = keyFromCurrentPose(t, focusSelect.value, unitsSelect.value as LengthUnits);
        if (!key) {
            return;
        }
        working.keyframes.push(key);
        edited();
        setStatus(`Keyframe ${working.keyframes.length} at ${key.t.toFixed(1)} s, relative to ${key.focus}.`);
    });

    newButton.addEventListener('click', () => {
        use(emptyTimeline(), null);
        setStatus('New timeline: fly somewhere and add a keyframe.');
    });

    exportButton.addEventListener('click', () => {
        const json = `${JSON.stringify(working, null, 2)}\n`;
        downloadBlob(new Blob([json], { type: 'application/json' }), `${working.id}.json`);
        setStatus(`Exported ${working.id}.json. Host it and link ?timeline=<its URL>.`);
    });

    importInput.addEventListener('change', () => {
        const file = importInput.files?.[0];
        importInput.value = '';
        if (!file) {
            return;
        }
        void file.text().then((text) => {
            const result = parseTimeline(JSON.parse(text));
            if (!result.ok) {
                setStatus(`Not a timeline: ${result.error}`);
                return;
            }
            use(result.timeline, null);
            writeDraft(result.timeline);
            setStatus(`Imported ${result.timeline.title}.`);
        }).catch((error: unknown) => {
            setStatus(`Could not read ${file.name}: ${error instanceof Error ? error.message : String(error)}`);
        });
    });

    copyLinkButton.addEventListener('click', () => {
        if (!shippedId) {
            setStatus('Only shipped timelines have a link. Export this one, host the JSON, and link ?timeline=<its URL>.');
            return;
        }
        const url = new URL(window.location.href);
        url.search = '';
        url.searchParams.set('timeline', shippedId);
        if (player.time() > 0) {
            url.searchParams.set('tt', player.time().toFixed(1));
        }
        void (navigator.clipboard?.writeText(url.toString()) ?? Promise.reject(new Error('no clipboard')))
            .then(() => setStatus('Link copied.'), () => setStatus(url.toString()));
    });

    captureStart.addEventListener('click', () => {
        const fps = Number(captureFps.value);
        const [width, height] = captureSize.value === 'canvas'
            ? [canvas.width, canvas.height]
            : captureSize.value.split('x').map(Number);
        const settings: CaptureSettings = { width, height, fps };
        tourPlayer?.stop();
        player.pause();
        capturing = new AbortController();
        const { signal } = capturing;
        captureCancel.hidden = false;
        captureProgress.hidden = false;
        captureProgress.value = 0;
        document.body.classList.add('capturing');
        refreshControls();
        const started = performance.now();
        void captureVideo({
            runtime,
            canvas,
            durationSec: working.durationSec,
            renderAt: (t) => {
                const frame = player.renderAt(t);
                scrub.value = String(t);
                showTime(t);
                return frame !== null;
            },
            playRealtime: (abort) => new Promise<void>((resolve) => {
                const done = (state: TimelinePlayerState): void => {
                    if (state === 'ended' || state === 'idle') {
                        stateListeners.delete(done);
                        resolve();
                    }
                };
                stateListeners.add(done);
                abort?.addEventListener('abort', () => player.stop(), { once: true });
                player.play(0);
            }),
        }, settings, {
            signal,
            onProgress: (done, total, phase) => {
                captureProgress.value = total > 0 ? done / total : 0;
                setStatus(`Recording: ${phase}, frame ${done} of ${total}`);
            },
        }).then((result) => {
            const seconds = ((performance.now() - started) / 1000).toFixed(0);
            downloadBlob(result.blob, `${working.id}.${result.extension}`);
            const size = result.blob.size >= 1e6
                ? `${(result.blob.size / 1e6).toFixed(1)} MB`
                : `${Math.ceil(result.blob.size / 1e3)} kB`;
            setStatus(result.method === 'webcodecs'
                ? `Saved ${working.id}.mp4: ${result.frames} frames, ${result.codec}, ${size}, in ${seconds} s.`
                : `Saved ${working.id}.${result.extension} (recorded in real time at the window's size; ${size}).`);
        }).catch((error: unknown) => {
            const cancelled = error instanceof DOMException && error.name === 'AbortError';
            setStatus(cancelled ? 'Recording cancelled.' : `Recording failed: ${error instanceof Error ? error.message : String(error)}`);
            if (!cancelled) {
                console.error('[Capture]', error);
            }
        }).finally(() => {
            capturing = null;
            captureCancel.hidden = true;
            captureProgress.hidden = true;
            document.body.classList.remove('capturing');
            refreshControls();
        });
    });

    captureCancel.addEventListener('click', () => {
        capturing?.abort();
    });

    refreshControls();

    if (deepLink.timeline) {
        open();
        const source = deepLink.timeline;
        void fetchTimeline(source).then((result) => {
            if (!result.ok) {
                setStatus(`Could not load the linked timeline: ${result.error}`);
                return;
            }
            use(result.timeline, SHIPPED_TIMELINES.some((shipped) => shipped.id === source) ? source : null);
            void playWhenReady(deepLink.timelineTime ?? 0);
        });
    }

    window.solarSystemTimeline = {
        player,
        load: async (source: string) => {
            const result = await fetchTimeline(source);
            if (result.ok) {
                open();
                use(result.timeline, null);
            }
            return result;
        },
        // One frame through the capture path, as a PNG data URL (null while a focus is
        // loading): for checking a shot from the console or a test without waiting on rAF.
        snapshot: (t: number, width = 0, height = 0) => {
            const began = runtime.beginCapture(width, height);
            try {
                const frame = player.renderAt(t);
                runtime.stepCaptureFrame(0);
                return frame ? canvas.toDataURL('image/png') : null;
            } finally {
                if (began) {
                    runtime.endCapture();
                }
            }
        },
    };

    return player;
}
