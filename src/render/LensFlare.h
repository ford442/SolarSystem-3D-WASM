#ifndef SOLARSYSTEM_LENSFLARE_H
#define SOLARSYSTEM_LENSFLARE_H
#include "Shader.h"
#include "TextureImage2D.h"
#include "../gfx/GlHandle.h"
#include "../Solar_System/Star.h"
#include <vector>

struct FlareVertex {
    glm::f32vec2 position;
    glm::f32vec2 uv;
    float offset;
};

struct FlareSprite {
    bool rotate;
    float offset;
    float size;
    GLuint textureIndex;
};

struct FlaresInfo {
    GLuint spritesPerRow;
    std::vector<FlareSprite> sprites;
};

static constexpr int VERTS_PER_QUAD = 4;
static constexpr int INDICES_PER_QUAD = 6;

class LensFlare {
public:
    explicit LensFlare(const Shader& shader, TextureImage2D lensTexture, const FlaresInfo& properties);
    void Render(const glm::mat4& projection, const glm::mat4& view, const glm::vec3& center, const glm::vec3& color, float aspectRatio, float size, float intensity,
                const std::optional<RingCameraInfo>& ringCameraInfo) const;

private:
    const Shader* _lensFlareShader;
    TextureImage2D _lensTexture;
    size_t _numSprites;
    GlVertexArray _vao; // Объект вершинного массива (VAO)
    GlBuffer _vbo; // Объект вершинного буфера (VBO)
    GlBuffer _ebo; // Объект элементного буфера (EBO)
};

#endif //SOLARSYSTEM_LENSFLARE_H
