# Solar System Companion — Three.js + WebGPU

This is the lightweight companion renderer for rapid WebGPU/UI experiments. It remains separate
from, and does not replace, the C++/Emscripten WebGL 2 renderer.

## Phase status

- Phase 0: complete — Vite/TypeScript, `WebGPURenderer`, WebGL fallback, procedural Earth.
- Phase 1: complete — shared orbital JSON, DDS→KTX2 pipeline, Mercury through Mars,
  focus presets, OrbitControls, damped WASD flight, and distance-driven low→high texture LOD.
- Phase 2: complete — Sun through Jupiter (full meshes + LOD), Galilean moons, outer-body proxy markers, denser starfield + optional KTX2 skybox cube.
- **Phase 3: complete** — bloom + ACES tone mapping, Fresnel atmosphere shells, ring systems for Saturn and Uranus, a loading overlay with per-asset progress, optional background music, and Saturn/Uranus/Neptune/Pluto promoted from proxies to full bodies (Moon, Titan and Triton added; **no proxies remain**).
- Next (Phase 4): hybrid evaluation — only after the typed WASM bridge lands. See the plan.
- WebXR spike: on the **WebGLRenderer** fallback, Three's `VRButton` is attached (`renderer.xr.enabled`). The primary WASM app owns the full immersive-vr path — see `docs/plans/WEBXR_PLAN.md`.

This stays a **lite sibling** to the C++/WASM WebGL 2 renderer, not a replacement:
no PCF/ray-traced shadows, no cloud-shadow pass, no lens flare, no Mie/Rayleigh
scattering, and a much smaller asset set.

## Run

```bash
cd web/threejs
npm install
npm run dev
```

Open <http://localhost:5173/solar-system/webgpu/>. The development command copies the Basis
transcoder required by `KTX2Loader`; committed KTX2 stubs work without a CDN.

Production check:

```bash
npm run build
npm run preview
```

Root CI (`web-build.yml` job `build-threejs-companion`) runs the same build.

## Controls

- Drag to orbit and right-drag to pan.
- Wheel to zoom toward the pointer.
- Use WASD plus Space/C for damped flight resembling the C++ camera acceleration.
- Focus presets: Overview, Sun, and Mercury–Pluto. Presets approach from the
  sunlit side so the framed body is lit rather than a black disc.
- Click body labels in the scene to focus.
- `♪ Music off` toggles background music (see below).

Append `?renderer=webgl` to the URL to force the plain `WebGLRenderer` rescue
path — the only way to exercise the `EffectComposer` post stack and the
`VRButton` spike, since `WebGPURenderer` otherwise falls back to its own WebGL
backend.

## Scene data

| File | Role |
|------|------|
| `src/data/orbital-parameters.json` | Positions / scales from `resource/planets.catalog.json` (codegen) |
| `src/data/companion-config.json` | Which bodies are full vs proxy, moons, skybox faces, bloom/tone-map settings, atmospheres, ring systems, music tracks |

Runtime modules:

| File | Role |
|------|------|
| `src/postFx.ts` | Bloom + ACES; TSL `RenderPipeline` on `WebGPURenderer`, `EffectComposer` on the plain WebGL rescue path |
| `src/atmosphere.ts` | Inverted-hull Fresnel shells (node material / GLSL twin) |
| `src/rings.ts` | Radius-mapped ring geometry with a procedural strip and optional KTX2 override |
| `src/loadingOverlay.ts` | Overlay + per-asset progress, mirroring the WASM `updateLoadingProgress` pattern |
| `src/audio.ts` | Optional background music from the WASM MP3 paths |

Regenerate orbital JSON from the repo root catalog:

```bash
node scripts/generate-planet-metadata.mjs   # from repo root
# or: cd web && npm run generate:planet-metadata
```

## KTX2 assets

Low-tier textures always load from the bundled Vite path:

```text
/solar-system/webgpu/textures/ktx2/<name>.ktx2
```

High-tier textures load when the camera is within 50 scene units (downgrade beyond 100 units).
By default the companion looks for local high stubs at:

```text
/solar-system/webgpu/textures/ktx2/high/<name>.ktx2
```

Skybox faces (optional):

```text
/solar-system/webgpu/textures/ktx2/skybox/{PositiveX,NegativeX,...}.ktx2
```

Point high-tier fetches at a CORS-enabled CDN or object-storage prefix with:

```bash
VITE_KTX2_BASE=https://cdn.example.com/solar-system/ktx2/ npm run dev
```

Generate or refresh the local stubs:

```bash
npm run assets:transcode        # low tier (Sun→Pluto, moons, both ring strips)
npm run assets:transcode:high   # high tier (or 128×128 dev stubs if DDS missing)
npm run assets:transcode:skybox # Main_SkyBox DDS → ktx2/skybox/
npm run assets:transcode:all    # all of the above
```

Convert arbitrary legacy DDS files (DXT1, DXT3, DXT5, or 32-bit RGBA) with:

```bash
node scripts/transcode-dds-to-ktx2.mjs \
  ../../../resource/textures/Mercury_Diffuse.dds \
  --tier high \
  --output public/textures/ktx2/high
```

The script preserves existing BC compression blocks when possible and writes standards-based KTX2.
For Basis/UASTC production conversion or DX10 DDS inputs, use Khronos `toktx`; the runtime loader is
already configured with the Basis transcoder copied by `npm run assets:prepare`.

## Background music (optional)

Tracks are fetched from the same paths as the WASM build — `resource/sounds/*.mp3`
one level above the companion base (`/solar-system/resource/sounds/`), or under
`VITE_ASSET_BASE` when set. Those MP3s are deploy artifacts (Git LFS, excluded
from the web preload), so a 404 is expected locally: the toggle then reads
`♪ Music unavailable` and the scene keeps running. Playback always starts from a
click so browser autoplay policies are satisfied.

## Known gaps

- Every committed `*.ktx2` here is transcoded from a 4×4 placeholder DDS, so
  planets, rings and the skybox render as flat colour until real assets are
  published to `VITE_KTX2_BASE`. The ring meshes fall back to a procedural
  banded strip for that reason.
- High-tier LOD keeps the absolute C++-parity thresholds (50 u upgrade), which
  are shorter than the distance a focus preset frames a planet from, so a
  high-res swap currently needs manual WASD flight.

See [the parent companion plan](../../docs/plans/WEBGPU_COMPANION_PLAN.md).
