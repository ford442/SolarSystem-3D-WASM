# Solar System Companion — Three.js + WebGPU

This is the lightweight companion renderer for rapid WebGPU/UI experiments. It remains separate
from, and does not replace, the C++/Emscripten WebGL 2 renderer.

## Phase status

- Phase 0: complete — Vite/TypeScript, `WebGPURenderer`, WebGL fallback, procedural Earth.
- Phase 1: complete — shared orbital JSON, DDS→KTX2 pipeline, Mercury through Mars,
  focus presets, OrbitControls, damped WASD flight, and distance-driven low→high texture LOD.
- **Phase 2: complete** — Sun through Jupiter (full meshes + LOD), Galilean moons (Io/Europa/Ganymede full; Callisto proxy), outer-body **proxy markers** (Saturn–Pluto), denser starfield + optional KTX2 skybox cube, focus presets for full + proxy bodies.
- Next (Phase 3): bloom/tone polish, simplified atmosphere, loading overlay polish.
- WebXR spike: on the **WebGLRenderer** fallback, Three's `VRButton` is attached (`renderer.xr.enabled`). The primary WASM app owns the full immersive-vr path — see `docs/plans/WEBXR_PLAN.md`.

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
- Focus presets: Overview, Sun, Mercury–Jupiter (full), and dashed **proxy** buttons for Saturn–Pluto.
- Click body labels in the scene to focus; proxy labels use a dashed style.

## Scene data

| File | Role |
|------|------|
| `src/data/orbital-parameters.json` | Positions / scales from `resource/planets.catalog.json` (codegen) |
| `src/data/companion-config.json` | Phase-2 **which bodies are full vs proxy**, moons, skybox face list |

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
npm run assets:transcode        # low tier (Mercury–Jupiter + Galilean moons)
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

See [the parent companion plan](../../docs/plans/WEBGPU_COMPANION_PLAN.md).
