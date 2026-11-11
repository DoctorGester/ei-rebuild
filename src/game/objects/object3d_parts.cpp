// The 3D object's figure parts by name, their removal and their place in the world, and its
// animation database. See objects/object3d.h and docs/notes/map_objects.md ("Figures").
#include "engine/core/base_manager.h"
#include "engine/models/figure_mesh.h"
#include "game/objects/object3d.h"

#include "lib/str.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace {
// The first of `parts` that `matches`, or null.
template <typename Parts, typename Match>
FigureMesh* FirstPart(const Parts& parts, Match matches) {
    for (int i = 0; i < std::ssize(parts); ++i) {
        FigureMesh* part = parts[i];
        if (matches(part)) return part;
    }
    return nullptr;
}

// Removes entry `index`, moving the ones after it down.
template <typename Parts>
void RemoveAt(Parts& parts, int index) { parts.erase(parts.begin() + index); }

template <typename Parts>
int IndexOf(const Parts& parts, const FigureMesh* part) {
    for (int i = 0; i < std::ssize(parts); ++i)
        if (parts[i] == part) return i;
    return -1;
}
} // namespace

FigureMesh* Object3D::FindPartByPrefix(std::string_view prefix) {
    // The original asks for the name twice; the parts return the same string both times.
    auto startsWith = [prefix](FigureMesh* part) { return std::string_view(part->GetName()).starts_with(prefix); };
    return FirstPart(parts, startsWith);
}

FigureMesh* Object3D::FindPart(std::string_view partName) {
    auto named = [partName](FigureMesh* part) { return str::Compare(part->GetName(), partName) == 0; };
    return FirstPart(parts, named);
}

int Object3D::RemovePart(FigureMesh* part) {
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

FigureMesh* Object3D::RemovePartNamed(std::string_view partName) {
    FigureMesh* part = FindPart(partName);
    if (!part) return nullptr;
    RemovePart(part);
    return part;
}

Vector3 Object3D::PartWorldPosition(const FigureMesh* part) {
    const Vector3 figureScale = attached->GetScale();
    const float scaleX = figureScale.x, scaleY = figureScale.y, scaleZ = figureScale.z;
    const Vector3 v = localRotation.Rotate(part->localPosition);
    return {v.x * scaleX + localPosition.x, v.y * scaleY + localPosition.y, v.z * scaleZ + localPosition.z};
}

// --- The animation database -------------------------------------------------------------------

AnimationDatabase* Object3D::Animations() {
    if (!animations && owner) animations = MapObjectManagerOf(this)->AnimationDatabaseOf(name);
    return animations;
}

FigureAnimation* Object3D::AnimationAt(std::uint32_t key) {
    if (!Animations()) return nullptr;
    std::uint32_t index = static_cast<std::uint8_t>(key);   // the entry's index is the low byte
    return index < animations->animations.size() ? &animations->animations[index] : nullptr;
}

FigureAnimation* Object3D::FindSkillAnimation(SkillAnimation number) {
    if (!Animations()) return nullptr;
    std::uint32_t wanted = AnimationNumberBits(static_cast<int>(number));
    for (FigureAnimation& entry : animations->animations)
        if (entry.Kind() == AnimationKind::Special && (entry.key & kAnimationNumberMask) == wanted) return &entry;
    return nullptr;
}

float Object3D::AnimationFactor(float time) {
    if (GetClassId() != ClassId::Unit || !Animations()) return 1.0f;
    return animations->TimeFactor(time);
}

int Object3D::FindPartIndex(std::string_view partName) {
    for (std::size_t i = 0; i < partNames.size(); ++i)
        if (str::Compare(partNames[i], partName) == 0) return static_cast<int>(i);
    return -1;
}
