# LOD Texture Loading - Testing Guide

## Prerequisites

### 1. Create Low-Resolution Textures

Before testing, you need to create low-resolution versions of Earth's textures. Here are several methods:

#### Method A: Using ImageMagick (Recommended)
```bash
# Install ImageMagick if not already installed
# Ubuntu/Debian: sudo apt-get install imagemagick
# macOS: brew install imagemagick

# Convert high-res DDS to PNG, resize, and convert back
# (Adjust percentage based on desired quality vs file size)

# For Earth Day Diffuse (example 50% scale)
convert resource/textures/Earth_Day_Diffuse.dds -resize 50% resource/textures_low/Earth_Day_Diffuse_Low.png
convert resource/textures_low/Earth_Day_Diffuse_Low.png resource/textures_low/Earth_Day_Diffuse_Low.dds

# For Earth Normal Map
convert resource/textures/Earth_Normal.dds -resize 50% resource/textures_low/Earth_Normal_Low.png
convert resource/textures_low/Earth_Normal_Low.png resource/textures_low/Earth_Normal_Low.dds

# For Earth Specular Map
convert resource/textures/Earth_Specular.dds -resize 50% resource/textures_low/Earth_Specular_Low.png
convert resource/textures_low/Earth_Specular_Low.png resource/textures_low/Earth_Specular_Low.dds
```

#### Method B: Using NVIDIA Texture Tools
```bash
# Use nvcompress or similar tools from NVIDIA Texture Tools
nvcompress -bc1 input_low_res.png Earth_Day_Diffuse_Low.dds
```

#### Method C: Using GIMP
1. Open the high-res texture in GIMP
2. Image → Scale Image → Set to 50% (or desired size)
3. Export as DDS with appropriate compression settings

### 2. Verify Texture Files

Ensure these files exist:
```bash
ls -lh resource/textures_low/
# Should show:
# Earth_Day_Diffuse_Low.dds
# Earth_Normal_Low.dds
# Earth_Specular_Low.dds

ls -lh resource/textures/
# Should show:
# Earth_Day_Diffuse.dds
# Earth_Normal.dds
# Earth_Specular.dds
# (Plus other texture files)
```

## Building for WebAssembly

### 1. Setup Emscripten (if not already done)
```bash
# Clone emsdk
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk

# Install latest
./emsdk install latest
./emsdk activate latest

# Source environment
source ./emsdk_env.sh
cd ..
```

### 2. Build the project
```bash
# Make build script executable
chmod +x build-web.sh

# Run the build
./build-web.sh
```

Expected output should include:
```
Checking and setting up dependencies...
Running CMake configuration...
Building project...
Deploying artifacts to web frontend...
Copying resources to web/public...
Created textures_low directory for LOD system
Copied SolarSystem.js to web/src/
Copied SolarSystem.wasm to web/public/
Copied SolarSystem.data to web/public/
Build & Deployment Complete!
```

### 3. Verify build artifacts
```bash
ls -lh web/public/
# Should contain:
# SolarSystem.wasm
# SolarSystem.data
# resource/ (directory with textures and textures_low subdirectory)

ls -lh web/public/resource/textures_low/
# Should contain the low-res DDS files
```

## Testing the LOD System

### 1. Start a local web server
```bash
cd build-web
python3 -m http.server 8000
```

Or use Node.js:
```bash
cd build-web
npx http-server -p 8000
```

### 2. Open in browser
Navigate to: `http://localhost:8000/SolarSystem.html`

**Note:** Do NOT open the file directly with `file://` protocol, as this will fail due to CORS restrictions.

### 3. Open browser console
- Chrome: F12 or Ctrl+Shift+I (Windows/Linux) or Cmd+Option+I (Mac)
- Firefox: F12 or Ctrl+Shift+K (Windows/Linux) or Cmd+Option+K (Mac)
- Safari: Cmd+Option+C (Mac, may need to enable Develop menu first)

### 4. Test scenario: Initial load
**Expected behavior:**
1. Application loads with low-res Earth textures
2. Console should show:
```
[WebResourceFetcher] Fetching: resource/textures_low/Earth_Day_Diffuse_Low.dds ...
[WebResourceFetcher] Downloaded and written to MEMFS: resource/textures_low/Earth_Day_Diffuse_Low.dds
resource/textures_low/Earth_Day_Diffuse_Low.dds Loaded
[WebResourceFetcher] Fetching: resource/textures_low/Earth_Normal_Low.dds ...
...
```

