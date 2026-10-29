// The 3D object's figure parts by name, their removal and their place in the world, and its
// animation database. See map_object_figures.h and docs/notes/map_objects.md ("Figures").
#include "base_manager.h"
#include "figure_mesh.h"
#include "map_object_figures.h"

#include "lib/str.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace {
// The first of `parts` that `matches`, or null.
template <typename Parts, typename Match>
CFigureMesh* FirstPart(const Parts& parts, Match matches) {
    for (int i = 0; i < std::ssize(parts); ++i) {
        CFigureMesh* part = parts[i];
        if (matches(part)) return part;
    }
    return nullptr;
}

// Removes entry `index`, moving the ones after it down.
template <typename Parts>
void RemoveAt(Parts& parts, int index) { parts.erase(parts.begin() + index); }

template <typename Parts>
int IndexOf(const Parts& parts, const CFigureMesh* part) {
    for (int i = 0; i < std::ssize(parts); ++i)
        if (parts[i] == part) return i;
    return -1;
}
} // namespace

CFigureMesh* CObject3D::FindPartByPrefix(std::string_view prefix) {
    // The original asks for the name twice; the parts return the same string both times.
    auto startsWith = [prefix](CFigureMesh* part) { return std::string_view(part->GetName()).starts_with(prefix); };
    return FirstPart(parts, startsWith);
}

CFigureMesh* CObject3D::FindPart(std::string_view partName) {
    auto named = [partName](CFigureMesh* part) { return str::Compare(part->GetName(), partName) == 0; };
    return FirstPart(parts, named);
}

int CObject3D::RemovePart(CFigureMesh* part) {
    if (attached) attached->OnPartRemoved(part);
    int index = IndexOf(parts, part);
    if (index < 0) return 0;
    parts[index].Take();   // released below, after the detach
    RemoveAt(parts, index);
    part->SetObject(nullptr);
    if (animation) animation->DetachMesh(part);
    part->Release();
    return 1;
}

CFigureMesh* CObject3D::RemovePartNamed(std::string_view partName) {
    CFigureMesh* part = FindPart(partName);
    if (!part) return nullptr;
    RemovePart(part);
    return part;
}

float* CObject3D::PartWorldPosition(float* out, const CFigureMesh* part) {
    float scaleX, scaleY, scaleZ;
    attached->GetScale(&scaleX, &scaleY, &scaleZ);
    // v' = 2 (u.v) u + (w^2 - u.u) v + 2 w (u x v) for the quaternion (w, u).
    const float w = localRotation.w;
    const float ux = localRotation.x, uy = localRotation.y, uz = localRotation.z;
    const float vx = part->localPosition.x, vy = part->localPosition.y, vz = part->localPosition.z;
    // 2(u.v) with the doubling on u, as /fp:fast spreads it over the written-out sum (an inlined
    // 2.0f * Dot regroups to (2xx + s) + s).
    float twiceDot = DotUnrounded<Sum3::ZYX>(ux * 2.0f, uy * 2.0f, uz * 2.0f, vx, vy, vz);
    float twiceW = w + w;
    float crossX = uy * vz - uz * vy;
    float crossY = uz * vx - vz * ux;
    float crossZ = ux * vy - uy * vx;
    float along = w * w - LengthSquaredUnrounded<Sum3::XYZ>(ux, uy, uz);
    float x = (along * vx + twiceW * crossX) + twiceDot * ux;
    float y = (along * vy + twiceW * crossY) + twiceDot * uy;
    float z = (along * vz + twiceW * crossZ) + twiceDot * uz;
    out[0] = x * scaleX + localPosition.x;
    out[1] = y * scaleY + localPosition.y;
    out[2] = z * scaleZ + localPosition.z;
    return out;
}

// --- The animation database -------------------------------------------------------------------

AnimationDatabase* CObject3D::Animations() {
    if (!animations && owner) {
        std::string file = name;
        file += ".adb";
        animations = static_cast<AnimationDatabase*>(owner->GetFile(file, nullptr));
    }
    return animations;
}

FigureAnimation* CObject3D::AnimationAt(std::uint32_t key) {
    if (!Animations()) return nullptr;
    std::uint32_t index = static_cast<std::uint8_t>(key);   // the entry's index is the low byte
    return index < animations->count ? &animations->animations[index] : nullptr;
}

FigureAnimation* CObject3D::FindSkillAnimation(SkillAnimation number) {
    if (!Animations()) return nullptr;
    std::uint32_t wanted = AnimationNumberBits(static_cast<int>(number));
    for (int i = 0; i < static_cast<int>(animations->count); ++i) {
        const FigureAnimation& entry = animations->animations[i];
        if (entry.Kind() == AnimationKind::Special && (entry.key & kAnimationNumberMask) == wanted)
            return &animations->animations[i];
    }
    return nullptr;
}

double CObject3D::AnimationFactor(float time) {
    if (GetClassId() != ClassId::Unit || !Animations()) return 1.0;
    return animations->TimeFactor(time);
}

int CObject3D::FindPartIndex(std::string_view partName) {
    for (std::size_t i = 0; i < partNames.size(); ++i)
        if (str::Compare(partNames[i], partName) == 0) return static_cast<int>(i);
    return -1;
}
