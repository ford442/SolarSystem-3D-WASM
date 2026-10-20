#include "Sun.h"
#include "../OrbitLayout.h"
#include "../SceneScale.h"

Sun::Sun(StarInfo&& starInfo) : Star(std::move(starInfo)) {
}

void Sun::TakeStarSystemCenter() {
    // The Sun is the simulation origin; in the render frame it sits at -renderOrigin.
    Place(glm::dvec3(0.0));
    Scale(glm::vec3(0.5f * SizeFactor()));
}

float Sun::SizeFactor() const {
    return SceneScale::SunSizeFactor();
}

float Sun::GetSceneRadius() const {
    return SceneScale::SunRadius();
}