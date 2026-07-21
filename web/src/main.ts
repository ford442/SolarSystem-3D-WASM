import './style.css'
import Module, {
    type OrbitScaleMode,
    type QualityPreset,
    type ShadowQuality,
    type SolarSystemModuleConfig,
} from './SolarSystem.js'
import { initTouchControls, isMobileLikeDevice } from './touchControls'
import { PlanetExplorer } from './planetExplorer'
import { initWebXr } from './webxr'

// Emscripten 6 prefers resizable WebAssembly buffers when the browser exposes
// toResizableBuffer(). Current Chrome DOM/WebGL APIs reject views backed by
// those buffers. Hiding the optional method selects Emscripten's built-in
// fixed-buffer fallback; memory growth still works by refreshing heap views.
const wasmMemoryPrototype = WebAssembly.Memory.prototype as WebAssembly.Memory & {
    toResizableBuffer?: () => ArrayBuffer;
};
if (typeof wasmMemoryPrototype.toResizableBuffer === 'function') {
    Object.defineProperty(wasmMemoryPrototype, 'toResizableBuffer', {
        configurable: true,
        value: undefined,
    });
}

const canvas = document.getElementById('canvas') as HTMLCanvasElement;
const loadingContainer = document.getElementById('loading-container') as HTMLElement;
const progressBar = document.getElementById('progress-bar') as HTMLElement;
const progressText = document.getElementById('progress-text') as HTMLElement;
const streamingProgress = document.getElementById('streaming-progress') as HTMLElement;
const streamingText = document.getElementById('streaming-text') as HTMLElement;
const streamingBar = document.getElementById('streaming-progress-bar') as HTMLElement;
const settingsPanel = document.getElementById('settings-panel') as HTMLElement;
const settingsToggle = document.getElementById('settings-toggle') as HTMLButtonElement;
const settingsToggleIcon = settingsToggle.querySelector('.settings-toggle-icon') as HTMLElement;
const settingsFieldset = document.getElementById('settings-fieldset') as HTMLFieldSetElement;
const qualitySelect = document.getElementById('quality-preset') as HTMLSelectElement;
const timeScaleInput = document.getElementById('time-scale') as HTMLInputElement;
const timeScaleValue = document.getElementById('time-scale-value') as HTMLOutputElement;
const simulationDateInput = document.getElementById('simulation-date') as HTMLInputElement;
const simulationDateSet = document.getElementById('simulation-date-set') as HTMLButtonElement;
const simulationDateNow = document.getElementById('simulation-date-now') as HTMLButtonElement;
const pausedInput = document.getElementById('simulation-paused') as HTMLInputElement;
const shadowsInput = document.getElementById('shadows-enabled') as HTMLInputElement;
const orbitLinesInput = document.getElementById('orbit-lines-enabled') as HTMLInputElement;
const musicVolumeInput = document.getElementById('music-volume') as HTMLInputElement;
const musicVolumeValue = document.getElementById('music-volume-value') as HTMLOutputElement;
const musicMutedInput = document.getElementById('music-muted') as HTMLInputElement;
const settingsReset = document.getElementById('settings-reset') as HTMLButtonElement;
const settingsStatus = document.getElementById('settings-status') as HTMLElement;
const enterVrButton = document.getElementById('enter-vr') as HTMLButtonElement;
const exitVrButton = document.getElementById('exit-vr') as HTMLButtonElement;
const explorerPanel = document.getElementById('explorer-panel') as HTMLElement;
const deployedBaseUrl = new URL(import.meta.env.BASE_URL, window.location.href);
const isMobileDevice = isMobileLikeDevice();
// Use bundled same-origin placeholders by default in every mode. Developers can
// opt into a separate runtime asset host with VITE_ASSET_BASE when needed.
const runtimeAssetBase = import.meta.env.VITE_ASSET_BASE?.trim() || deployedBaseUrl.toString();

