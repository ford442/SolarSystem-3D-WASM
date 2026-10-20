#ifndef SOLARSYSTEM_TRANSFORMABLE_H
#define SOLARSYSTEM_TRANSFORMABLE_H
#include "../gfx/Shader.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class Transformable {
public:
    Transformable() = default;
    void Translate(const glm::vec3& position);
    void Scale(const glm::vec3& scale);
    void Rotate(float angle, const glm::vec3& axisRotation);
    void LoadIdentityModelMatrix();
    const glm::mat4& GetModelMatrix() const;
    glm::mat4 GetRotationMatrix() const;
    glm::vec3 GetPosition() const;
    float GetLastRotationAngle() const;

private:
    glm::mat4 _matrixModel = glm::mat4(1.0f), _rotationMatrix = glm::mat4(1.0f); // rotationMatrix is separately needed for the correct ring normal for ray casting
    glm::vec3 _position = glm::vec3();
    float _lastRotationAngle = 0.0;

    void UpdateRotationMatrix();
};

#endif //SOLARSYSTEM_TRANSFORMABLE_H
