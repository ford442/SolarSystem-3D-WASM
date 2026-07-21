#include "AsteroidField.h"

#include "OrbitLayout.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <glm/gtc/matrix_transform.hpp>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {

constexpr float kTwoPi = static_cast<float>(2.0 * M_PI);

class Rng {
public:
    explicit Rng(uint32_t seed) : _state(seed ? seed : 1u) {}

    uint32_t NextU32() {
        _state = _state * 1664525u + 1013904223u;
        return _state;
    }

    float Next01() {
        return static_cast<float>((NextU32() >> 8) & 0xFFFFFFu) / static_cast<float>(0x1000000u);
    }

    float NextRange(float lo, float hi) {
        return lo + (hi - lo) * Next01();
    }

private:
    uint32_t _state;
};

} // namespace

AsteroidField::AsteroidField(uint32_t seed, int instanceCount)
    : _seed(seed)
{
    // Always generate the full pool so quality presets can raise the draw count later.
    _asteroids = AsteroidOrbit::GenerateBelt(seed, kMaxInstances);
    _comets = AsteroidOrbit::GenerateComets();
    _activeCount = std::clamp(instanceCount, 0, static_cast<int>(_asteroids.size()));
    _instanceData.resize(static_cast<size_t>(kMaxInstances));

    InitRockMesh();
    InitInstanceBuffer();
    InitCometTailMesh();
    InitShaders();
    RebuildInstanceBuffer();
    UploadInstances();

    std::cout << "[AsteroidField] seed=0x" << std::hex << seed << std::dec
              << " asteroids=" << _activeCount << "/" << _asteroids.size()
              << " comets=" << _comets.size() << std::endl;
}

AsteroidField::~AsteroidField() {
    if (_instanceVbo) {
        glDeleteBuffers(1, &_instanceVbo);
    }
    if (_rockEbo) {
        glDeleteBuffers(1, &_rockEbo);
    }
    if (_rockVbo) {
        glDeleteBuffers(1, &_rockVbo);
    }
    if (_rockVao) {
        glDeleteVertexArrays(1, &_rockVao);
    }
    if (_tailVbo) {
        glDeleteBuffers(1, &_tailVbo);
    }
    if (_tailVao) {
        glDeleteVertexArrays(1, &_tailVao);
    }
}

void AsteroidField::SetInstanceCount(int count) {
    count = std::clamp(count, 0, static_cast<int>(_asteroids.size()));
    if (count == _activeCount) {
        return;
    }
    _activeCount = count;
    RebuildInstanceBuffer();
    UploadInstances();
}

void AsteroidField::InitRockMesh() {
    struct Vert {
        glm::vec3 p;
        glm::vec3 n;
    };

    const glm::vec3 corners[6] = {
        {0, 1, 0}, {0, -1, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}
    };
    const int faces[8][3] = {
        {0, 2, 4}, {0, 4, 3}, {0, 3, 5}, {0, 5, 2},
        {1, 4, 2}, {1, 3, 4}, {1, 5, 3}, {1, 2, 5}
    };

    Rng rng(_seed ^ 0x5F3759DFu);
    std::vector<Vert> verts;
    std::vector<unsigned int> indices;
    verts.reserve(24);
    indices.reserve(24);

    for (const auto& face : faces) {
        glm::vec3 tri[3];
        for (int k = 0; k < 3; ++k) {
            glm::vec3 p = corners[face[k]];
            p += glm::vec3(rng.NextRange(-0.18f, 0.18f),
                           rng.NextRange(-0.18f, 0.18f),
                           rng.NextRange(-0.18f, 0.18f));
            p = glm::normalize(p) * rng.NextRange(0.75f, 1.15f);
            tri[k] = p;
        }
        const glm::vec3 n = glm::normalize(glm::cross(tri[1] - tri[0], tri[2] - tri[0]));
        const unsigned int base = static_cast<unsigned int>(verts.size());
        for (int k = 0; k < 3; ++k) {
            verts.push_back({tri[k], n});
            indices.push_back(base + static_cast<unsigned int>(k));
        }
    }

    _rockIndexCount = static_cast<GLsizei>(indices.size());

    struct Packed {
        float px, py, pz;
        float nx, ny, nz;
    };
    std::vector<Packed> packed(verts.size());
    for (size_t i = 0; i < verts.size(); ++i) {
        packed[i] = {verts[i].p.x, verts[i].p.y, verts[i].p.z,
                     verts[i].n.x, verts[i].n.y, verts[i].n.z};
    }

    glGenVertexArrays(1, &_rockVao);
    glGenBuffers(1, &_rockVbo);
    glGenBuffers(1, &_rockEbo);
    glBindVertexArray(_rockVao);

    glBindBuffer(GL_ARRAY_BUFFER, _rockVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(packed.size() * sizeof(Packed)),
                 packed.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _rockEbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(unsigned int)),
                 indices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Packed), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Packed),
                          reinterpret_cast<void*>(offsetof(Packed, nx)));

    glBindVertexArray(0);
}