// Global progress tracking

// Function to update progress bar
function updateProgress(loaded: number, total: number) {
    
    const percentage = total > 0 ? Math.round((loaded / total) * 100) : 0;
    
    if (progressBar && progressText) {
        progressBar.style.width = percentage + '%';
        progressText.textContent = percentage + '%';
    }
    
    console.log(`Loading progress: ${loaded}/${total} (${percentage}%)`);
    
    // Hide loading screen when complete
    if (loaded >= total && total > 0) {
        setTimeout(() => {
            if (loadingContainer) {
                loadingContainer.classList.add('hidden');
                // Ensure it cannot block mouse/keyboard interaction after initial load
                loadingContainer.style.pointerEvents = 'none';
                loadingContainer.style.zIndex = '-1';
            }
        }, 500);
    }
}

// Called by C++ (via EM_ASM) to show high-res texture streaming progress.
// completed = settled textures; total = cumulative queued; active = in-flight downloads.
function updateStreamingProgress(completed: number, total: number, active = 0) {
    if (!streamingProgress) return;
    if (total <= 0) {
        streamingProgress.style.display = 'none';
        return;
    }
    streamingProgress.style.display = 'block';
    const pct = total > 0 ? Math.round((completed / total) * 100) : 0;
    const activeSuffix = active > 0 ? `, ${active} active` : '';
    if (streamingText) {
        streamingText.textContent = `High-res upgrade: ${completed}/${total} (${pct}%${activeSuffix})`;
    } else {
        streamingProgress.textContent = `High-res upgrade: ${completed}/${total}`;
    }
    if (streamingBar) {
        streamingBar.style.width = pct + '%';
    }
    if (completed >= total) {
        // Auto-hide after streaming is done
        setTimeout(() => {
            if (streamingProgress) streamingProgress.style.display = 'none';
        }, 2500);
    }
}

// Expose progress functions globally for C++ to call
window.updateLoadingProgress = updateProgress;
window.updateStreamingProgress = updateStreamingProgress;
window.__solarSystemAssetBase = runtimeAssetBase;

const SETTINGS_STORAGE_KEY = 'solar-system.settings.v1';
const MIN_TIME_SCALE = 0.01;
const MAX_TIME_SCALE = 10000;

interface PersistedSettings {
    quality: QualityPreset;
    timeScale: number;
    paused: boolean;
    shadows: boolean;
    orbitLines: boolean;
    musicVolume: number;
    musicMuted: boolean;
    simulationDate?: string;
}

function isQualityPreset(value: unknown): value is QualityPreset {
    return value === 0 || value === 1 || value === 2;
}

function readPersistedSettings(): Partial<PersistedSettings> {
    try {
        const raw = window.localStorage.getItem(SETTINGS_STORAGE_KEY);
        if (!raw) return {};
        const parsed = JSON.parse(raw) as Record<string, unknown>;
        return {
            quality: isQualityPreset(parsed.quality) ? parsed.quality : undefined,
            timeScale: typeof parsed.timeScale === 'number' && Number.isFinite(parsed.timeScale)
                ? Math.min(MAX_TIME_SCALE, Math.max(MIN_TIME_SCALE, parsed.timeScale))
                : undefined,
            paused: typeof parsed.paused === 'boolean' ? parsed.paused : undefined,
            shadows: typeof parsed.shadows === 'boolean' ? parsed.shadows : undefined,
            orbitLines: typeof parsed.orbitLines === 'boolean' ? parsed.orbitLines : undefined,
            musicVolume: typeof parsed.musicVolume === 'number' && Number.isFinite(parsed.musicVolume)
                ? Math.min(100, Math.max(0, Math.round(parsed.musicVolume)))
                : undefined,
            musicMuted: typeof parsed.musicMuted === 'boolean' ? parsed.musicMuted : undefined,
            simulationDate: typeof parsed.simulationDate === 'string' && /^\d{4}-\d{2}-\d{2}$/.test(parsed.simulationDate)
                ? parsed.simulationDate
                : undefined,
        };
    } catch (error) {
        console.warn('[Settings] Could not read saved settings:', error);
        return {};
    }
}

