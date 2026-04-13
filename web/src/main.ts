import './style.css'
import Module from './SolarSystem.js'

const canvas = document.getElementById('canvas') as HTMLCanvasElement;
const loadingContainer = document.getElementById('loading-container') as HTMLElement;
const progressBar = document.getElementById('progress-bar') as HTMLElement;
const progressText = document.getElementById('progress-text') as HTMLElement;
const deployedBaseUrl = new URL(import.meta.env.BASE_URL, window.location.href);

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
            }
        }, 500);
    }
}

// Expose progress function globally for C++ to call
(window as any).updateLoadingProgress = updateProgress;
(window as any).__solarSystemAssetBase = deployedBaseUrl.toString();

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
Module(moduleConfig).then((instance) => {
    console.log("Module loaded successfully", instance);
});
