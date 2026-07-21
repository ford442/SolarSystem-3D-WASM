#ifndef SOLARSYSTEM_ASTEROIDFIELD_H
#define SOLARSYSTEM_ASTEROIDFIELD_H

#include "AsteroidOrbit.h"
#include "../Auxiliary_Modules/Shader.h"
#include <glm/glm.hpp>
#include <memory>
#include <vector>

/**
 * Procedural main-belt asteroid field + a few named comets.
 * Asteroids are drawn with a single glDrawElementsInstanced call.
 * Comet nuclei share the rock mesh; tails are additive billboards.
 */
class AsteroidField {
public:
    static constexpr uint32_t kDefaultSeed = AsteroidOrbit::kDefaultSeed;
    static constexpr int kMaxInstances = AsteroidOrbit::kMaxInstances;
    static constexpr int kCometCount = AsteroidOrbit::kCometCount;

    using OrbitalElements = AsteroidOrbit::OrbitalElements;

    explicit AsteroidField(uint32_t seed = kDefaultSeed, int instanceCount = 1800);
    ~AsteroidField();

    AsteroidField(const AsteroidField&) = delete;
    AsteroidField& operator=(const AsteroidField&) = delete;

    void SetInstanceCount(int count);
    int GetActiveInstanceCount() const { return _activeCount; }
    int GetGeneratedCount() const { return static_cast<int>(_asteroids.size()); }

    /** Advance mean anomalies / spin; rebuild instance matrices. */
    void Update(float simDeltaSeconds);

    /**
     * Draw belt (one instanced call) then comet nuclei + tails.
     * No self-shadowing — depth writes enabled for rock meshes only.
     */
    void Render(const glm::mat4& projection, const glm::mat4& view,
                const glm::vec3& sunPos, const glm::vec3& viewPos,
                const glm::vec3& cameraRight, const glm::vec3& cameraUp,
                float zCoef) const;

    const std::vector<OrbitalElements>& GetAsteroids() const { return _asteroids; }
    const std::vector<OrbitalElements>& GetComets() const { return _comets; }

private:
    struct InstanceGPU {
        glm::mat4 model;
        glm::vec4 color;
    };

    void InitRockMesh();
    void InitInstanceBuffer();
    void InitCometTailMesh();
    void InitShaders();
    void RebuildInstanceBuffer();
    void UploadInstances() const;

    uint32_t _seed = kDefaultSeed;
    int _activeCount = 0;
    std::vector<OrbitalElements> _asteroids;
    std::vector<OrbitalElements> _comets;
    std::vector<InstanceGPU> _instanceData;

    GLuint _rockVao = 0;
    GLuint _rockVbo = 0;
    GLuint _rockEbo = 0;
    GLuint _instanceVbo = 0;
    GLsizei _rockIndexCount = 0;

    GLuint _tailVao = 0;
    GLuint _tailVbo = 0;

    std::unique_ptr<Shader> _asteroidShader;
    std::unique_ptr<Shader> _cometTailShader;
};

#endif // SOLARSYSTEM_ASTEROIDFIELD_H
