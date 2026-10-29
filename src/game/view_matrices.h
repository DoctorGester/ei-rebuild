#pragma once

// The view matrix of the 3D transform library side. Matrices are row-major 4x4
// float arrays (translation in column 3). See docs/notes/script.md ("Leftovers").

#include "maths.h"

#include <cstdint>

namespace view {

// The view matrix of a camera at `position` turned by `rotation` (the world's draw passes the pose it shows,
// camera::DrawnView): rows are the rotation's X, Y and Z axes, column 3 minus the position projected on each.
void ViewMatrix(float* out, const Vector3& position, const Quaternion& rotation);

} // namespace view
