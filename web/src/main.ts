import './style.css'
import Module, {
    type QualityPreset,
    type ShadowQuality,
    type SolarSystemModuleConfig,
} from './SolarSystem.js'

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
const pausedInput = document.getElementById('simulation-paused') as HTMLInputElement;
const shadowsInput = document.getElementById('shadows-enabled') as HTMLInputElement;
const settingsReset = document.getElementById('settings-reset') as HTMLButtonElement;
const settingsStatus = document.getElementById('settings-status') as HTMLElement;
const deployedBaseUrl = new URL(import.meta.env.BASE_URL, window.location.href);
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
// completed = number of textures fully streamed; total = total queued for streaming.
function updateStreamingProgress(completed: number, total: number) {
    if (!streamingProgress) return;
    if (total <= 0) {
        streamingProgress.style.display = 'none';
        return;
    }
    streamingProgress.style.display = 'block';
    const pct = total > 0 ? Math.round((completed / total) * 100) : 0;
    if (streamingText) {
        streamingText.textContent = `High-res upgrade: ${completed}/${total} (${pct}%)`;
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

function updateTimeScaleDisplay(scale: number): void {
    timeScaleValue.value = formatTimeScale(scale);
    timeScaleValue.textContent = formatTimeScale(scale);
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
}

const moduleConfig: SolarSystemModuleConfig = {
    canvas: canvas,
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
    window.setShadowQuality = instance.cwrap('SetShadowQuality', null, ['number']);
    window.getShadowQuality = instance.cwrap('GetShadowQuality', 'number', []);
    window.focusPlanet = instance.cwrap('FocusPlanet', null, ['number']);

    const saved = readPersistedSettings();
    // An explicit URL preset wins for shareable links. Otherwise restore the
    // user's choice, falling back to the value selected by the C++ runtime.
    const quality = qualityFromUrl() ?? saved.quality ?? window.getQualityPreset();
    const timeScale = saved.timeScale ?? window.getTimeScale();
    const paused = saved.paused ?? Boolean(window.getPaused());
    const shadows = saved.shadows ?? window.getShadowQuality() > 0;

    qualitySelect.value = String(quality);
    timeScaleInput.value = String(Math.log10(timeScale));
    updateTimeScaleDisplay(timeScale);
    pausedInput.checked = paused;
    shadowsInput.checked = shadows;

    window.setQualityPreset(quality);
    window.setTimeScale(timeScale);
    window.setPaused(paused);
    window.setShadowQuality(shadows ? (quality + 1) as ShadowQuality : 0);
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

    settingsReset.addEventListener('click', () => {
        qualitySelect.value = '2';
        timeScaleInput.value = '0';
        pausedInput.checked = false;
        shadowsInput.checked = true;
        updateTimeScaleDisplay(1);
        window.setQualityPreset?.(2);
        window.setTimeScale?.(1);
        window.setPaused?.(false);
        window.setShadowQuality?.(3);
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
        pausedInput.checked = Boolean(window.getPaused?.());
        shadowsInput.checked = (window.getShadowQuality?.() ?? 0) > 0;
    }, 500);
}).catch((error: unknown) => {
    settingsStatus.textContent = 'Controls unavailable';
    console.error('Failed to initialize SolarSystem WASM:', error);
});
