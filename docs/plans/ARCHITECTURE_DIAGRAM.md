# Architecture Diagram: Progress Bar and LOD System

## System Flow Diagram

```
┌─────────────────────────────────────────────────────────────────────┐
│                     Application Startup (WASM)                       │
└─────────────────────────────────────────────────────────────────────┘
                                   │
                                   ▼
┌─────────────────────────────────────────────────────────────────────┐
│  Application::Application() → InitSystems() → InitScene()           │
└─────────────────────────────────────────────────────────────────────┘
                                   │
                    ┌──────────────┴──────────────┐
                    │                             │
              WASM Build                    Desktop Build
                    │                             │
                    ▼                             ▼
         LoadResources()                InitSceneObjects()
         _appState = LOADING            _appState = RUNNING
                    │
                    ▼
┌─────────────────────────────────────────────────────────────────────┐
│                    LoadResources() Process                           │
│  - Create list of 63 resources                                      │
│  - Set _totalResources = 63                                         │
│  - Set _resourcesPending = 63                                       │
│  - Call UpdateLoadingProgress() → JS shows 0%                       │
│  - For each resource:                                               │
│    • WebResourceFetcher::DownloadFile(url, path, callback)         │
│    • On callback: _resourcesPending--                              │
│    • Call UpdateLoadingProgress() → JS shows N%                    │
└─────────────────────────────────────────────────────────────────────┘
                    │
                    ▼
┌─────────────────────────────────────────────────────────────────────┐
│                  Main Loop: RunOneFrame()                            │
│  While _appState == LOADING:                                        │
│    - Clear screen (black)                                           │
│    - UpdateLoadingProgress() → Update JS progress bar              │
│    - If _resourcesPending <= 0:                                     │
│      • InitSceneObjects()                                           │
│      • _appState = RUNNING                                          │
│      • JS hides loading screen                                      │
└─────────────────────────────────────────────────────────────────────┘
                    │
                    ▼
┌─────────────────────────────────────────────────────────────────────┐
│                  Main Loop: RunOneFrame()                            │
│  While _appState == RUNNING:                                        │
│    - Process input                                                  │
│    - Render scene                                                   │
│    - For each planet:                                               │
│      • planet->LoadHighResIfClose(camera.GetPosition())            │
└─────────────────────────────────────────────────────────────────────┘
```

## Progress Bar Communication Flow

```
┌──────────────┐
│     C++      │
│ Application  │
└──────┬───────┘
       │
       │ _totalResources = 63
       │ _resourcesPending = 63, 62, 61, ... 0
       │
       ▼
┌──────────────────────┐
│ UpdateLoadingProgress│
│    loaded = total - pending
│    EM_ASM({...})     │
└──────┬───────────────┘
       │
       │ JavaScript bridge
       │ (Emscripten EM_ASM)
       │
       ▼
┌───────────────────────────┐
│  window.updateLoadingProgress(loaded, total)
│  - Calculate percentage   │
│  - Update progress bar    │
│  - Update text            │
│  - Hide when complete     │
└───────┬───────────────────┘
        │
        ▼
┌───────────────────────────┐
│       HTML/CSS DOM        │
│  #progress-bar.style.width│
│  #progress-text.textContent│
│  #loading-container.hidden│
└───────────────────────────┘
```

## LOD System Flow

```
┌─────────────────────────────────────────────────────────────────────┐
│                    Planet Initialization (WASM)                      │
│  GetTexturePath("low_res.dds", "high_res.dds")                      │
│    → Returns "low_res.dds" for WASM                                 │
│    → Returns "high_res.dds" for Desktop                             │
└─────────────────────────────────────────────────────────────────────┘
                                   │
                                   ▼
┌─────────────────────────────────────────────────────────────────────┐
│              TextureImage2D Constructor                              │
│  - LoadTextureFromFile(path)                                        │
│  - WebResourceFetcher::Fetch(path)                                  │
│  - Load texture to GPU                                              │
└─────────────────────────────────────────────────────────────────────┘
                                   │
                                   ▼
┌─────────────────────────────────────────────────────────────────────┐
│                 Runtime: RenderPass() Each Frame                     │
│  planet->LoadHighResIfClose(camera.GetPosition())                   │
└─────────────────────────────────────────────────────────────────────┘
                                   │
                                   ▼
┌─────────────────────────────────────────────────────────────────────┐
│            Planet::LoadHighResIfClose() (WASM only)                  │
│  1. Check: if (_isHighResLoaded) return;                           │
│  2. Calculate: distance = length(cameraPos - planetPos)            │
│  3. Check: if (distance < _lodThreshold) // 50 units               │
│     - texture.ReloadTexture("high_res.dds")                        │
│     - _isHighResLoaded = true                                       │
└─────────────────────────────────────────────────────────────────────┘
                                   │
                                   ▼
┌─────────────────────────────────────────────────────────────────────┐
│              TextureImage2D::ReloadTexture()                         │
│  1. Delete old texture: glDeleteTextures(1, &_textureID)           │
│  2. WebResourceFetcher::Fetch(high_res_path)                       │
│  3. LoadTextureFromFile(high_res_path)                             │
│  4. Upload to GPU: CDDSImage.upload_texture2D()                    │
└─────────────────────────────────────────────────────────────────────┘
```

