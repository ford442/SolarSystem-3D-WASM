#ifndef SOLARSYSTEM_SKYBOX_H
#define SOLARSYSTEM_SKYBOX_H
#include "../Auxiliary_Modules/Shader.h"
#include "../3rdparty/nv_dds.h"
#include "../gfx/GlHandle.h"
#include <vector>

using namespace nv_dds;

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
