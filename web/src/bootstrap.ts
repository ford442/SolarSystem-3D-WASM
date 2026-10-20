import './style.css';
import Module, { type SolarSystemModule, type SolarSystemModuleConfig } from './SolarSystem.js';
import { buildShareableUrl, parseDeepLinkFromUrl } from './deepLink';
import { isoDateFromJulianDate } from './ephemeris';
import { showFatalPanel } from './fatalOverlay';
import { initEducationalLayer } from './educationalLayer';
import { initObservePanel, type ObservePanelController } from './observe';
import { publishInitConfig, resolveInitConfig } from './initialSettings';
import { PlanetExplorer } from './planetExplorer';
import { createProgressCallbacks } from './progressOverlay';
import { createMusicPlayer } from './music';
import { initSettingsPanel } from './settingsPanel';
import { initTouchControls, isMobileLikeDevice } from './touchControls';
import { initWebXr } from './webxr';
import {
    createSolarSystemRuntime,
    exposeConsoleHelpers,
    type SolarSystemRuntime,
} from './wasmBridge';
import { installWasmCallbacksOnConfig, setModuleInstance } from './wasmCallbacks';
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
const observeElements = {
    exploreButton: document.getElementById('mode-explore') as HTMLButtonElement,
    observeButton: document.getElementById('mode-observe') as HTMLButtonElement,
    controls: document.getElementById('observe-controls') as HTMLElement,
    siteSelect: document.getElementById('observe-site') as HTMLSelectElement,
    latInput: document.getElementById('observe-lat') as HTMLInputElement,
    lonInput: document.getElementById('observe-lon') as HTMLInputElement,
    locateButton: document.getElementById('observe-locate') as HTMLButtonElement,
    timeInput: document.getElementById('observe-time') as HTMLInputElement,
    rateSelect: document.getElementById('observe-rate') as HTMLSelectElement,
    stepButtons: Array.from(document.querySelectorAll<HTMLButtonElement>('[data-step-days]')),
    nowButton: document.getElementById('observe-now') as HTMLButtonElement,
    lookSunButton: document.getElementById('observe-look-sun') as HTMLButtonElement,
    lookMoonButton: document.getElementById('observe-look-moon') as HTMLButtonElement,
    eclipseButton: document.getElementById('observe-eclipse') as HTMLButtonElement,
    eyepieceSelect: document.getElementById('observe-eyepiece') as HTMLSelectElement,
    telescopeOverlay: document.getElementById('telescope-overlay') as HTMLElement,
    telescopeLabel: document.getElementById('telescope-label') as HTMLElement,
    readout: document.getElementById('observe-readout') as HTMLElement,
    dateInput: simulationDateInput,
    status: settingsStatus,
};
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

// Which KTX2 texture packs this deployment has published alongside the .dds assets,
// e.g. VITE_TEXTURE_PACKS="bc3,astc,etc2". The WASM module probes the WebGL context,
// picks its preferred pack (BC7 > BC3 > ASTC > ETC2) and uses it only if it appears
// here; with none published it stays on .dds, and a GPU without S3TC falls back to the
// software BC decoder. See scripts/convert_textures_ktx2.py and docs/ARCHITECTURE.md.
const publishedTexturePacks: string[] = String(import.meta.env.VITE_TEXTURE_PACKS ?? '')
    .split(',')
    .map((pack: string) => pack.trim())
    .filter((pack: string) => pack.length > 0);

const progressCallbacks = createProgressCallbacks({
    loadingContainer,
    progressBar,
    progressText,
    streamingProgress,
    streamingText,
    streamingBar,
});

window.__solarSystemAssetBase = runtimeAssetBase;

// Music is a browser media concern: tracks stream straight from the asset base, outside the
// WASM core-resource count, and a missing mp3 just logs and skips.
const music = createMusicPlayer({ assetBase: runtimeAssetBase });
music.start();
window.__solarSystemTexturePacks = publishedTexturePacks;

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

const BROWSER_HINTS = [
    'Turn on hardware acceleration in your browser settings, then reload.',
    'Update your graphics driver, or try a current Chrome, Edge, Firefox or Safari.',
    'Close other tabs that use 3D graphics to free GPU memory.',
];

let moduleInstance: SolarSystemModule | null = null;
let runtimeForReload: SolarSystemRuntime | null = null;
let contextLost = false;

