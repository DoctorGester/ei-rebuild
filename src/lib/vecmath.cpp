// Quaternion's out-of-line members (vecmath.h): identity, products, rotation from an axis and an angle, spherical
// interpolation and the rotation matrix.
#include "vecmath.h"

#include "maths.h"

Quaternion* Quaternion::SetIdentity() {
    w = 1.0f;
    x = 0.0f;
    y = 0.0f;
    z = 0.0f;
    return this;
}

void Quaternion::PreMultiply(const Quaternion& r) { *this = r * *this; }

namespace {

// Below this 1 - cos interpolates linearly.
constexpr float kSlerpLinear = 0.0001f;

} // namespace

// *this = slerp(a, b, t) along the shorter arc (b negated when the dot product is negative); nearly equal
// rotations interpolate linearly.
void Quaternion::Slerp(float t, const Quaternion& from, const Quaternion& to) {
    float cosine = Dot(from, to);
    Quaternion c = to;
    if (cosine < 0.0f) {
        cosine = -cosine;
        c.x = -to.x;
        c.y = -to.y;
        c.z = -to.z;
        c.w = -to.w;
    }
    float weightA;
    float weightB;
    if (1.0f - cosine > kSlerpLinear) {
        const float angle = lib::Acos(cosine);
        const float inverseSine = 1.0f / lib::Sin(angle);
        weightA = lib::Sin((1.0f - t) * angle) * inverseSine;
        weightB = lib::Sin(angle * t) * inverseSine;
    } else {   // also NaN
        weightA = 1.0f - t;
        weightB = t;
    }
    x = weightA * from.x + weightB * c.x;
    y = weightA * from.y + weightB * c.y;
    z = weightA * from.z + weightB * c.z;
    w = weightA * from.w + weightB * c.w;
}

// The half angle's sine over the axis's length and its cosine.
Quaternion Quaternion::AxisAngle(const Vector3& axis, float angle) {
    float half = angle * 0.5f;
    float s = lib::Sin(half) / Length(axis);
    return {lib::Cos(half), s * axis.x, s * axis.y, s * axis.z};
}

void Quaternion::ToMatrix(float* out) const {
    // The unit quaternion's rotation matrix, from twice the vector part.
    const float x2 = x + x, y2 = y + y, z2 = z + z;
    const float wx = x2 * w, wy = y2 * w, wz = z2 * w;
    const float xx = x2 * x, xy = y2 * x, xz = z2 * x;
    const float yy = y2 * y, yz = z2 * y, zz = z2 * z;
    out[0] = 1.0f - (yy + zz);
    out[1] = xy - wz;
    out[2] = xz + wy;
    out[4] = xy + wz;
    out[5] = 1.0f - (xx + zz);
    out[6] = yz - wx;
    out[8] = xz - wy;
    out[9] = yz + wx;
    out[10] = 1.0f - (xx + yy);
}
