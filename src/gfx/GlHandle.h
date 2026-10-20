#ifndef SOLARSYSTEM_GFX_GLHANDLE_H
#define SOLARSYSTEM_GFX_GLHANDLE_H
#include "GlPlatform.h"
#include <utility>

/**
 * Move-only owners for GL object names. This header is the only place in src/ allowed to
 * call glGen* / glDelete* / glCreate{Shader,Program} (CI greps for it), so a class that
 * holds GL state holds these instead and gets the right destructor, move and no-copy for
 * free.
 *
 * A default-constructed handle is empty (name 0) and touches no GL; `Create()` makes the
 * object. That keeps lazily-built members (a probe VAO made on first use, an FBO rebuilt on
 * resize) and GL-free unit tests simple. `reset()` deletes early; assigning a fresh
 * `Create()` replaces. Deleting while the context is gone is the caller's ordering bug
 * (Application::Dispose releases the scene before terminating the window).
 *
 * The traits are structs of static functions rather than `template <auto Gen, auto Del>`:
 * under GLEW `glGenBuffers` is a runtime function pointer, not a constant expression.
 */
template <class Traits>
class GlHandle {
public:
    GlHandle() noexcept = default;
    ~GlHandle() { reset(); }

    GlHandle(GlHandle&& other) noexcept : _id(std::exchange(other._id, 0)) {}
    GlHandle& operator=(GlHandle&& other) noexcept {
        if (this != &other) {
            reset();
            _id = std::exchange(other._id, 0);
        }
        return *this;
    }
    GlHandle(const GlHandle&) = delete;
    GlHandle& operator=(const GlHandle&) = delete;

    template <class... Args>
    static GlHandle Create(Args... args) {
        GlHandle handle;
        handle._id = Traits::Gen(args...);
        return handle;
    }

    GLuint get() const noexcept { return _id; }
    explicit operator bool() const noexcept { return _id != 0; }

    void reset() noexcept {
        if (_id != 0) {
            Traits::Del(_id);
            _id = 0;
        }
    }

private:
    GLuint _id = 0;
};

namespace gl_traits {
    struct Buffer {
        static GLuint Gen() { GLuint id = 0; glGenBuffers(1, &id); return id; }
        static void Del(GLuint id) { glDeleteBuffers(1, &id); }
    };
    struct VertexArray {
        static GLuint Gen() { GLuint id = 0; glGenVertexArrays(1, &id); return id; }
        static void Del(GLuint id) { glDeleteVertexArrays(1, &id); }
    };
    struct Texture {
        static GLuint Gen() { GLuint id = 0; glGenTextures(1, &id); return id; }
        static void Del(GLuint id) { glDeleteTextures(1, &id); }
    };
    struct Framebuffer {
        static GLuint Gen() { GLuint id = 0; glGenFramebuffers(1, &id); return id; }
        static void Del(GLuint id) { glDeleteFramebuffers(1, &id); }
    };
    struct Renderbuffer {
        static GLuint Gen() { GLuint id = 0; glGenRenderbuffers(1, &id); return id; }
        static void Del(GLuint id) { glDeleteRenderbuffers(1, &id); }
    };
    struct Query {
        static GLuint Gen() { GLuint id = 0; glGenQueries(1, &id); return id; }
        static void Del(GLuint id) { glDeleteQueries(1, &id); }
    };
    struct Program {
        static GLuint Gen() { return glCreateProgram(); }
        static void Del(GLuint id) { glDeleteProgram(id); }
    };
    struct ShaderStage {
        static GLuint Gen(GLenum type) { return glCreateShader(type); }
        static void Del(GLuint id) { glDeleteShader(id); }
    };
}

using GlBuffer = GlHandle<gl_traits::Buffer>;
using GlVertexArray = GlHandle<gl_traits::VertexArray>;
using GlTexture = GlHandle<gl_traits::Texture>;
using GlFramebuffer = GlHandle<gl_traits::Framebuffer>;
using GlRenderbuffer = GlHandle<gl_traits::Renderbuffer>;
using GlQuery = GlHandle<gl_traits::Query>;
using GlProgram = GlHandle<gl_traits::Program>;
using GlShaderStage = GlHandle<gl_traits::ShaderStage>; // GlShaderStage::Create(GL_VERTEX_SHADER)

#endif // SOLARSYSTEM_GFX_GLHANDLE_H
