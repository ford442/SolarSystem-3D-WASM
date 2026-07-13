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

### 5. High-Res LOD Streaming (Queue + UI)
Fly or teleport inside ~50 units of a planet center:
```js
window.setCameraPose(1900, 0, 0, 180, 0);  // very close to Earth
```
Expected (console + overlay):
- `[LOD] Camera distance to Earth: 12.3 units. Queueing high-res textures...`
- `updateStreamingProgress` DOM element appears: "High-res upgrade: 1/3 (33%)" etc.
- Per-planet queue logs + final: `[LOD] Earth high-res textures loaded successfully`
- `TextureLoadingQueue` ensures: dedup (no duplicates if you jiggle camera), backoff on transient errors, serialized.
- Visual (real assets): texture sharpness jumps. Placeholders: no visible change but paths exercised.
- No further requests while `_isHighResLoaded`.

Test queue cancel:
- Start load → immediately `setCameraPose` far away → console deprioritize/cancel for that planet's paths; loading aborts without applying (or partial).

### 6. Downgrade + Hysteresis + Presets
- Fly away > ~100 units (2× threshold):
  - `[LOD] ... Downgrading high-res to low-res...`
  - `[LOD] Earth high-res textures downgraded (VRAM freed)`
  - `_isHighResLoaded=false`; low-res reloaded via ReloadTexture.
- Quality preset (low forces immediate downgrade of any loaded + suppresses upgrades):
  - URL: `.../solar-system/?quality=0`
  - Or: `Module.SetQualityPreset(0)` (also 1/2)
  - In code: `g_qualityPreset==0` path uses fake-far to drive downgrade for all.
- Medium/full allow upgrades normally.

### 7. Streaming UI + Throttled Progress
- High-res phase uses separate overlay (`#streaming-progress` in index.html) + bar.
- C++ calls `updateStreamingProgress(completed, total)` every frame while queued (throttled naturally by queue).
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

### 10. Verification Commands / JS Helpers
- Teleport list (approximate centers):
  - Mercury ~500, Venus ~900, Earth 1900, Mars ~2600, Jupiter ~5000-ish, etc. (exact from planet ctors or labels).
- Watch: `window.__solarSystemAssetBase` (base for fetches).
- Force streaming hide: manually set display none on `#streaming-progress`.
- Low preset URL test + close/far cycles.
- Check `_renderableSceneComponents` length grows only as planets activate (via any debug exposure).

### 11. Performance / Memory
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
- See `VERIFICATION_CHECKLIST.md`, `staged_loading_plan.md`, `LOD_IMPLEMENTATION.md`, `grok.md`, `AGENTS.md`.

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
- Implement for other planets (Mars, Jupiter, etc.)
- Add intermediate LOD levels (low, medium, high)
- Implement texture streaming for progressive loading
- Add distance-based unloading (revert to low-res when far)
