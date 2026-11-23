import './style.css';
import Module, { type SolarSystemModule, type SolarSystemModuleConfig } from './SolarSystem.js';
import { buildShareableUrl, parseDeepLinkFromUrl, runtimeLinkReaders } from './deepLink';
import { queryRequired, queryRequiredIn } from './dom';
import { showFatalPanel } from './fatalOverlay';
import { initEducationalLayer, initStormCard } from './educationalLayer';
import { initObservePanel, type ObservePanelController } from './observe';
import { publishInitConfig, resolveInitConfig } from './initialSettings';
import { PlanetExplorer } from './planetExplorer';
import { createProgressCallbacks } from './progressOverlay';
import { createMusicPlayer } from './music';
import { initSettingsPanel } from './settingsPanel';
import { initSystemPicker, loadSystemIndex, SOL_SYSTEM_ID, systemFactsUrl } from './systemPicker';
import { initTimelineEditor } from './timelineEditor';
import { initTouchControls, isMobileLikeDevice } from './touchControls';
import { initWebXr } from './webxr';
import { installPerfHud, perfHudRequested } from './perfHud';
import { createSolarSystemRuntime, exposeConsoleHelpers, type SolarSystemRuntime } from './wasmBridge';
import { installWasmCallbacksOnConfig, registerWasmCallbacks, setModuleInstance } from './wasmCallbacks';
import { assetCacheEnabled, createAssetFetcher } from './assetCache';
import { computeWebGlSizing } from './webglContext';
import { installContextLossHandling, parseLoseContextSeconds, startContextLossCycle } from './contextLoss';
import { chooseWasmVariant, locateWasmFile, THREADED_GLUE } from './wasmVariant';