// Reload into the view the user had, using the same deep-link encoding as "Copy view link".
// The getters below only read simulation state, so they still work after the GL context is gone.
function currentViewUrl(): string {
    const runtime = runtimeForReload;
    if (!runtime) {
        return window.location.href;
    }
    return buildShareableUrl({
        getQualityPreset: () => runtime.getQualityPreset(),
        getTimeScale: () => runtime.getTimeScale(),
        getPaused: () => runtime.getPaused(),
        getSimulationEpoch: () => runtime.getSimulationEpoch(),
        getOrbitScaleMode: () => runtime.getOrbitScaleMode(),
        getShadowQuality: () => runtime.getShadowQuality(),
        getOrbitLines: () => runtime.getOrbitLines(),
        getMagneticFields: () => runtime.getMagneticFields(),
        getFocusedPlanetIndex: () => runtime.getFocusedPlanetIndex(),
        getFocusedMission: () => runtime.getFocusedMission(),
        getCameraPose: () => runtime.getCameraPose(),
        getObserveState: () => runtime.getObserveState(),
        isoDateFromJulianDate,
    });
}

function pauseMainLoop(): void {
    // Emscripten's library_browser.js puts these on the module instance, which is built on the
    // config object, so they are there during loading too, before the factory promise resolves.
    const target = (moduleInstance ?? moduleConfig) as Pick<SolarSystemModule, 'pauseMainLoop'>;
    target['pauseMainLoop']?.();
}

// A mobile GPU reset or a long-backgrounded tab can drop the context. preventDefault() is
// what tells the browser we may want it back; v1 does not rebuild GL state in place, so it
// stops the frame loop (every GL call would fail) and offers a reload into the same view.
canvas.addEventListener('webglcontextlost', (event) => {
    event.preventDefault();
    if (contextLost) return;
    contextLost = true;
    console.error('[WebGL] Context lost; pausing the main loop');
    pauseMainLoop();
    showFatalPanel({
        title: 'Graphics context lost',
        message: 'The browser reset the GPU context, so the scene can no longer be drawn. '
            + 'Reload to pick up where you left off.',
        reloadUrl: currentViewUrl,
    });
}, false);

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
}, { ...progressCallbacks, onMusicDuck: (duck: number) => music.setDuck(duck) });

function startModule(): Promise<SolarSystemModule> {
    if (!preinitializedWebGLContext) {
        // GLFW would try the same context creation and fail inside main(); say so up front.
        return Promise.reject(new Error('WebGL 2 is not available'));
    }
    return Module(moduleConfig);
}

void startModule().then((instance) => {
    console.log('Module loaded successfully', instance);
    moduleInstance = instance;
    setModuleInstance(instance);
    if (contextLost) {
        pauseMainLoop();
    }

    const runtime = createSolarSystemRuntime(instance);
    runtimeForReload = runtime;
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
            observe: {
                isActive: () => runtime.getObserveMode(),
                scrubTime: (seconds) => runtime.setSimulationEpoch(runtime.getSimulationEpoch() + seconds / 86400),
                turn: (degrees) => {
                    const state = runtime.getObserveState();
                    runtime.setObserveView(state.azDeg + degrees, state.elDeg);
                },
            },
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

    let observeController: ObservePanelController | null = null;

    initSettingsPanel({
        getObserveTimeOfDayDays: () => observeController?.timeOfDayDays() ?? 0,
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
        music,
        deepLink,
        isMobileDevice,
    });

    observeController = initObservePanel({
        elements: observeElements,
        runtime,
        startInObserve: deepLink.mode === 'observe',
        deepLink: { ...deepLink.observer, paused: deepLink.paused },
        epochFromLink: deepLink.julianDate !== undefined || deepLink.simulationDate !== undefined,
        onModeChanged: (active) => {
            explorerPanel.classList.toggle('is-observing', active);
        },
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
}, (error: unknown) => {
    settingsStatus.textContent = 'Controls unavailable';
    console.error('Failed to initialize SolarSystem WASM:', error);
    const noWebGl = !preinitializedWebGLContext;
    showFatalPanel({
        title: noWebGl ? 'WebGL 2 is not available' : 'The simulation failed to start',
        message: noWebGl
            ? 'This browser could not create a WebGL 2 context, which the 3D scene needs.'
            : `Loading the simulation failed: ${error instanceof Error ? error.message : String(error)}`,
        hints: noWebGl
            ? BROWSER_HINTS
            : ['Check your connection and reload.', ...BROWSER_HINTS],
    });
}).catch((error: unknown) => {
    settingsStatus.textContent = 'Controls unavailable';
    console.error('Failed to initialize the SolarSystem UI:', error);
});
