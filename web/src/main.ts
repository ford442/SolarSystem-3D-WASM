import './style.css'
import Module from './SolarSystem.js'

const canvas = document.getElementById('canvas') as HTMLCanvasElement;
const loadingContainer = document.getElementById('loading-container') as HTMLElement;
const progressBar = document.getElementById('progress-bar') as HTMLElement;
const progressText = document.getElementById('progress-text') as HTMLElement;
const streamingProgress = document.getElementById('streaming-progress') as HTMLElement;
const streamingText = document.getElementById('streaming-text') as HTMLElement;
const streamingBar = document.getElementById('streaming-progress-bar') as HTMLElement;
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
(window as any).updateLoadingProgress = updateProgress;
(window as any).updateStreamingProgress = updateStreamingProgress;
(window as any).__solarSystemAssetBase = runtimeAssetBase;

// TypeScript declarations for C++-called window callbacks (and asset base)
declare global {
  interface Window {
    updateLoadingProgress?: (loaded: number, total: number) => void;
    updateStreamingProgress?: (completed: number, total: number) => void;
    setCameraPose?: (x: number, y: number, z: number, yaw: number, pitch: number) => void;
    __solarSystemAssetBase?: string;
  }
}

const moduleConfig = {
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
Module(moduleConfig).then((instance: any) => {
    console.log("Module loaded successfully", instance);
    (window as any).setCameraPose = instance.cwrap('SetCameraPose', null, ['number', 'number', 'number', 'number', 'number']);
    (window as any).setQualityPreset = instance.cwrap('SetQualityPreset', null, ['number']);
    (window as any).focusPlanet = instance.cwrap('FocusPlanet', null, ['number']);

    // URL param support: ?quality=low|medium|full (or ?q=0|1|2)
    try {
        const params = new URLSearchParams(window.location.search);
        const q = (params.get('quality') || params.get('q') || 'full').toLowerCase();
        let preset = 2;
        if (q === 'low' || q === '0') preset = 0;
        else if (q === 'medium' || q === 'med' || q === '1') preset = 1;
        else if (q === 'full' || q === '2') preset = 2;
        if ((window as any).setQualityPreset) (window as any).setQualityPreset(preset);
    } catch(e) { /* ignore */ }
});
