# Progress Bar and LOD Testing Guide

## Overview
This document describes how to test the new loading progress bar and verify the LOD (Level of Detail) texture loading system works correctly.

## Changes Made

### 1. Progress Bar Implementation

#### Frontend (Web Interface)
- **File**: `web/index.html`
  - Added a loading container with progress bar UI
  - Styled with CSS for professional appearance
  - Progress bar shows percentage and visual bar
  
- **File**: `web/src/main.ts`
  - Added `updateLoadingProgress()` function exposed globally
  - Function updates progress bar and percentage text
  - Auto-hides loading screen when resources are loaded

#### Backend (C++ Application)
- **File**: `src/Application.h`
  - Added `UpdateLoadingProgress()` method declaration

- **File**: `src/Application.cpp`
  - Implemented `UpdateLoadingProgress()` using `EM_ASM` to call JavaScript
  - Updates progress in `RunOneFrame()` during LOADING state
  - Updates progress after each resource download in `LoadResources()`

### 2. LOD System Verification

The following planets have LOD implementations that load low-res textures initially and upgrade to high-res when camera zooms within threshold distance:

- **Earth**: Threshold 50 units
  - Low-res: `resource/textures_low/Earth_Day_Diffuse_Low.dds`
  - High-res: `resource/textures/Earth_Day_Diffuse.dds`
  - Also loads: Normal map, Specular map

- **Mercury**: Threshold 50 units
  - Low-res: `resource/textures_low/Mercury_Diffuse_Low.dds`
  - High-res: `resource/textures/Mercury_Diffuse.dds`
  - Also loads: Normal map, Specular map

- **Venus**: Threshold 50 units
  - Low-res: `resource/textures_low/Venus_Diffuse_Low.dds`
  - High-res: `resource/textures/Venus_Diffuse.dds`
  - Also loads: Normal map

- **Uranus**: Threshold 50 units
  - Low-res: `resource/textures_low/Uranus_Diffuse_Low.dds`
  - High-res: `resource/textures/Uranus_Diffuse.dds`
  - Also loads: Normal map

- **Pluto**: Threshold 50 units
  - Low-res: `resource/textures_low/Pluto_Diffuse_Low.dds`
  - High-res: `resource/textures/Pluto_Diffuse.dds`
  - Also loads: Normal map, Specular map

Additional planets with LOD: Jupiter, Saturn, Mars, Neptune

## Testing Instructions

### Prerequisites
1. Install Emscripten SDK
2. Run `./setup_web_dependencies.sh` to build dependencies
3. Ensure low-res texture files exist in `resource/textures_low/`

### Building
```bash
./build-web.sh
```

### Running
```bash
cd build-web
python3 -m http.server 8000
```

Open browser to: `http://localhost:8000/SolarSystem.html`

### Test Cases

#### Test 1: Progress Bar Display
**Expected Behavior:**
1. Loading screen appears immediately on page load
2. Progress bar shows 0% initially
3. Progress bar updates as resources download
4. Percentage text updates (e.g., "25%", "50%", "100%")
5. Loading screen fades out when 100% complete
6. Canvas becomes visible

**Console Output to Check:**
```
Loading 63 resources...
Starting download: resource/textures/Main_SkyBox/PositiveX.dds to resource/textures/Main_SkyBox/PositiveX.dds
Loading progress: 1/63 (1%)
...
Loading progress: 63/63 (100%)
All resources downloaded. Initializing scene...
```

#### Test 2: WebResourceFetcher Downloads
**Expected Behavior:**
- All resources download successfully
- No "Failed to download resource!" errors in console
- Files written to MEMFS (Emscripten virtual file system)

**Console Output to Check:**
```
[WebResourceFetcher] Fetching: resource/textures/Earth_Day_Diffuse.dds ...
[WebResourceFetcher] Downloaded and written to MEMFS: resource/textures/Earth_Day_Diffuse.dds
resource/textures/Earth_Day_Diffuse.dds Loaded
```

