import './style.css'
import Module from './SolarSystem.js'

const canvas = document.getElementById('canvas') as HTMLCanvasElement;
const loadingContainer = document.getElementById('loading-container') as HTMLElement;
const progressBar = document.getElementById('progress-bar') as HTMLElement;
const progressText = document.getElementById('progress-text') as HTMLElement;

// Global progress tracking
let totalResources = 0;
let loadedResources = 0;

// Function to update progress bar
function updateProgress(loaded: number, total: number) {
    loadedResources = loaded;
    totalResources = total;
    
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

const moduleConfig = {
    canvas: canvas,
    // CRITICAL: Tell Emscripten to look for assets at the domain root
    // because we moved .wasm and .data to the 'public' folder.
    locateFile: (path: string, prefix: string) => {
        if (path.endsWith('.wasm') || path.endsWith('.data')) {
            return './' + path;
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
