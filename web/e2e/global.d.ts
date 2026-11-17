export {};

declare global {
    interface Window {
        setCameraPose?: (x: number, y: number, z: number, yaw: number, pitch: number) => void;
        setQualityPreset?: (preset: 0 | 1 | 2) => void;
        getQualityPreset?: () => 0 | 1 | 2;
        /** perfHud.ts under ?perf=1: the last parsed GetFrameStatsJson. */
        __solarSystemPerf?: {
            latest: () => {
                frameMs: number;
                worstFrameMs: number;
                frames: number;
                slowFrames: number;
                gpu: Record<string, number> | null;
                gpuSupported: boolean;
                jobs: { workers: number; queued: number; inFlight: number; completed: number };
                uploads: { bytes: number; pending: number };
            } | null;
        };
    }
}