void AsteroidField::InitInstanceBuffer() {
    glGenBuffers(1, &_instanceVbo);
    glBindVertexArray(_rockVao);
    glBindBuffer(GL_ARRAY_BUFFER, _instanceVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(kMaxInstances * sizeof(InstanceGPU)),
                 nullptr, GL_DYNAMIC_DRAW);

    const GLsizei stride = static_cast<GLsizei>(sizeof(InstanceGPU));
    for (int i = 0; i < 4; ++i) {
        glEnableVertexAttribArray(2 + i);
        glVertexAttribPointer(2 + i, 4, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<void*>(sizeof(float) * 4 * i));
        glVertexAttribDivisor(2 + i, 1);
    }
    glEnableVertexAttribArray(6);
    glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(offsetof(InstanceGPU, color)));
    glVertexAttribDivisor(6, 1);

    glBindVertexArray(0);
}

void AsteroidField::InitCometTailMesh() {
    const float quad[] = {
        -0.5f, 0.0f, 0.0f, 0.0f,
         0.5f, 0.0f, 1.0f, 0.0f,
         0.5f, 1.0f, 1.0f, 1.0f,
        -0.5f, 0.0f, 0.0f, 0.0f,
         0.5f, 1.0f, 1.0f, 1.0f,
        -0.5f, 1.0f, 0.0f, 1.0f,
    };

    glGenVertexArrays(1, &_tailVao);
    glGenBuffers(1, &_tailVbo);
    glBindVertexArray(_tailVao);
    glBindBuffer(GL_ARRAY_BUFFER, _tailVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          reinterpret_cast<void*>(2 * sizeof(float)));
    glBindVertexArray(0);
}

void AsteroidField::InitShaders() {
    _asteroidShader = std::make_unique<Shader>("resource/shaders/asteroidField.vs",
                                               "resource/shaders/asteroidField.fs");
    _cometTailShader = std::make_unique<Shader>("resource/shaders/cometTail.vs",
                                                "resource/shaders/cometTail.fs");
}

void AsteroidField::RebuildInstanceBuffer() {
    for (int i = 0; i < _activeCount; ++i) {
        const OrbitalElements& el = _asteroids[static_cast<size_t>(i)];
        const glm::vec3 pos = AsteroidOrbit::HeliocentricPosition(el);
        glm::mat4 model(1.0f);
        model = glm::translate(model, pos);
        model = glm::rotate(model, el.spinPhaseRad, glm::vec3(0.3f, 1.0f, 0.2f));
        model = glm::scale(model, glm::vec3(el.visualRadius));
        _instanceData[static_cast<size_t>(i)].model = model;
        _instanceData[static_cast<size_t>(i)].color = glm::vec4(el.color, 1.0f);
    }
}