## Planet LOD Implementations

```
┌─────────────────────────────────────────────────────────────────────┐
│                        Planet LOD Matrix                             │
├──────────┬─────────┬───────────┬──────────────────────────────────┤
│ Planet   │ Impl.   │ Threshold │ Textures Upgraded                 │
├──────────┼─────────┼───────────┼──────────────────────────────────┤
│ Earth    │   ✅    │ 50 units  │ Diffuse, Normal, Specular         │
│ Mercury  │   ✅    │ 50 units  │ Diffuse, Normal, Specular         │
│ Venus    │   ✅    │ 50 units  │ Diffuse, Normal                   │
│ Mars     │   ✅    │ 50 units  │ Diffuse, Normal                   │
│ Jupiter  │   ✅    │ 50 units  │ Diffuse, Normal                   │
│ Saturn   │   ✅    │ 50 units  │ Diffuse, Normal                   │
│ Uranus   │   ✅    │ 50 units  │ Diffuse, Normal                   │
│ Neptune  │   ✅    │ 50 units  │ Diffuse, Normal                   │
│ Pluto    │   ✅    │ 50 units  │ Diffuse, Normal, Specular         │
└──────────┴─────────┴───────────┴──────────────────────────────────┘
```

## Resource Loading Timeline

```
Time →
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│ T=0ms:   Application starts                                         │
│          InitSystems() → Create window, init OpenGL                 │
│          InitScene() → LoadResources() [WASM only]                  │
│          _appState = LOADING                                        │
│          Progress bar visible: 0%                                   │
│                                                                      │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│ T=0-5s:  Resources downloading in parallel                          │
│          Each completion: _resourcesPending--                       │
│          Progress bar updates: 1%, 2%, 3%, ... 100%                 │
│          Console: "Loading 63 resources..."                         │
│          Console: "Successfully downloaded: resource/..."           │
│                                                                      │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│ T=5s:    All resources downloaded                                   │
│          _resourcesPending = 0                                      │
│          Progress bar: 100%                                         │
│          Console: "All resources downloaded. Initializing scene..." │
│                                                                      │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│ T=5.5s:  InitSceneObjects() completes                               │
│          _appState = RUNNING                                        │
│          Progress bar hides (500ms delay)                           │
│          Scene rendering begins                                     │
│                                                                      │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│ T=5.5s+: User navigates scene                                       │
│          Planets render with low-res textures                       │
│          Camera distance checked each frame                         │
│                                                                      │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│ T=30s:   User zooms into Earth                                      │
│          Distance < 50 units detected                               │
│          High-res textures download                                 │
│          Textures swap on GPU                                       │
│          Console: "[LOD] Earth high-res textures loaded..."         │
│          Visual quality improves                                    │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

## Key Design Decisions

### 1. Platform Separation
```cpp
#ifdef __EMSCRIPTEN__
    // WASM: Async loading + progress bar + LOD
    LoadResources();
    _appState = LOADING;
#else
    // Desktop: Synchronous loading + high-res only
    InitSceneObjects();
    _appState = RUNNING;
#endif
```

### 2. Progress Tracking
```cpp
// Atomic counter (thread-safe for callback context)
std::atomic<int> _resourcesPending{0};
int _totalResources = 0;

// Calculate loaded
int loaded = _totalResources - _resourcesPending;
```

### 3. JavaScript Bridge
```cpp
// C++ → JavaScript via EM_ASM
EM_ASM({
    if (typeof window.updateLoadingProgress === 'function') {
        window.updateLoadingProgress($0, $1);
    }
}, loaded, _totalResources);
```

### 4. LOD Distance Check
```cpp
// Simple Euclidean distance
float distance = glm::length(cameraPos - GetPosition());
if (distance < _lodThreshold) { /* upgrade textures */ }
```

### 5. Texture Path Resolution
```cpp
std::string GetTexturePath(const std::string& lowRes, const std::string& highRes) {
#ifdef __EMSCRIPTEN__
    return lowRes;  // Use low-res for WASM initial load
#else
    return highRes; // Use high-res for Desktop
#endif
}
```

## Security Considerations

- Uses existing `WebResourceFetcher` (proven in production)
- Same-origin policy enforced by browser
- No new network code introduced
- Progress data stays client-side
- No sensitive information exposed
