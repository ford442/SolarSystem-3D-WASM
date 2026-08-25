#ifndef SOLARSYSTEM_JS_BRIDGE_H
#define SOLARSYSTEM_JS_BRIDGE_H

#ifdef __EMSCRIPTEN__

void NotifySettingsChanged(const char* field);
void NotifyLoadingProgress(int loaded, int total);
void NotifyStreamingProgress(int completed, int total, int active, int tierCode);
void NotifyPlanetFocused(int index);

#else

inline void NotifySettingsChanged(const char*) {}
inline void NotifyLoadingProgress(int, int) {}
inline void NotifyStreamingProgress(int, int, int, int) {}
inline void NotifyPlanetFocused(int) {}

#endif

#endif // SOLARSYSTEM_JS_BRIDGE_H
