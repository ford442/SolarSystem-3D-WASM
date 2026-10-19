/**
 * Background music bed for the web build.
 *
 * The WASM module opens no audio device: SDL_mixer's Emscripten backend sat on a deprecated
 * ScriptProcessorNode, and a single music bed needs nothing more than HTMLAudioElement. This
 * mirrors the native player (src/AudioPlayer.cpp): shuffled playlist, 5 s fade in/out per
 * track, loop forever. Volume and mute are owned here (the settings panel persists them);
 * the only scene fact needed from C++ is the duck amount, delivered via Module.onMusicDuck.
 */

/** Keep in sync with GetBackgroundSongPaths() in src/ResourceManifest.cpp. */
export const MUSIC_TRACKS: readonly string[] = [
    'resource/sounds/Stellardrone - Galaxies.mp3',
    'resource/sounds/Stellardrone - Mars.mp3',
    'resource/sounds/Stellardrone - Billions And Billions.mp3',
    'resource/sounds/Stellardrone - Gravitation (Remix).mp3',
    'resource/sounds/Stellardrone - The Edge of Forever.mp3',
];

export const DEFAULT_MUSIC_VOLUME = 0.3;
const FADE_SECONDS = 5;
const TICK_MS = 100;
// Ducking arrives per frame from C++; smooth it so a step does not click.
const DUCK_SMOOTHING_PER_TICK = 0.25;
const VOLUME_STEP = 0.05;

export interface MusicPlayer {
    setVolume(volume: number): void;
    getVolume(): number;
    setMuted(muted: boolean): void;
    getMuted(): boolean;
    /** 0–1 gain from scene proximity (1 = no ducking). */
    setDuck(duck: number): void;
    /** Begin playback; if the browser blocks autoplay, waits for the first user gesture. */
    start(): void;
    /** Fires after PageUp/PageDown change the volume so the panel can mirror it. */
    onVolumeKey(handler: (volume: number) => void): void;
}

export interface MusicPlayerOptions {
    /** Absolute or page-relative base URL that `resource/sounds/...` is appended to. */
    assetBase: string;
    tracks?: readonly string[];
}

function shuffled<T>(items: readonly T[]): T[] {
    const out = items.slice();
    for (let i = out.length - 1; i > 0; i--) {
        const j = Math.floor(Math.random() * (i + 1));
        [out[i], out[j]] = [out[j], out[i]];
    }
    return out;
}

function clamp01(value: number): number {
    return Number.isFinite(value) ? Math.min(1, Math.max(0, value)) : 0;
}

function isTypingTarget(target: EventTarget | null): boolean {
    return target instanceof HTMLElement
        && (target.isContentEditable || /^(INPUT|TEXTAREA|SELECT)$/.test(target.tagName));
}

export function createMusicPlayer(options: MusicPlayerOptions): MusicPlayer {
    const base = new URL(options.assetBase, window.location.href);
    const playlist = shuffled(options.tracks ?? MUSIC_TRACKS);
    const unplayable = new Set<string>();

    let volume = DEFAULT_MUSIC_VOLUME;
    let muted = false;
    let duck = 1;
    let smoothedDuck = 1;
    let index = -1;
    let started = false;
    let waitingForGesture = false;
    let audio: HTMLAudioElement | null = null;
    let volumeKeyHandler: ((volume: number) => void) | undefined;

    function fadeGain(element: HTMLAudioElement): number {
        const t = element.currentTime;
        const d = element.duration;
        let gain = Math.min(1, t / FADE_SECONDS);
        if (Number.isFinite(d) && d > 0) {
            gain = Math.min(gain, Math.max(0, (d - t) / FADE_SECONDS));
        }
        return gain;
    }

    function applyVolume(): void {
        if (!audio) return;
        audio.muted = muted;
        audio.volume = clamp01(volume * smoothedDuck * fadeGain(audio));
    }

    function tick(): void {
        smoothedDuck += (duck - smoothedDuck) * DUCK_SMOOTHING_PER_TICK;
        applyVolume();
    }

    function skip(path: string, reason: string): void {
        // Same contract as the old LoadOptionalSounds: log, skip, never block loading.
        console.warn(`[Audio] Missing track (skipped): ${path} (${reason})`);
        unplayable.add(path);
        playNext();
    }

    function playNext(): void {
        if (audio) {
            audio.removeAttribute('src');
            audio.load();
            audio = null;
        }
        for (let attempt = 0; attempt < playlist.length; attempt++) {
            index = (index + 1) % playlist.length;
            const path = playlist[index];
            if (unplayable.has(path)) continue;

            const element = new Audio(new URL(path, base).toString());
            element.preload = 'auto';
            element.addEventListener('ended', playNext);
            element.addEventListener('error', () => skip(path, element.error?.message || 'load failed'));
            audio = element;
            applyVolume();
            element.play().then(() => {
                console.log(`[Audio] Now playing: ${path.replace(/^resource\/sounds\//, '')}`);
            }).catch((error: unknown) => {
                if (audio !== element) return;
                if (error instanceof DOMException && error.name === 'NotAllowedError') {
                    // Autoplay policy: retry on the first gesture, same track.
                    index = (index + playlist.length - 1) % playlist.length;
                    audio = null;
                    awaitGesture();
                } else if (!(error instanceof DOMException && error.name === 'AbortError')) {
                    skip(path, String(error));
                }
            });
            return;
        }
        console.warn('[Audio] No playable tracks; music disabled');
    }

    function awaitGesture(): void {
        if (waitingForGesture) return;
        waitingForGesture = true;
        const resume = () => {
            window.removeEventListener('pointerdown', resume, true);
            window.removeEventListener('keydown', resume, true);
            window.removeEventListener('touchend', resume, true);
            waitingForGesture = false;
            playNext();
        };
        window.addEventListener('pointerdown', resume, true);
        window.addEventListener('keydown', resume, true);
        window.addEventListener('touchend', resume, true);
    }

    window.addEventListener('keydown', (event) => {
        if (isTypingTarget(event.target)) return;
        const delta = event.code === 'PageUp' ? VOLUME_STEP : event.code === 'PageDown' ? -VOLUME_STEP : 0;
        if (delta === 0) return;
        player.setVolume(Math.round((volume + delta) * 100) / 100);
        volumeKeyHandler?.(volume);
    });

    const player: MusicPlayer = {
        setVolume(next) {
            volume = clamp01(next);
            applyVolume();
        },
        getVolume: () => volume,
        setMuted(next) {
            muted = next;
            applyVolume();
        },
        getMuted: () => muted,
        setDuck(next) {
            duck = clamp01(next);
        },
        start() {
            if (started || playlist.length === 0) return;
            started = true;
            window.setInterval(tick, TICK_MS);
            playNext();
        },
        onVolumeKey(handler) {
            volumeKeyHandler = handler;
        },
    };

    return player;
}
