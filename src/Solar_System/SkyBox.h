#ifndef SOLARSYSTEM_SKYBOX_H
#define SOLARSYSTEM_SKYBOX_H
#include "../gfx/Shader.h"
#include "../gfx/GlHandle.h"
#include <string>
#include <vector>

class SkyBox : public IGpuRestorable {
public:
    explicit SkyBox(const std::vector<std::string>& faces);
    SkyBox(const SkyBox&) = delete;
    SkyBox& operator=(const SkyBox&) = delete;
    /** After a lost context: the cube and the cubemap again, from the faces still in MEMFS. */
    void RestoreGpu() override;
    void Render(const Shader& shader) const;

private:
    std::vector<std::string> _faces;
    GlTexture _texture;
    GlVertexArray _vao;
    GlBuffer _vbo;

    void InitBuffers();
    void LoadCubeMap(const std::vector<std::string>& faces);
};

#endif //SOLARSYSTEM_SKYBOX_H
