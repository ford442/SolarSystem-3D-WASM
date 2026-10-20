#ifndef SOLARSYSTEM_SHADERLIBRARY_H
#define SOLARSYSTEM_SHADERLIBRARY_H
#include "Shader.h"
#include <map>
#include <memory>
#include <string>

/**
 * Programs keyed by ShaderSpec (stage files + permutation defines), built once each.
 *
 * Prefetch() only submits a program (Shader::Submit), so prefetching a batch before the
 * first Get() lets KHR_parallel_shader_compile overlap their compiles. Shaders live behind
 * unique_ptr: the references Get() hands out stay valid until Clear(), and hot reload
 * replaces a program in place.
 */
class ShaderLibrary {
public:
    /** Submit `spec` if it is not in the library yet. A preprocess error is kept for Get(). */
    void Prefetch(const ShaderSpec& spec);
    /** The finished program; waits for it. Throws (after printing why) if it failed to build. */
    Shader& Get(const ShaderSpec& spec);
    /** Get(), but null (once reported) when the program failed — for optional effects. */
    Shader* TryGet(const ShaderSpec& spec);
    /**
     * Prefetch, and the program only once the driver says it has compiled; null while it is
     * still compiling or when it failed. For variants picked mid-frame, so a cold one never
     * stalls the frame.
     */
    Shader* GetIfReady(const ShaderSpec& spec);
    /** Delete every program (needs the GL context current). */
    void Clear() { _entries.clear(); }
    size_t Size() const { return _entries.size(); }

private:
    struct Entry {
        std::unique_ptr<Shader> shader; // null once it failed
        std::string error;
    };
    Entry& Submit(const ShaderSpec& spec);
    /** Finish `entry`; on failure drop its shader and keep the error. */
    static Shader* Finish(Entry& entry);

    std::map<ShaderSpec, Entry> _entries;
};

#endif // SOLARSYSTEM_SHADERLIBRARY_H
