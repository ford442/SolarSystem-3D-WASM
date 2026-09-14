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

void NotifyLoadingProgress(int loaded, int total) {
    EM_ASM({
        if (typeof Module['updateLoadingProgress'] === 'function') {
            Module['updateLoadingProgress']($0, $1);
        }
    }, loaded, total);
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

void NotifyPlanetFocused(int index) {
    EM_ASM({
        if (typeof Module['onPlanetFocused'] === 'function') {
            Module['onPlanetFocused']($0);
        }
    }, index);
}

#endif
