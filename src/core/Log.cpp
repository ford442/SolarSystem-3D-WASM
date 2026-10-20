#include "Log.h"

#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace Log {
namespace {

std::atomic<int> s_level{static_cast<int>(Level::Info)};

Sink& TestSink() {
    static Sink sink;
    return sink;
}

#ifdef __EMSCRIPTEN__
// The `log` query parameter of the page, malloc'd (null when absent or outside a window).
EM_JS(char*, LogQueryParameter, (), {
    try {
        if (typeof location === 'undefined') {
            return 0;
        }
        const value = new URLSearchParams(location.search).get('log');
        if (!value) {
            return 0;
        }
        const length = lengthBytesUTF8(value) + 1;
        const buffer = _malloc(length);
        stringToUTF8(value, buffer, length);
        return buffer;
    } catch (e) {
        return 0;
    }
});

void Emit(Level level, const std::string& line) {
    int flags = EM_LOG_CONSOLE;
    switch (level) {
        case Level::Debug: flags |= EM_LOG_DEBUG; break;
        case Level::Info: flags |= EM_LOG_INFO; break;
        case Level::Warn: flags |= EM_LOG_WARN; break;
        case Level::Error: flags |= EM_LOG_ERROR; break;
        case Level::Off: return;
    }
    emscripten_log(flags, "%s", line.c_str());
}
#else
void Emit(Level level, const std::string& line) {
    const auto now = std::chrono::system_clock::now();
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    const long long millis =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &seconds);
#else
    localtime_r(&seconds, &local);
#endif
    // One fprintf per line so lines from different threads (audio) don't interleave.
    std::fprintf(stderr, "%02d:%02d:%02d.%03lld %-5s %s\n", local.tm_hour, local.tm_min, local.tm_sec, millis,
                 LevelName(level), line.c_str());
}
#endif

std::string FormatV(const char* format, va_list args) {
    va_list copy;
    va_copy(copy, args);
    char stackBuffer[512];
    const int length = std::vsnprintf(stackBuffer, sizeof stackBuffer, format, copy);
    va_end(copy);
    if (length < 0) {
        return format;
    }
    if (static_cast<size_t>(length) < sizeof stackBuffer) {
        return std::string(stackBuffer, static_cast<size_t>(length));
    }
    std::vector<char> heapBuffer(static_cast<size_t>(length) + 1);
    std::vsnprintf(heapBuffer.data(), heapBuffer.size(), format, args);
    return std::string(heapBuffer.data(), static_cast<size_t>(length));
}

} // namespace

Level CurrentLevel() {
    return static_cast<Level>(s_level.load(std::memory_order_relaxed));
}

void SetLevel(Level level) {
    s_level.store(static_cast<int>(level), std::memory_order_relaxed);
}

std::optional<Level> ParseLevel(std::string_view text) {
    std::string lower;
    for (const char ch : text) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    if (lower == "debug") return Level::Debug;
    if (lower == "info") return Level::Info;
    if (lower == "warn" || lower == "warning") return Level::Warn;
    if (lower == "error") return Level::Error;
    if (lower == "off" || lower == "none") return Level::Off;
    return std::nullopt;
}

const char* LevelName(Level level) {
    switch (level) {
        case Level::Debug: return "DEBUG";
        case Level::Info: return "INFO";
        case Level::Warn: return "WARN";
        case Level::Error: return "ERROR";
        case Level::Off: return "OFF";
    }
    return "?";
}

void InitFromEnvironment() {
#ifdef __EMSCRIPTEN__
    char* raw = LogQueryParameter();
    const std::string requested = raw ? raw : "";
    std::free(raw);
    const char* source = "?log=";
#else
    const char* raw = std::getenv("SOLARSYSTEM_LOG");
    const std::string requested = raw ? raw : "";
    const char* source = "SOLARSYSTEM_LOG=";
#endif
    if (requested.empty()) {
        return;
    }
    if (const std::optional<Level> level = ParseLevel(requested)) {
        SetLevel(*level);
        LOG_INFO("Log", "level %s (%s%s)", LevelName(*level), source, requested.c_str());
    } else {
        LOG_WARN("Log", "unknown level %s%s; expected debug, info, warn, error or off", source, requested.c_str());
    }
}

std::string FormatLine(const char* category, std::string_view message) {
    std::string line;
    line.reserve(message.size() + 16);
    line.push_back('[');
    line += category;
    line += "] ";
    line += message;
    return line;
}

void Write(Level level, const char* category, const char* format, ...) {
    va_list args;
    va_start(args, format);
    const std::string message = FormatV(format, args);
    va_end(args);
    const std::string line = FormatLine(category, message);
    if (const Sink& sink = TestSink()) {
        sink(level, line);
        return;
    }
    Emit(level, line);
}

void SetSinkForTesting(Sink sink) {
    TestSink() = std::move(sink);
}

} // namespace Log
