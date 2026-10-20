#ifndef SOLARSYSTEM_CORE_LOG_H
#define SOLARSYSTEM_CORE_LOG_H
#include <functional>
#include <optional>
#include <string>
#include <string_view>

/**
 * The one diagnostic channel: LOG_DEBUG/INFO/WARN/ERROR(category, fmt, ...).
 *
 * `category` is the bracketed tag a line starts with ("Shader" → "[Shader] ..."), so
 * existing greps for "[StagedLoading]" or "[ShaderSelfTest]" keep matching. `fmt` is
 * printf-style and is only formatted when the level is enabled.
 *
 * Output: native writes "HH:MM:SS.mmm LEVEL [Category] message" to stderr; web goes through
 * emscripten_log to console.debug/info/warn/error, so DevTools can filter by severity.
 *
 * Level: Info by default. Native reads SOLARSYSTEM_LOG, web reads the `?log=` query
 * parameter (debug, info, warn, error, off) — see Log::InitFromEnvironment.
 */
namespace Log {

enum class Level : int { Debug = 0, Info, Warn, Error, Off };

Level CurrentLevel();
void SetLevel(Level level);
inline bool Enabled(Level level) { return level != Level::Off && level >= CurrentLevel(); }

/** "debug", "info", "warn"/"warning", "error", "off"/"none" (case-insensitive). */
std::optional<Level> ParseLevel(std::string_view text);
const char* LevelName(Level level);

/** Apply SOLARSYSTEM_LOG (native) or `?log=` (web); an unknown value warns and keeps Info. */
void InitFromEnvironment();

/** "[category] message" — the text both sinks print (native adds a timestamp and level). */
std::string FormatLine(const char* category, std::string_view message);

#if defined(__GNUC__) || defined(__clang__)
#define SOLARSYSTEM_PRINTF_FORMAT(fmtIndex, firstArg) __attribute__((format(printf, fmtIndex, firstArg)))
#else
#define SOLARSYSTEM_PRINTF_FORMAT(fmtIndex, firstArg)
#endif

void Write(Level level, const char* category, const char* format, ...) SOLARSYSTEM_PRINTF_FORMAT(3, 4);

/** Tests: capture lines instead of printing them; pass an empty function to restore. */
using Sink = std::function<void(Level, const std::string& line)>;
void SetSinkForTesting(Sink sink);

} // namespace Log

#define SOLARSYSTEM_LOG_AT(level, category, ...)                                                         \
    do {                                                                                                 \
        if (::Log::Enabled(level)) {                                                                     \
            ::Log::Write(level, category, __VA_ARGS__);                                                  \
        }                                                                                                \
    } while (0)

#define LOG_DEBUG(category, ...) SOLARSYSTEM_LOG_AT(::Log::Level::Debug, category, __VA_ARGS__)
#define LOG_INFO(category, ...) SOLARSYSTEM_LOG_AT(::Log::Level::Info, category, __VA_ARGS__)
#define LOG_WARN(category, ...) SOLARSYSTEM_LOG_AT(::Log::Level::Warn, category, __VA_ARGS__)
#define LOG_ERROR(category, ...) SOLARSYSTEM_LOG_AT(::Log::Level::Error, category, __VA_ARGS__)

#endif // SOLARSYSTEM_CORE_LOG_H
