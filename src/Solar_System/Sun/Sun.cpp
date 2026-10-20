#include "Sun.h"
#include "../OrbitLayout.h"

Sun::Sun(StarInfo&& starInfo) : Star(std::move(starInfo)) {
}

void Sun::TakeStarSystemCenter() {
    LoadIdentityModelMatrix();
    // The Sun is the simulation origin; in the render frame it sits at -renderOrigin.
    Translate(OrbitLayout::GetRenderPosition(OrbitLayout::Body::Sun));
    Scale(glm::vec3(0.5f));
}