#include "Camera.h"

#include <cmath>

Camera::Camera(float near, float far, float movementSpeed, float mouseSensitivity, glm::dvec3 position, glm::vec3 up, float yaw, float pitch, float zoom)
    : _frontVector(glm::vec3(0.0f, 0.0f, 0.0f)), _movementSpeed(movementSpeed), _mouseSensitivity(mouseSensitivity), _zoom(zoom)
{
    _worldPosition = position;
    _worldUpVector = up;
    _yaw = yaw;
    _pitch = pitch;
    _nearPlane = near;
    _farPlane = far;
    UpdateCameraVectors();
}

void Camera::SetMovementSpeed(float speed) {
    _movementSpeed = speed;
}

void Camera::SetAspect(float aspect) {
    _aspect = aspect;
}

void Camera::SetNear(float near) {
    _nearPlane = near;
}

void Camera::SetFar(float far) {
    _farPlane = far;
}

float Camera::GetAspect() const {
    return _aspect;
}

float Camera::GetNear() const {
    return _nearPlane;
}

float Camera::GetFar() const {
    return _farPlane;
}

float Camera::GetZoom() const {
    return _zoom;
}

float Camera::GetMovementSpeed() const {
    return _movementSpeed;
}

glm::mat4 Camera::GetViewMatrix(const glm::vec3& eyeRenderPosition) const {
    return glm::lookAt(eyeRenderPosition, eyeRenderPosition + _frontVector, _upVector);
}

glm::mat4 Camera::GetProjectionMatrix() const {
    return glm::perspective(glm::radians(_zoom), _aspect, _nearPlane, _farPlane);
}

glm::vec3 Camera::GetRightVector() const {
    return _rightVector;
}

glm::vec3 Camera::GetFrontVector() const {
    return _frontVector;
}

glm::vec3 Camera::GetUpVector() const {
    return _upVector;
}

glm::vec3 Camera::GetWorldUpVector() const {
    return _worldUpVector;
}

// processes input received from any keyboard-like input system. Accepts input parameter in the form of camera defined ENUM (to abstract it from windowing systems)
void Camera::ProcessKeyboard(CameraVector direction, float deltaTime) {
    if (_movementLocked) return;
    glm::dvec3 axis(0.0);
    if (direction == CameraVector::FORWARD)
        axis = _frontVector;
    if (direction == CameraVector::BACKWARD)
        axis = -_frontVector;
    if (direction == CameraVector::LEFT)
        axis = -_rightVector;
    if (direction == CameraVector::RIGHT)
        axis = _rightVector;
    if (direction == CameraVector::UP)
        axis = _upVector;
    if (direction == CameraVector::WORLD_UP)
        axis = _worldUpVector;
    if (direction == CameraVector::WORLD_DOWN)
        axis = -_worldUpVector;
    // The float product first, as before, so compressed scale (scale 1) moves exactly as it did.
    const float velocity = _movementSpeed * deltaTime;
    _worldPosition += axis * (static_cast<double>(velocity) * _speedScale);
}

// processes input received from a mouse input system. Expects the offset value in both the x and y direction.
void Camera::ProcessMouseMovement(float xoffset, float yoffset, bool constrainPitch) {
    const float lookScale = _skyZoomReferenceFov > 0.0f ? _zoom / _skyZoomReferenceFov : 1.0f;
    xoffset *= _mouseSensitivity * lookScale;
    yoffset *= _mouseSensitivity * lookScale;

    _yaw   += xoffset;
    _pitch += yoffset;

    // make sure that when pitch is out of bounds, screen doesn't get flipped
    if (constrainPitch)
    {
        if (_pitch > 89.0f)
            _pitch = 89.0f;
        if (_pitch < -89.0f)
            _pitch = -89.0f;
    }

    // update _frontVector, _rightVector and _upVector Vectors using the updated Euler angles
    UpdateCameraVectors();
}

// processes input received from a mouse scroll-wheel event. Only requires input on the vertical wheel-axis
void Camera::ProcessMouseScroll(float yoffset) {
    if (_skyZoomReferenceFov > 0.0f) {
        // ~16% per notch: 60 deg to the 0.25 deg telescope floor in about 35 notches.
        _zoom = glm::clamp(_zoom * std::exp(-0.15f * yoffset), _zoomMin, _zoomMax);
        return;
    }
    _zoom = glm::clamp(_zoom - yoffset, _zoomMin, _zoomMax);
}

void Camera::UpdateCameraVectors() {
    glm::vec3 front;
    front.x = cos(glm::radians(_yaw)) * cos(glm::radians(_pitch));
    front.y = sin(glm::radians(_pitch));
    front.z = sin(glm::radians(_yaw)) * cos(glm::radians(_pitch));
    _frontVector = glm::normalize(front);
    _rightVector = glm::normalize(glm::cross(_frontVector, _worldUpVector));  // normalize the vectors, because their length gets closer to 0 the more you look up or down which results in slower movement.
    _upVector    = glm::normalize(glm::cross(_rightVector, _frontVector));
    if (_roll != 0.0f) {
        const glm::mat3 roll(glm::rotate(glm::mat4(1.0f), glm::radians(_roll), _frontVector));
        _rightVector = roll * _rightVector;
        _upVector = roll * _upVector;
    }
}

void Camera::Reorient(const glm::dmat3& rotation) {
    const glm::dvec3 front = glm::normalize(rotation * glm::dvec3(_frontVector));
    const glm::dvec3 up = glm::normalize(rotation * glm::dvec3(_upVector));
    // Straight up or down has no yaw of its own: keep the one it had.
    if (std::abs(front.y) < 0.999999) {
        _yaw = static_cast<float>(glm::degrees(std::atan2(front.z, front.x)));
    }
    _pitch = static_cast<float>(glm::degrees(std::asin(glm::clamp(front.y, -1.0, 1.0))));
    // Roll is the angle from the roll-free up (in the plane of front and world up), about front.
    const glm::dvec3 rightFree = glm::normalize(glm::cross(front, glm::dvec3(_worldUpVector)));
    const glm::dvec3 upFree = glm::cross(rightFree, front);
    _roll = static_cast<float>(glm::degrees(std::atan2(glm::dot(glm::cross(upFree, up), front), glm::dot(upFree, up))));
    UpdateCameraVectors();
}

void Camera::StartTransitionTo(const glm::dvec3& targetPos, float targetYaw, float targetPitch, float durationSeconds) {
    if (durationSeconds <= 0.0f) {
        _worldPosition = targetPos;
        _yaw = targetYaw;
        _pitch = targetPitch;
        UpdateCameraVectors();
        _transitionActive = false;
        return;
    }
    _transitionStartPos = _worldPosition;
    _transitionStartYaw = _yaw;
    _transitionStartPitch = _pitch;
    _transitionTargetPos = targetPos;
    _transitionTargetYaw = targetYaw;
    _transitionTargetPitch = targetPitch;
    _transitionDuration = durationSeconds;
    _transitionElapsed = 0.0f;
    _transitionActive = true;
}

void Camera::UpdateTransition(float deltaTime) {
    if (!_transitionActive) return;
    _transitionElapsed += deltaTime;
    float t = _transitionElapsed / _transitionDuration;
    if (t >= 1.0f) {
        t = 1.0f;
        _transitionActive = false;
    }
    _worldPosition = glm::mix(_transitionStartPos, _transitionTargetPos, static_cast<double>(t));
    _yaw = glm::mix(_transitionStartYaw, _transitionTargetYaw, t);
    _pitch = glm::mix(_transitionStartPitch, _transitionTargetPitch, t);
    UpdateCameraVectors();
}