function currentPanelSettings(): PersistedSettings {
    return {
        quality: Number(qualitySelect.value) as QualityPreset,
        timeScale: Math.pow(10, Number(timeScaleInput.value)),
        paused: pausedInput.checked,
        shadows: shadowsInput.checked,
        orbitLines: orbitLinesInput.checked,
        musicVolume: Number(musicVolumeInput.value),
        musicMuted: musicMutedInput.checked,
        simulationDate: simulationDateInput.value || undefined,
    };
}

function persistPanelSettings(): void {
    try {
        window.localStorage.setItem(SETTINGS_STORAGE_KEY, JSON.stringify(currentPanelSettings()));
    } catch (error) {
        console.warn('[Settings] Could not save settings:', error);
    }
}

function formatTimeScale(scale: number): string {
    if (scale < 0.1) return `${scale.toFixed(2)}×`;
    if (scale < 10) return `${scale.toFixed(scale < 1 ? 2 : 1).replace(/\.0+$/, '')}×`;
    return `${Math.round(scale).toLocaleString()}×`;
}

function updateMusicVolumeDisplay(percent: number): void {
    const label = `${percent}%`;
    musicVolumeValue.value = label;
    musicVolumeValue.textContent = label;
}

function applyMusicVolumePercent(percent: number): void {
    const clamped = Math.min(100, Math.max(0, percent));
    musicVolumeInput.value = String(clamped);
    updateMusicVolumeDisplay(clamped);
    window.setMusicVolume?.(clamped / 100);
}

function updateTimeScaleDisplay(scale: number): void {
    timeScaleValue.value = formatTimeScale(scale);
    timeScaleValue.textContent = formatTimeScale(scale);
}

