// The skeleton's pose: each bone of a figure takes its place from its parent and its animator, and places the
// mesh it carries. Called from the root (FigureAnimationState) when the animation changed or the figure was
// rescaled, with the figure's scaled time factor as the origin. See docs/notes/map_objects.md.
#include "map_object_figures.h"   // FigureBone, AnimationDatabase

#include "bone_pose.h"
#include "figure_mesh.h"          // FigureMesh
#include "map_object_animation.h" // AnimationTrack
#include "lib/containers.h"
#include "lib/vecmath.h"

namespace {

// The pose's rotation `t` times the parent's `r` (Hamilton order) with the original's sums (its w subtracts
// the vector parts' dot, z + x first).
Quaternion Combine(const Quaternion& r, const Quaternion& t) {
    Vector3 tv = {t.x, t.y, t.z};
    Vector3 rv = {r.x, r.y, r.z};
    Quaternion q;
    q.w = t.w * r.w - DotZXY(tv, rv);
    q.x = ((r.z * t.y - t.z * r.y) + t.w * r.x) + t.x * r.w;
    q.y = ((t.z * r.x - t.x * r.z) + t.w * r.y) + r.w * t.y;
    q.z = ((t.x * r.y - t.y * r.x) + t.w * r.z) + t.z * r.w;
    return q;
}

} // namespace

void FigureBone::Place(const Vector3& origin) {
    if (parent) {
        position = TurnByQuaternion(parent->rotation, offset) + parent->position;
        rotation = Combine(parent->rotation, pose->rotation);
    } else if (pose) {
        if (pose->animation) {
            float factor = AnimationDatabase::Of(pose->animation)->TimeFactor(morphU);
            position = {factor * pose->position.x, factor * pose->position.y, factor * pose->position.z};
        } else {
            position = pose->position;
        }
        rotation = pose->rotation;
        position -= origin;
    }
    if (mesh) {
        mesh->localPosition = position;
        mesh->localRotation = rotation;
        // The pose's morphed vertices ({data, count}; read even without a pose, like the original).
        FigureMesh::OffsetSpan vertices{pose->vertices.data(), static_cast<int>(lib::Count(pose->vertices))};
        mesh->SetOffsets(&vertices);
    }
    for (auto node = children.begin(); node != children.end();) {
        FigureBone* child = *node;
        ++node;
        child->Place(origin);
    }
}

// --- Map object cage ---------------------------------------------------------------------------------

// Interpolation inside a deformation cage (the figure manager's morphing): a point at parameters (t, u, w)
// between the corners of a cube (8 points) or a prism (6 points) read from a record stream: a bone's offset at
// its morph coefficients (FigureBone, map_object_figures.h); see docs/notes/map_objects.md.

namespace {
// from + (to - from) * s of two corners in the stream.
Vector3 Lerp(const float* from, const float* to, float s) {
    return Vector3{::Lerp(from[0], to[0], s), ::Lerp(from[1], to[1], s), ::Lerp(from[2], to[2], s)};
}

Vector3 Corner(const float* p) { return Vector3{p[0], p[1], p[2]}; }
} // namespace

void FigureBone::InterpolateCube(const float** stream) {
    Vector3& point = offset;
    const float t = morphS, u = morphT, w = morphU;
    const float* p = *stream;
    point = Lerp(p + 0, p + 3, t);
    point = Lerp(point, Lerp(p + 6, p + 9, t), u);
    const float* q = p + 12;
    Vector3 c = Lerp(q + 0, q + 3, t);
    Vector3 e = Lerp(q + 6, q + 9, t);
    c = Lerp(c, e, u);
    point = Lerp(point, c, w);
    *stream += 0x60 / sizeof(float);
    *stream += 0x60 / sizeof(float);
}

void FigureBone::InterpolatePrism(const float** stream) {
    Vector3& point = offset;
    const float t = morphS, u = morphT, w = morphU;
    const float* p = *stream;
    // Edge 0-1 at t, towards point 2 at u.
    point = Lerp(p + 0, p + 3, t);
    point = Lerp(point, Corner(p + 6), u);
    // Edge 3-4 at t, towards point 5 at u, then between the two at w.
    Vector3 b = Lerp(p + 9, p + 12, t);
    Vector3 f = (Corner(p + 15) - b) * u;
    point = ((f + b) - point) * w + point;
    *stream += 0x48 / sizeof(float);
}
