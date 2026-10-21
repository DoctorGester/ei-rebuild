// The fake unit skeleton's frames made relative (0044EB60, called by 00449240 for the root bone of
// each frame and recursively for every child, children first): a bone's translation and rotation
// keys of frame `frame` become relative to its parent's, its rest offset and rotation too (frame 0
// only), and each rotation key is turned into an axis-angle step from the previous frame. Plain
// float maths (rule 7). Names provisional; see docs/notes/objects.md ("Rewritten").
#include "figure.h"
#include "maths.h"

#include "ei/hook.h"
#include "game_data.h"
#include "lib/nan_audit.h"

#include <cmath>
#include <cstdint>
#include <cstring>

Quaternion* __fastcall MultiplyByConjugate(Quaternion* out, const Quaternion& a, const Quaternion& b);   // quaternion.cpp

namespace scene {
namespace {

// The bones are CAnimationNodes (figure.h): rotation keys, translation keys, the rest rotation and
// position, and position_ (the rest position in the parent's frame).
using RotationKey = AnimationRotationKey;


Quaternion AxisAngle(const Vector3& axis, float angle) {
    Quaternion q;
    q.SetAxisAngle(axis, angle);
    return q;
}

// The axis and angle of a unit rotation: the angle 2 acos(w), the axis the vector part over its
// length; (1, 0, 0) and 0 when the squared length is at most 1e-8 (0073BAE0).
float ToAxisAngle(const Quaternion& q, Vector3* axis) {
    float squared = LengthSquaredUnrounded<Sum3::ZYX>(q.x, q.y, q.z);
    EI_NAN_AUDIT(squared);
    if (squared <= 1e-8f /*0073BAE0*/) {
        *axis = {1.0f, 0.0f, 0.0f};
        return 0.0f /*0073B858*/;
    }
    double angle = std::acos(static_cast<double>(q.w));
    angle += angle;
    double inverse = 1.0f /*0073B84C*/ / std::sqrt(static_cast<double>(squared));
    axis->x = static_cast<float>(q.x * inverse);
    axis->y = static_cast<float>(q.y * inverse);
    axis->z = static_cast<float>(q.z * inverse);
    return static_cast<float>(angle);
}

RotationKey* Keys(CAnimationNode* bone) { return bone->rotationKeys_.data; }

} // namespace

void __fastcall MakeFrameRelative(CAnimationNode* parent, CAnimationNode* bone, int frame) {
    for (int i = 0; i < bone->children_.size; ++i) MakeFrameRelative(bone, bone->children_.data[i], frame);

    RotationKey& key = Keys(bone)[frame];
    if (!parent) {
        key.rotation = AxisAngle(key.axis, -key.angle);
    } else {
        if (frame == 0) {
            // The rest offset in the parent's frame (the parent's rotation as a matrix, -2 from
            // 0073C168) and the rest rotation relative to the parent's.
            const Vector3& from = bone->restPosition_;
            const Vector3& origin = parent->restPosition_;
            Vector3 d{from.x - origin.x, from.y - origin.y, from.z - origin.z};
            bone->position_ = d;
            const Quaternion& p = parent->restRotation_;
            const float k = (-2.0f /*0073C168*/);
            const float one = 1.0f /*0073B84C*/;
            float a = p.x * k, b = p.y * k, c = p.z * k;
            float xw = a * p.w, yw = b * p.w, zw = c * p.w;
            float xx = -(a * p.x), xy = -(b * p.x), xz = -(c * p.x);
            float yy = -(b * p.y), yz = -(c * p.y), zz = -(c * p.z);
            float m00 = one - (zz + yy), m01 = xy - zw, m02 = xz + yw;
            float m10 = xy + zw, m11 = one - (zz + xx), m12 = yz - xw;
            float m20 = xz - yw, m21 = yz + xw, m22 = one - (xx + yy);
            Vector3& offset = bone->position_;
            Vector3 dd = offset;
            offset.x = DotUnrounded<Sum3::ZXY>(m00, m01, m02, dd.x, dd.y, dd.z);
            offset.y = DotUnrounded<Sum3::ZXY>(m10, m11, m12, dd.x, dd.y, dd.z);
            offset.z = DotUnrounded<Sum3::ZXY>(m20, m21, m22, dd.x, dd.y, dd.z);

            Vector3& rest = bone->restPosition_;
            rest.x = rest.x - origin.x;
            rest.y = rest.y - origin.y;
            rest.z = rest.z - origin.z;
            Quaternion own = bone->restRotation_;
            Quaternion theirs = parent->restRotation_;
            MultiplyByConjugate(&bone->restRotation_, own, theirs);
        }
        Vector3& t = bone->translationKeys_.data[frame].position;
        const Vector3& pt = parent->translationKeys_.data[frame].position;
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
        EI_NAN_AUDIT(step.w);
        if (step.w < 0.0f /*0073B858*/) {   // the shorter way round
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
EI_REWRITTEN(0x0044EB60, MakeFrameRelative);

} // namespace scene
