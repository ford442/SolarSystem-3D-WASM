# LOD + Staged Loading Behavior Screenshots

These capture representative states during testing of the current architecture (staged manifests + proxies + high-res queue + streaming UI + downgrade). Screenshots were captured via headless Chrome runs against the built preview (local placeholders; real textures remote).

## Files
- `lod-staged-proxy-example.png` — Typical view with Sun + camera labels + possible early proxy markers visible in scene (or loading UI). Proxies appear as 3D text labels before planets initialize.
- `lod-streaming-teleport-example.png` — Camera pose after `setCameraPose(...)` teleport to approach/near a planet zone. Used to trigger `NOT_LOADED` → `DOWNLOADING` (proxy % label) → READY + subsequent LOD high-res when <50u.

## How to Reproduce / Observe (see TESTING_GUIDE.md for full steps)
1. `./build-web.sh && cd web && npm run preview`
2. Open `http://localhost:4173/solar-system/`
3. DevTools console:
   ```js
   window.setCameraPose(1850, 20, 30, 90, -10); // approach Earth proxy (~1900)
   ```
4. Observe:
   - 3D labels: "Earth (approach to load)" → "Earth [downloading 65%]"
   - Console: [StagedLoading] messages, then planet low-res appears
   - Close in further (<50u): "[LOD] ... Queueing high-res textures..." + streaming overlay "High-res upgrade: X/Y (Z%)"
5. Fly far: downgrade log + low-res reload.
6. `?quality=0` or `Module.SetQualityPreset(0)` to test force-low path.

## Notes
- With committed 4×4 placeholders, visual surface change on "high-res" is minimal, but all code paths (queue, reload, downgrade, proxies %, streaming bar, MAX_LEVEL safety) are exercised and logged.
- On deployment with real DDS: clear visual upgrade on LOD, full textures on staged init.
- No black textures; fallbacks + explicit BASE/MAX_LEVEL everywhere.

Cross-ref: TESTING_GUIDE.md, VERIFICATION_CHECKLIST.md (Staged+Proxy section), grok.md, AGENTS.md §9.4–9.5.

Captured: 2026-06-22.