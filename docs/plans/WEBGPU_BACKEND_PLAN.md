# WebGPU backend — plan and status

**Status:** phase 1 landed (device, RHI backend, GLSL→WGSL toolchain, backend selection).
The frame still renders through WebGL 2 / GL 4.6 everywhere; WebGL 2 stays the default.
**Supersedes** the "do not migrate" decision in [WEBGPU_COMPANION_PLAN.md](WEBGPU_COMPANION_PLAN.md)
for the C++ renderer: WebGPU becomes a second backend of the RHI (`gfx/rhi/`), not a port.
Architecture: [ARCHITECTURE §7.9](../ARCHITECTURE.md#79-the-webgpu-backend).

## Why

WebGL 2 is the ceiling for compute (the solar wind advects with transform feedback, terrain
normals are baked offline), storage buffers, indirect draws and timestamp queries most
browsers expose. WebGPU now ships in Chrome, Edge, Safari 26 and Firefox.

## Acceptance (from the issue) and where each stands

| Criterion | State |
|---|---|
| Sol renders on WebGPU with the same golden images (ΔE < 2) at Medium | **Not yet.** The harness exists and the first real pass meets it: `wgpu.WgpuParity.TheToneMapPassMatchesGlWithinDeltaE2` runs `passThrough.vs` + `hdr.fs` (tone map + FXAA) on GL and on WebGPU and gets **max ΔE 0.000** on lavapipe and SwiftShader. Every other pass is phases 2–4. |
| WebGL 2 binary size and behaviour unchanged | **Yes, to 0.05 %.** `SOLARSYSTEM_WEBGPU` is off by default and nothing WebGPU is compiled into the WebGL 2 binary (the WGSL dialect is host-only, the status hook is `#ifdef`ed). Measured against `main` (emsdk 6.0.0, `web-release`): `SolarSystem.js` identical (94 671 B), `SolarSystem.wasm` +1 548 B (3 068 255 → 3 069 803: the frame-stats JSON's `backend`, `backendName` and `webgpu: null`), `SolarSystem.data` +904 B (the `// vertex:` / `// webgpu:` comment lines in eight shaders). Native golden images are pixel-identical to `main`: 24/24 shots (Low/Medium/Full × 8 scenes). |
| Perf HUD shows backend name and per-pass timestamps | **Half.** The HUD's first line is the backend (and, in the WebGPU binary, the device and its WGSL count). Per-pass GPU times on WebGPU are implemented in `rhi::webgpu::CommandList` (timestamp-query pairs per render pass, billed to the pass kind's timer slot) and tested; they reach the HUD when the frame runs on that command list (phase 2). |

## Phase 1 — this change

- **RHI, GL-free and unit-tested** (`tests/test_rhi_webgpu_model.cpp`):
  `rhi::Backend` (+ names/ids), `PipelineTracker` (folds the optional `PipelineState` deltas
  exactly as GL would hold them) and `PipelineKey` (program, vertex layout, topology, formats,
  and GL's implicit rules made explicit: depth test off ⇒ no depth writes, polygon offset ⇒
  depth bias, y-flip ⇒ front face swapped), `RenderPassPlanner` (GL's bind/clear/draw order
  into WebGPU render passes and load ops: lazy open, clears become load ops, a clear after
  draws splits the pass, a clear nothing draws after still runs, `glDepthMask` gates depth
  clears, the viewport is context state), `TimestampPairs`, `VertexLayout`, `WgslBindings.h`.
- **`gfx/rhi/wgpu/`** (CMake `SOLARSYSTEM_WEBGPU`): `Device` (page-created `GPUDevice` on the
  web through emdawnwebgpu; Dawn natively, synchronous, TimedWaitAny), `Program` (WGSL modules,
  explicit bind group layouts, std140 uniform blocks packed at glslang's reflected offsets,
  `Set*` like `Shader::Set*`), `CommandList : rhi::CommandList` (render passes from the planner,
  pipeline cache by `PipelineKey`, groups 0/1/2, a per-frame uniform ring with dynamic offsets,
  timestamps, `ReadColor`, `Present` with the row flip), `SelfTestWgsl`, `BringUp`.
- **GLSL stays the source of truth.** `tools/wgsl_shader_compiler`: `ShaderSource`'s new
  `SpirvWebGpu` dialect → glslang (`-V -R`) → Tint (`--sampler-mapping`,
  `--allow-non-uniform-derivatives`) → `resource/shaders-wgsl/*.wgsl` + `manifest.json`, for
  every stage and every subset of its declared permutations. **164/164** stage permutations
  translate; **135/135** linked programs compile and link on a real device (Dawn on lavapipe
  and SwiftShader). The two solar-wind point-sprite stages are marked `// webgpu: replaced`.
- **Self-test WGSL leg.** `SOLARSYSTEM_SHADER_SELFTEST=1` in a `native-wgpu` build compiles
  every WGSL module and links every program on a WebGPU device after the GL legs.
- **Page.** `web/src/gpuContext.ts` (was `webglContext.ts`), `web/src/webgpuContext.ts`
  (adapter + device with timestamp-query and the texture-compression features when present),
  `wasmVariant.ts` choosing among `{webgl2, webgpu} × {single, threaded}` with `?gpu=webgl|webgpu`,
  falling back to WebGL 2 at every step (no `navigator.gpu`, no adapter, no device, binary not
  published). `auto` stays on WebGL 2 (`WEBGPU_AUTO_SELECT = false`) until parity.
- **The WebGPU binary today** (`SolarSystem-wgpu[-mt]`, `build-web.sh --webgpu`, preset
  `web-release-wgpu`): the same app, still drawing with WebGL 2, beside the page's WebGPU device,
  on which every WGSL module is compiled by the *browser's* WGSL compiler at start-up and counted
  in the perf HUD (`webgpu <device>: WGSL n/m`). That checks our WGSL against Chrome's Tint,
  Firefox's Naga and WebKit's compiler, which the native Dawn tests cannot. Verified in headless
  Chromium on SwiftShader WebGPU: the page's device is imported (timestamp-query and the three
  texture-compression features granted), **164/164 modules compile**, the scene renders, and a
  missing `-wgpu-mt` glue falls back to `-wgpu`. (Chrome logs "code is unreachable" warnings for
  two atmosphere stages: Tint's own SPIR-V→WGSL output, harmless.) Sizes: `SolarSystem-wgpu.wasm`
  3.10 MB (+28 KB over WebGL 2: emdawnwebgpu's bindings and the backend), `.js` 117 KB,
  `.data` 4.9 MB (the WGSL adds 2.8 MB uncompressed; it compresses well and only this binary
  loads it).
- **Build.** Presets `native-wgpu` (+ build, test and workflow presets), `web-release-wgpu`,
  `web-release-wgpu-mt`. `scripts/setup-webgpu-toolchain.sh` builds Dawn, Tint and glslang at
  the Dawn release emsdk's emdawnwebgpu port pins (same `webgpu.h` both sides);
  `scripts/build-wgsl-shaders.sh` is the web build's WGSL step.

### Design rules this phase fixes

- **Rendering is y-flipped on WebGPU.** The vertex epilogue negates `gl_Position.y` and maps z
  to [0, 1]. Every target then holds GL's row order, so GL's bottom-left viewport origin,
  `gl_FragCoord`, render-target sampling and `glReadPixels`-order readback are unchanged;
  `Present` flips once into the canvas. Front faces swap with it (`PipelineKey`).
- **Fixed binding numbers** (`gfx/rhi/WgslBindings.h`): group 0 = per-view UBOs at their GL
  binding points, group 1 = unit *u*'s texture at 2*u* and sampler at 2*u*+1, group 2 = the
  vertex stage's loose uniforms at 0 and the fragment stage's at 1 (separate std140 layouts).
- **Stages link by location.** Vertex outputs are numbered in declaration order (every
  `#ifdef` branch counted, so the numbers do not depend on the defines); a fragment stage takes
  its partner's numbers. A fragment stage behind a vertex stage of another name says so with
  `// vertex: x.vs` (hdr.fs, the bloom shaders, cloudsLighting.fs, atmospherePbr.fs,
  shadowMap.fs for terrain.vs).
- **Same state machine.** A pass ported to WebGPU keeps relying on what the previous pass left,
  exactly as on GL, because `PipelineTracker` replays the GL deltas.

## Phase 2 — passes draw through the RHI (both backends)

Move the draw-level calls (`SetProgram`, uniforms, `SetVertexBuffer`, `Draw`/`DrawIndexed`)
from `rhi::webgpu::CommandList` into `rhi::CommandList`, implement them on `GlCommandList`
(goldens must stay pixel-identical, as for every earlier RHI move), and port pass families off
direct GL in this order, each with a `WgpuParity` test at ΔE < 2:

1. Full-screen and HUD passes: `screen-fade`, `hdr-composite` (harness done), `star-glow`,
   `lens-flare`, `magnetic-bloom`/`aurora-bloom` composites, text (`TextRenderer`).
2. Resources behind backend-neutral handles: `Mesh` (CPU vertices are already kept),
   `TextureImage2D` (`TexturePayload` → `queue.writeTexture`, BC/ETC2/ASTC from the KTX2 packs with
   no change to `TextureDecode`), `ShadowMapFBO`, `HDR`, `MagneticFieldBloom` chains — the
   FrameGraph's imported/transient resources map onto `CommandList::CreateTexture/CreateFramebuffer`.
3. `skybox`, `orbit-paths`, `mission-paths`, `asteroids`, `scene:*` (planetLighting variants,
   shells, rings, eclipse), `shadow:*` (depth-only targets; depth textures bound as
   `unfilterable-float` or `depth` — bind group layouts then follow the bound texture's format).
4. Observe mode, terrain tiles, XR (WebXR's WebGPU binding when browsers ship it).

Then the frame runs on `rhi::webgpu::CommandList` in the WebGPU binary: no WebGL context, the
backend id in the stats becomes `webgpu`, and per-pass timestamps reach the HUD.

## Phase 3 — parity and default

- Native golden images on WebGPU: `SOLARSYSTEM_GOLDEN=…,backend=webgpu` reads back the
  backbuffer (`ReadColor`, already in GL order) and `scripts/golden-compare.py` gains `--delta-e`.
  Medium within ΔE < 2 of GL is the gate.
- Lost-device restore (WebGPU's `device.lost` → rebuild through the same `IGpuRestorable` walk).
- Flip `WEBGPU_AUTO_SELECT` once Medium passes and the web smoke test runs on Chromium's
  SwiftShader WebGPU; publish the binary (`VITE_WEBGPU_BUILD=1`).

## Phase 4 — compute-first features

- Solar wind advection as a compute pass (1 M+ particles on Full), drawn as instanced quads
  (replaces `solarWind.vs/.fs`' point sprites and `solarWindUpdate`'s transform feedback).
- GPU frustum/occlusion culling with `drawIndirect` for the asteroid belt and terrain tiles.
- On-GPU terrain normal/curvature generation from height tiles (no offline normal bake).

## How to run what exists

```bash
scripts/setup-webgpu-toolchain.sh          # Dawn + Tint + glslang into external/webgpu (once; long)
cmake --workflow --preset native-wgpu      # build + unit tests + WebGPU tests
# WebGPU tests need a Vulkan adapter and, for the parity tests, a GL 4.6 context:
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json MESA_GL_VERSION_OVERRIDE=4.6 \
  MESA_GLSL_VERSION_OVERRIDE=460 xvfb-run -a ./build-wgpu/tests/SolarSystemWgpuTests
SOLARSYSTEM_SHADER_SELFTEST=1 ./build-wgpu/SolarSystem   # GLSL legs, then the WGSL leg
SOLARSYSTEM_GPU=webgpu ./build-wgpu/SolarSystem          # the app, with the WebGPU bring-up
./build-web.sh --webgpu                    # adds SolarSystem-wgpu.{js,wasm,data} to web/public
VITE_WEBGPU_BUILD=1 npm run dev            # then open /solar-system/?gpu=webgpu&perf=1
```

## Known gaps

- WebGPU rasterises 1-pixel points only and has no `gl_PointCoord`: the solar-wind point
  sprites are replaced in phase 4, not translated.
- Transform feedback (`solarWindUpdate`) has no WebGPU equivalent: compute, phase 4.
- Depth textures sampled as `sampler2D` (the shadow map) need `unfilterable-float` layouts and
  non-filtering samplers: phase 2, step 3.
- Tint's CLI parses `--sampler-mapping` binding numbers with the group's digit count; the
  compiler writes the group as `01` to get two-digit bindings through (see main.cpp).
