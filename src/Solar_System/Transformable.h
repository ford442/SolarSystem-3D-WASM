#ifndef SOLARSYSTEM_TRANSFORMABLE_H
#define SOLARSYSTEM_TRANSFORMABLE_H
#include "../gfx/Shader.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class Transformable {
public:
    Transformable() = default;
    /**
     * Start a fresh model matrix at a heliocentric scene position, kept in double for
     * GetWorldPosition; the matrix translates by its render-frame offset
     * (OrbitLayout::ToRenderFrame, subtracted in double). Scale/Rotate follow.
     */
    void Place(const glm::dvec3& worldPosition);
    /**
     * After the render origin moved: re-derive the translation from the world position.
     * Place translates before any Scale/Rotate, so the translation is the matrix's last
     * column and nothing else changes.
     */
    void Rebase();
    void Translate(const glm::vec3& position);
    void Scale(const glm::vec3& scale);
    void Rotate(float angle, const glm::vec3& axisRotation);
    void LoadIdentityModelMatrix();
    const glm::mat4& GetModelMatrix() const;
    glm::mat4 GetRotationMatrix() const;
    /** Render-frame position (float): what the model matrix translates by. */
    glm::vec3 GetPosition() const;
    /** Heliocentric scene position (double) from the last Place(). */
    const glm::dvec3& GetWorldPosition() const;
    float GetLastRotationAngle() const;

private:
    glm::mat4 _matrixModel = glm::mat4(1.0f), _rotationMatrix = glm::mat4(1.0f); // rotationMatrix is separately needed for the correct ring normal for ray casting
    glm::vec3 _position = glm::vec3();
    glm::dvec3 _worldPosition = glm::dvec3(0.0);
    float _lastRotationAngle = 0.0;

    void UpdateRotationMatrix();
};

#endif //SOLARSYSTEM_TRANSFORMABLE_H
