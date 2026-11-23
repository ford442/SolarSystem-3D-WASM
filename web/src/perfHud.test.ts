import { describe, expect, it } from 'vitest';
import { formatFrameStats, parseFrameStats, perfHudRequested } from './perfHud';

// The shape FrameProfile::ToJson writes (tests/test_frame_profile.cpp pins the C++ side).
const SAMPLE = JSON.stringify({
    frameMs: 18.25,
    worstFrameMs: 41.5,
    frames: 300,
    slowFrames: 2,
    cpu: { update: 1, background: 0.5, shadow: 2, scene: 4, foreground: 1.5, hud: 0.25, uploads: 0.75 },
    gpuSupported: true,
    gpu: null,
    jobs: { workers: 3, queued: 1, inFlight: 2, completed: 40 },
    uploads: { bytes: 2 * 1024 * 1024, pending: 4 },
    uniformCalls: 812,
    passes: [
        { kind: 'skybox', stage: 'background', count: 1, cpu: 0.25, gpu: null },
        { kind: 'shadow', stage: 'shadow', count: 6, cpu: 2, gpu: null },
        { kind: 'scene', stage: 'scene', count: 7, cpu: 4, gpu: null },
        { kind: 'aurora-bloom', stage: 'foreground', count: 0, cpu: 0, gpu: null },
    ],
});

describe('parseFrameStats', () => {
    it('reads the C++ profile', () => {
        const stats = parseFrameStats(SAMPLE);
        expect(stats).not.toBeNull();
        expect(stats?.cpu.scene).toBe(4);
        expect(stats?.gpu).toBeNull();
        expect(stats?.gpuSupported).toBe(true);
        expect(stats?.jobs.workers).toBe(3);
        expect(stats?.uploads.pending).toBe(4);
        expect(stats?.slowFrames).toBe(2);
        expect(stats?.passes.map((pass) => pass.kind)).toEqual(['skybox', 'shadow', 'scene', 'aurora-bloom']);
        expect(stats?.passes[2]).toEqual({ kind: 'scene', stage: 'scene', count: 7, cpu: 4, gpu: null });
    });

    it('reads pass GPU times once they exist and skips malformed passes', () => {
        const stats = parseFrameStats(
            '{"cpu":{},"passes":[{"kind":"scene","count":2,"cpu":1,"gpu":3.5},{"count":1},"x",{"kind":"hud"}]}',
        );
        expect(stats?.passes).toEqual([
            { kind: 'scene', stage: '', count: 2, cpu: 1, gpu: 3.5 },
            { kind: 'hud', stage: '', count: 0, cpu: 0, gpu: null },
        ]);
        // An older build without frame-graph passes.
        expect(parseFrameStats('{"cpu":{}}')?.passes).toEqual([]);
    });

    it('returns null before the application exists and for junk', () => {
        expect(parseFrameStats('{}')).toBeNull();
        expect(parseFrameStats('not json')).toBeNull();
        expect(parseFrameStats('[1,2]')).toBeNull();
    });

    it('treats missing stage values as zero', () => {
        const stats = parseFrameStats('{"cpu":{"scene":3},"gpu":{"shadow":"x"}}');
        expect(stats?.cpu.update).toBe(0);
        expect(stats?.cpu.scene).toBe(3);
        expect(stats?.gpu?.shadow).toBe(0);
    });
});

describe('formatFrameStats', () => {
    it('lists every stage with a total, and says when GPU timing is pending', () => {
        const stats = parseFrameStats(SAMPLE);
        if (!stats) {
            throw new Error('sample did not parse');
        }
        const text = formatFrameStats(stats);
        expect(text).toContain('worst 41.5 ms');
        expect(text).toContain('slow (>33 ms) 2 / 300');
        expect(text).toContain('gpu…');
        expect(text).toMatch(/total\s+10\.00/);
        expect(text).toContain('jobs 3 workers, 1 queued, 2 running');
        expect(text).toContain('uploads 2.0 MB this frame, 4 pending');

        const withGpu = formatFrameStats({ ...stats, gpu: { ...stats.cpu, scene: 6 } });
        expect(withGpu).toMatch(/scene\s+4\.00\s+6\.00/);
        // Three-digit values keep a gap between the columns.
        const slow = formatFrameStats({
            ...stats,
            cpu: { ...stats.cpu, foreground: 0.79 },
            gpu: { ...stats.cpu, foreground: 233.9 },
        });
        expect(slow).toMatch(/foreground\s+0\.79\s+233\.90/);
        expect(formatFrameStats({ ...stats, gpuSupported: false })).toContain('gpu n/a');
        expect(formatFrameStats({ ...stats, jobs: { ...stats.jobs, workers: 0 } })).toContain('jobs inline');
    });

    it('lists the pass kinds that ran, costliest first', () => {
        const stats = parseFrameStats(SAMPLE);
        if (!stats) {
            throw new Error('sample did not parse');
        }
        const text = formatFrameStats(stats);
        const lines = text.split('\n');
        const header = lines.findIndex((line) => line.startsWith('pass'));
        expect(header).toBeGreaterThan(0);
        expect(lines[header + 1]).toMatch(/^scene ×7\s+4\.00$/);
        expect(lines[header + 2]).toMatch(/^shadow ×6\s+2\.00$/);
        expect(lines[header + 3]).toMatch(/^skybox ×1\s+0\.25$/);
        expect(text).not.toContain('aurora-bloom'); // did not run this frame

        const passes = stats.passes.map((pass) => ({ ...pass, gpu: pass.kind === 'skybox' ? 9 : 1 }));
        const withGpu = formatFrameStats({ ...stats, gpu: stats.cpu, passes });
        expect(withGpu).toMatch(/pass\s+cpu\s+gpu/);
        expect(withGpu.split('\n').find((line) => line.startsWith('skybox'))).toMatch(/0\.25\s+9\.00$/);
        const order = withGpu.split('\n').filter((line) => /×\d/.test(line));
        expect(order[0]).toMatch(/^skybox/); // sorted by GPU time once there is one
    });
});

describe('perfHudRequested', () => {
    it('is on for ?perf=1 and off for absent, 0 or false', () => {
        expect(perfHudRequested('?perf=1')).toBe(true);
        expect(perfHudRequested('?x=1&perf')).toBe(true);
        expect(perfHudRequested('')).toBe(false);
        expect(perfHudRequested('?perf=0')).toBe(false);
        expect(perfHudRequested('?perf=false')).toBe(false);
    });
});
