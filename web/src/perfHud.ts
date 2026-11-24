/**
 * `?perf=1`: a small overlay with the last frame's profile from C++ (core/FrameProfile.h via
 * GetFrameStatsJson): the GPU backend the frame records against (and, in the WebGPU binary, its
 * device and how many WGSL modules compiled), frame time and the worst frame since the previous read, CPU and GPU
 * milliseconds per frame stage and per frame-graph pass kind (render/graph/FramePlan: "scene",
 * "shadow", "aurora-bloom", …, summed over components and views), the job pool and the
 * texture-upload pump. Turning it on also turns on the GPU timers (EXT_disjoint_timer_query_webgl2;
 * browsers that withhold the extension show "n/a" in that column).
 *
 * The parsed stats are also on `window.__solarSystemPerf` for scripts (Playwright reads the
 * slow-frame count from there).
 */
import type { SolarSystemRuntime } from './wasmBridge.js';

export const FRAME_STAGES = [
    'update',
    'background',
    'shadow',
    'scene',
    'foreground',
    'hud',
    'uploads',
    'spaceWeather',
] as const;
export type FrameStage = (typeof FRAME_STAGES)[number];
export type StageTimes = Record<FrameStage, number>;

/** One frame-graph pass kind: every pass of that kind this frame, summed. */
export interface PassTimes {
    kind: string;
    stage: string;
    /** Passes of this kind that ran (one per component and view for "scene"). */
    count: number;
    cpu: number;
    /** Null until the GPU timers have a finished frame (like FrameStats.gpu). */
    gpu: number | null;
}

/** A WebGPU device beside the frame (gfx/rhi/wgpu/WgpuBringUp.h), in the SolarSystem-wgpu binary. */
export interface WebGpuStatus {
    device: string;
    timestamps: boolean;
    wgslTotal: number;
    wgslCompiled: number;
    wgslFailed: number;
}

export interface FrameStats {
    /** rhi::BackendId of the API the frame records against: "webgl2", "opengl", "webgpu". */
    backend: string;
    /** "WebGL 2", "WebGPU", …; the HUD's first line. */
    backendName: string;
    webgpu: WebGpuStatus | null;
    frameMs: number;
    /** Worst frame since the previous GetFrameStatsJson read. */
    worstFrameMs: number;
    frames: number;
    /** Frames over 33.4 ms since start. */
    slowFrames: number;
    cpu: StageTimes;
    gpuSupported: boolean;
    /** Null until a frame's timer queries have been read back (or with no timer extension). */
    gpu: StageTimes | null;
    jobs: { workers: number; queued: number; inFlight: number; completed: number };
    uploads: { bytes: number; pending: number };
    uniformCalls: number;
    /** Per pass kind, in the order the frame graph first ran them. */
    passes: PassTimes[];
}

function num(value: unknown): number {
    return typeof value === 'number' && Number.isFinite(value) ? value : 0;
}

function record(value: unknown): Record<string, unknown> | null {
    return typeof value === 'object' && value !== null && !Array.isArray(value)
        ? (value as Record<string, unknown>)
        : null;
}

function passTimes(value: unknown): PassTimes[] {
    if (!Array.isArray(value)) {
        return [];
    }
    const passes: PassTimes[] = [];
    for (const entry of value) {
        const raw = record(entry);
        if (!raw || typeof raw.kind !== 'string') {
            continue;
        }
        passes.push({
            kind: raw.kind,
            stage: typeof raw.stage === 'string' ? raw.stage : '',
            count: num(raw.count),
            cpu: num(raw.cpu),
            gpu: typeof raw.gpu === 'number' && Number.isFinite(raw.gpu) ? raw.gpu : null,
        });
    }
    return passes;
}

function stageTimes(value: unknown): StageTimes | null {
    const raw = record(value);
    if (!raw) {
        return null;
    }
    const out = {} as StageTimes;
    for (const stage of FRAME_STAGES) {
        out[stage] = num(raw[stage]);
    }
    return out;
}

/** GetFrameStatsJson's text, or null for "{}" (no application yet) and anything malformed. */
export function parseFrameStats(json: string): FrameStats | null {
    let raw: Record<string, unknown> | null;
    try {
        raw = record(JSON.parse(json));
    } catch {
        return null;
    }
    const cpu = raw ? stageTimes(raw.cpu) : null;
    if (!raw || !cpu) {
        return null;
    }
    const jobs = record(raw.jobs) ?? {};
    const uploads = record(raw.uploads) ?? {};
    const webgpu = record(raw.webgpu);
    return {
        backend: typeof raw.backend === 'string' ? raw.backend : '',
        backendName: typeof raw.backendName === 'string' ? raw.backendName : '',
        webgpu: webgpu
            ? {
                  device: typeof webgpu.device === 'string' ? webgpu.device : '',
                  timestamps: webgpu.timestamps === true,
                  wgslTotal: num(webgpu.wgslTotal),
                  wgslCompiled: num(webgpu.wgslCompiled),
                  wgslFailed: num(webgpu.wgslFailed),
              }
            : null,
        frameMs: num(raw.frameMs),
        worstFrameMs: num(raw.worstFrameMs),
        frames: num(raw.frames),
        slowFrames: num(raw.slowFrames),
        cpu,
        gpuSupported: raw.gpuSupported === true,
        gpu: stageTimes(raw.gpu),
        jobs: {
            workers: num(jobs.workers),
            queued: num(jobs.queued),
            inFlight: num(jobs.inFlight),
            completed: num(jobs.completed),
        },
        uploads: { bytes: num(uploads.bytes), pending: num(uploads.pending) },
        uniformCalls: num(raw.uniformCalls),
        passes: passTimes(raw.passes),
    };
}

