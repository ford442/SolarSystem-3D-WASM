#include "AuroraShell.h"
#include "BodyCatalog.h"
#include "BodyShape.h"
#include "OrbitLayout.h"
#include "OuterShell.h"
#include "Planet.h"
#include "SceneScale.h"
#include "../gfx/MeshHolder.h"
#include <algorithm>
#include <glm/geometric.hpp>
#include <memory>
#include <utility>

AuroraShell::AuroraShell(MeshHolder model, std::shared_ptr<Planet> planet, const BodyCatalog::Entry& parentEntry,
                         const BodyCatalog::AuroraRow& row)
    : OuterShell(std::move(model), planet, 1.0f), _planet(std::move(planet)), _parentEntry(parentEntry), _row(row),
      _body(static_cast<OrbitLayout::Body>(parentEntry.index)) {
    const float radiusKm = std::max(parentEntry.radiusKm, 1.0f);
    _bottomRatio = 1.0f + row.bottomKm / radiusKm;
    _topRatio = 1.0f + row.topKm / radiusKm;
}

void AuroraShell::AdjustToParent() {
    Place(_parent->GetWorldPosition());
    // sphere.obj has radius kMeshRadius; the shell's is the top of the layer, a little more
    // so the mesh's flat facets stay outside the sphere the fragments march.
    Scale(glm::vec3(_planet->GetRadius() * _topRatio * 1.01f / SceneScale::kMeshRadius));
    Rotate(_parentEntry.axialTiltDegrees, glm::vec3(0.0f, 0.0f, 1.0f));
    if (_parentEntry.artTiltXDegrees != 0.0f) {
        Rotate(_parentEntry.artTiltXDegrees, glm::vec3(1.0f, 0.0f, 0.0f));
    }
    if (BodyShape::IsOblate(_parentEntry)) {
        Scale(BodyShape::PolarScale(_parentEntry));
    }
}

glm::mat3 AuroraShell::ToSphere() const {
    return BodyShape::BodyToSphere(_parentEntry) * (1.0f / std::max(_planet->GetRadius(), 1.0e-9f));
}

bool AuroraShell::Contains(const glm::vec3& point) const {
    return glm::length(ToSphere() * (point - _planet->GetPosition())) <= _topRatio * 1.01f;
}
