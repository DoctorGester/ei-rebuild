// The fake unit skeleton's frames made relative (0044EB60, called by 00449240 for the root bone of
// each frame and recursively for every child, children first): a bone's translation and rotation
// keys of frame `frame` become relative to its parent's, its rest offset and rotation too (frame 0
// only), and each rotation key is turned into an axis-angle step from the previous frame. Plain
// float maths (rule 7). Names provisional; see docs/notes/objects.md ("Sixth round").
#include "maths.h"

#include "ei/hook.h"

#include <cmath>
#include <cstdint>
#include <cstring>

Quaternion* __fastcall MultiplyByConjugate(Quaternion* out, const Quaternion& a, const Quaternion& b);   // quaternion.cpp

namespace scene {
namespace {

template <typename T>
T& At(const void* base, std::size_t offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(const_cast<void*>(base)) + offset);
}

// Bone members (0x128 bytes; see scene_bones.cpp).
constexpr std::size_t kChildren = 0x20;       // aligned array of bone pointers (data, size)
constexpr std::size_t kRotationKeys = 0x44;   // aligned array of RotationKey
constexpr std::size_t kTranslations = 0x58;   // aligned array of 32-byte keys, the vector at +8
constexpr std::size_t kRestRotation = 0xE4;   // Quaternion
constexpr std::size_t kRestPosition = 0xF4;   // Vector3
constexpr std::size_t kParentOffset = 0x110;  // Vector3: the rest position in the parent's frame

struct RotationKey {   // 40 bytes
    std::uint32_t frame;
    std::uint32_t next;
    Quaternion rotation;   // +0x08
    Vector3 axis;          // +0x18
    float angle;           // +0x24
};
static_assert(sizeof(RotationKey) == 40);

float GameFloat(std::uintptr_t address) {
    float f;
    std::memcpy(&f, reinterpret_cast<const void*>(address), sizeof(f));
    return f;
}

Quaternion AxisAngle(const Vector3& axis, float angle) {
    Quaternion q;
    q.SetAxisAngle(axis, angle);
    return q;
}

// The axis and angle of a unit rotation: the angle 2 acos(w), the axis the vector part over its
// length; (1, 0, 0) and 0 when the squared length is at most 1e-8 (0073BAE0) or NaN.
float ToAxisAngle(const Quaternion& q, Vector3* axis) {
    float squared = q.z * q.z + q.y * q.y + q.x * q.x;
    if (!(squared > GameFloat(0x0073BAE0))) {
        *axis = {1.0f, 0.0f, 0.0f};
        return GameFloat(0x0073B858);
    }
    double angle = std::acos(static_cast<double>(q.w));
    angle += angle;
    double inverse = GameFloat(0x0073B84C) / std::sqrt(static_cast<double>(squared));
    axis->x = static_cast<float>(q.x * inverse);
    axis->y = static_cast<float>(q.y * inverse);
    axis->z = static_cast<float>(q.z * inverse);
    return static_cast<float>(angle);
}

RotationKey* Keys(char* bone) { return At<RotationKey*>(bone, kRotationKeys); }

} // namespace

void __fastcall MakeFrameRelative(char* parent, char* bone, int frame) {
    for (int i = 0; i < At<int>(bone, kChildren + 4); ++i) MakeFrameRelative(bone, At<char**>(bone, kChildren)[i], frame);

    RotationKey& key = Keys(bone)[frame];
    if (!parent) {
        key.rotation = AxisAngle(key.axis, -key.angle);
    } else {
        if (frame == 0) {
            // The rest offset in the parent's frame (the parent's rotation as a matrix, -2 from
            // 0073C168) and the rest rotation relative to the parent's.
            const Vector3& from = At<Vector3>(bone, kRestPosition);
            const Vector3& origin = At<Vector3>(parent, kRestPosition);
            Vector3 d{from.x - origin.x, from.y - origin.y, from.z - origin.z};
            At<Vector3>(bone, kParentOffset) = d;
            const Quaternion& p = At<Quaternion>(parent, kRestRotation);
            const float k = GameFloat(0x0073C168);
            const float one = GameFloat(0x0073B84C);
            float a = p.x * k, b = p.y * k, c = p.z * k;
            float xw = a * p.w, yw = b * p.w, zw = c * p.w;
            float xx = -(a * p.x), xy = -(b * p.x), xz = -(c * p.x);
            float yy = -(b * p.y), yz = -(c * p.y), zz = -(c * p.z);
            float m00 = one - (zz + yy), m01 = xy - zw, m02 = xz + yw;
            float m10 = xy + zw, m11 = one - (zz + xx), m12 = yz - xw;
            float m20 = xz - yw, m21 = yz + xw, m22 = one - (xx + yy);
            Vector3& offset = At<Vector3>(bone, kParentOffset);
            Vector3 dd = offset;
            offset.x = (m02 * dd.z + m00 * dd.x) + m01 * dd.y;
            offset.y = (m12 * dd.z + m10 * dd.x) + m11 * dd.y;
            offset.z = (m22 * dd.z + m20 * dd.x) + m21 * dd.y;

            Vector3& rest = At<Vector3>(bone, kRestPosition);
            rest.x = rest.x - origin.x;
            rest.y = rest.y - origin.y;
            rest.z = rest.z - origin.z;
            Quaternion own = At<Quaternion>(bone, kRestRotation);
            Quaternion theirs = At<Quaternion>(parent, kRestRotation);
            MultiplyByConjugate(&At<Quaternion>(bone, kRestRotation), own, theirs);
        }
        Vector3& t = At<Vector3>(At<char*>(bone, kTranslations) + frame * 32, 8);
        const Vector3& pt = At<Vector3>(At<char*>(parent, kTranslations) + frame * 32, 8);
        t.x = t.x - pt.x;
        t.y = t.y - pt.y;
        t.z = t.z - pt.z;

        RotationKey& theirs = Keys(parent)[frame];
        Quaternion qa = AxisAngle(key.axis, key.angle);
        Quaternion qb = AxisAngle(theirs.axis, theirs.angle);
        Quaternion relative;
        MultiplyByConjugate(&relative, qa, qb);
        Vector3 axis;
        float angle = ToAxisAngle(relative, &axis);
        key.rotation = AxisAngle(axis, -angle);
    }

    if (frame > 0) {
        RotationKey& previous = Keys(bone)[frame - 1];
        Quaternion before = previous.rotation;
        Quaternion now = Keys(bone)[frame].rotation;
        Quaternion step;
        MultiplyByConjugate(&step, now, before);
        if (!(step.w >= GameFloat(0x0073B858))) {   // the shorter way round (or NaN)
            step.w = -step.w;
            step.x = -step.x;
            step.y = -step.y;
            step.z = -step.z;
        }
        Vector3 axis;
        float angle = ToAxisAngle(step, &axis);
        previous.axis = axis;
        previous.angle = angle;
    }
}
EI_REPLACE(0x0044EB60, MakeFrameRelative);

} // namespace scene