3. Visual check: Earth should be visible but with lower texture quality

### 5. Test scenario: Zoom into Earth
**Steps:**
1. Use mouse scroll or W/S keys to move camera toward Earth
2. Navigate until you're within 50 units of Earth's center
   - The distance is measured from camera position to planet center
   - Earth's position is approximately (1900, 0, 0) from the sun
   - Starting camera is at (-134, 0, 0)

**Expected behavior:**
1. When distance < 50 units, console should show:
```
[LOD] Camera distance to Earth: 45.2 units. Loading high-res textures...
[WebResourceFetcher] Fetching: resource/textures/Earth_Day_Diffuse.dds ...
[WebResourceFetcher] Downloaded and written to MEMFS: resource/textures/Earth_Day_Diffuse.dds
resource/textures/Earth_Day_Diffuse.dds Loaded
[WebResourceFetcher] Fetching: resource/textures/Earth_Normal.dds ...
[WebResourceFetcher] Downloaded and written to MEMFS: resource/textures/Earth_Normal.dds
resource/textures/Earth_Normal.dds Loaded
[WebResourceFetcher] Fetching: resource/textures/Earth_Specular.dds ...
[WebResourceFetcher] Downloaded and written to MEMFS: resource/textures/Earth_Specular.dds
resource/textures/Earth_Specular.dds Loaded
[LOD] Earth high-res textures loaded successfully (Day Diffuse, Normal, Specular)
```

2. Visual check: Earth's texture quality should noticeably improve
3. No further console messages should appear (textures loaded only once)

### 6. Test scenario: Network monitoring
**Steps:**
1. Open browser DevTools Network tab
2. Reload the page
3. Observe texture file downloads

**Expected behavior:**
1. Initial load: Only low-res textures downloaded
   - `Earth_Day_Diffuse_Low.dds`
   - `Earth_Normal_Low.dds`
   - `Earth_Specular_Low.dds`

2. After zooming in: High-res textures downloaded
   - `Earth_Day_Diffuse.dds`
   - `Earth_Normal.dds`
   - `Earth_Specular.dds`

3. File sizes should be significantly different (high-res should be larger)

### 7. Test scenario: Error handling
**Steps to test error handling:**
1. Temporarily rename one high-res texture file:
```bash
mv web/public/resource/textures/Earth_Normal.dds web/public/resource/textures/Earth_Normal.dds.backup
```

2. Reload page and zoom into Earth

**Expected behavior:**
Console should show error message:
```
[LOD] Camera distance to Earth: 45.2 units. Loading high-res textures...
[WebResourceFetcher] FATAL: Failed to download resource/textures/Earth_Normal.dds
[LOD] ERROR: Failed to load high-res textures: Image resource/textures/Earth_Normal.dds cannot be loaded
```

3. Earth should continue to render with low-res textures (graceful degradation)

4. Restore the file:
```bash
mv web/public/resource/textures/Earth_Normal.dds.backup web/public/resource/textures/Earth_Normal.dds
```

## Performance Testing

### 1. Monitor frame rate
- Initial load with low-res: Note FPS
- After zoom-in: Note any frame drops during texture loading
- After textures loaded: FPS should return to normal

