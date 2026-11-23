#ifndef SOLARSYSTEM_GFX_GPURESOURCEREGISTRY_H
#define SOLARSYSTEM_GFX_GPURESOURCEREGISTRY_H
#include "GlPlatform.h"
#include <cstddef>
#include <cstdint>

/**
 * GPU resource lifetime across a lost context (docs/ARCHITECTURE.md §7.8).
 *
 * Two intrusive lists, both main-thread only and touched only when a GL object is made,
 * moved or dropped (never per draw):
 *
 *  - every non-empty GlHandle (gfx/GlHandle.h). InvalidateAll() zeroes their names without
 *    calling glDelete*: the context that owned them is gone, and on a restored or recreated
 *    context the same integers may already name new objects. An owner that builds its GL
 *    state lazily (`if (!_vao) Build();` — the FrameUBO, the terrain grid and tile textures)
 *    is restored by this alone.
 *  - every IGpuRestorable. RecreateAll() calls RestoreGpu() on each, phase by phase, so an
 *    owner that builds eagerly from CPU state it keeps (a texture's source path, a mesh's
 *    vertices, a framebuffer's size) rebuilds itself in place: its address, and every
 *    pointer to it, stay valid.
 *
 * Restore order: InvalidateAll(), then whatever the context needs before objects exist
 * (global state, extensions, capability probe), then RecreateAll(). Application::
 * RestoreGpuContext is the one caller in the app.
 */
namespace GpuResources {

/** RecreateAll walks owners in this order; an owner registers under the last kind it needs. */
enum class Phase : std::uint8_t {
    Buffers,
    Textures,
    Framebuffers, // after Textures: an FBO may attach another owner's texture
    Programs,
    VertexArrays, // last: a VAO captures buffer bindings
};
constexpr std::size_t kPhaseCount = 5;

/** Non-empty GlHandles right now (the debug HUD / logs, and the tests). */
std::size_t LiveHandleCount();
/** Registered IGpuRestorable owners right now. */
std::size_t RestorableCount();
/** Zero every live GlHandle without deleting it (the context is gone). Returns how many. */
std::size_t InvalidateAll();
/**
 * RestoreGpu() on every owner registered when the call starts, in Phase order; owners made
 * during the walk are already on the new context. Returns how many were restored. An owner
 * that throws is logged and skipped, so one bad texture cannot keep the scene down.
 */
std::size_t RecreateAll();
/** Times RecreateAll has run (a restore count for logs and tests). */
std::uint32_t Generation();

} // namespace GpuResources

/** Intrusive node every GlHandle derives from; linked exactly while its name is non-zero. */
class GlHandleBase {
protected:
    GlHandleBase() noexcept = default;
    ~GlHandleBase() = default;
    GlHandleBase(const GlHandleBase&) = delete;
    GlHandleBase& operator=(const GlHandleBase&) = delete;
    GlHandleBase(GlHandleBase&&) = delete;
    GlHandleBase& operator=(GlHandleBase&&) = delete;

    /** Take `id` (non-zero) and join the live list. */
    void Adopt(GLuint id) noexcept;
    /** Leave the live list and return the name (0 if empty). */
    GLuint Release() noexcept;

    GLuint _id = 0;

private:
    friend std::size_t GpuResources::InvalidateAll();
    GlHandleBase* _prev = nullptr;
    GlHandleBase* _next = nullptr;
};

namespace GpuResources::detail {
// Constant-initialised, so no static-order question: a handle may die after main returns.
inline GlHandleBase* g_liveHead = nullptr;
inline std::size_t g_liveCount = 0;
} // namespace GpuResources::detail

// GCC's -Wdangling-pointer sees a local's address stored in a global list; every node
// unlinks itself in its destructor, so the list never holds a dead one.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdangling-pointer"
#endif
inline void GlHandleBase::Adopt(GLuint id) noexcept {
    _id = id;
    if (id == 0) {
        return;
    }
    _prev = nullptr;
    _next = GpuResources::detail::g_liveHead;
    if (_next) {
        _next->_prev = this;
    }
    GpuResources::detail::g_liveHead = this;
    ++GpuResources::detail::g_liveCount;
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

inline GLuint GlHandleBase::Release() noexcept {
    const GLuint id = _id;
    if (id == 0) {
        return 0;
    }
    if (_prev) {
        _prev->_next = _next;
    } else {
        GpuResources::detail::g_liveHead = _next;
    }
    if (_next) {
        _next->_prev = _prev;
    }
    _prev = _next = nullptr;
    _id = 0;
    --GpuResources::detail::g_liveCount;
    return id;
}

/**
 * An owner of GL objects that rebuilds them in RestoreGpu() from CPU state it keeps. Derive
 * from it and name the phase; copies and moves register the new object too. RestoreGpu()
 * runs after InvalidateAll(), so every handle the owner holds is already empty: build, do
 * not reset. It must not destroy or move other owners (no reallocating a vector of them).
 */
class IGpuRestorable {
public:
    virtual void RestoreGpu() = 0;

protected:
    explicit IGpuRestorable(GpuResources::Phase phase) noexcept : _phase(phase) { Link(); }
    IGpuRestorable(const IGpuRestorable& other) noexcept : _phase(other._phase) { Link(); }
    // Stays registered under its own phase: assignment changes contents, not identity.
    // No move members on purpose: rvalues bind to these, so a derived class's defaulted move
    // still compiles and links the new object.
    IGpuRestorable& operator=(const IGpuRestorable&) noexcept { return *this; }
    virtual ~IGpuRestorable() { Unlink(); }

private:
    friend std::size_t GpuResources::RecreateAll();
    friend std::size_t GpuResources::RestorableCount();
    void Link() noexcept;
    void Unlink() noexcept;

    GpuResources::Phase _phase;
    IGpuRestorable* _prev = nullptr;
    IGpuRestorable* _next = nullptr;
};

namespace GpuResources::detail {
inline IGpuRestorable* g_restorableHead = nullptr;
}

// GCC's -Wdangling-pointer sees a local's address stored in a global list; every node
// unlinks itself in its destructor, so the list never holds a dead one.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdangling-pointer"
#endif
inline void IGpuRestorable::Link() noexcept {
    _next = GpuResources::detail::g_restorableHead;
    if (_next) {
        _next->_prev = this;
    }
    GpuResources::detail::g_restorableHead = this;
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

inline void IGpuRestorable::Unlink() noexcept {
    if (_prev) {
        _prev->_next = _next;
    } else if (GpuResources::detail::g_restorableHead == this) {
        GpuResources::detail::g_restorableHead = _next;
    }
    if (_next) {
        _next->_prev = _prev;
    }
    _prev = _next = nullptr;
}

#endif // SOLARSYSTEM_GFX_GPURESOURCEREGISTRY_H
