export interface SolarSystemModule {
  canvas: HTMLCanvasElement;
  _RunOneFrame: () => void;
  // Add other exported C++ functions here if needed
}

declare const SolarSystem: (config: any) => Promise<SolarSystemModule>;
export default SolarSystem;
