#ifndef SOLARSYSTEM_ASTEROIDFIELD_H
#define SOLARSYSTEM_ASTEROIDFIELD_H

#include "AsteroidOrbit.h"
#include "../Auxiliary_Modules/Shader.h"
#include <glm/glm.hpp>
#include <memory>
#include <vector>

/**
 * Procedural main-belt asteroid field + a few named comets, or (Population::Kuiper) the
 * sparse trans-Neptunian disk with no comets, drawn only in realistic scale so it never
 * piles up on Pluto's compressed art orbit.
 * Asteroids are drawn with a single glDrawElementsInstanced call.
 * Comet nuclei share the rock mesh; tails are additive billboards.
 */
class AsteroidField {
public:
    static constexpr uint32_t kDefaultSeed = AsteroidOrbit::kDefaultSeed;
    static constexpr int kMaxInstances = AsteroidOrbit::kMaxInstances;
    static constexpr int kCometCount = AsteroidOrbit::kCometCount;

    using OrbitalElements = AsteroidOrbit::OrbitalElements;

    enum class Population { MainBelt, Kuiper };
    static constexpr uint32_t kKuiperSeed = 0x6B1D2E57u;

    explicit AsteroidField(uint32_t seed = kDefaultSeed, int instanceCount = 1800,
                           Population population = Population::MainBelt);

    AsteroidField(const AsteroidField&) = delete;
    AsteroidField& operator=(const AsteroidField&) = delete;

    void SetInstanceCount(int count);
    int GetActiveInstanceCount() const { return _activeCount; }
    int GetGeneratedCount() const { return static_cast<int>(_asteroids.size()); }

    /**
     * Pose every rock and comet for `julianDate` and rebuild the instance matrices. A
     * function of the date, not an accumulator: scrubbing the date jumps the belt with the
     * planets, and calling it twice for one date (an XR frame, a scale change) moves nothing.
     */
    void Update(double julianDate);

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
    Population _population = Population::MainBelt;
    int _activeCount = 0;
    std::vector<OrbitalElements> _asteroids;
    std::vector<OrbitalElements> _comets;
    // Mean anomaly and spin phase of each body at _epochJd; Update() advances from these.
    struct Phase {
        float meanAnomalyRad;
        float spinPhaseRad;
    };
    std::vector<Phase> _asteroidEpoch;
    std::vector<Phase> _cometEpoch;
    double _epochJd = 0.0;
    std::vector<InstanceGPU> _instanceData;

    GlVertexArray _rockVao;
    GlBuffer _rockVbo;
    GlBuffer _rockEbo;
    GlBuffer _instanceVbo;
    GLsizei _rockIndexCount = 0;

    GlVertexArray _tailVao;
    GlBuffer _tailVbo;

    std::unique_ptr<Shader> _asteroidShader;
    std::unique_ptr<Shader> _cometTailShader;
};

#endif // SOLARSYSTEM_ASTEROIDFIELD_H
