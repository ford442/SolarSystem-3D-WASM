#ifndef SOLARSYSTEM_ATMOSPHERE_H
#define SOLARSYSTEM_ATMOSPHERE_H
#include "OuterShell.h"
#include "BodyCatalog.generated.h"

/**
 * A planet's (or Titan's) atmosphere shell, configured from its catalog row
 * (render.atmosphere in planets.catalog.json).
 *
 * Two ways to draw it:
 *  - O'Neil (`row.oneil`): the single-scattering shell every preset can afford.
 *  - Physical (`row.physical`): atmospherePbr.fs sampling transmittance and
 *    multi-scattering LUTs baked offline by tools/atmosphere_lut_baker. Used when the
 *    quality tier asks for it and both LUTs loaded; otherwise this falls back to O'Neil.
 * See docs/ARCHITECTURE.md §9.2.
 */
class Atmosphere : public OuterShell {
public:
    Atmosphere(MeshHolder model, const Shader& shader, const BodyCatalog::AtmosphereRow& row,
               std::shared_ptr<SpaceObject> parent, float parentRadius);
    ~Atmosphere() override;
    Atmosphere(const Atmosphere&) = delete;
    Atmosphere& operator=(const Atmosphere&) = delete;

    void AdjustToParent(float timeScale = 0.0f) override;

    const BodyCatalog::AtmosphereRow& GetRow() const { return *_row; }
    glm::vec3 GetAtmosphereColor() const;
    glm::vec3 GetMieTint() const;
    float GetInnerRadius() const;
    float GetOuterRadius() const;
    float GetParentRadius() const { return _parentRadius; }
    /** Distance from the centre inside which the camera is "in" the shell being drawn. */
    float GetAtmosphereOuterBoundary() const;

    /** Takes ownership of both LUT textures (0 = not loaded). */
    void SetLuts(unsigned int transmittance, unsigned int multiScattering);
    bool HasLuts() const { return _transmittanceLut != 0 && _multiScatteringLut != 0; }
    unsigned int GetTransmittanceLut() const { return _transmittanceLut; }
    unsigned int GetMultiScatteringLut() const { return _multiScatteringLut; }

    /** Physical path on/off for this frame; changes the shell size. */
    void SetPhysicalPathActive(bool active) { _physicalPathActive = active && HasLuts(); }
    bool IsPhysicalPathActive() const { return _physicalPathActive; }
    /** Ground and top radius after `thicknessScale`, in km. */
    float GetPhysicalGroundRadiusKm() const;
    float GetPhysicalTopRadiusKm() const;
    /** Scene units → km for this body (the catalog radius over the drawn radius). */
    float GetKmPerSceneUnit() const;

private:
    const BodyCatalog::AtmosphereRow* _row;
    float _parentRadius, _innerRadius;
    unsigned int _transmittanceLut = 0, _multiScatteringLut = 0;
    bool _physicalPathActive = false;

    // sphere.obj's radius. The shell mesh is this sphere scaled by the active path's factor.
    static constexpr float kShellMeshRadius = 2.0f;
    // The mesh is a tessellated sphere whose faces sit inside the analytic one; draw the
    // physical shell a little larger so no top-of-atmosphere fragment is lost at the edge.
    static constexpr float kPhysicalShellMargin = 1.02f;

    float ShellScale() const;
};

#endif //SOLARSYSTEM_ATMOSPHERE_H
