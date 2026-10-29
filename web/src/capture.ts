/**
 * Offline video capture of a timeline (docs/ARCHITECTURE.md §11.2). The browser's main loop
 * stops and every frame is drawn on demand: the timeline puts the scene at t = frame / fps,
 * StepCaptureFrame advances the engine's animation clock by exactly 1 / fps and draws at
 * the requested size, and the canvas goes straight into a WebCodecs VideoEncoder, muxed to
 * MP4 by mp4-muxer. A slow GPU or a long encode only makes the capture take longer; the
 * video is the same. Before each frame is taken, pending textures get a bounded time to
 * arrive, so the first frames of a shot are not of placeholder textures.
 *
 * Without WebCodecs (or without an encoder for the size), MediaRecorder records
 * canvas.captureStream() at the canvas's own size while the timeline plays in real time: no
 * determinism, but a video.
 */
import { ArrayBufferTarget, Muxer } from 'mp4-muxer';
import type { SolarSystemRuntime } from './wasmBridge.js';

export interface CaptureSettings {
    /** Output size in pixels; rounded down to even (4:2:0 video). */
    width: number;
    height: number;
    fps: number;
    /** Bits per second; defaults to defaultBitrate(). */
    bitrate?: number;
    /** Longest wait per frame for textures to finish loading, ms. */
    settleTimeoutMs?: number;
}

export interface CaptureTarget {
    runtime: SolarSystemRuntime;
    canvas: HTMLCanvasElement;
    durationSec: number;
    /** Put the scene at t; false while a focus body is still loading. */
    renderAt(t: number): boolean;
    /** MediaRecorder fallback: play the timeline in real time; resolves at its end. */
    playRealtime(signal?: AbortSignal): Promise<void>;
}

export type CapturePhase = 'rendering' | 'waiting for textures' | 'finishing' | 'recording in real time';

export interface CaptureCallbacks {
    onProgress?(done: number, total: number, phase: CapturePhase): void;
    signal?: AbortSignal;
}

export interface CaptureResult {
    blob: Blob;
    extension: 'mp4' | 'webm';
    codec: string;
    frames: number;
    method: 'webcodecs' | 'mediarecorder';
}

interface CodecChoice {
    codec: string;
    container: 'avc' | 'av1' | 'vp9';
}

/**
 * Encoders to try, best first: H.264 High (level 5.2 covers 4K60) down to Baseline, which is
 * all some software encoders offer, then AV1 and VP9 (both muxable into MP4).
 */
export const CODEC_CANDIDATES: readonly CodecChoice[] = [
    { codec: 'avc1.640034', container: 'avc' },
    { codec: 'avc1.64002a', container: 'avc' },
    { codec: 'avc1.4d002a', container: 'avc' },
    { codec: 'avc1.42e02a', container: 'avc' },
    { codec: 'avc1.42e01f', container: 'avc' },
    { codec: 'av01.0.13M.08', container: 'av1' },
    { codec: 'av01.0.08M.08', container: 'av1' },
    { codec: 'vp09.00.40.08', container: 'vp9' },
];

/** Frames to draw: one at t = 0 and one at the very end, 1 / fps apart. */
export function frameCount(durationSec: number, fps: number): number {
    return Math.max(1, Math.round(durationSec * fps) + 1);
}

export function evenSize(width: number, height: number): { width: number; height: number } {
    return { width: Math.max(2, Math.floor(width / 2) * 2), height: Math.max(2, Math.floor(height / 2) * 2) };
}

/** About 0.12 bits per pixel per frame: clean H.264 for slow camera moves over a starfield. */
export function defaultBitrate(width: number, height: number, fps: number): number {
    return Math.min(80e6, Math.max(2e6, Math.round(width * height * fps * 0.12)));
}

export function webCodecsAvailable(): boolean {
    return typeof VideoEncoder !== 'undefined' && typeof VideoFrame !== 'undefined';
}

