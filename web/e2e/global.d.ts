export {};

declare global {
  interface Window {
    setCameraPose?: (x: number, y: number, z: number, yaw: number, pitch: number) => void;
    getQualityPreset?: () => number;
  }
}
