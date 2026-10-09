// The skeleton's pose (0050C230): each bone of a figure takes its place from its parent and its
// animator, and places the mesh it carries. Called from the root (CFigureAnimationState +0x20) by
// 0050D3E0 / 0050D450 (the animation changed) and 0050D7C0 (the figure was rescaled), with the
// figure's scaled time factor as the origin. See docs/notes/map_objects.md.
#include "map_object_figures.h"   // FigureBone, AnimationDatabase

#include "figure_mesh.h"          // CFigureMesh
#include "map_object_animation.h" // AnimationTrack
#include "bone_pose.h"

#include "ei/hook.h"
#include "lib/vecmath.h"

namespace {

// The pose's rotation `t` times the parent's `r` (Hamilton order) with the original's sums (its w
// subtracts the vector parts' dot, z + x first).
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

void FigureBone::Place(const Vector3* origin) {
    if (parent) {
        position = TurnByQuaternion(parent->rotation, offset) + parent->position;
        rotation = Combine(parent->rotation, pose->rotation);
    } else if (pose) {
        if (pose->animation) {
            double factor = AnimationDatabase::Of(pose->animation)->TimeFactor(morphU);
            position = {static_cast<float>(factor * pose->position.x), static_cast<float>(factor * pose->position.y),
                        static_cast<float>(factor * pose->position.z)};
        } else {
            position = pose->position;
        }
        rotation = pose->rotation;
        position -= *origin;
    }
    if (mesh) {
        mesh->position_ = position;
        mesh->rotation_ = rotation;
        // The pose's morphed vertices ({data, count} at +0x68; read even without a pose, as the
        // original does).
        CFigureMesh::OffsetSpan vertices{reinterpret_cast<const Vector3*>(pose->vertices), pose->verticesSize};
        mesh->SetOffsets(&vertices);
    }
    for (auto* node = children.head; node;) {
        FigureBone* child = node->data;
        node = node->next;
        child->Place(origin);
    }
}
EI_REPLACE_METHOD(0x0050C230, FigureBone::Place);