async function pickEncoder(width: number, height: number, fps: number, bitrate: number):
Promise<{ choice: CodecChoice; config: VideoEncoderConfig } | null> {
    for (const choice of CODEC_CANDIDATES) {
        const config: VideoEncoderConfig = { codec: choice.codec, width, height, bitrate, framerate: fps };
        if (choice.container === 'avc') {
            config.avc = { format: 'avc' }; // length-prefixed NAL units with a decoder config, as MP4 wants
        }
        try {
            const support = await VideoEncoder.isConfigSupported(config);
            if (support.supported) {
                return { choice, config: support.config ?? config };
            }
        } catch {
            // An unparseable codec string on this browser: try the next.
        }
    }
    return null;
}

/** Yield to the event loop without setTimeout's 4 ms clamp (progress UI, network callbacks). */
function nextTask(): Promise<void> {
    return new Promise((resolve) => {
        const channel = new MessageChannel();
        channel.port1.onmessage = () => resolve();
        channel.port2.postMessage(null);
    });
}

const sleep = (ms: number): Promise<void> => new Promise((resolve) => setTimeout(resolve, ms));

function throwIfAborted(signal?: AbortSignal): void {
    if (signal?.aborted) {
        throw new DOMException('Capture cancelled', 'AbortError');
    }
}

async function captureWithWebCodecs(
    target: CaptureTarget,
    settings: CaptureSettings,
    encoder: { choice: CodecChoice; config: VideoEncoderConfig },
    callbacks: CaptureCallbacks,
): Promise<CaptureResult> {
    const { runtime, canvas } = target;
    const { width, height } = evenSize(settings.width, settings.height);
    const fps = settings.fps;
    const settleTimeoutMs = settings.settleTimeoutMs ?? 5000;
    if (!runtime.beginCapture(width, height)) {
        throw new Error('Capture could not start (is a VR session presenting?)');
    }
    let videoEncoder: VideoEncoder | null = null;
    try {
        const muxTarget = new ArrayBufferTarget();
        const muxer = new Muxer({
            target: muxTarget,
            video: { codec: encoder.choice.container, width, height, frameRate: fps },
            fastStart: 'in-memory',
            firstTimestampBehavior: 'offset',
        });
        let failure: unknown = null;
        videoEncoder = new VideoEncoder({
            output: (chunk, metadata) => muxer.addVideoChunk(chunk, metadata),
            error: (error) => {
                failure = error;
            },
        });
        videoEncoder.configure(encoder.config);

        const total = frameCount(target.durationSec, fps);
        const frameMicros = 1e6 / fps;
        // Once a frame has waited out the timeout for textures, later frames stop waiting for
        // them (a missing file would otherwise cost every frame the full timeout) and only
        // wait for focus bodies that are not loaded at all.
        let texturesStalled = false;
        for (let index = 0; index < total; ++index) {
            throwIfAborted(callbacks.signal);
            if (failure) {
                throw failure;
            }
            const t = Math.min(index / fps, target.durationSec);
            let ready = target.renderAt(t);
            runtime.stepCaptureFrame(index === 0 ? 0 : 1 / fps);
            const waitStart = performance.now();
            while (!ready || (!texturesStalled && runtime.getPendingAssetCount() > 0)) {
                if (performance.now() - waitStart > settleTimeoutMs) {
                    texturesStalled = ready;
                    break;
                }
                callbacks.onProgress?.(index, total, 'waiting for textures');
                await sleep(40);
                throwIfAborted(callbacks.signal);
                ready = target.renderAt(t);
                runtime.stepCaptureFrame(0); // same instant: uploads what arrived, redraws
            }
            // Same task as the draw: the WebGL drawing buffer still holds this frame.
            const frame = new VideoFrame(canvas, {
                timestamp: Math.round(index * frameMicros),
                duration: Math.round(frameMicros),
            });
            videoEncoder.encode(frame, { keyFrame: index % (2 * fps) === 0 });
            frame.close();
            callbacks.onProgress?.(index + 1, total, 'rendering');
            while (videoEncoder.encodeQueueSize > 4) {
                await sleep(2);
            }
            await nextTask();
        }
        callbacks.onProgress?.(total, total, 'finishing');
        await videoEncoder.flush();
        if (failure) {
            throw failure;
        }
        muxer.finalize();
        return {
            blob: new Blob([muxTarget.buffer], { type: 'video/mp4' }),
            extension: 'mp4',
            codec: encoder.choice.codec,
            frames: total,
            method: 'webcodecs',
        };
    } finally {
        if (videoEncoder && videoEncoder.state !== 'closed') {
            videoEncoder.close();
        }
        runtime.endCapture();
    }
}