### 2. Memory usage
- Open browser Task Manager (Chrome: Shift+Esc)
- Note memory usage before and after texture upgrade
- Old textures should be freed (memory shouldn't double)

### 3. Load time comparison

**With LOD (this implementation):**
- Initial load: ~500KB (example, depends on low-res size)
- Total on zoom: +10MB (example, high-res textures)

**Without LOD (previous implementation):**
- Initial load: ~10MB (all high-res)

## Testing Desktop Build

### 1. Build for desktop
```bash
chmod +x build.sh
./build.sh
```

### 2. Run the application
```bash
cd build
./SolarSystem
```

### 3. Expected behavior
- Should load high-res textures directly at startup
- No LOD messages in console (system is disabled on desktop)
- No behavior change from previous version

## Troubleshooting

### Issue: "Cannot read resource/textures_low/*.dds"
**Solution:** Ensure low-res textures are created and copied to `web/public/resource/textures_low/`

### Issue: "CORS error" when loading
**Solution:** Use a local web server (python, node, etc.), not `file://` protocol

### Issue: High-res textures never load
**Solution:**
1. Check console for distance readings: `[LOD] Camera distance to Earth: X units`
2. Ensure distance is < 50 units
3. Verify high-res files exist in `web/public/resource/textures/`

### Issue: Console shows "Failed to load high-res textures"
**Solution:**
1. Check network tab for 404 errors
2. Verify file paths and names match exactly
3. Ensure files are valid DDS format

### Issue: Visual artifacts after texture upgrade
**Solution:**
1. Verify DDS compression format matches between low and high-res
2. Check that mipmaps are generated correctly
3. Ensure texture dimensions are powers of 2

## Success Criteria

✅ **Test passes if:**
1. Initial load is fast (only core + low-res for first planets)
2. Low-res textures (or 4×4 placeholders) display
3. Zooming close triggers high-res queue + streaming UI (console + overlay)
4. High-res textures (or fallback) load without black/incomplete artifacts
5. Downgrade on distance > ~100u works (VRAM note in logs)
6. No repeated downloads (dedup + _isHighResLoaded guard)
7. Desktop build unchanged (loads high-res directly, no LOD messages)
8. Error handling works (graceful on missing high/optional; required fails still init with fallbacks)
9. Proxy markers + % labels appear for unloaded planets; disappear on READY

## Current Architecture End-to-End (Staged + LOD + Async Streaming)

The original Earth-focused guide is still useful for manual low-res creation when real assets are available. The architecture has since been extended to **all planets**, staged loading for the whole system, a robust queue, hysteresis downgrade, quality presets, streaming progress, and proxy markers.

### 1. One-Time Setup (Placeholders are shipped)
Repo now includes `resource/textures_low/*_Low.dds` (4×4 grey checkers for planets + moons/rings/clouds). No manual `convert` needed for basic LOD/staged verification.
- Dev, preview, and production use same-origin assets by default, including the bundled low-res placeholders.
- Set `VITE_ASSET_BASE` when a test specifically needs a remote high-res asset host.
- All required low-res are referenced in manifests + GetTexturePath.

### 2. Build & Serve for Testing
```bash
./build-web.sh
cd web
npm run dev       # same-origin placeholders at http://localhost:5173/solar-system/
npm run preview   # production bundle at http://localhost:4173/solar-system/

# Optional: exercise remote high-res/staged asset delivery.
VITE_ASSET_BASE=https://test.1ink.us/solar-system/ npm run dev
```
Use Chrome. Open DevTools → Console + Network.

With the default `npm run dev`, staged planet loads should request
`/solar-system/resource/textures_low/*_Low.dds` from the local Vite server and display the
grey-checker placeholders. The optional override must point at a base URL containing the
`resource/` directory and should end with `/`.

### 3. Initial State (no planets yet)
- Loading bar → 100% → hides.
- Sun (corona + lens flare + HDR), 3D distance labels (when bodies present), full camera (WASD + mouse + scroll) work immediately.
- Console: core resource messages.
- Planets: **3D proxy markers** only (rendered via TextRenderer in 3D mode at proxy orbital positions).

### 4. Trigger Staged Loading (Proxy → Download → READY)
Teleport or fly near a planet's proxy (Sun = 0,0,0; Earth ~1900 on +X):
```js
// In DevTools console
window.setCameraPose(1850, 10, 50, 90, -5);   // near Earth proxy; yaw/pitch in degrees
```
Expected:
- Console: `[StagedLoading] Camera within ... — starting download for Earth`
- 3D label updates: `Earth [downloading 37%]`
- Network: low-res planet textures + moons (Earth: Earth_*_Low + Moon_* + clouds etc.)
- When all required done: `[StagedLoading] Required assets ready for Earth — initializing system.`
- Planet (low-res) + moons + clouds/rings appear; proxy label vanishes.
- Other planets remain as proxies.

Repeat for outer (larger radius) or inner. Multiple can download in parallel.

### 5. Three-tier LOD Streaming (mid + high, Queue + UI)

Activate Earth, then fly or teleport near its center. Paths use `TextureLODController` (planets, moons, rings, clouds).

```js
window.setCameraPose(1900, 0, 0, 180, 0);  // on top of Earth proxy origin
```

**Medium preset** (`?quality=medium` or `setQualityPreset(1)`):
- Effective upgrade radius ~33 units (T = 50 / 1.5).
- Inside radius: Network tab shows `resource/textures_mid/*_Mid.dds` only — **no** `resource/textures/Earth_*.dds` full maps.
- Console: `[LOD][Earth_Day_Diffuse] Queueing mid texture` then `Resident tier now mid`.
- Overlay: `Mid-res upgrade: …` (`updateStreamingProgress(..., tierCode=1)`).

**Full preset** (`?quality=full`):
- Inside ~50 units: mid loads first.
- Inside ~25 units (0.5×T): high maps queue (`textures/Earth_*.dds`); overlay may switch to `High-res upgrade`.
- Console sequences mid then high per map.

Test queue cancel:
- Start mid/high load → immediately `setCameraPose` far away → cancel logs; cancelled apply does not leave a stale higher tier.

### 6. Downgrade + Hysteresis + Quality Presets
- From high (full): fly beyond ~T → `[LOD][…] Downgraded high → mid`.
- Beyond ~2×T → `Downgraded mid → low`.
- `setQualityPreset(0)` while mid/high resident → force low and cancel in-flight.

| Preset | Max texture tier | Effective mid upgrade radius | Shadow map | Concurrency | WebGL MSAA |
|--------|------------------|------------------------------|------------|-------------|------------|
| `low` / `0` | low only | none | 1024² | 0 | off |
| `medium` / `1` | mid only | ~33 units (T/1.5) | 2048² | 2 | off |
| `full` / `2` | high (via mid) | mid at 50u; high at ~25u | 3000² | 4 | 4× if supported |

Use a URL preset when comparing MSAA because WebGL antialiasing is fixed at context creation:

```text
http://localhost:4173/solar-system/?quality=medium
http://localhost:4173/solar-system/?quality=full
```

`window.setQualityPreset(0|1|2)` changes LOD, shadow resolution, and queue concurrency at runtime. Reload with the corresponding URL to change MSAA.

**WebXR:** Entering VR forces the **medium** preset (or keeps **low**). Because MSAA is fixed at context creation, launch with `?quality=medium` (or low) before clicking **Enter VR** for the best headset framerate. See [WEBXR_PLAN.md](WEBXR_PLAN.md).

#### Measurable medium-vs-full check

1. Open each URL in a fresh tab and confirm the `[Quality]` console line:
   - medium: `shadow=2048x2048`, `high-res concurrency=2`, `max texture LOD=mid`, `LOD distance multiplier=1.5`, `MSAA=0x`
   - full: `shadow=3000x3000`, `high-res concurrency=4`, `max texture LOD=high`, `LOD distance multiplier=1.0`, `MSAA=4x`
2. Load Earth, then position the camera about 40 units from its center:

   ```js
   window.setCameraPose(1940, 0, 0, 180, 0)
   ```

3. Full (T=50): 40 < 50 → should queue **mid** (not yet high; high needs <25). Medium (T≈33): 40 > 33 → stay low.
4. Move to ~20 units in medium: mid only (Network has no full `textures/Earth_Day_Diffuse.dds`). Same pose in full: mid then high as you close further.
5. Queue concurrency must never exceed `active 2/2` for medium or `active 4/4` for full.
6. Memory: visit several systems on full; WASM heap stays under 1 GB; optional backpressure log at ~768 MiB is OK.

Unit tests (native): `SolarSystemTests` includes `test_texture_lod_controller.cpp` (mid-only medium, mid→high full, hysteresis, cancel, quality cap).

### 6b. Magnetic Field Mode

Settings → **Magnetic fields** (default off; persisted like orbit lines). Deep link: `?fields=1`.

Console: `[MagneticField] Built field-line ribbons for quality preset N`.

| Preset | Bodies with ribbons | Bloom |
|--------|---------------------|-------|
| low | Sun only (toroidal + fast flow) | off |
| medium | Sun, Earth, Jupiter, Uranus | half-res, 1 blur pass |
| full | + Mercury, Saturn, Neptune | half-res, 2 blur passes (1 on mobile) |

Venus/Mars/Pluto/moons stay omitted.

Manual:
1. `?quality=medium&fields=1` — approach Earth: cyan tilted dipole with a soft glow; planet disk is dimmed; orbit lines are faint. Approach Sun: orange equatorial torus, faster pulse, visible twist; Sun itself stays bright.
2. Teleport to Jupiter / Uranus and confirm tilt (Uranus extreme teal). Close-up: planet disk occludes ribbons; glow should not punch through the near face.
3. `?quality=low&fields=1` — Sun ribbons only, no bloom halo.
4. Settings checkbox persists across reload (`localStorage` key `solar-system.settings.v1` → `magneticFields`).
5. Press **M** — checkbox and overlay update together (`onSettingsChanged('magneticFields')` / `'magneticFieldMode'`). `Module.GetMagneticFieldMode()` matches `GetMagneticFields()`.
6. Toggle off — ribbons disappear, planet brightness and orbit-line alpha restore; orbit-lines / shadows / quality controls still work.
7. Native unit tests: `test_magnetic_field.cpp` (dipole polarity, torus \(B_\phi\), monotonic arc length, catalog gating) and `test_quality_settings.cpp` (bloom flags per preset).

### 7. Streaming UI + Throttled Progress
- Streaming phase uses overlay (`#streaming-progress` / `#streaming-text`) + bar.
- C++ calls `updateStreamingProgress(completed, total, active, tierCode)` (`tierCode` 1=mid, 2=high).
- Auto-hides ~2.5s after done. Also updates main loading hook during streaming for compatibility.
- In 3D: while planet DOWNLOADING, labels show live % from `totalDownloads - pendingDownloads`.

### 8. Multi-Planet + Moons/Rings/Clouds
- Jupiter etc. bring many optionals (Io/Europa... + rings). Required block init; optionals log "[StagedLoading] Optional asset unavailable..." but do not block (use fallbacks).
- All planets implement LoadHighResIfClose + Unload (delegates to Load).
- Test by visiting several systems; watch console for independent high-res queues.

### 9. Error / Resilience Cases
- Temporarily rename a high-res DDS → on zoom: queue attempts, some fail, final message "attempt finished (some or all failed, keeping low-res permanently)". Planet keeps low.
- Rename required low-res for a not-yet-loaded planet → staged will fail init for that system (others unaffected).
- Optional missing: tolerated.
- Rapid camera changes: no duplicate jobs, cancels work, backoff prevents spam.
- MAX_LEVEL safety: no black on load/reload/GenerateMipmap paths (watch for absence of "incomplete texture" GL complaints).

### 10. Automated Headless Texture Verification

The Puppeteer smoke test runs production output in headless Chrome using ANGLE's SwiftShader
fallback. Build and start the Vite preview server in one terminal:

```bash
./build-web.sh --no-emsdk
cd web
npm run build
npm run preview
```

Then run the verifier in another terminal:

```bash
cd web
npm run test:textures
```

The default target is `http://localhost:4173/solar-system/`. Override it with
`VERIFY_TEXTURES_URL` when necessary. The test waits for and requires successful `Loaded`
messages for all six `resource/textures_low/Main_SkyBox/*.dds` faces, which catches face-name,
directory, and cubemap-path regressions. It exits non-zero for unhandled page errors and console
diagnostics indicating any of the following:

- incomplete or black textures;
- a texture that WebGL reports as not renderable;
- an incomplete GPU upload;
- `GL_TEXTURE_MAX_LEVEL=0` with a mip-filtered texture.

Missing optional high-resolution assets may still use the documented fallback path and do not by
themselves fail this smoke test. `build-web.sh` mirrors the six committed skybox placeholders into
the high-res runtime fetch path, preventing Vite's HTML SPA fallback from masquerading as a DDS
response during local/CI runs. All six faces must always load cleanly.

### Moon, ring, and cloud LOD smoke test

The shared LOD controller covers Moon, Io, Europa, Ganymede, Titan, Saturn/Uranus rings, and the
Earth/Neptune/Uranus cloud shells. Use a deployment with the corresponding optional high-resolution
DDS files from `resource/asset-manifest.json`, activate the planet system, and teleport within 50
world units of the object. For Titan, the console must show:

```text
[LOD][Titan] Queueing high-res diffuse texture
[TextureLoadingQueue][satellite] Loading texture: Titan_Diffuse_High
```

The streaming overlay total must advance for that satellite request. Retreat beyond 100 world
units and verify `[LOD][Titan] Downgraded diffuse texture`; repeated approach/retreat cycles should
not grow the live WebGL texture count because `ReloadTexture` deletes the texture it replaces.
Retreating while a request is active should instead log cancellation at 90 world units (the 1.8x
hysteresis boundary), and the progress overlay must still settle.

**CI gates for pull requests to `main`:**

- `.github/workflows/web-build.yml` — fast compile gate. Installs Emscripten 6.0.0, runs
  `./build-web.sh --no-emsdk`, then `npx tsc && npx vite build` in `web/`. Fails on WASM link
  errors, broken `--preload-file` paths, `EXPORTED_FUNCTIONS` mismatches, or TypeScript/Vite
  issues. Uploads `web/dist` as a build artifact. A parallel job builds `web/threejs`.
  The `unit-tests` job runs GoogleTest via `ctest`. The `build-web` job also serves the Vite
  production bundle and runs `npm run test:smoke` (Playwright) to verify WASM boot, exported JS
  helpers, and staged-loading reactions to `window.setCameraPose`.
- `.github/workflows/native-linux-build.yml` — native binary smoke test plus the same GoogleTest
  suite (`ctest` after `-DSOLARSYSTEM_BUILD_TESTS=ON`).
- `.github/workflows/texture-verification.yml` — runtime texture regression gate. Also builds
  WASM and the Vite bundle, then starts the preview server on port 4173 and invokes
  `npm run test:textures` via Puppeteer/Chrome. Chrome is launched headlessly with
  `--use-gl=angle`, `--use-angle=swiftshader`, and `--enable-unsafe-swiftshader`, so the WebGL 2
  check does not require a hardware GPU on the runner.

### Automated tests (local)

**Native C++ unit tests** (GoogleTest, behind `SOLARSYSTEM_BUILD_TESTS`):

```bash
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DSOLARSYSTEM_BUILD_TESTS=ON -B build-test
cmake --build build-test --target SolarSystemTests
ctest --test-dir build-test --output-on-failure
```

Coverage today: `OrbitLayout`, `QualitySettings`, `PlanetManifestLoader`, and
`TextureLoadingQueue` enqueue/dedup/cancel logic.

**Web smoke test** (Playwright, production bundle):

```bash
./build-web.sh --no-emsdk
cd web && npm ci && npx tsc && npx vite build
npx playwright install chromium
npm run preview -- --host 127.0.0.1 &
SMOKE_TEST_URL=http://127.0.0.1:4173/solar-system/ npm run test:smoke
```

### 11. Verification Commands / JS Helpers
- Teleport list (approximate centers):
  - Mercury ~500, Venus ~900, Earth 1900, Mars ~2600, Jupiter ~5000-ish, etc. (exact from planet ctors or labels).
- Watch: `window.__solarSystemAssetBase` (base for fetches).
- Force streaming hide: manually set display none on `#streaming-progress`.
- Low preset URL test + close/far cycles.
- Check `_renderableSceneComponents` length grows only as planets activate (via any debug exposure).

### 12. Performance / Memory
- Task manager: watch GPU/JS heap before/after high-res of a heavy world (Saturn/Jupiter). Downgrade should release.
- Initial payload small; per-planet incremental.
- FPS may throttle to 0 in background tab (rAF); confirm by labels moving on input.

## Additional Notes

### Adjusting LOD Threshold or Activation Radius
- LOD: per-planet e.g. `src/Solar_System/Earth_System/Earth.cpp` (and .h `_lodThreshold`).
- Staged radius: in `Application.cpp` manifest initializers (search `activationRadius`).

Smaller LOD = closer for upgrade. Larger activation = planets "pop in" from farther.

### Monitoring
- Chrome Performance + Memory.
- WebGL inspector (active textures, MAX_LEVEL values, completeness).
- Console is authoritative for flow.

### Future / Related
- [docs/ARCHITECTURE.md](../ARCHITECTURE.md) — canonical loading/LOD architecture
- [docs/plans/archive/](../plans/archive/) — completed implementation plans
- [AGENTS.md](../../AGENTS.md) — Cursor Cloud agent notes

**Related GitHub Issues (cross-linked per P3 task)**: #51–#58 (mipmap safety, async/queue, feedback/UI, presets+memory, staged+proxies, render, nav, docs alignment).

## Additional Notes

### Adjusting LOD Threshold
To change the distance threshold, edit `src/Solar_System/Earth_System/Earth.h`:
```cpp
const float _lodThreshold = 50.0f;  // Change this value
```

Smaller values = must be closer to trigger high-res
Larger values = triggers from farther away

### Monitoring Performance
Use Chrome's Performance profiler to identify any frame stutter during texture loading.

### Future Enhancements
- ~~Implement for other planets (Mars, Jupiter, etc.)~~ — covered via shared `TextureLODController`
- ~~Add intermediate LOD levels (low, medium, high)~~ — `textures_mid` + preset max tier (see §5–6)
- ~~Add distance-based unloading (revert to low-res when far)~~ — high→mid→low hysteresis
- Prefetch mid maps during staged activation (`optionalMidRes` in `planet_manifest.json`)
- Priority queue: prefer mid jobs over high under memory backpressure
