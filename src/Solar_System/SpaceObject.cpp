#include "SpaceObject.h"
#include <algorithm>

SpaceObject::SpaceObject(MeshHolder model, std::wstring engName, std::wstring otherLangName) :
    _objectModel(std::move(model)), _engName(std::move(engName)), _otherLangName(std::move(otherLangName))
{
}

void SpaceObject::Draw(const Shader& shader) const {
    // `model` is uploaded here, at draw time, rather than when AdjustToParent builds the
    // matrix: placement runs for every component before any pass binds a program.
    shader.SetMat4("model", GetModelMatrix());
    _objectModel.Draw(shader);
}

BoundingSphere SpaceObject::GetBoundingSphere() const {
    const glm::mat4& model = GetModelMatrix();
    const float scale = std::max({glm::length(glm::vec3(model[0])), glm::length(glm::vec3(model[1])),
                                  glm::length(glm::vec3(model[2]))});
    return {glm::vec3(model[3]), _objectModel.BoundingRadius() * scale};
}

const MeshHolder& SpaceObject::GetModel() const {
    return _objectModel;
}

const std::wstring& SpaceObject::GetEngName() const {
    return _engName;
}

const std::wstring& SpaceObject::GetOtherLangName() const {
    return _otherLangName;
}

void SpaceObject::SetMagneticField(const MagneticFieldParams& params) {
    _magneticField = params;
}

const MagneticFieldParams& SpaceObject::GetMagneticField() const {
    return _magneticField;
}

bool SpaceObject::HasMagneticField() const {
    return _magneticField.enabled;
}
