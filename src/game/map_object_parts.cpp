// The 3D object's figure parts by name, their removal and their place in the world. See
// map_object_figures.h and docs/notes/map_objects.md ("Figures").
#include "map_object_figures.h"

#include "ei/hook.h"
#include "originals_units.h"

#include <cstring>

namespace {
// Not rewritten, forwarder: 0050D350, the animation state forgets `part` (RET 4).
__declspec(naked) int __fastcall AnimationRemovePart(void* /*animation*/, void*, FigurePart* /*part*/)
    EI_ORIGINAL(0x0050D350)

// The first of `parts` that `matches`, or null.
template <typename Match>
FigurePart* FirstPart(const AlignedPtrArray& parts, Match matches) {
    for (int i = 0; i < parts.size; ++i) {
        auto* part = static_cast<FigurePart*>(parts.data[i]);
        if (matches(part)) return part;
    }
    return nullptr;
}

// Removes entry `index`, moving the ones after it down.
void RemoveAt(AlignedPtrArray& parts, int index) {
    int after = parts.size - index - 1;
    if (after) std::memmove(&parts.data[index], &parts.data[index + 1], after * sizeof(void*));
    --parts.size;
}

int IndexOf(const AlignedPtrArray& parts, const FigurePart* part) {
    for (int i = 0; i < parts.size; ++i)
        if (parts.data[i] == part) return i;
    return -1;
}
} // namespace

FigurePart* CObject3D::FindPartByPrefix(const char* prefix) {
    // The original asks for the name twice; the parts return the same string both times.
    auto startsWith = [prefix](FigurePart* part) { return std::strstr(part->GetName(), prefix) == part->GetName(); };
    if (FigurePart* part = FirstPart(parts, startsWith)) return part;
    return FirstPart(pendingParts, startsWith);
}
EI_REPLACE_METHOD(0x00516A80, CObject3D::FindPartByPrefix);

FigurePart* CObject3D::FindPart(const char* partName) {
    auto named = [partName](FigurePart* part) { return std::strcmp(part->GetName(), partName) == 0; };
    if (FigurePart* part = FirstPart(parts, named)) return part;
    return FirstPart(pendingParts, named);
}
EI_REPLACE_METHOD(0x005169A0, CObject3D::FindPart);

int CObject3D::RemovePart(FigurePart* part) {
    if (attached) Specific()->OnPartRemoved(part);
    int index = IndexOf(parts, part);
    if (index >= 0) {
        RemoveAt(parts, index);
        FigurePartSetOwnerOriginal(part, nullptr, nullptr);
    } else {
        index = IndexOf(pendingParts, part);
        if (index < 0) return 0;
        RemoveAt(pendingParts, index);
    }
    if (animation) AnimationRemovePart(animation, nullptr, part);
    part->Release();
    return 1;
}
EI_REPLACE_METHOD(0x005167E0, CObject3D::RemovePart);

FigurePart* CObject3D::RemovePartNamed(const char* partName) {
    FigurePart* part = FindPart(partName);
    if (!part) return nullptr;
    RemovePart(part);
    return part;
}
EI_REPLACE_METHOD(0x005168F0, CObject3D::RemovePartNamed);

float* CObject3D::PartWorldPosition(float* out, const FigurePart* part) {
    float scaleX, scaleY, scaleZ;
    Specific()->GetScale(&scaleX, &scaleY, &scaleZ);
    // v' = 2 (u.v) u + (w^2 - u.u) v + 2 w (u x v) for the quaternion (w, u).
    const float w = orientation.w;
    const float ux = orientation.x, uy = orientation.y, uz = orientation.z;
    const float vx = part->position[0], vy = part->position[1], vz = part->position[2];
    float twiceDot = 2.0f * ((uz * vz + uy * vy) + ux * vx);
    float twiceW = w + w;
    float crossX = uy * vz - uz * vy;
    float crossY = uz * vx - vz * ux;
    float crossZ = ux * vy - uy * vx;
    float along = w * w - (ux * ux + uy * uy + uz * uz);
    float x = (along * vx + twiceW * crossX) + twiceDot * ux;
    float y = (along * vy + twiceW * crossY) + twiceDot * uy;
    float z = (along * vz + twiceW * crossZ) + twiceDot * uz;
    out[0] = x * scaleX + position[0];
    out[1] = y * scaleY + position[1];
    out[2] = z * scaleZ + position[2];
    return out;
}
EI_REPLACE_METHOD(0x005188B0, CObject3D::PartWorldPosition);
