# Solar System Companion — Three.js + WebGPU

This is the lightweight companion renderer for rapid WebGPU/UI experiments. It remains separate
from, and does not replace, the C++/Emscripten WebGL 2 renderer.

## Phase status

- Phase 0: complete — Vite/TypeScript, `WebGPURenderer`, WebGL fallback, procedural Earth.
- Phase 1: complete — shared orbital JSON, DDS→KTX2 pipeline, Mercury through Mars,
  focus presets, OrbitControls, and damped WASD flight.
- Next: low→high texture streaming, labels/proxies for outer planets, and simplified effects.

## Run

```bash
cd web/threejs
npm install
npm run dev
```

Open <http://localhost:5173/solar-system/webgpu/>. The development command copies the Basis
transcoder required by `KTX2Loader`; four committed 4×4 KTX2 stubs work without a CDN.

Production check:

```bash
npm run build
npm run preview
```

## Controls

- Drag to orbit and right-drag to pan.
- Wheel to zoom toward the pointer.
- Use WASD plus Space/C for damped flight resembling the C++ camera acceleration.
- Choose Overview, Mercury, Venus, Earth, or Mars for a smooth focus transition.

## Shared orbital data

`src/data/orbital-parameters.json` contains positions, radius scales, axial tilts, and rotation rates
extracted from the four C++ planet classes. Companion-only visual scaling is applied in `main.ts`;
the JSON values remain in C++ world units so a future shared generator can consume them directly.

## KTX2 assets

By default textures load from the local Vite path:

```text
/solar-system/webgpu/textures/ktx2/<name>.ktx2
```

Point the same build at a CORS-enabled CDN or object-storage prefix with:

```bash
VITE_KTX2_BASE=https://cdn.example.com/solar-system/ktx2/ npm run dev
```

Generate or refresh the local stubs:

```bash
npm run assets:transcode
```

Convert arbitrary legacy DDS files (DXT1, DXT3, DXT5, or 32-bit RGBA) with:

```bash
node scripts/transcode-dds-to-ktx2.mjs \
  ../../../resource/textures/Mercury_Diffuse.dds \
  --output public/textures/ktx2
```

The script preserves existing BC compression blocks when possible and writes standards-based KTX2.
For Basis/UASTC production conversion or DX10 DDS inputs, use Khronos `toktx`; the runtime loader is
already configured with the Basis transcoder copied by `npm run assets:prepare`.

See [the parent companion plan](../../docs/plans/WEBGPU_COMPANION_PLAN.md).
