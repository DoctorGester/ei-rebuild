// Small slots of a 3D object's side half (CObject3D + 0x94): the client half's extra scale and
// part materials (CObject3DClientSpecific) and the server half's scale
// (CObject3DServer). See map_object_shadow.h and map_object_figures.h.
#include "link_figure.h"   // CLinkMesh::screenRect
#include "map_object_figures.h"
#include "map_object_shadow.h"
#include "material.h"
#include "world.h"

#include "lib/gameapi.h"

void CObject3DClientSpecific::GetScale(float* x, float* y, float* z) {
    *x = figureScale.x;
    *y = figureScale.y;
    *z = figureScale.z;
}

void CObject3DClientSpecific::RegisterPartMaterials() {
    CMaterialManager* manager = WorldContext()->MaterialManager();
    if (materials.empty()) return;
    materialCursor = 0;
    for (CMaterial* material = materials[0]; material;) {
        manager->FillMaterial(material, static_cast<std::uint32_t>(material->type));
        ++materialCursor;
        if (std::ssize(materials) <= materialCursor) break;
        material = materials[materialCursor];
    }
}

// --- Screen picking, part lookups, class slots, the base half's destructor -----------------

namespace {
// "CObject3DClientSpecific".
const char* ClientClassName() { return "CObject3DClientSpecific"; }

POINT AsPoint(const long* point) { return POINT{point[0], point[1]}; }
const RECT* PartRect(const CFigureMesh* part) { return &part->drawer->screen; }

constexpr float kNoScale = 1.0f;

} // namespace

int CObject3DClientSpecific::BoundsContainPoint(const long* point) {
    if (!(object->flags & MapObjectFlag::Shown)) return FALSE;
    return gameapi::PtInRect(&object->dirtyRect, AsPoint(point));
}

int CObject3DClientSpecific::PartsContainPoint(const long* point) {
    CObject3D* target = object;
    if (!(target->flags & MapObjectFlag::Shown)) return FALSE;
    for (int i = 0; i < std::ssize(target->parts); ++i) {
        CFigureMesh* part = target->parts[i];
        if (!(part->flags & FigureMeshFlag::Drawn)) continue;
        if (gameapi::PtInRect(PartRect(part), AsPoint(point))) return TRUE;
    }
    return FALSE;
}

int CObject3DClientSpecific::PartRectsMeet(const tagRECT* rect) {
    CObject3D* target = object;
    if (!(target->flags & MapObjectFlag::Shown)) return FALSE;
    for (int i = 0; i < std::ssize(target->parts); ++i) {
        CFigureMesh* part = target->parts[i];
        if (!(part->flags & FigureMeshFlag::Drawn)) continue;
        RECT common;
        gameapi::IntersectRect(&common, PartRect(part), rect);
        if (!gameapi::IsRectEmpty(&common)) return TRUE;
    }
    return FALSE;
}

CFigureMesh* CObject3DClientSpecific::FindPart(std::string_view name) { return object->FindPart(name); }

CFigureMesh* CObject3DClientSpecific::FindPartOfType(int partType) { return object->FindPartOfType(partType); }

CFigureMesh* CObject3D::FindPartOfType(int partType) {
    for (int i = 0; i < std::ssize(parts); ++i)
        if (parts[i]->type == partType) return parts[i];
    return nullptr;
}

// The class name and id 0x37.
const char* CObject3DClientSpecific::ClassName() { return ClientClassName(); }
ClassId CObject3DClientSpecific::GetClassId() { return ClassId::Object3DClientSpecific; }

// The base half's destructor (deleting form): the object forgotten.
CObject3DSpecific::~CObject3DSpecific() { object = nullptr; }

void CObject3DSpecific::GetScale(float* x, float* y, float* z) {
    *x = kNoScale;
    *y = kNoScale;
    *z = kNoScale;
}