#### Test 3: LOD - Earth Texture Transition
**Steps:**
1. Wait for application to load (100% progress)
2. Navigate camera away from Earth
3. Observe low-resolution textures on Earth
4. Zoom camera toward Earth until distance < 50 units
5. Observe texture quality improvement

**Console Output to Check:**
```
[LOD] Camera distance to Earth: 45.2 units. Loading high-res textures...
[WebResourceFetcher] Fetching: resource/textures/Earth_Day_Diffuse.dds ...
[WebResourceFetcher] Downloaded and written to MEMFS: resource/textures/Earth_Day_Diffuse.dds
resource/textures/Earth_Day_Diffuse.dds Loaded
[LOD] Earth high-res textures loaded successfully (Day Diffuse, Normal, Specular)
```

#### Test 4: LOD - Mercury Texture Transition
**Steps:**
1. Navigate to Mercury
2. Zoom in until distance < 50 units
3. Verify textures upgrade

**Console Output to Check:**
```
[LOD] Camera distance to Mercury: 47.8 units. Loading high-res textures...
[LOD] Mercury high-res textures loaded successfully
```

#### Test 5: LOD - Venus Texture Transition
**Steps:**
1. Navigate to Venus
2. Zoom in until distance < 50 units
3. Verify textures upgrade

**Console Output to Check:**
```
[LOD] Camera distance to Venus: 48.1 units. Loading high-res textures...
[LOD] Venus high-res textures loaded successfully
```

#### Test 6: LOD - Uranus Texture Transition
**Steps:**
1. Navigate to Uranus
2. Zoom in until distance < 50 units
3. Verify textures upgrade

**Console Output to Check:**
```
[LOD] Camera distance to Uranus: 49.3 units. Loading high-res textures...
[LOD] Uranus high-res textures loaded successfully
```

#### Test 7: LOD - Pluto Texture Transition
**Steps:**
1. Navigate to Pluto
2. Zoom in until distance < 50 units
3. Verify textures upgrade

**Console Output to Check:**
```
[LOD] Camera distance to Pluto: 46.5 units. Loading high-res textures...
[LOD] Pluto high-res textures loaded successfully
```

### Visual Verification

#### Progress Bar
- Green gradient bar should be visible
- Smooth animation as bar fills
- Clear percentage text
- Professional appearance matching space theme

#### LOD Transitions
- Initial render: Lower quality, smaller file size textures
- Post-zoom: Higher quality, more detailed textures
- No visual glitches during transition
- Single frame may stutter slightly during first high-res load (expected)

### Performance Metrics

#### Initial Load
- Faster initial load with low-res textures
- Reduced bandwidth usage
- Smaller .data file preloaded

#### Runtime
- High-res textures load on-demand
- Only loaded once per planet per session
- Memory managed (old texture deleted before new one loaded)

## Troubleshooting

### Progress Bar Not Appearing
- Check browser console for JavaScript errors
- Verify `updateLoadingProgress` function is defined
- Ensure loading container has correct ID

### Progress Not Updating
- Check that `EM_ASM` macro is available (requires `-lembind` or appropriate flags)
- Verify callbacks are firing in C++ code
- Check network tab to see if downloads are happening

### LOD Not Triggering
- Verify camera position is within threshold (< 50 units)
- Check console for LOD messages
- Ensure `#ifdef __EMSCRIPTEN__` blocks are active
- Verify planet's `LoadHighResIfClose()` is being called

### Texture Load Failures
- Check that high-res texture files exist
- Verify file paths are correct
- Look for WebResourceFetcher error messages
- Check network tab for 404 errors

## Known Limitations

1. **Progress granularity**: Progress updates per file, not per byte
2. **Synchronous downloads**: Uses `emscripten_wget_data` which blocks
3. **One-way LOD**: Cannot revert from high-res to low-res
4. **Frame stutter**: Brief pause when loading high-res textures for first time
5. **Single threshold**: Binary low/high switch, no graduated levels

## Future Improvements

1. Byte-level progress tracking using OnProgress2 callback
2. Asynchronous texture loading with minimal frame impact
3. Multi-level LOD (very low, low, medium, high)
4. Dynamic texture unloading for distant planets
5. Preemptive loading based on camera velocity