const canvas = queryRequired('canvas', HTMLCanvasElement);
const loadingContainer = queryRequired('loading-container');
const progressBar = queryRequired('progress-bar');
const progressText = queryRequired('progress-text');
const streamingProgress = queryRequired('streaming-progress');
const streamingText = queryRequired('streaming-text');
const streamingBar = queryRequired('streaming-progress-bar');
const settingsPanel = queryRequired('settings-panel');
const settingsToggle = queryRequired('settings-toggle', HTMLButtonElement);
const settingsToggleIcon = queryRequiredIn(settingsToggle, '.settings-toggle-icon', HTMLElement);
const settingsFieldset = queryRequired('settings-fieldset', HTMLFieldSetElement);
const qualitySelect = queryRequired('quality-preset', HTMLSelectElement);
const timeScaleInput = queryRequired('time-scale', HTMLInputElement);
const timeScaleValue = queryRequired('time-scale-value', HTMLOutputElement);
const simulationDateInput = queryRequired('simulation-date', HTMLInputElement);
const simulationDateSet = queryRequired('simulation-date-set', HTMLButtonElement);
const simulationDateNow = queryRequired('simulation-date-now', HTMLButtonElement);
const pausedInput = queryRequired('simulation-paused', HTMLInputElement);
const shadowsInput = queryRequired('shadows-enabled', HTMLInputElement);
const orbitLinesInput = queryRequired('orbit-lines-enabled', HTMLInputElement);
const magneticFieldsInput = queryRequired('magnetic-fields-enabled', HTMLInputElement);
const solarWindInput = queryRequired('solar-wind-enabled', HTMLInputElement);
const kpOverrideInput = queryRequired('kp-override', HTMLInputElement);
const kpOverrideValue = queryRequired('kp-override-value', HTMLOutputElement);
const launchCmeButton = queryRequired('launch-cme', HTMLButtonElement);
const musicVolumeInput = queryRequired('music-volume', HTMLInputElement);
const musicVolumeValue = queryRequired('music-volume-value', HTMLOutputElement);
const musicMutedInput = queryRequired('music-muted', HTMLInputElement);
const settingsReset = queryRequired('settings-reset', HTMLButtonElement);
const copyViewLinkButton = queryRequired('copy-view-link', HTMLButtonElement);
const settingsStatus = queryRequired('settings-status');
const skyEventChip = queryRequired('next-sky-event');
const skyEventText = queryRequired('next-sky-event-text');
const skyEventJump = queryRequired('next-sky-event-jump', HTMLButtonElement);
const skyEventLandmark = queryRequired('next-sky-event-landmark', HTMLButtonElement);
const stormChip = queryRequired('next-storm');
const stormText = queryRequired('next-storm-text');
const stormPlay = queryRequired('next-storm-play', HTMLButtonElement);
const stormCardElement = queryRequired('storm-card');
const stormCardTitle = queryRequired('storm-card-title');
const stormCardLines = queryRequired('storm-card-lines');
const observeElements = {
    exploreButton: queryRequired('mode-explore', HTMLButtonElement),
    observeButton: queryRequired('mode-observe', HTMLButtonElement),
    controls: queryRequired('observe-controls'),
    siteSelect: queryRequired('observe-site', HTMLSelectElement),
    latInput: queryRequired('observe-lat', HTMLInputElement),
    lonInput: queryRequired('observe-lon', HTMLInputElement),
    locateButton: queryRequired('observe-locate', HTMLButtonElement),
    timeInput: queryRequired('observe-time', HTMLInputElement),
    rateSelect: queryRequired('observe-rate', HTMLSelectElement),
    stepButtons: Array.from(document.querySelectorAll<HTMLButtonElement>('[data-step-days]')),
    nowButton: queryRequired('observe-now', HTMLButtonElement),
    lookSunButton: queryRequired('observe-look-sun', HTMLButtonElement),
    lookMoonButton: queryRequired('observe-look-moon', HTMLButtonElement),
    eclipseButton: queryRequired('observe-eclipse', HTMLButtonElement),
    eyepieceSelect: queryRequired('observe-eyepiece', HTMLSelectElement),
    constellationsToggle: queryRequired('observe-constellations', HTMLInputElement),
    telescopeOverlay: queryRequired('telescope-overlay'),
    telescopeLabel: queryRequired('telescope-label'),
    readout: queryRequired('observe-readout'),
    dateInput: simulationDateInput,
    status: settingsStatus,
};
const enterVrButton = queryRequired('enter-vr', HTMLButtonElement);
const exitVrButton = queryRequired('exit-vr', HTMLButtonElement);
const explorerPanel = queryRequired('explorer-panel');
const explorerMissions = queryRequired('explorer-missions');
const explorerMissionList = queryRequired('explorer-mission-list');
const systemPickerRoot = queryRequired('system-picker');
const tourPlay = queryRequired('tour-play', HTMLButtonElement);
const tourStop = queryRequired('tour-stop', HTMLButtonElement);
const tourCopyStep = queryRequired('tour-copy-step', HTMLButtonElement);
const tourCaption = queryRequired('tour-caption');
const xrHud = queryRequired('xr-hud');
const xrTooltip = queryRequired('xr-tooltip');
const xrControllers = queryRequired('xr-controllers');

const deployedBaseUrl = new URL(import.meta.env.BASE_URL, window.location.href);
const isMobileDevice = isMobileLikeDevice();
const runtimeAssetBase = import.meta.env.VITE_ASSET_BASE?.trim() || deployedBaseUrl.toString();

// Which KTX2 texture packs this deployment has published alongside the .dds assets,
// e.g. VITE_TEXTURE_PACKS="bc3,astc,etc2". The WASM module probes the WebGL context,
// picks its preferred pack (BC7 > BC3 > ASTC > ETC2) and uses it only if it appears
// here; with none published it stays on .dds, and a GPU without S3TC falls back to the
// software BC decoder. See scripts/convert_textures_ktx2.py and docs/ARCHITECTURE.md.
const publishedTexturePacks: string[] = (import.meta.env.VITE_TEXTURE_PACKS ?? '')
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