/** Civil UTC date YYYY-MM-DD → Julian Date at 0h (matches C++ Ephemeris::JulianDateFromYmd). */
function julianDateFromIsoDate(isoDate: string): number | undefined {
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

function isoDateFromJulianDate(jd: number): string {
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

function isoDateUtcNow(): string {
    const now = new Date();
    const y = now.getUTCFullYear();
    const m = String(now.getUTCMonth() + 1).padStart(2, '0');
    const d = String(now.getUTCDate()).padStart(2, '0');
    return `${y}-${m}-${d}`;
}

function julianDateUtcNow(): number {
    return Date.now() / 86400000 + 2440587.5;
}

function applySimulationDateIso(isoDate: string, statusMessage?: string): void {
    const jd = julianDateFromIsoDate(isoDate);
    if (jd === undefined) {
        settingsStatus.textContent = 'Invalid date';
        return;
    }
    simulationDateInput.value = isoDate;
    window.setSimulationEpoch?.(jd);
    settingsStatus.textContent = statusMessage ?? `Date set to ${isoDate}`;
    persistPanelSettings();
}

function syncSimulationDateFromRuntime(): void {
    if (document.activeElement === simulationDateInput) {
        return;
    }
    const jd = window.getSimulationEpoch?.();
    if (jd === undefined || !Number.isFinite(jd)) {
        return;
    }
    simulationDateInput.value = isoDateFromJulianDate(jd);
}

function qualityFromUrl(): QualityPreset | undefined {
    const params = new URLSearchParams(window.location.search);
    const value = params.get('quality') ?? params.get('q');
    if (!value) return undefined;
    const normalized = value.toLowerCase();
    if (normalized === 'low' || normalized === '0') return 0;
    if (normalized === 'medium' || normalized === 'med' || normalized === '1') return 1;
    if (normalized === 'full' || normalized === '2') return 2;
    return undefined;
}

function setPanelCollapsed(collapsed: boolean): void {
    settingsPanel.classList.toggle('is-collapsed', collapsed);
    settingsToggle.setAttribute('aria-expanded', String(!collapsed));
    settingsToggleIcon.textContent = collapsed ? '+' : '−';
}

settingsToggle.addEventListener('click', () => {
    setPanelCollapsed(!settingsPanel.classList.contains('is-collapsed'));
});

// Do not let pointer or keyboard input intended for the overlay reach GLFW.
for (const eventName of ['pointerdown', 'pointerup', 'click', 'keydown', 'keyup']) {
    settingsPanel.addEventListener(eventName, (event) => event.stopPropagation());
    explorerPanel.addEventListener(eventName, (event) => event.stopPropagation());
}

const moduleConfig: SolarSystemModuleConfig = {
    canvas: canvas,
    // Request an XR-compatible context up front so makeXRCompatible() can succeed.
    contextAttributes: {
        xrCompatible: true,
        majorVersion: 2,
        minorVersion: 0,
    },
    // Keep Emscripten runtime artifacts under the deployed Vite base path.
    locateFile: (path: string, prefix: string) => {
        if (path.endsWith('.wasm') || path.endsWith('.data')) {
            return new URL(path, deployedBaseUrl).toString();
        }
        return prefix + path;
    },
    print: (text: string) => console.log(text),
    printErr: (text: string) => console.error(text),
    onRuntimeInitialized: () => {
        console.log('SolarSystem WASM initialized');
    }
};

// Initialize
Module(moduleConfig).then((instance) => {
    console.log("Module loaded successfully", instance);
    window.setCameraPose = instance.cwrap('SetCameraPose', null, ['number', 'number', 'number', 'number', 'number']);
    window.setQualityPreset = instance.cwrap('SetQualityPreset', null, ['number']);
    window.getQualityPreset = instance.cwrap('GetQualityPreset', 'number', []);
    window.setTimeScale = instance.cwrap('SetTimeScale', null, ['number']);
    window.getTimeScale = instance.cwrap('GetTimeScale', 'number', []);
    window.setPaused = instance.cwrap('SetPaused', null, ['number']);
    window.getPaused = instance.cwrap('GetPaused', 'number', []);
    window.setSimulationEpoch = instance.cwrap('SetSimulationEpoch', null, ['number']);
    window.getSimulationEpoch = instance.cwrap('GetSimulationEpoch', 'number', []);
    window.setShadowQuality = instance.cwrap('SetShadowQuality', null, ['number']);
    window.getShadowQuality = instance.cwrap('GetShadowQuality', 'number', []);
    window.setTouchMovement = instance.cwrap('SetTouchMovement', null, ['number', 'number', 'number']);
    window.addTouchLook = instance.cwrap('AddTouchLook', null, ['number', 'number']);
    window.addTouchZoom = instance.cwrap('AddTouchZoom', null, ['number']);
    window.isMobileWeb = instance.cwrap('IsMobileWeb', 'number', []);
    window.setMusicVolume = instance.cwrap('SetMusicVolume', null, ['number']);
    window.getMusicVolume = instance.cwrap('GetMusicVolume', 'number', []);
    window.setMusicMuted = instance.cwrap('SetMusicMuted', null, ['number']);
    window.getMusicMuted = instance.cwrap('GetMusicMuted', 'number', []);
    window.focusPlanet = instance.cwrap('FocusPlanet', null, ['number']);
    window.setOrbitScaleMode = instance.cwrap('SetOrbitScaleMode', null, ['number']);
    window.getOrbitScaleMode = instance.cwrap('GetOrbitScaleMode', 'number', []);
    window.getNearestPlanetIndex = instance.cwrap('GetNearestPlanetIndex', 'number', []);
    window.getFocusedPlanetIndex = instance.cwrap('GetFocusedPlanetIndex', 'number', []);
    window.getPlanetSceneDistance = instance.cwrap('GetPlanetSceneDistance', 'number', ['number']);
    window.setOrbitLines = instance.cwrap('SetOrbitLines', null, ['number']);
    window.getOrbitLines = instance.cwrap('GetOrbitLines', 'number', []);

    window.setXrSessionActive = instance.cwrap('SetXrSessionActive', null, ['number']);
    window.setXrEyeCount = instance.cwrap('SetXrEyeCount', null, ['number']);
    window.setXrEyeViewport = instance.cwrap('SetXrEyeViewport', null, ['number', 'number', 'number', 'number', 'number']);
    window.getXrMatrixScratch = instance.cwrap('GetXrMatrixScratch', 'number', []);
    window.commitXrEyeMatrices = instance.cwrap('CommitXrEyeMatrices', null, ['number']);
    window.runXrFrame = instance.cwrap('RunXrFrame', null, []);
    window.getCameraPositionX = instance.cwrap('GetCameraPositionX', 'number', []);
    window.getCameraPositionY = instance.cwrap('GetCameraPositionY', 'number', []);
    window.getCameraPositionZ = instance.cwrap('GetCameraPositionZ', 'number', []);

    const explorer = new PlanetExplorer(explorerPanel);
    void explorer.init({
        focusPlanet: window.focusPlanet,
        getNearestPlanetIndex: window.getNearestPlanetIndex,
        getFocusedPlanetIndex: window.getFocusedPlanetIndex,
        getPlanetSceneDistance: window.getPlanetSceneDistance,
        getOrbitScaleMode: () => (window.getOrbitScaleMode?.() ?? 0) as OrbitScaleMode,
        setOrbitScaleMode: window.setOrbitScaleMode,
    }).catch((error: unknown) => {
        console.error('Failed to initialize planet explorer:', error);
    });

    if (isMobileDevice) {
        setPanelCollapsed(true);
        explorerPanel.classList.add('is-collapsed');
    }

    initTouchControls({
        canvas,
        settingsPanel,
        explorerPanel,
        loadingContainer,
        bindings: {
            setTouchMovement: (forward, right, vertical) => {
                window.setTouchMovement?.(forward, right, vertical);
            },
            addTouchLook: (deltaX, deltaY) => {
                window.addTouchLook?.(deltaX, deltaY);
            },
            addTouchZoom: (delta) => {
                window.addTouchZoom?.(delta);
            },
        },
    });

    void initWebXr({
        canvas,
        enterVrButton,
        exitVrButton,
        overlayRoots: [settingsPanel, explorerPanel],
        bindings: {
            setTouchMovement: (forward, right, vertical) => {
                window.setTouchMovement?.(forward, right, vertical);
            },
            addTouchLook: (deltaX, deltaY) => {
                window.addTouchLook?.(deltaX, deltaY);
            },
            setQualityPreset: (preset) => {
                window.setQualityPreset?.(preset);
            },
            getQualityPreset: () => window.getQualityPreset?.() ?? 2,
            getCameraPosition: () => ({
                x: window.getCameraPositionX?.() ?? 0,
                y: window.getCameraPositionY?.() ?? 0,
                z: window.getCameraPositionZ?.() ?? 0,
            }),
            setXrSessionActive: (active) => {
                window.setXrSessionActive?.(active ? 1 : 0);
            },
            setXrEyeCount: (count) => {
                window.setXrEyeCount?.(count);
            },
            setXrEyeViewport: (eye, x, y, w, h) => {
                window.setXrEyeViewport?.(eye, x, y, w, h);
            },
            commitXrEyeMatrices: (eye) => {
                window.commitXrEyeMatrices?.(eye);
            },
            getXrMatrixScratchPtr: () => window.getXrMatrixScratch?.() ?? 0,
            runXrFrame: () => {
                window.runXrFrame?.();
            },
            getHeapF32: () => instance.HEAPF32,
        },
    }).then((controller) => {
        if (controller) {
            console.log('[WebXR] Enter VR control ready');
        } else {
            console.log('[WebXR] immersive-vr unavailable — staying 2D');
        }
    }).catch((error: unknown) => {
        console.warn('[WebXR] init failed:', error);
        enterVrButton.hidden = true;
    });

    const saved = readPersistedSettings();
    // An explicit URL preset wins for shareable links. On mobile, default to low
    // when nothing was saved. Otherwise restore the user's choice.
    const defaultQuality: QualityPreset = isMobileDevice ? 0 : 2;
    const quality = qualityFromUrl() ?? saved.quality ?? defaultQuality;
    const timeScale = saved.timeScale ?? window.getTimeScale();
    const paused = saved.paused ?? Boolean(window.getPaused());
    const shadows = saved.shadows ?? true;
    const orbitLines = saved.orbitLines ?? true;
    const musicVolumePercent = saved.musicVolume ?? Math.round((window.getMusicVolume?.() ?? 0.3) * 100);
    const musicMuted = saved.musicMuted ?? Boolean(window.getMusicMuted?.());
    const simulationDate = saved.simulationDate
        ?? (window.getSimulationEpoch ? isoDateFromJulianDate(window.getSimulationEpoch()) : isoDateUtcNow());

    qualitySelect.value = String(quality);
    timeScaleInput.value = String(Math.log10(timeScale));
    updateTimeScaleDisplay(timeScale);
    simulationDateInput.value = simulationDate;
    pausedInput.checked = paused;
    shadowsInput.checked = shadows;
    orbitLinesInput.checked = orbitLines;
    applyMusicVolumePercent(musicVolumePercent);
    musicMutedInput.checked = musicMuted;

    window.setQualityPreset(quality);
    window.setTimeScale(timeScale);
    window.setPaused(paused);
    {
        const jd = julianDateFromIsoDate(simulationDate);
        if (jd !== undefined) {
            window.setSimulationEpoch?.(jd);
        }
    }
    window.setShadowQuality(shadows ? (quality + 1) as ShadowQuality : 0);
    window.setOrbitLines?.(orbitLines ? 1 : 0);
    window.setMusicMuted?.(musicMuted);
    if (!musicMuted) {
        window.setMusicVolume?.(musicVolumePercent / 100);
    }
    settingsFieldset.disabled = false;
    settingsStatus.textContent = 'Controls ready';
    persistPanelSettings();

    qualitySelect.addEventListener('change', () => {
        const preset = Number(qualitySelect.value) as QualityPreset;
        window.setQualityPreset?.(preset);
        if (shadowsInput.checked) window.setShadowQuality?.((preset + 1) as ShadowQuality);
        settingsStatus.textContent = `Quality set to ${qualitySelect.selectedOptions[0]?.text ?? preset}`;
        persistPanelSettings();
    });

    timeScaleInput.addEventListener('input', () => {
        const scale = Math.pow(10, Number(timeScaleInput.value));
        updateTimeScaleDisplay(scale);
        window.setTimeScale?.(scale);
        settingsStatus.textContent = `Animation speed ${formatTimeScale(scale)}`;
        persistPanelSettings();
    });

    simulationDateSet.addEventListener('click', () => {
        applySimulationDateIso(simulationDateInput.value);
    });

    simulationDateInput.addEventListener('change', () => {
        applySimulationDateIso(simulationDateInput.value);
    });

    simulationDateNow.addEventListener('click', () => {
        const iso = isoDateUtcNow();
        simulationDateInput.value = iso;
        window.setSimulationEpoch?.(julianDateUtcNow());
        settingsStatus.textContent = `Date set to now (${iso} UTC)`;
        persistPanelSettings();
    });

    pausedInput.addEventListener('change', () => {
        window.setPaused?.(pausedInput.checked);
        settingsStatus.textContent = pausedInput.checked ? 'Animation paused' : 'Animation resumed';
        persistPanelSettings();
    });

    shadowsInput.addEventListener('change', () => {
        const preset = Number(qualitySelect.value) as QualityPreset;
        window.setShadowQuality?.(shadowsInput.checked ? (preset + 1) as ShadowQuality : 0);
        settingsStatus.textContent = shadowsInput.checked ? 'Shadows enabled' : 'Shadows disabled';
        persistPanelSettings();
    });

    orbitLinesInput.addEventListener('change', () => {
        window.setOrbitLines?.(orbitLinesInput.checked ? 1 : 0);
        settingsStatus.textContent = orbitLinesInput.checked ? 'Orbit lines enabled' : 'Orbit lines disabled';
        persistPanelSettings();
    });

    musicVolumeInput.addEventListener('input', () => {
        const percent = Number(musicVolumeInput.value);
        updateMusicVolumeDisplay(percent);
        if (!musicMutedInput.checked) {
            window.setMusicVolume?.(percent / 100);
        }
        settingsStatus.textContent = `Music volume ${percent}%`;
        persistPanelSettings();
    });

    musicMutedInput.addEventListener('change', () => {
        window.setMusicMuted?.(musicMutedInput.checked);
        if (!musicMutedInput.checked) {
            window.setMusicVolume?.(Number(musicVolumeInput.value) / 100);
        }
        settingsStatus.textContent = musicMutedInput.checked ? 'Music muted' : 'Music unmuted';
        persistPanelSettings();
    });

    settingsReset.addEventListener('click', () => {
        const resetQuality: QualityPreset = isMobileDevice ? 0 : 2;
        const resetShadowQuality: ShadowQuality = isMobileDevice ? 1 : 3;
        qualitySelect.value = String(resetQuality);
        timeScaleInput.value = '0';
        pausedInput.checked = false;
        shadowsInput.checked = true;
        orbitLinesInput.checked = true;
        musicMutedInput.checked = false;
        updateTimeScaleDisplay(1);
        applyMusicVolumePercent(30);
        window.setQualityPreset?.(resetQuality);
        window.setTimeScale?.(1);
        window.setPaused?.(false);
        window.setShadowQuality?.(resetShadowQuality);
        window.setOrbitLines?.(1);
        window.setMusicMuted?.(false);
        window.setMusicVolume?.(0.3);
        const nowIso = isoDateUtcNow();
        simulationDateInput.value = nowIso;
        window.setSimulationEpoch?.(julianDateUtcNow());
        settingsStatus.textContent = 'Settings reset';
        persistPanelSettings();
    });

    // Reflect keyboard-driven C++ changes without fighting a control that the
    // user is actively manipulating.
    window.setInterval(() => {
        if (document.activeElement !== qualitySelect) {
            qualitySelect.value = String(window.getQualityPreset?.() ?? qualitySelect.value);
        }
        if (document.activeElement !== timeScaleInput) {
            const runtimeScale = window.getTimeScale?.();
            if (runtimeScale !== undefined) {
                timeScaleInput.value = String(Math.log10(runtimeScale));
                updateTimeScaleDisplay(runtimeScale);
            }
        }
        syncSimulationDateFromRuntime();
        pausedInput.checked = Boolean(window.getPaused?.());
        shadowsInput.checked = (window.getShadowQuality?.() ?? 0) > 0;
        if (document.activeElement !== musicVolumeInput) {
            const runtimeVolume = window.getMusicVolume?.();
            if (runtimeVolume !== undefined) {
                applyMusicVolumePercent(Math.round(runtimeVolume * 100));
            }
        }
        musicMutedInput.checked = Boolean(window.getMusicMuted?.());
    }, 500);
}).catch((error: unknown) => {
    settingsStatus.textContent = 'Controls unavailable';
    console.error('Failed to initialize SolarSystem WASM:', error);
});
