// The skeleton's pose: each bone of a figure takes its place from its parent and its animator, and places the
// mesh it carries. Called from the root (FigureAnimationState) when the animation changed or the figure was
// rescaled, with the figure's scaled time factor as the origin. See docs/notes/map_objects.md.
#include "map_object_figures.h"   // FigureBone, AnimationDatabase

#include "figure_mesh.h"          // FigureMesh
#include "map_object_animation.h" // AnimationTrack
#include "lib/containers.h"
#include "lib/vecmath.h"

namespace {

} // namespace

void FigureBone::Place(const Vector3& origin, float blend) {
    const bool blended = blend < 1.0f && blendFrom;
    if (parent) {
        position = parent->rotation.Rotate(offset) + parent->position;
        Quaternion turn = pose->rotation;
        if (blended) turn.Slerp(blend, fromTurn, pose->rotation);
        rotation = turn * parent->rotation;
        shownTurn = turn;
        shown = true;
    } else if (pose) {
        Vector3 at;
        if (pose->animation) {
            float factor = pose->animation->database->TimeFactor(morphU);
            at = {factor * pose->position.x, factor * pose->position.y, factor * pose->position.z};
        } else {
            at = pose->position;
        }
        Quaternion turn = pose->rotation;
        if (blended) {
            at = Lerp(fromPosition, at, blend);
            turn.Slerp(blend, fromTurn, pose->rotation);
        }
        rotation = turn;
        position = at - origin;
        shownTurn = turn;
        shownPosition = at;
        shown = true;
    }
    if (mesh) {
        mesh->localPosition = position;
        mesh->localRotation = rotation;
        // The pose's morphed vertices ({data, count}; read even without a pose, like the original), within a
        // blend lerped from the outgoing ones when both have as many.
        const std::vector<Vector3>* offsets = &pose->vertices;
        if (blended && !fromOffsets.empty() && fromOffsets.size() == pose->vertices.size()) {
            blendedOffsets.resize(fromOffsets.size());
            for (std::size_t i = 0; i < fromOffsets.size(); ++i)
                blendedOffsets[i] = Lerp(fromOffsets[i], pose->vertices[i], blend);
            offsets = &blendedOffsets;
        }
        FigureMesh::OffsetSpan vertices{offsets->data(), static_cast<int>(lib::Count(*offsets))};
        mesh->SetOffsets(&vertices);
    }
    for (auto node = children.begin(); node != children.end();) {
        FigureBone* child = *node;
        ++node;
        child->Place(origin, blend);
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
