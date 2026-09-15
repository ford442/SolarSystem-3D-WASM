import './style.css';
import Module, { type SolarSystemModuleConfig } from './SolarSystem.js';
import { parseDeepLinkFromUrl } from './deepLink';
import { initEducationalLayer } from './educationalLayer';
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
import { computeWebGlSizing } from './webglContext';

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
const skyEventChip = document.getElementById('next-sky-event') as HTMLElement;
const skyEventText = document.getElementById('next-sky-event-text') as HTMLElement;
const skyEventJump = document.getElementById('next-sky-event-jump') as HTMLButtonElement;
const skyEventLandmark = document.getElementById('next-sky-event-landmark') as HTMLButtonElement;
const enterVrButton = document.getElementById('enter-vr') as HTMLButtonElement;
const exitVrButton = document.getElementById('exit-vr') as HTMLButtonElement;
const explorerPanel = document.getElementById('explorer-panel') as HTMLElement;
const explorerMissions = document.getElementById('explorer-missions') as HTMLElement;
const explorerMissionList = document.getElementById('explorer-mission-list') as HTMLElement;
const tourPlay = document.getElementById('tour-play') as HTMLButtonElement;
const tourStop = document.getElementById('tour-stop') as HTMLButtonElement;
const tourCopyStep = document.getElementById('tour-copy-step') as HTMLButtonElement;
const tourCaption = document.getElementById('tour-caption') as HTMLElement;
const xrHud = document.getElementById('xr-hud') as HTMLElement;
const xrTooltip = document.getElementById('xr-tooltip') as HTMLElement;
const xrControllers = document.getElementById('xr-controllers') as HTMLElement;

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

// Emscripten's GLFW port builds its own WebGL context-attributes object from GLFW
// window hints only (antialias/depth/stencil/alpha) and ignores Module.contextAttributes
// entirely — passing powerPreference/xrCompatible/etc. there is a silent no-op. Creating
// the context ourselves and handing it to Module.preinitializedWebGLContext is the
// supported hook Emscripten actually honors: GLFW reuses this context as-is instead of
// creating its own. See docs/plans/PORTING_GUIDE.md and web/src/webglContext.ts.
const { contextOptions, tier } = computeWebGlSizing(initConfig);
const preinitializedWebGLContext = canvas.getContext('webgl2', contextOptions) as WebGL2RenderingContext | null;
if (preinitializedWebGLContext) {
    console.log(
        `[WebGL] tier=${tier} requested`, contextOptions,
        'actual', preinitializedWebGLContext.getContextAttributes(),
    );
} else {
    console.error('[WebGL] Failed to create a WebGL2 context with', contextOptions);
}

for (const eventName of ['pointerdown', 'pointerup', 'click', 'keydown', 'keyup']) {
    settingsPanel.addEventListener(eventName, (event) => event.stopPropagation());
    explorerPanel.addEventListener(eventName, (event) => event.stopPropagation());
}

const moduleConfig: SolarSystemModuleConfig = installWasmCallbacksOnConfig({
    canvas,
    ...(preinitializedWebGLContext ? { preinitializedWebGLContext } : {}),
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
        getNextConjunction: () => runtime.getNextConjunction(),
        setSimulationEpoch: runtime.setSimulationEpoch.bind(runtime),
    }, {
        skipPlanetRestore: deepLink.planet !== undefined || deepLink.camera !== undefined || deepLink.mission !== undefined,
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
        overlayRoots: [settingsPanel, explorerPanel, tourCaption],
        hudRoot: xrHud,
        tooltip: xrTooltip,
        controllersRoot: xrControllers,
        bindings: {
            setTouchMovement: runtime.setTouchMovement.bind(runtime),
            addTouchLook: runtime.addTouchLook.bind(runtime),
            setQualityPreset: runtime.setQualityPreset.bind(runtime),
            getQualityPreset: runtime.getQualityPreset.bind(runtime),
            getCameraPosition: () => runtime.getCameraPosition(),
            getNearestPlanetIndex: () => runtime.getNearestPlanetIndex(),
            getFocusedPlanetIndex: () => runtime.getFocusedPlanetIndex(),
            setXrSessionActive: runtime.setXrSessionActive.bind(runtime),
            setXrBaseLayerFramebuffer: runtime.setXrBaseLayerFramebuffer.bind(runtime),
            registerXrFramebuffer: runtime.registerXrFramebuffer.bind(runtime),
            setXrEyeCount: runtime.setXrEyeCount.bind(runtime),
            setXrEyeViewport: runtime.setXrEyeViewport.bind(runtime),
            commitXrEyeMatrices: runtime.commitXrEyeMatrices.bind(runtime),
            getXrMatrixScratchPtr: runtime.getXrMatrixScratchPtr.bind(runtime),
            runXrFrame: runtime.runXrFrame.bind(runtime),
            getHeapF32: () => runtime.heapF32,
            setXrControllerRay: runtime.setXrControllerRay.bind(runtime),
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
            skyEventChip,
            skyEventText,
            skyEventJump,
            skyEventLandmark,
        },
        runtime,
        deepLink,
        isMobileDevice,
    });

    initEducationalLayer({
        runtime,
        deepLink,
        missionList: explorerMissionList,
        missionsRoot: explorerMissions,
        tourPlay,
        tourStop,
        tourCopyStep,
        tourCaption,
        settingsStatus,
    });

    if (deepLink.planet !== undefined && !deepLink.mission && !deepLink.tour) {
        explorer.applyDeepLinkPlanet(deepLink.planet, { focusCamera: false });
    }
}).catch((error: unknown) => {
    settingsStatus.textContent = 'Controls unavailable';
    console.error('Failed to initialize SolarSystem WASM:', error);
});