void AsteroidField::UploadInstances() const {
    if (_instanceVbo == 0 || _activeCount <= 0) {
        return;
    }
    glBindBuffer(GL_ARRAY_BUFFER, _instanceVbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    static_cast<GLsizeiptr>(static_cast<size_t>(_activeCount) * sizeof(InstanceGPU)),
                    _instanceData.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void AsteroidField::Update(float simDeltaSeconds) {
    if (simDeltaSeconds != 0.0f) {
        for (auto& el : _asteroids) {
            el.meanAnomalyRad = std::fmod(el.meanAnomalyRad + el.meanMotionRadPerSec * simDeltaSeconds, kTwoPi);
            el.spinPhaseRad = std::fmod(el.spinPhaseRad + el.spinRateRadPerSec * simDeltaSeconds, kTwoPi);
        }
        for (auto& el : _comets) {
            el.meanAnomalyRad = std::fmod(el.meanAnomalyRad + el.meanMotionRadPerSec * simDeltaSeconds, kTwoPi);
            el.spinPhaseRad = std::fmod(el.spinPhaseRad + el.spinRateRadPerSec * simDeltaSeconds, kTwoPi);
        }
    }

    RebuildInstanceBuffer();
    UploadInstances();
}

void AsteroidField::Render(const glm::mat4& projection, const glm::mat4& view,
                           const glm::vec3& sunPos, const glm::vec3& viewPos,
                           const glm::vec3& cameraRight, const glm::vec3& /*cameraUp*/,
                           float zCoef) const {
    if (!_asteroidShader || _rockVao == 0) {
        return;
    }

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    _asteroidShader->Use();
    _asteroidShader->SetMat4("projection", projection);
    _asteroidShader->SetMat4("view", view);
    _asteroidShader->SetVec3("lightPos", sunPos);
    _asteroidShader->SetVec3("viewPos", viewPos);
    _asteroidShader->SetFloat("zCoef", zCoef);
    _asteroidShader->SetFloat("ambientFactor", 0.12f);

    if (_activeCount > 0) {
        glBindVertexArray(_rockVao);
        glDrawElementsInstanced(GL_TRIANGLES, _rockIndexCount, GL_UNSIGNED_INT, nullptr, _activeCount);
        glBindVertexArray(0);
    }

    for (const auto& comet : _comets) {
        const glm::vec3 pos = AsteroidOrbit::HeliocentricPosition(comet);
        glm::mat4 model(1.0f);
        model = glm::translate(model, pos);
        model = glm::rotate(model, comet.spinPhaseRad, glm::vec3(0.2f, 1.0f, 0.1f));
        model = glm::scale(model, glm::vec3(comet.visualRadius));

        InstanceGPU one{model, glm::vec4(comet.color, 1.0f)};
        glBindBuffer(GL_ARRAY_BUFFER, _instanceVbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(InstanceGPU), &one);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        glBindVertexArray(_rockVao);
        glDrawElementsInstanced(GL_TRIANGLES, _rockIndexCount, GL_UNSIGNED_INT, nullptr, 1);
        glBindVertexArray(0);
    }

    if (_activeCount > 0) {
        UploadInstances();
    }

    if (_cometTailShader && _tailVao != 0 && !_comets.empty()) {
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        glDisable(GL_CULL_FACE);

        _cometTailShader->Use();
        _cometTailShader->SetMat4("projection", projection);
        _cometTailShader->SetMat4("view", view);
        _cometTailShader->SetVec3("lightPos", sunPos);
        _cometTailShader->SetVec3("cameraRight", cameraRight);
        _cometTailShader->SetFloat("zCoef", zCoef);

        glBindVertexArray(_tailVao);
        for (const auto& comet : _comets) {
            const glm::vec3 pos = AsteroidOrbit::HeliocentricPosition(comet);
            const float e = std::clamp(comet.eccentricity, 0.0f, 0.999f);
            const float rAuExact = AsteroidOrbit::CurrentRadiusAu(comet);
            const float peri = comet.semiMajorAu * (1.0f - e);
            const float apo = comet.semiMajorAu * (1.0f + e);
            const float span = std::max(apo - peri, 0.05f);
            const float perihelionFactor = std::clamp(1.0f - (rAuExact - peri) / (span * 0.35f), 0.0f, 1.0f);

            if (perihelionFactor < 0.02f) {
                continue;
            }

            const float tailLength = (8.0f + comet.visualRadius * 12.0f) * perihelionFactor * perihelionFactor;
            const float tailWidth = (1.2f + comet.visualRadius) * (0.35f + 0.65f * perihelionFactor);

            _cometTailShader->SetVec3("nucleusPos", pos);
            _cometTailShader->SetVec3("tailColor", comet.color);
            _cometTailShader->SetFloat("tailLength", tailLength);
            _cometTailShader->SetFloat("tailWidth", tailWidth);
            _cometTailShader->SetFloat("tailOpacity", 0.55f * perihelionFactor);

            glDrawArrays(GL_TRIANGLES, 0, 6);
        }
        glBindVertexArray(0);

        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glEnable(GL_CULL_FACE);
    }
}
