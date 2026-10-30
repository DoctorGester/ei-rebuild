#include "object_maths.h"

#include "core_maths.h"

#include <cstdint>
#include <cstring>

void Vector3::DivideInPlace(float d) {
    *this /= d;
}
