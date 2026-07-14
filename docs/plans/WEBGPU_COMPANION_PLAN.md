# WebGPU Companion Renderer — Multi-Session Plan

**Status:** Phase 1 complete; companion remains opt-in
**Target:** Parallel track alongside the existing C++/WASM WebGL 2 renderer  
**Live reference:** [test.1ink.us/solar-system](https://test.1ink.us/solar-system/index.html) (premium WebGL 2 build)

---

## Executive Summary

**Do not migrate the current renderer to WebGPU.** The production path stays C++17 → Emscripten → WebGL 2 with GLSL 300 es, DDS textures, custom shadows, atmospheric scattering, and staged planet loading.

**Instead:** Build a lighter **companion** implementation using **Three.js + WebGPURenderer** in a separate folder (`web/threejs/` or a dedicated branch). This enables fast iteration on UI, post-processing, camera presets, and music reactivity without rewriting `nv_dds.cpp`, `ShadowMapFBO`, shader pipelines, or the `#ifdef __EMSCRIPTEN__` split.

A full WebGPU port of the custom C++ renderer (Emdawnwebgpu / webgpu.h / WGSL) would be a near-complete backend rewrite and is **out of scope** for the near term.

---

## Goals

| Goal | Companion (Three.js) | Premium (C++/WASM) |
|------|----------------------|-------------------|
| 8K+ DDS, custom PCF/ray shadows | Subset / converted assets | Full fidelity |
| LOD + async streaming | Simpler distance-based swaps | `LoadHighResIfClose` + manifests |
| Fast UI / effects iteration | Primary focus | Slower to change |
| Deployed demo stability | Optional second entry point | Primary demo |

---

## Phased Roadmap (Several Sessions)

### Phase 0 — Scaffold (Session 1)

- [x] Create `web/threejs/` Vite + TypeScript project at `/solar-system/webgpu/`.
- [x] Add Three.js with `WebGPURenderer` fallback to `WebGLRenderer` when WebGPU is unavailable.
- [x] Minimal scene: Sun point light, textured Earth, and OrbitControls.
- [x] Document build/serve commands in this file and `web/threejs/README.md`.
- [x] **No C++ changes** in this phase.

**Exit criteria:** Local dev server shows a rotating Earth with acceptable lighting; WebGPU path verified in Chrome.

### Phase 1 — Asset Pipeline (Session 2)

- [x] Extract Mercury–Mars positions, size scales, tilts, and rotation rates into shared JSON.
- [x] Add a DDS → KTX2 script supporting legacy DXT1/3/5 and 32-bit RGBA inputs.
- [x] Load KTX2 from local stubs or `VITE_KTX2_BASE` CDN/object-storage prefix.
- [x] Render Mercury, Venus, Earth, and Mars with labels and smooth focus presets.
- [x] Add OrbitControls plus damped WASD/Space/C flight approximating the C++ camera feel.
- [ ] Add distance-driven low→high texture replacement after production KTX2 assets are published.

**Exit criteria:** Inner planets render with low→high res swap on approach.

### Phase 2 — Solar System Subset (Session 3)

- [ ] Procedural or JSON-driven orbital parameters (reuse approximate distances from C++ scene).
- [ ] Sun + Mercury through Mars (or through Jupiter) with labels.
- [ ] Simple starfield skybox (reuse `resource/textures` sky DDS converted to cube map).
- [ ] Proxy markers for bodies not yet implemented (parity with staged loading UX).

**Exit criteria:** Fly-through from Sun to Mars feels coherent; no WebGPU validation errors.

### Phase 3 — Effects & Polish (Session 4+)

- [ ] Post-processing: bloom, tone mapping (match HDR feel loosely).
- [ ] Basic atmosphere shader (simplified; not full Mie/scattering port).
- [ ] Loading overlay + per-body fetch progress (reuse `updateLoadingProgress` pattern from `web/src/main.ts`).
- [ ] Optional: background music via same MP3 paths as WASM build.

**Exit criteria:** Demo is presentable as a “lite” sibling to the main app.

### Phase 4 — Hybrid Evaluation (Future)

Only after Phases 0–3 are stable:

- [ ] Evaluate WASM ↔ JS interop: C++ core for orbit/simulation, Three.js for presentation.
- [ ] Shared camera state via `Module.cwrap` or Embind.
- [ ] Decide: separate deploy vs. tab toggle in single `index.html`.

**Exit criteria:** Written decision in this doc (separate tracks vs. hybrid).

---

## Non-Goals (This Plan)

- Rewriting GLSL 300 es shaders to WGSL inside the C++ codebase.
- Replacing `WebResourceFetcher`, `PlanetSystemManifest`, or `nv_dds` with WebGPU-native paths.
- Feature parity with PCF shadows, cloud shadows, lens flare, or full atmospheric scattering in the companion build.
- Removing or deprecating the WebGL 2 WASM renderer.

---

## Technical Notes

### Why Three.js + WebGPURenderer

- Mature ecosystem; aligns with patterns used in other projects (Zephyr, Candy World, Chromashift, etc.).
- WebGPURenderer is optional — graceful fallback keeps Safari/Firefox usable.
- Faster iteration on post-processing and UI than recompiling Emscripten.

### Repository Layout (Proposed)

```
web/
├── src/              # existing WASM frontend
├── threejs/          # companion WebGPU project (new)
│   ├── package.json
│   ├── vite.config.ts
│   ├── src/main.ts
│   └── README.md
└── ...
```

Alternative: long-lived branch `feature/webgpu-companion` if folder pollution is a concern.

### Asset Reuse

| C++ / WASM | Companion |
|------------|-----------|
| `resource/textures_low/*.dds` | Converted thumbnails at build or fetch time |
| `resource/textures/*.dds` | KTX2 or high-res JPEG/PNG on CDN |
| `resource/models/*.obj` | glTF/OBJ via Three.js loaders |
| Shaders in `resource/shaders/` | Three.js `ShaderMaterial` / node materials (reimplement simply) |

### Deployment

- Same SFTP host as main demo; path e.g. `/solar-system/webgpu/index.html`.
- Ensure server MIME types for `.wasm` unchanged; companion may not need WASM initially.

---

## Dependencies & Prerequisites

- **P0 stability complete:** DDS mipmaps and async streaming reliable on WebGL 2 (see GitHub issues).
- Node 20+, npm; Chrome 113+ for WebGPU.
- Optional: `toktx` / `gltf-transform` for asset conversion.

---

## Risks

| Risk | Mitigation |
|------|------------|
| Two diverging codebases | Companion stays intentionally smaller; shared assets only |
| WebGPU browser support gaps | WebGLRenderer fallback |
| Large texture downloads | Same LOD philosophy as main app |
| Scope creep into full port | This doc + issue labels; phase gates |

---

## Related Issues & Docs

- GitHub: label `webgpu` — companion renderer epic
- `grok.md` — current WebGL 2 priorities (stabilize before expanding)
- `AGENTS.md` §9 — LOD and staged loading architecture
- `docs/plans/LOD_IMPLEMENTATION.md` — reference for distance thresholds (~50 units)

---

## Session Log

| Date | Session | Notes |
|------|---------|-------|
| 2026-06-22 | 0 | Scaffold `web/threejs/`: Vite+TS, WebGPURenderer+WebGL fallback, single textured Earth sphere (procedural canvas), OrbitControls, sun light. Local dev verified. No C++ changes. README + plan log updated. |
| 2026-07-14 | 1 | Shared inner-planet orbital JSON, DDS→KTX2 conversion and local stubs, CDN-aware KTX2Loader, Mercury–Mars scene, focus presets, and damped flight controls. No C++ changes. |

_Update this table at the end of each work session._
