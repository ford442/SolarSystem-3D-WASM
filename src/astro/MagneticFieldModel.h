#pragma once

#include "MagneticFieldParams.h"
#include <glm/mat3x3.hpp>
#include <glm/vec3.hpp>

/**
 * Pure magnetic-field math. No GL.
 *
 * Positions are in unit magnetic space: body radius = 1, magnetic axis = +Y.
 * World pose, body radius, and dipoleTiltDeg are applied by the renderer
 * model matrix, not here.
 */
namespace MagneticFieldModel {

/** Dipole + optional toroidal B at a point in magnetic space. */
glm::vec3 SampleField(const glm::vec3& magneticPos, const MagneticFieldParams& params);

/**
 * Body-frame-from-magnetic-frame rotation about +X by dipoleTiltDeg.
 * Matches Renderer::RenderMagneticFields (glm::rotate(..., tilt, X)).
 * TiltMatrix(90°) maps +Y to +Z.
 */
glm::mat3 TiltMatrix(float dipoleTiltDeg);

/**
 * Body frame from magnetic frame: the tilt, then poleAzimuthDeg about the spin axis (+Y).
 * DipoleFrame(params) * (0, 1, 0) is the magnetic north pole in the body frame; the field
 * lines (RenderMagneticFields), the auroral ovals and the solar-wind particles all use it.
 */
glm::mat3 DipoleFrame(const MagneticFieldParams& params);

} // namespace MagneticFieldModel
