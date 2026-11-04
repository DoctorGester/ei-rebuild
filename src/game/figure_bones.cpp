// The skeleton's pose: each bone of a figure takes its place from its parent and its animator, and places the
// mesh it carries. Called from the root (FigureAnimationState) when the animation changed or the figure was
// rescaled, with the figure's scaled time factor as the origin. See docs/notes/map_objects.md.
#include "map_object_figures.h"   // FigureBone, AnimationDatabase

#include "figure_mesh.h"          // FigureMesh
#include "map_object_animation.h" // AnimationTrack
#include "lib/containers.h"
#include "lib/vecmath.h"

void FigureBone::Place(const Vector3& origin, float blend) {
    const bool blended = blend < 1.0f && blendFrom;
    if (parent && pose) {
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
        // The pose's morphed vertices (none without a pose), within a blend lerped from the outgoing ones when
        // both have as many.
        static const std::vector<Vector3> kNoOffsets;
        const std::vector<Vector3>& poseOffsets = pose ? pose->vertices : kNoOffsets;
        const std::vector<Vector3>* offsets = &poseOffsets;
        if (blended && !fromOffsets.empty() && fromOffsets.size() == poseOffsets.size()) {
            blendedOffsets.resize(fromOffsets.size());
            for (std::size_t i = 0; i < fromOffsets.size(); ++i)
                blendedOffsets[i] = Lerp(fromOffsets[i], poseOffsets[i], blend);
            offsets = &blendedOffsets;
        }
        FigureMesh::OffsetSpan vertices{offsets->data(), static_cast<int>(lib::Count(*offsets))};
        mesh->SetOffsets(&vertices);
    }
    for (FigureBone* child : children) child->Place(origin, blend);
}
