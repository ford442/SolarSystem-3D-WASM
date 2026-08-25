import './style.css';
import Module, { type SolarSystemModuleConfig } from './SolarSystem.js';
import { parseDeepLinkFromUrl } from './deepLink';
import { publishInitConfig, resolveInitConfig } from './initialSettings';
import { PlanetExplorer } from './planetExplorer';
import { createProgressCallbacks } from './progressOverlay';
import { initSettingsPanel } from './settingsPanel';
import { initTouchControls, isMobileLikeDevice } from './touchControls';
import { initWebXr } from './webxr';
import {
    createSolarSystemRuntime,
    exposeConsoleHelpers,
} from './wasmBridge';
import { installWasmCallbacksOnConfig } from './wasmCallbacks';

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
const magneticFieldsInput = document.getElementById('magnetic-fields-enabled') as HTMLInputElement;
const musicVolumeInput = document.getElementById('music-volume') as HTMLInputElement;
const musicVolumeValue = document.getElementById('music-volume-value') as HTMLOutputElement;
const musicMutedInput = document.getElementById('music-muted') as HTMLInputElement;
const settingsReset = document.getElementById('settings-reset') as HTMLButtonElement;
const copyViewLinkButton = document.getElementById('copy-view-link') as HTMLButtonElement;
const settingsStatus = document.getElementById('settings-status') as HTMLElement;
const enterVrButton = document.getElementById('enter-vr') as HTMLButtonElement;
const exitVrButton = document.getElementById('exit-vr') as HTMLButtonElement;
const explorerPanel = document.getElementById('explorer-panel') as HTMLElement;

const deployedBaseUrl = new URL(import.meta.env.BASE_URL, window.location.href);
const isMobileDevice = isMobileLikeDevice();
const runtimeAssetBase = import.meta.env.VITE_ASSET_BASE?.trim() || deployedBaseUrl.toString();

const progressCallbacks = createProgressCallbacks({
    loadingContainer,
    progressBar,
    progressText,
    streamingProgress,
    streamingText,
    streamingBar,
});

window.__solarSystemAssetBase = runtimeAssetBase;

// Resolve quality/mobile before WASM main() creates the GLFW/WebGL context.
const initConfig = resolveInitConfig();
publishInitConfig(initConfig);

for (const eventName of ['pointerdown', 'pointerup', 'click', 'keydown', 'keyup']) {
    settingsPanel.addEventListener(eventName, (event) => event.stopPropagation());
    explorerPanel.addEventListener(eventName, (event) => event.stopPropagation());
}

const moduleConfig: SolarSystemModuleConfig = installWasmCallbacksOnConfig({
    canvas,
    contextAttributes: {
        xrCompatible: true,
        majorVersion: 2,
        minorVersion: 0,
        antialias: true,
        depth: true,
        stencil: false,
        alpha: false,
        premultipliedAlpha: true,
        preserveDrawingBuffer: false,
        powerPreference: 'default' as const,
        failIfMajorPerformanceCaveat: false,
    },
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
    },
}, progressCallbacks);

void Module(moduleConfig).then((instance) => {
    console.log('Module loaded successfully', instance);

    const runtime = createSolarSystemRuntime(instance);
    exposeConsoleHelpers(runtime);

    const deepLink = parseDeepLinkFromUrl();
    const explorer = new PlanetExplorer(explorerPanel);
    void explorer.init({
        focusPlanet: runtime.focusPlanet.bind(runtime),
        getNearestPlanetIndex: runtime.getNearestPlanetIndex.bind(runtime),
        getFocusedPlanetIndex: runtime.getFocusedPlanetIndex.bind(runtime),
        getPlanetSceneDistance: runtime.getPlanetSceneDistance.bind(runtime),
        getOrbitScaleMode: () => runtime.getOrbitScaleMode(),
        setOrbitScaleMode: runtime.setOrbitScaleMode.bind(runtime),
    }, {
        skipPlanetRestore: deepLink.planet !== undefined || deepLink.camera !== undefined,
        initialOrbitScale: deepLink.orbitScale,
    }).catch((error: unknown) => {
        console.error('Failed to initialize planet explorer:', error);
    });

    if (isMobileDevice) {
        explorerPanel.classList.add('is-collapsed');
    }

    initTouchControls({
        canvas,
        settingsPanel,
        explorerPanel,
        loadingContainer,
        bindings: {
            setTouchMovement: runtime.setTouchMovement.bind(runtime),
            addTouchLook: runtime.addTouchLook.bind(runtime),
            addTouchZoom: runtime.addTouchZoom.bind(runtime),
        },
    });

    void initWebXr({
        canvas,
        enterVrButton,
        exitVrButton,
        overlayRoots: [settingsPanel, explorerPanel],
        bindings: {
            setTouchMovement: runtime.setTouchMovement.bind(runtime),
            addTouchLook: runtime.addTouchLook.bind(runtime),
            setQualityPreset: runtime.setQualityPreset.bind(runtime),
            getQualityPreset: runtime.getQualityPreset.bind(runtime),
            getCameraPosition: () => runtime.getCameraPosition(),
            setXrSessionActive: runtime.setXrSessionActive.bind(runtime),
            setXrEyeCount: runtime.setXrEyeCount.bind(runtime),
            setXrEyeViewport: runtime.setXrEyeViewport.bind(runtime),
            commitXrEyeMatrices: runtime.commitXrEyeMatrices.bind(runtime),
            getXrMatrixScratchPtr: runtime.getXrMatrixScratchPtr.bind(runtime),
            runXrFrame: runtime.runXrFrame.bind(runtime),
            getHeapF32: () => runtime.heapF32,
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

    initSettingsPanel({
        elements: {
            settingsPanel,
            settingsToggle,
            settingsToggleIcon,
            settingsFieldset,
            qualitySelect,
            timeScaleInput,
            timeScaleValue,
            simulationDateInput,
            simulationDateSet,
            simulationDateNow,
            pausedInput,
            shadowsInput,
            orbitLinesInput,
            magneticFieldsInput,
            musicVolumeInput,
            musicVolumeValue,
            musicMutedInput,
            settingsReset,
            copyViewLinkButton,
            settingsStatus,
        },
        runtime,
        deepLink,
        isMobileDevice,
    });

    if (deepLink.planet !== undefined) {
        explorer.applyDeepLinkPlanet(deepLink.planet, { focusCamera: false });
    }
}).catch((error: unknown) => {
    settingsStatus.textContent = 'Controls unavailable';
    console.error('Failed to initialize SolarSystem WASM:', error);
});
