export interface SolarSystemModule {
  canvas: HTMLCanvasElement;
  _RunOneFrame: () => void;
  // Add other exported C++ functions here if needed
}

declare const SolarSystem: (config: any) => Promise<SolarSystemModule>;
export default SolarSystem;

// Global callbacks invoked from C++ via EM_ASM / emscripten
declare global {
  interface Window {
    updateLoadingProgress?: (loaded: number, total: number) => void;
    updateStreamingProgress?: (completed: number, total: number) => void;
    setCameraPose?: (x: number, y: number, z: number, yaw: number, pitch: number) => void;
    setQualityPreset?: (preset: number) => void; // 0=low,1=medium,2=full
    focusPlanet?: (idx: number) => void; // 0=sun,1=merc,...9=pluto
    __solarSystemAssetBase?: string;
  }
}
