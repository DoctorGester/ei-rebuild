// Small slots of a 3D object's side half (CObject3D + 0x94): the client half's extra scale and
// part materials (CObject3DClientSpecific, 0051AD20 / 0051C970) and the server half's scale
// (CObject3DServer, 0051AC30). See map_object_shadow.h and map_object_figures.h.
#include "map_object_shadow.h"

#include "map_object_figures.h"
#include "material.h"
#include "world.h"

#include "link_figure.h"   // CLinkMesh::screenRect_

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "originals_render.h"

void CObject3DClientSpecific::ExtraScale(float* x, float* y, float* z) {
    *x = figureScale.x;
    *y = figureScale.y;
    *z = figureScale.z;
}
EI_REPLACE_METHOD(0x0051AD20, CObject3DClientSpecific::ExtraScale);

void CObject3DClientSpecific::RegisterPartMaterials() {
    void* manager = static_cast<CWorld*>(ManagerContext())->MaterialManager();
    if (materials.size <= 0) return;
    materialCursor = 0;
    for (CMaterial* material = materials[0]; material;) {
        static_cast<CMaterialManager*>(manager)->FillMaterial(material, static_cast<std::uint32_t>(material->type_));
        ++materialCursor;
        if (materials.size <= materialCursor) break;
        material = materials[materialCursor];
    }
}
EI_REPLACE_METHOD(0x0051C970, CObject3DClientSpecific::RegisterPartMaterials);

// --- A-U11: screen picking, part lookups, class slots, the base half's destructor -----------------

namespace {
constexpr std::uint32_t kObjectShown = 0x20;    // CObject3D::flags_
constexpr std::uint32_t kPartOwnMesh = 0x40;    // CFigureMesh::flags_: the part is drawn through its drawer
constexpr int kClientClassId = 0x37;
constexpr std::uintptr_t kSpecificBaseVtable = std::uintptr_t(0x0073DEFC);
// 0078F47C: "CObject3DClientSpecific".
const char* ClientClassName() { return reinterpret_cast<const char*>(std::uintptr_t(0x0078F47C)); }

POINT AsPoint(const long* point) { return POINT{point[0], point[1]}; }
const RECT* PartRect(const CFigureMesh* part) { return &part->drawer_->screen_; }
CFigureMesh* PartAt(const AlignedPtrArray& parts, int i) { return static_cast<CFigureMesh*>(parts.data[i]); }
} // namespace

int CObject3DClientSpecific::BoundsContainPoint(const long* point) {
    if (!(object->flags_ & kObjectShown)) return FALSE;
    return gameapi::PtInRect(object->DirtyRect(), AsPoint(point));
}
EI_REPLACE_METHOD(0x0051C130, CObject3DClientSpecific::BoundsContainPoint);

int CObject3DClientSpecific::PartsContainPoint(const long* point) {
    CObject3D* target = object;
    if (!(target->flags_ & kObjectShown)) return FALSE;
    for (int i = 0; i < target->parts.size; ++i) {
        CFigureMesh* part = PartAt(target->parts, i);
        if (!(part->flags_ & kPartOwnMesh)) continue;
        if (gameapi::PtInRect(PartRect(part), AsPoint(point))) return TRUE;
    }
    return FALSE;
}
EI_REPLACE_METHOD(0x0051C160, CObject3DClientSpecific::PartsContainPoint);

int CObject3DClientSpecific::PartsIntersectRect(const tagRECT* rect) {
    CObject3D* target = object;
    if (!(target->flags_ & kObjectShown)) return FALSE;
    for (int i = 0; i < target->parts.size; ++i) {
        CFigureMesh* part = PartAt(target->parts, i);
        if (!(part->flags_ & kPartOwnMesh)) continue;
        RECT common;
        gameapi::IntersectRect(&common, PartRect(part), rect);
        if (!gameapi::IsRectEmpty(&common)) return TRUE;
    }
    return FALSE;
}
EI_REPLACE_METHOD(0x0051C1D0, CObject3DClientSpecific::PartsIntersectRect);

CFigureMesh* CObject3DClientSpecific::ObjectPartNamed(const char* name) { return object->FindPart(name); }
EI_REPLACE_METHOD(0x0051B2E0, CObject3DClientSpecific::ObjectPartNamed);

CFigureMesh* CObject3DClientSpecific::ObjectPartOfType(int type) { return object->FindPartOfType(type); }
EI_REPLACE_METHOD(0x0051B2D0, CObject3DClientSpecific::ObjectPartOfType);

CFigureMesh* CObject3D::FindPartOfType(int type) {
    for (int i = 0; i < parts.size; ++i)
        if (PartAt(parts, i)->type_ == type) return PartAt(parts, i);
    for (int i = 0; i < pendingParts.size; ++i)
        if (PartAt(pendingParts, i)->type_ == type) return PartAt(pendingParts, i);
    return nullptr;
}
EI_REPLACE_METHOD(0x00516920, CObject3D::FindPartOfType);

namespace {
// Slots 11 / 12 (0051AD50 / 0051AD60): the class name and id 0x37.
const char* __fastcall ClientSpecificClassName(CObject3DClientSpecific*) { return ClientClassName(); }
int __fastcall ClientSpecificClassId(CObject3DClientSpecific*) { return kClientClassId; }
} // namespace

// 0051AD00: the side half's base destructor body (vtable 0073DEFC, the object forgotten, then the
// engine object's); 0051ACE0 (that vtable's slot 0, RET 4): it, then operator delete when bit 0 of
// `flags` is set. Returns the object.
void __fastcall SpecificBaseDestruct(CObject3DClientSpecific* self) {
    ei::SetVtable(self, kSpecificBaseVtable);
    self->object = nullptr;
    self->DestructBase();
}
CObject3DClientSpecific* CObject3DClientSpecific::DeleteScalar(unsigned flags) {
    SpecificBaseDestruct(this);
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE(0x0051AD50, ClientSpecificClassName);
EI_REPLACE(0x0051AD60, ClientSpecificClassId);
EI_REPLACE(0x0051AD00, SpecificBaseDestruct);
EI_REPLACE_METHOD(0x0051ACE0, CObject3DClientSpecific::DeleteScalar);

namespace {
constexpr float kNoScale = 1.0f;
} // namespace

// 0051AC30. CObject3DServer's slot 18 (thiscall, RET 0xC; `this` unused): the server draws
// nothing, so the extra scale is 1, 1, 1.
void __fastcall ServerExtraScale(void*, void*, float* x, float* y, float* z) {
    *x = kNoScale;
    *y = kNoScale;
    *z = kNoScale;
}
EI_REPLACE(0x0051AC30, ServerExtraScale);