// Runtime downloads (textures, planet manifests) go through the Cache API so a repeat visit
// skips the network. Installed before Module() because the first downloads happen inside
// main(). Core preloads are in SolarSystem.data and never come through here.
if (assetCacheEnabled(window.location.search, import.meta.env.DEV)) {
    window.__solarSystemFetchAsset = createAssetFetcher({
        assetBase: runtimeAssetBase,
        baseHref: window.location.href,
        caches: typeof caches === 'undefined' ? undefined : caches,
        fetch: window.fetch.bind(window),
        log: (message) => console.debug(message),
    });
}

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
const preinitializedWebGLContext = canvas.getContext('webgl2', contextOptions);
if (preinitializedWebGLContext) {
    console.log(
        `[WebGL] tier=${tier} requested`,
        contextOptions,
        'actual',
        preinitializedWebGLContext.getContextAttributes(),
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

// Reload into the view the user had, using the same deep-link encoding as "Copy view link".
// The getters below only read simulation state, so they still work after the GL context is gone.
function currentViewUrl(): string {
    const runtime = runtimeForReload;
    if (!runtime) {
        return window.location.href;
    }
    return buildShareableUrl(runtimeLinkReaders(runtime));
}

type MainLoopControls = Pick<SolarSystemModule, 'pauseMainLoop' | 'resumeMainLoop'>;

// Emscripten's library_browser.js puts these on the module instance, which is built on the
// config object, so they are there during loading too, before the factory promise resolves.
function mainLoopTarget(): MainLoopControls & Partial<Pick<SolarSystemModule, '_OnContextRestored'>> {
    return (moduleInstance ?? moduleConfig) as MainLoopControls &
        Partial<Pick<SolarSystemModule, '_OnContextRestored'>>;
}

function pauseMainLoop(): void {
    mainLoopTarget()['pauseMainLoop']?.();
}

function resumeMainLoop(): void {
    mainLoopTarget()['resumeMainLoop']?.();
}

// C++ rebuilds every GPU resource in place (docs/ARCHITECTURE.md §7.8). Before the wasm module
// is instantiated there is nothing on the GPU yet, so nothing to rebuild.
function restoreGpuContext(): boolean {
    const restore = mainLoopTarget()['_OnContextRestored'];
    return restore ? restore() !== 0 : true;
}

// A mobile GPU reset or a long-backgrounded tab can drop the context: pause, and rebuild in
// place when the browser gives it back (contextLoss.ts). Only a failed rebuild offers a reload.
const contextLoss = installContextLossHandling(canvas, {
    pause: pauseMainLoop,
    resume: resumeMainLoop,
    restore: restoreGpuContext,
    onFatal: () => {
        showFatalPanel({
            title: 'Graphics context lost',
            message:
                'The browser reset the GPU context and the scene could not be rebuilt. ' +
                'Reload to pick up where you left off.',
            reloadUrl: currentViewUrl,
        });
    },
});
const loseContextSeconds = parseLoseContextSeconds(window.location.search);
if (loseContextSeconds !== null && preinitializedWebGLContext) {
    // Test hook (web/e2e/contextLoss.spec.ts): drop and restore the context on a timer.
    startContextLossCycle(preinitializedWebGLContext, canvas, loseContextSeconds * 1000);
}

for (const eventName of ['pointerdown', 'pointerup', 'click', 'keydown', 'keyup']) {
    settingsPanel.addEventListener(eventName, (event) => event.stopPropagation());
    explorerPanel.addEventListener(eventName, (event) => event.stopPropagation());
}

const moduleConfig: SolarSystemModuleConfig = installWasmCallbacksOnConfig(
    {
        canvas,
        ...(preinitializedWebGLContext ? { preinitializedWebGLContext } : {}),
        locateFile: (path: string, prefix: string) => locateWasmFile(path, prefix, deployedBaseUrl),
        print: (text: string) => console.log(text),
        printErr: (text: string) => console.error(text),
        onRuntimeInitialized: () => {
            console.log('SolarSystem WASM initialized');
        },
    },
    { ...progressCallbacks, onMusicDuck: (duck: number) => music.setDuck(duck) },
);

type ModuleFactory = typeof Module;

/**
 * The pthread build when this page can run its workers (wasmVariant.ts), else the bundled
 * single-threaded one. A deployment without SolarSystem-mt.js (built with --no-threads)
 * falls back to the single-threaded build after one failed import.
 */
async function loadModuleFactory(): Promise<ModuleFactory> {
    if (
        chooseWasmVariant({ crossOriginIsolated: window.crossOriginIsolated, search: window.location.search }) !==
        'threaded'
    ) {
        return Module;
    }
    const url = new URL(THREADED_GLUE, deployedBaseUrl).toString();
    try {
        const threaded = (await import(/* @vite-ignore */ url)) as { default: ModuleFactory };
        console.log('[Jobs] page is cross-origin isolated: loading the threaded build');
        return threaded.default;
    } catch (error: unknown) {
        console.warn('[Jobs] threaded build unavailable, using the single-threaded one:', error);
        return Module;
    }
}

async function startModule(): Promise<SolarSystemModule> {
    if (!preinitializedWebGLContext) {
        // GLFW would try the same context creation and fail inside main(); say so up front.
        throw new Error('WebGL 2 is not available');
    }
    const factory = await loadModuleFactory();
    return factory(moduleConfig);
}

void startModule()
    .then(
        (instance) => {
            console.log('Module loaded successfully', instance);
            moduleInstance = instance;
            setModuleInstance(instance);
            if (contextLoss.lost) {
                pauseMainLoop();
            }

            const runtime = createSolarSystemRuntime(instance);
            runtimeForReload = runtime;
            exposeConsoleHelpers(runtime);
            if (perfHudRequested(window.location.search)) {
                installPerfHud(runtime);
            }
            if (import.meta.env.DEV) {
                void import('./shaderReload').then(({ installShaderReload }) =>
                    installShaderReload(instance, runtime, deployedBaseUrl),
                );
            }

            const deepLink = parseDeepLinkFromUrl();
            const systemIndexPromise = loadSystemIndex(deployedBaseUrl.toString()).catch((error: unknown) => {
                console.warn('[Systems] index unavailable; the picker is hidden:', error);
                return null;
            });
            const explorer = new PlanetExplorer(explorerPanel);
            const explorerReady = explorer
                .init(
                    {
                        focusPlanet: runtime.focusPlanet.bind(runtime),
                        getNearestPlanetIndex: runtime.getNearestPlanetIndex.bind(runtime),
                        getFocusedPlanetIndex: runtime.getFocusedPlanetIndex.bind(runtime),
                        getPlanetSceneDistance: runtime.getPlanetSceneDistance.bind(runtime),
                        getOrbitScaleMode: () => runtime.getOrbitScaleMode(),
                        setOrbitScaleMode: runtime.setOrbitScaleMode.bind(runtime),
                        getNextConjunction: () => runtime.getNextConjunction(),
                        setSimulationEpoch: runtime.setSimulationEpoch.bind(runtime),
                    },
                    {
                        skipPlanetRestore:
                            deepLink.planet !== undefined ||
                            deepLink.camera !== undefined ||
                            deepLink.mission !== undefined,
                        initialOrbitScale: deepLink.orbitScale,
                    },
                )
                .catch((error: unknown) => {
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
                        scrubTime: (seconds) =>
                            runtime.setSimulationEpoch(runtime.getSimulationEpoch() + seconds / 86400),
                        turn: (degrees) => {
                            const state = runtime.getObserveState();
                            runtime.setObserveView(state.azDeg + degrees, state.elDeg);
                        },
                    },
                },
            })
                .then((controller) => {
                    if (controller) {
                        console.log('[WebXR] Enter VR control ready');
                    } else {
                        console.log('[WebXR] immersive-vr unavailable — staying 2D');
                    }
                })
                .catch((error: unknown) => {
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
                    solarWindInput,
                    kpOverrideInput,
                    kpOverrideValue,
                    launchCmeButton,
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
                    stormChip,
                    stormText,
                    stormPlay,
                },
                runtime,
                music,
                deepLink,
                isMobileDevice,
            });

            observeController = initObservePanel({
                elements: observeElements,
                runtime,
                startInObserve: deepLink.mode === 'observe' && deepLink.system === undefined,
                deepLink: { ...deepLink.observer, paused: deepLink.paused },
                epochFromLink: deepLink.julianDate !== undefined || deepLink.simulationDate !== undefined,
                onModeChanged: (active) => {
                    explorerPanel.classList.toggle('is-observing', active);
                },
            });

            // Star systems (ARCHITECTURE §12). C++ swaps the scene and reports it as phase 1 of
            // onSystemChanged — after a jump, and once at start-up for a `?system=` link, which C++
            // reads itself while building the scene (after this code has run with Sol's UI).
            const observe = observeController;
            const applySystem = (id: string): void => {
                const inSol = id === SOL_SYSTEM_ID;
                document.body.classList.toggle('in-other-system', !inSol);
                observe.setAvailable(inSol, 'Observe stands on Earth, so it is only in the Solar System');
                void explorerReady
                    .then(() => explorer.showSystem(id, systemFactsUrl(id, deployedBaseUrl.toString())))
                    .catch((error: unknown) => {
                        console.error('[Systems] facts failed to load:', error);
                    });
            };
            let linkSystemArrived = false;
            registerWasmCallbacks({
                onSystemChanged: (id: string, phase: number) => {
                    if (phase !== 1) return;
                    applySystem(id);
                    if (!linkSystemArrived && id === deepLink.system) {
                        // The landing replaced what the settings panel applied from the link.
                        linkSystemArrived = true;
                        if (deepLink.timeScale !== undefined) runtime.setTimeScale(deepLink.timeScale);
                        if (deepLink.camera) {
                            const { x, y, z, yaw, pitch } = deepLink.camera;
                            runtime.setCameraPose(x, y, z, yaw, pitch);
                        }
                    }
                },
            });
            if (runtime.getActiveSystemId() !== SOL_SYSTEM_ID) {
                applySystem(runtime.getActiveSystemId());
            }
            void systemIndexPromise.then((index) => {
                if (!index || index.systems.length < 2) {
                    systemPickerRoot.hidden = true;
                    return;
                }
                initSystemPicker({ root: systemPickerRoot, runtime, index, status: settingsStatus });
            });

            const tourPlayer = initEducationalLayer({
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

            initStormCard({ runtime, card: stormCardElement, title: stormCardTitle, lines: stormCardLines });

            initTimelineEditor({ runtime, canvas, deepLink, tourPlayer, caption: tourCaption });

            if (deepLink.planet !== undefined && !deepLink.mission && !deepLink.tour && !deepLink.timeline) {
                explorer.applyDeepLinkPlanet(deepLink.planet, { focusCamera: false });
            }
        },
        (error: unknown) => {
            settingsStatus.textContent = 'Controls unavailable';
            console.error('Failed to initialize SolarSystem WASM:', error);
            const noWebGl = !preinitializedWebGLContext;
            showFatalPanel({
                title: noWebGl ? 'WebGL 2 is not available' : 'The simulation failed to start',
                message: noWebGl
                    ? 'This browser could not create a WebGL 2 context, which the 3D scene needs.'
                    : `Loading the simulation failed: ${error instanceof Error ? error.message : String(error)}`,
                hints: noWebGl ? BROWSER_HINTS : ['Check your connection and reload.', ...BROWSER_HINTS],
            });
        },
    )
    .catch((error: unknown) => {
        settingsStatus.textContent = 'Controls unavailable';
        console.error('Failed to initialize the SolarSystem UI:', error);
    });
