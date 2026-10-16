// The 3D object's figure parts by name, their removal and their place in the world, and its
// animation database. See map_object_figures.h and docs/notes/map_objects.md ("Figures").
#include "map_object_figures.h"

#include "base_manager.h"

#include "lib/crt.h"
#include "lib/mfc.h"

#include "ei/hook.h"
#include "originals_units.h"

#include <cstring>

namespace {
constexpr int kClassUnit = 0x50;
constexpr std::uint32_t kKindMask = 0x3C0000;         // FigureAnimation::key
constexpr std::uint32_t kKindSkill = 0x40000;
constexpr std::uint32_t kNumberMask = 0x3FC00000;
constexpr int kNumberShift = 22;

// The first of `parts` that `matches`, or null.
template <typename Match>
CFigureMesh* FirstPart(const AlignedPtrArray& parts, Match matches) {
    for (int i = 0; i < parts.size; ++i) {
        auto* part = static_cast<CFigureMesh*>(parts.data[i]);
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

int IndexOf(const AlignedPtrArray& parts, const CFigureMesh* part) {
    for (int i = 0; i < parts.size; ++i)
        if (parts.data[i] == part) return i;
    return -1;
}
} // namespace

CFigureMesh* CObject3D::FindPartByPrefix(const char* prefix) {
    // The original asks for the name twice; the parts return the same string both times.
    auto startsWith = [prefix](CFigureMesh* part) { return std::strstr(part->GetName(), prefix) == part->GetName(); };
    if (CFigureMesh* part = FirstPart(parts, startsWith)) return part;
    return FirstPart(pendingParts, startsWith);
}
EI_REPLACE_METHOD(0x00516A80, CObject3D::FindPartByPrefix);

CFigureMesh* CObject3D::FindPart(const char* partName) {
    auto named = [partName](CFigureMesh* part) { return std::strcmp(part->GetName(), partName) == 0; };
    if (CFigureMesh* part = FirstPart(parts, named)) return part;
    return FirstPart(pendingParts, named);
}
EI_REPLACE_METHOD(0x005169A0, CObject3D::FindPart);

int CObject3D::RemovePart(CFigureMesh* part) {
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
    if (animation) static_cast<CFigureAnimationState*>(animation)->DetachMesh(part);
    part->Release();
    return 1;
}
EI_REPLACE_METHOD(0x005167E0, CObject3D::RemovePart);

CFigureMesh* CObject3D::RemovePartNamed(const char* partName) {
    CFigureMesh* part = FindPart(partName);
    if (!part) return nullptr;
    RemovePart(part);
    return part;
}
EI_REPLACE_METHOD(0x005168F0, CObject3D::RemovePartNamed);

float* CObject3D::PartWorldPosition(float* out, const CFigureMesh* part) {
    float scaleX, scaleY, scaleZ;
    Specific()->GetScale(&scaleX, &scaleY, &scaleZ);
    // v' = 2 (u.v) u + (w^2 - u.u) v + 2 w (u x v) for the quaternion (w, u).
    const float w = orientation.w;
    const float ux = orientation.x, uy = orientation.y, uz = orientation.z;
    const float vx = part->localPosition_.x, vy = part->localPosition_.y, vz = part->localPosition_.z;
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
    out[0] = x * scaleX + position.x;
    out[1] = y * scaleY + position.y;
    out[2] = z * scaleZ + position.z;
    return out;
}
EI_REPLACE_METHOD(0x005188B0, CObject3D::PartWorldPosition);

// --- The animation database -------------------------------------------------------------------

AnimationDatabase* CObject3D::Animations() {
    if (!animations && owner_) {
        mfc::CString file(name);
        file += ".adb";
        animations = static_cast<AnimationDatabase*>(owner_->GetFile(file, nullptr));
    }
    return animations;
}
EI_REPLACE_METHOD(0x00518CA0, CObject3D::Animations);

FigureAnimation* CObject3D::AnimationAt(std::uint32_t key) {
    if (!Animations()) return nullptr;
    std::uint32_t index = key & 0xFF;
    return index < animations->count ? &animations->animations[index] : nullptr;
}
EI_REPLACE_METHOD(0x00518F80, CObject3D::AnimationAt);

FigureAnimation* CObject3D::FindSkillAnimation(int number) {
    if (!Animations()) return nullptr;
    std::uint32_t wanted = static_cast<std::uint32_t>(number) << kNumberShift;
    for (int i = 0; i < static_cast<int>(animations->count); ++i) {
        std::uint32_t key = animations->animations[i].key;
        if ((key & kKindMask) == kKindSkill && (key & kNumberMask) == wanted) return &animations->animations[i];
    }
    return nullptr;
}
EI_REPLACE_METHOD(0x00518FC0, CObject3D::FindSkillAnimation);

double CObject3D::AnimationFactor(float time) {
    if (GetClassId() != kClassUnit || !Animations()) return 1.0;
    return animations->TimeFactor(time);
}
EI_REPLACE_METHOD(0x00518D30, CObject3D::AnimationFactor);

int CObject3D::FindPartIndex(const char* partName) {
    for (int i = 0; i < partNameCount; ++i)
        if (gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(partNames[i]),
                            reinterpret_cast<const unsigned char*>(partName)) == 0)
            return i;
    return -1;
}
EI_REPLACE_METHOD(0x00517990, CObject3D::FindPartIndex);
