#include "Star.h"
#include "../gfx/TextureUnits.h"

Star::Star(StarInfo&& starInfo) : SpaceObject(starInfo.starModel, starInfo.engName, starInfo.otherLangName),
    _starTemperature(starInfo.starTemperature), _starRadius(starInfo.starRadius), _glowTintMult(starInfo.glowTintMult),
    _starSpectrumTexture(std::move(starInfo.starSpectrum))
{
    InitBuffers();
#ifndef __EMSCRIPTEN__
    for (GlQuery& query : _starOcclusion) {
        query = GlQuery::Create();
    }
#endif
    _starTemperatureColorUCoordinate = glm::clamp((_starTemperature - 800.0f) / 29200.f, 0.0f, 1.0f);
    CalculateShiftColor();
}

void Star::RenderGlow(const Shader& glowShader, const glm::mat4& projection, const glm::mat4& view, const glm::vec3& vs, float aspect, float distance,
                      const std::optional<RingCameraInfo>& ringCameraInfo, float starTemperature)
{
    if (starTemperature != _starTemperature) {
        _starTemperature = starTemperature;
        _starTemperatureColorUCoordinate = glm::clamp((_starTemperature - 800.0f) / 29200.f, 0.0f, 1.0f);
        CalculateShiftColor();
    }

    CalculateGlowSize(distance);
    _currentGlowSize *= _visibility * 0.7f;
    _currentGlowSize = glm::max(0.0001f, _currentGlowSize);
    const glm::vec2 glowDimensions(_currentGlowSize, _currentGlowSize * aspect);

    glowShader.Use();
    glowShader.SetMat4("projection", projection);
    glowShader.SetMat4("view", view);
    glowShader.SetVec3("center", GetPosition());
    glowShader.SetVec3("colorMult", _glowTintMult);
    glowShader.SetVec2("dims", glowDimensions * 0.5f);
    glowShader.SetFloat("uColorMap", _starTemperatureColorUCoordinate);
    glowShader.SetFloat("noiseZ", (vs.x + vs.y - vs.z) * 0.25f);
    glBindTextureUnit(Unit(TextureUnit::Diffuse), _starSpectrumTexture.GetTexture());

    glowShader.SetBool("isPlanetaryRingInView", ringCameraInfo.has_value());
    if (ringCameraInfo) {
        glowShader.SetVec3("cameraPosition", ringCameraInfo->cameraPosition);
        glowShader.SetVec3("ringCenter", ringCameraInfo->ringCenter);
        glowShader.SetVec3("ringNormal", ringCameraInfo->ringNormal);
        glowShader.SetVec2("ringInnerOuterRadiuses", ringCameraInfo->ringInnerOuterRadiuses);
        glBindTextureUnit(Unit(TextureUnit::Ring), ringCameraInfo->ringDiffuse);
    }

    glBindVertexArray(_glowVao.get());
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, nullptr);
    glBindVertexArray(0);
}

void Star::SetVisibility(float visibility) {
    _visibility = visibility;
}

float Star::GetStarTemperatureInKelvin() const {
    return _starTemperature;
}

float Star::GetStarRadius() const {
    return _starRadius;
}

float Star::GetTemperatureColorUCoordinate() const {
    return _starTemperatureColorUCoordinate;
}

float Star::GetVisibility() const {
    return _visibility;
}

GLuint Star::GetStarSpectrumTexture() const {
    return _starSpectrumTexture.GetTexture();
}

float Star::GetCurrentGlowSize() const {
    return _currentGlowSize;
}

glm::vec3 Star::GetShiftColor() const {
    return _starShiftColor;
}

glm::vec3 Star::GetGlowTintMult() const {
    return _glowTintMult;
}

GLuint Star::GetStarOcclusionValue(size_t index) const {
    return _starOcclusion.at(index).get();
}

void Star::CalculateShiftColor() {
    _starShiftColor = glm::vec3(_starTemperature * (0.0534 / 255.0) - (43.0 / 255.0),
                                _starTemperature * (0.0628 / 255.0) - (77.0 / 255.0),
                                _starTemperature * (0.0735 / 255.0) - (115.0 / 255.0));
}

void Star::CalculateGlowSize(float distance) {
    // We approximate the luminosity of the star by comparing our temperature and diameter to that of the sun,
    // since we know the luminosity, diameter, and temperature of the sun.

    static constexpr float DSUN = 1392684.0;
    static constexpr float TSUN = 5778.0;

    float d = distance; // Distance
    float D = _starRadius * 2 * DSUN;
    float L = (D * D) * std::pow(_starTemperature / TSUN, 4.0); // Luminosity
    _currentGlowSize = 0.016 * std::pow(L, 0.25) / std::pow(d, 0.5) * 0.001667; // Size
}

void Star::DrawCoronaSlices(int count) const {
    glBindVertexArray(_glowVao.get());
    glDrawElementsInstanced(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, nullptr, count);
    glBindVertexArray(0);
}

void Star::InitBuffers() {
    constexpr glm::vec2 cPositions[4] = {
            glm::vec2(-1.0f, 1.0f),
            glm::vec2(-1.0f, -1.0f),
            glm::vec2(1.0f, -1.0f),
            glm::vec2(1.0f, 1.0f)
    };

    constexpr uint16_t cIndices[6] = {0, 1, 2, 2, 3, 0};

    _glowVao = GlVertexArray::Create();
    _glowVbo = GlBuffer::Create();
    _glowEbo = GlBuffer::Create();

    glBindVertexArray(_glowVao.get());
    glBindBuffer(GL_ARRAY_BUFFER, _glowVbo.get());

    glBufferData(GL_ARRAY_BUFFER, sizeof(cPositions), cPositions, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _glowEbo.get());
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(cIndices), cIndices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);

    glBindVertexArray(0);
}
