#ifndef SOLARSYSTEM_CAMERA_H
#define SOLARSYSTEM_CAMERA_H
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

enum class CameraVector {
    FORWARD,
    BACKWARD,
    LEFT,
    RIGHT,
    UP,
    WORLD_UP,
    WORLD_DOWN
};

class Camera {
public:
    Camera(float near, float far, float movementSpeed, float mouseSensitivity, glm::vec3 position = glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f),
           float yaw = -90.0f, float pitch = 0.0f, float zoom = 45.0f);
    void SetMovementSpeed(float speed);
    void SetAspect(float aspect);
    void SetNear(float near);
    void SetFar(float far);
    float GetAspect() const;
    float GetNear() const;
    float GetFar() const;
    float GetZoom() const;
    float GetMovementSpeed() const;
    glm::mat4 GetViewMatrix() const;
    glm::mat4 GetProjectionMatrix() const;
    glm::vec3 GetPosition() const;
    float GetYaw() const { return _yaw; }
    float GetPitch() const { return _pitch; }
    glm::vec3 GetRightVector() const;
    glm::vec3 GetFrontVector() const;
    glm::vec3 GetUpVector() const;
    glm::vec3 GetWorldUpVector() const;

    void SetPosition(const glm::vec3& position) { _position = position; }
    void SetYawPitch(float yaw, float pitch) { _yaw = yaw; _pitch = pitch; UpdateCameraVectors(); }

    // Processes input received from any keyboard-like input system. Accepts input parameter in the form of camera defined ENUM (to abstract it from windowing systems)
    void ProcessKeyboard(CameraVector direction, float deltaTime);

    // Processes input received from a mouse input system. Expects the offset value in both the x and y direction.
    void ProcessMouseMovement(float xoffset, float yoffset, bool constrainPitch = true);

    // Processes input received from a mouse scroll-wheel event. Only requires input on the vertical wheel-axis
    void ProcessMouseScroll(float yoffset);

    // Starts a smooth transition to target position and orientation over given seconds
    void StartTransitionTo(const glm::vec3& targetPos, float targetYaw, float targetPitch, float durationSeconds = 2.0f);

    // Updates any active smooth transition (call every frame with deltaTime)
    void UpdateTransition(float deltaTime);
private:
    glm::vec3 _position;
    glm::vec3 _frontVector;
    glm::vec3 _upVector;
    glm::vec3 _rightVector;
    glm::vec3 _worldUpVector;
    float _yaw;
    float _pitch;
    float _movementSpeed;
    float _mouseSensitivity;
    float _zoom;
    float _nearPlane, _farPlane, _aspect;

    // Smooth transition state
    bool _transitionActive = false;
    glm::vec3 _transitionStartPos{0};
    float _transitionStartYaw = 0, _transitionStartPitch = 0;
    glm::vec3 _transitionTargetPos{0};
    float _transitionTargetYaw = 0, _transitionTargetPitch = 0;
    float _transitionDuration = 2.0f;
    float _transitionElapsed = 0.0f;

    void UpdateCameraVectors();
};

#endif //SOLARSYSTEM_CAMERA_H