async function captureWithMediaRecorder(
    target: CaptureTarget,
    settings: CaptureSettings,
    callbacks: CaptureCallbacks,
): Promise<CaptureResult> {
    if (typeof MediaRecorder === 'undefined' || typeof target.canvas.captureStream !== 'function') {
        throw new Error('This browser has neither WebCodecs nor MediaRecorder for canvas video');
    }
    const mimeType = ['video/mp4;codecs=avc1.42E01E', 'video/mp4', 'video/webm;codecs=vp9', 'video/webm']
        .find((type) => MediaRecorder.isTypeSupported(type));
    if (!mimeType) {
        throw new Error('MediaRecorder supports no video format here');
    }
    const stream = target.canvas.captureStream(settings.fps);
    const recorder = new MediaRecorder(stream, {
        mimeType,
        videoBitsPerSecond: settings.bitrate ?? defaultBitrate(target.canvas.width, target.canvas.height, settings.fps),
    });
    const chunks: Blob[] = [];
    recorder.ondataavailable = (event) => {
        if (event.data.size > 0) {
            chunks.push(event.data);
        }
    };
    const started = new Promise<void>((resolve, reject) => {
        recorder.onstart = () => resolve();
        recorder.onerror = (event) => reject(new Error(`MediaRecorder failed: ${(event as ErrorEvent).message ?? event.type}`));
    });
    const stopped = new Promise<void>((resolve) => {
        recorder.onstop = () => resolve();
    });
    callbacks.onProgress?.(0, 1, 'recording in real time');
    recorder.start(1000);
    try {
        // start() is asynchronous; frames drawn before the start event are not recorded.
        await started;
        await target.playRealtime(callbacks.signal);
    } finally {
        if (recorder.state !== 'inactive') {
            recorder.stop();
        }
        await stopped;
        stream.getTracks().forEach((track) => track.stop());
    }
    throwIfAborted(callbacks.signal);
    const blob = new Blob(chunks, { type: mimeType.split(';')[0] });
    if (blob.size === 0) {
        throw new Error('MediaRecorder recorded no frames (is the page in a background tab?)');
    }
    const extension = mimeType.startsWith('video/mp4') ? 'mp4' : 'webm';
    return {
        blob,
        extension,
        codec: mimeType,
        frames: Math.round(target.durationSec * settings.fps),
        method: 'mediarecorder',
    };
}

/** Render a timeline to a video: WebCodecs + mp4-muxer when possible, else MediaRecorder. */
export async function captureVideo(
    target: CaptureTarget,
    settings: CaptureSettings,
    callbacks: CaptureCallbacks = {},
): Promise<CaptureResult> {
    const { width, height } = evenSize(settings.width, settings.height);
    if (webCodecsAvailable()) {
        const bitrate = settings.bitrate ?? defaultBitrate(width, height, settings.fps);
        const encoder = await pickEncoder(width, height, settings.fps, bitrate);
        if (encoder) {
            return captureWithWebCodecs(target, { ...settings, width, height, bitrate }, encoder, callbacks);
        }
        console.warn(`[Capture] No WebCodecs encoder for ${width}x${height}@${settings.fps}; recording in real time`);
    }
    return captureWithMediaRecorder(target, settings, callbacks);
}

export function downloadBlob(blob: Blob, filename: string): void {
    const url = URL.createObjectURL(blob);
    const link = document.createElement('a');
    link.href = url;
    link.download = filename;
    document.body.appendChild(link);
    link.click();
    link.remove();
    // Long enough for the download to start; revoking at once can cancel it in some browsers.
    setTimeout(() => URL.revokeObjectURL(url), 60_000);
}
