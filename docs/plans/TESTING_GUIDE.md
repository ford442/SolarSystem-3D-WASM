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
1. Initial load is fast (only low-res textures downloaded)
2. Low-res textures display correctly on Earth
3. Zooming close triggers high-res download (console logs visible)
4. High-res textures display correctly after load
5. No memory leaks (memory usage reasonable after texture swap)
6. No repeated downloads (textures fetched only once)
7. Desktop build unchanged (loads high-res directly)
8. Error handling works (graceful degradation on missing files)

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