/** One right-aligned column: wide enough for a software rasteriser's 1000+ ms with a gap. */
function ms(value: number): string {
    return value.toFixed(2).padStart(9);
}

function kib(bytes: number): string {
    return bytes >= 1024 * 1024 ? `${(bytes / (1024 * 1024)).toFixed(1)} MB` : `${Math.round(bytes / 1024)} KB`;
}

/** Pass rows shown: the costliest kinds (by GPU time once there is one, else CPU). */
const MAX_PASS_ROWS = 12;

/** The overlay's text: one row per stage, then the totals, then the costliest pass kinds. */
export function formatFrameStats(stats: FrameStats): string {
    const gpuLabel = stats.gpu ? 'gpu'.padStart(9) : stats.gpuSupported ? '   gpu…' : '   gpu n/a';
    const lines: string[] = [];
    if (stats.backendName) {
        lines.push(`backend ${stats.backendName}`);
    }
    if (stats.webgpu) {
        const { device, wgslTotal, wgslCompiled, wgslFailed } = stats.webgpu;
        const pending = wgslTotal - wgslCompiled - wgslFailed;
        const wgsl =
            wgslTotal === 0
                ? 'no WGSL'
                : `WGSL ${String(wgslCompiled)}/${String(wgslTotal)}${wgslFailed > 0 ? ` (${String(wgslFailed)} failed)` : ''}${pending > 0 ? ' …' : ''}`;
        lines.push(`webgpu ${device}: ${wgsl}`);
    }
    lines.push(
        `frame ${stats.frameMs.toFixed(1)} ms  worst ${stats.worstFrameMs.toFixed(1)} ms`,
        `slow (>33 ms) ${String(stats.slowFrames)} / ${String(stats.frames)}`,
        `${'stage'.padEnd(11)}${'cpu'.padStart(9)}${gpuLabel}`,
    );
    let cpuTotal = 0;
    let gpuTotal = 0;
    for (const stage of FRAME_STAGES) {
        cpuTotal += stats.cpu[stage];
        gpuTotal += stats.gpu?.[stage] ?? 0;
        lines.push(`${stage.padEnd(11)}${ms(stats.cpu[stage])}${stats.gpu ? ms(stats.gpu[stage]) : ''}`);
    }
    lines.push(`${'total'.padEnd(11)}${ms(cpuTotal)}${stats.gpu ? ms(gpuTotal) : ''}`);
    const ran = stats.passes.filter((pass) => pass.count > 0);
    if (ran.length > 0) {
        const cost = (pass: PassTimes): number => pass.gpu ?? pass.cpu;
        const rows = [...ran].sort((a, b) => cost(b) - cost(a)).slice(0, MAX_PASS_ROWS);
        lines.push(`${'pass'.padEnd(22)}${'cpu'.padStart(9)}${stats.gpu ? 'gpu'.padStart(9) : ''}`);
        for (const pass of rows) {
            const label = `${pass.kind} ×${String(pass.count)}`;
            lines.push(`${label.padEnd(22)}${ms(pass.cpu)}${pass.gpu !== null ? ms(pass.gpu) : ''}`);
        }
    }
    const { workers, queued, inFlight } = stats.jobs;
    lines.push(
        workers > 0
            ? `jobs ${String(workers)} workers, ${String(queued)} queued, ${String(inFlight)} running`
            : 'jobs inline (no workers)',
    );
    lines.push(`uploads ${kib(stats.uploads.bytes)} this frame, ${String(stats.uploads.pending)} pending`);
    return lines.join('\n');
}

export function perfHudRequested(search: string): boolean {
    const value = new URLSearchParams(search).get('perf');
    return value !== null && value !== '0' && value !== 'false';
}

declare global {
    interface Window {
        __solarSystemPerf?: { latest: () => FrameStats | null };
    }
}

/** How often the overlay reads C++; the worst-frame column covers everything in between. */
const REFRESH_MS = 250;

export function installPerfHud(runtime: SolarSystemRuntime, doc: Document = document): () => void {
    runtime.setGpuTimersEnabled(true);
    const panel = doc.createElement('pre');
    panel.id = 'perf-hud';
    panel.setAttribute('aria-live', 'off');
    panel.textContent = 'perf: waiting for the first frame…';
    doc.body.appendChild(panel);

    let latest: FrameStats | null = null;
    window.__solarSystemPerf = { latest: () => latest };
    const timer = window.setInterval(() => {
        const stats = parseFrameStats(runtime.getFrameStatsJson());
        if (stats) {
            latest = stats;
            panel.textContent = formatFrameStats(stats);
            panel.classList.toggle('is-slow', stats.worstFrameMs > 33.4);
        }
    }, REFRESH_MS);

    return () => {
        window.clearInterval(timer);
        runtime.setGpuTimersEnabled(false);
        panel.remove();
        delete window.__solarSystemPerf;
    };
}
