#ifndef SOLARSYSTEM_SKYBOX_H
#define SOLARSYSTEM_SKYBOX_H
#include "../gfx/Shader.h"
#include "../gfx/GlHandle.h"
#include <string>
#include <vector>

class SkyBox {
public:
    explicit SkyBox(const std::vector<std::string>& faces);
    void Render(const Shader& shader) const;

private:
    GlTexture _texture;
    GlVertexArray _vao;
    GlBuffer _vbo;

    void InitBuffers();
    void LoadCubeMap(const std::vector<std::string>& faces);
};

#endif //SOLARSYSTEM_SKYBOX_H
