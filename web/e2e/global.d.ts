export {};

declare global {
  interface Window {
    setCameraPose?: (x: number, y: number, z: number, yaw: number, pitch: number) => void;
    setQualityPreset?: (preset: 0 | 1 | 2) => void;
    getQualityPreset?: () => 0 | 1 | 2;
  }
}
