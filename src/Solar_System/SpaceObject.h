#ifndef SOLARSYSTEM_SPACEOBJECT_H
#define SOLARSYSTEM_SPACEOBJECT_H
#include "../astro/MagneticFieldParams.h"
#include "../gfx/MeshHolder.h"
#include "Transformable.h"
#include "../gfx/Frustum.h"

class SpaceObject : public Transformable {
public:
    explicit SpaceObject(MeshHolder model, std::wstring engName = L"", std::wstring otherLangName = L"");
    virtual ~SpaceObject() = default;
    /**
     * Draw with `shader`, which the caller has Use()d and given its pass uniforms. The
     * object adds its own (`model`, material maps) — it holds no program of its own, so the
     * shadow and colour passes draw the same object with different programs.
     */
    virtual void Draw(const Shader& shader) const;
    /** The planetLighting permutation this object draws with now (CatalogMaterial::Variant); 0 = none. */
    virtual unsigned ShaderVariant() const { return 0; }
    /**
     * Drawn size over art size: 1 on compressed scale, true radius over art radius on
     * realistic (SceneScale::BodySizeFactor). Lengths tied to a body — its shells, rings,
     * focus and LOD distances — are the art numbers times this.
     */
    virtual float SizeFactor() const { return 1.0f; }
    const MeshHolder& GetModel() const;
    /** The mesh's bounds under the current model matrix (render frame). */
    BoundingSphere GetBoundingSphere() const;
    // Frustum culling: Application::CullScene sets it once per frame (in any view); the
    // colour pass skips what no view sees. Shadow passes ignore it (a hidden moon still casts).
    void SetVisible(bool visible) { _visible = visible; }
    bool IsVisible() const { return _visible; }
    const std::wstring& GetEngName() const;
    const std::wstring& GetOtherLangName() const;

    void SetMagneticField(const MagneticFieldParams& params);
    const MagneticFieldParams& GetMagneticField() const;
    bool HasMagneticField() const;

protected:
    MeshHolder _objectModel;
    std::wstring _engName, _otherLangName;
    glm::mat4 _lightSpaceMatrix = glm::mat4(1.0);
    MagneticFieldParams _magneticField;
    bool _visible = true;
};

#endif //SOLARSYSTEM_SPACEOBJECT_H
