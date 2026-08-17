#include "SpaceObject.h"

SpaceObject::SpaceObject(MeshHolder model, const Shader& shader, std::wstring engName, std::wstring otherLangName) : Transformable(shader),
    _objectModel(std::move(model)), _engName(std::move(engName)), _otherLangName(std::move(otherLangName))
{
}

void SpaceObject::Render() const {
    _objectModel.Draw(*_shader);
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
