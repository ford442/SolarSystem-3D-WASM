#include "Sun.h"
#include "../OrbitLayout.h"
#include "../SceneScale.h"

Sun::Sun(StarInfo&& starInfo) : Star(std::move(starInfo)) {
}

void Sun::TakeStarSystemCenter() {
    // The Sun is the simulation origin; in the render frame it sits at -renderOrigin.
    Place(glm::dvec3(0.0));
    // sphere.obj has radius 2: 0.5 makes the Sun's art radius 1. Another star's is scaled by
    // SunArtRadius (exactly 1 for the Sun, so this is the same product it always was).
    Scale(glm::vec3(0.5f * SceneScale::SunArtRadius() * SizeFactor()));
}

float Sun::SizeFactor() const {
    return SceneScale::SunSizeFactor();
}

float Sun::GetSceneRadius() const {
    return SceneScale::SunRadius();
}