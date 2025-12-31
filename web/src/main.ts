import './style.css'
import Module from './SolarSystem.js'

const canvas = document.getElementById('canvas') as HTMLCanvasElement;

const moduleConfig = {
    canvas: canvas,
    // CRITICAL: Tell Emscripten to look for assets at the domain root
    // because we moved .wasm and .data to the 'public' folder.
    locateFile: (path: string, prefix: string) => {
        if (path.endsWith('.wasm') || path.endsWith('.data')) {
            return '/' + path;
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
