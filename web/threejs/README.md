# Solar System Companion — Three.js + WebGPU (Phase 0+)

Parallel lightweight demo for rapid iteration. **Does not replace** the C++/Emscripten WebGL 2 production renderer.

## Phase 0 (current)
- Vite + TS scaffold
- `WebGPURenderer` with WebGL fallback
- Single textured Earth sphere + OrbitControls
- Sun point light + basic lighting
- ~2s smooth camera focus placeholders (via code)

## Run
```bash
cd web/threejs
npm install   # only first time
npm run dev
```

Open http://localhost:5173/solar-system/webgpu/  (base configured)

For production preview:
```bash
npm run build && npm run preview
```

## WebGPU verification
- Chrome/Edge with `chrome://flags/#enable-unsafe-webgpu` (or recent stable)
- Falls back gracefully.

## Next phases
See parent plan: `../docs/plans/WEBGPU_COMPANION_PLAN.md`

## Notes
- Textures currently procedural for zero-asset Phase 0.
- Matches camera "feel" loosely; later can tune to match C++ `Camera` accel/zoom.
- No C++ / WASM changes.
