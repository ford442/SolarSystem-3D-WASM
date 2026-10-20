#include "JsBridge.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

// Every JS callback below is reached with bracket notation (Module['foo'], not
// Module.foo). Closure Compiler (--closure 1, enabled for Release in CMakeLists.txt)
// renames dotted property accesses it has no externs for, which would silently turn
// these into calls on a mangled name that web/src/wasmCallbacks.ts never assigns.
// Quoted string keys are never renamed. Same rule applies to the window.__solarSystem*
// reads in QualitySettings.cpp and WebResourceFetcher.cpp.

void NotifySettingsChanged(const char* field) {
    EM_ASM({
        if (typeof Module['onSettingsChanged'] === 'function') {
            Module['onSettingsChanged'](UTF8ToString($0));
        }
    }, field);
}

// `failed` is already counted in `loaded`: it says how many of the finished resources
// fell back instead of arriving.
void NotifyLoadingProgress(int loaded, int total, int failed) {
    EM_ASM({
        if (typeof Module['updateLoadingProgress'] === 'function') {
            Module['updateLoadingProgress']($0, $1, $2);
        }
    }, loaded, total, failed);
}

void NotifyStreamingProgress(int completed, int total, int active, int tierCode) {
    EM_ASM({
        if (typeof Module['updateStreamingProgress'] === 'function') {
            Module['updateStreamingProgress']($0, $1, $2, $3);
        }
        if (typeof Module['updateLoadingProgress'] === 'function' && $1 > 0) {
            Module['updateLoadingProgress']($0, $1);
        }
    }, completed, total, active, tierCode);
}

// 0–1 gain the music bed should apply for scene proximity; web/src/music.ts owns the device.
void NotifyMusicDuck(float duck) {
    EM_ASM({
        if (typeof Module['onMusicDuck'] === 'function') {
            Module['onMusicDuck']($0);
        }
    }, duck);
}

void NotifyPlanetFocused(int index) {
    EM_ASM({
        if (typeof Module['onPlanetFocused'] === 'function') {
            Module['onPlanetFocused']($0);
        }
    }, index);
}

// Same numbering as GetNearestPlanetIndexForJs(); fired only when it changes.
void NotifyNearestPlanetChanged(int jsIndex) {
    EM_ASM({
        if (typeof Module['onNearestPlanetChanged'] === 'function') {
            Module['onNearestPlanetChanged']($0);
        }
    }, jsIndex);
}

// -1 when the mission follow stops.
void NotifyMissionFocused(int index) {
    EM_ASM({
        if (typeof Module['onMissionFocused'] === 'function') {
            Module['onMissionFocused']($0);
        }
    }, index);
}

#endif
