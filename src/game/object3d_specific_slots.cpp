// Small slots of a 3D object's side half (CObject3D + 0x94): the client half's extra scale and
// part materials (CObject3DClientSpecific, 0051AD20 / 0051C970) and the server half's scale
// (CObject3DServer, 0051AC30). See map_object_shadow.h and map_object_figures.h.
#include "map_object_shadow.h"

#include "engine_object_class.h"
#include "map_object_figures.h"
#include "material.h"
#include "world.h"

#include "link_figure.h"   // CLinkMesh::screenRect_

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "lib/gameapi.h"

void CObject3DClientSpecific::ExtraScale(float* x, float* y, float* z) {
    *x = figureScale.x;
    *y = figureScale.y;
    *z = figureScale.z;
}
EI_REWRITTEN(0x0051AD20, CObject3DClientSpecific::ExtraScale);

void CObject3DClientSpecific::RegisterPartMaterials() {
    CMaterialManager* manager = WorldContext()->MaterialManager();
    if (materials.size <= 0) return;
    materialCursor = 0;
    for (CMaterial* material = materials[0]; material;) {
        manager->FillMaterial(material, static_cast<std::uint32_t>(material->type_));
        ++materialCursor;
        if (materials.size <= materialCursor) break;
        material = materials[materialCursor];
    }
}
EI_REWRITTEN(0x0051C970, CObject3DClientSpecific::RegisterPartMaterials);

// --- A-U11: screen picking, part lookups, class slots, the base half's destructor -----------------

namespace {
constexpr std::uint32_t kObjectShown = 0x20;    // CObject3D::flags_
constexpr std::uint32_t kPartOwnMesh = 0x40;    // CFigureMesh::flags_: the part is drawn through its drawer
constexpr int kClientClassId = 0x37;
// 0078F47C: "CObject3DClientSpecific".
const char* ClientClassName() { return "CObject3DClientSpecific"; }

POINT AsPoint(const long* point) { return POINT{point[0], point[1]}; }
const RECT* PartRect(const CFigureMesh* part) { return &part->drawer_->screen_; }
} // namespace

int CObject3DClientSpecific::DirtyRectContains(const long* point) {
    if (!(object->flags_ & kObjectShown)) return FALSE;
    return gameapi::PtInRect(object->DirtyRect(), AsPoint(point));
}
EI_REWRITTEN(0x0051C130, CObject3DClientSpecific::DirtyRectContains);

int CObject3DClientSpecific::PartRectsContain(const long* point) {
    CObject3D* target = object;
    if (!(target->flags_ & kObjectShown)) return FALSE;
    for (int i = 0; i < target->parts.size; ++i) {
        CFigureMesh* part = target->parts.data[i];
        if (!(part->flags_ & kPartOwnMesh)) continue;
        if (gameapi::PtInRect(PartRect(part), AsPoint(point))) return TRUE;
    }
    return FALSE;
}
EI_REWRITTEN(0x0051C160, CObject3DClientSpecific::PartRectsContain);

int CObject3DClientSpecific::PartRectsMeet(const tagRECT* rect) {
    CObject3D* target = object;
    if (!(target->flags_ & kObjectShown)) return FALSE;
    for (int i = 0; i < target->parts.size; ++i) {
        CFigureMesh* part = target->parts.data[i];
        if (!(part->flags_ & kPartOwnMesh)) continue;
        RECT common;
        gameapi::IntersectRect(&common, PartRect(part), rect);
        if (!gameapi::IsRectEmpty(&common)) return TRUE;
    }
    return FALSE;
}
EI_REWRITTEN(0x0051C1D0, CObject3DClientSpecific::PartRectsMeet);

CFigureMesh* CObject3DClientSpecific::ObjectPartNamed(const char* name) { return object->FindPart(name); }
EI_REWRITTEN(0x0051B2E0, CObject3DClientSpecific::ObjectPartNamed);

CFigureMesh* CObject3DClientSpecific::ObjectPartOfType(int type) { return object->FindPartOfType(type); }
EI_REWRITTEN(0x0051B2D0, CObject3DClientSpecific::ObjectPartOfType);

CFigureMesh* CObject3D::FindPartOfType(int type) {
    for (int i = 0; i < parts.size; ++i)
        if (parts.data[i]->type_ == type) return parts.data[i];
    for (int i = 0; i < pendingParts.size; ++i)
        if (pendingParts.data[i]->type_ == type) return pendingParts.data[i];
    return nullptr;
}
EI_REWRITTEN(0x00516920, CObject3D::FindPartOfType);

namespace {
// Slots 11 / 12 (0051AD50 / 0051AD60): the class name and id 0x37.
const char* __fastcall ClientSpecificClassName(CObject3DClientSpecific*) { return ClientClassName(); }
int __fastcall ClientSpecificClassId(CObject3DClientSpecific*) { return kClientClassId; }
} // namespace

// 0051AD00: the side half's base destructor body (vtable 0073DEFC, the object forgotten, then the
// engine object's); 0051ACE0 (that vtable's slot 0, RET 4): it, then operator delete when bit 0 of
// `flags` is set. Returns the object.
void __fastcall SpecificBaseDestruct(CObject3DClientSpecific* self) {
    ei::SetVtable(self, CObject3DSpecific::BaseVtable());
    self->object = nullptr;
    self->DestructBase();
}
CObject3DClientSpecific* CObject3DClientSpecific::DeleteScalar(unsigned flags) {
    SpecificBaseDestruct(this);
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REWRITTEN(0x0051AD50, ClientSpecificClassName);
EI_REWRITTEN(0x0051AD60, ClientSpecificClassId);
EI_REWRITTEN(0x0051AD00, SpecificBaseDestruct);
EI_REWRITTEN(0x0051ACE0, CObject3DClientSpecific::DeleteScalar);

namespace {
constexpr float kNoScale = 1.0f;
} // namespace

// 0051AC30. CObject3DServer's slot 18 (thiscall, RET 0xC; `this` unused): the server draws
// nothing, so the extra scale is 1, 1, 1.
void __stdcall ServerExtraScale(float* x, float* y, float* z) {
    *x = kNoScale;
    *y = kNoScale;
    *z = kNoScale;
}
EI_REWRITTEN(0x0051AC30, ServerExtraScale);

// --- The compiled tables (C19) -------------------------------------------------------------------------
// Compiled tags (engine_object_class.h) in place of game.exe's 0073DEFC (the base half: the halves' constructors
// store it first, their destructors last), 0073DFBC (CObject3DServer) and 0073DE90 (CObject3DClientSpecific).
// HalfClass declares CObject3DSpecific's slots 13-26 (map_object_figures.h) with the base half's bodies; the
// server's and the client's tags override the slots their game.exe tables change (qualified: no dispatch).

namespace {

template <typename Self, typename T>
class HalfClass : public EngineObjectClass<Self, T> {
public:
    virtual int OnPartAdded(CFigureMesh*) { return _purecall(); }   // 13: pure
    virtual CFigureMesh* RemoveFromPartSets(const char*) {          // 14: pure
        _purecall();
        return nullptr;
    }
    virtual int OnPartRemoved(CFigureMesh*) { return _purecall(); }   // 15: pure
    virtual CFigureMesh* FindPart(const char*) {                      // 16: pure
        _purecall();
        return nullptr;
    }
    virtual CFigureMesh* FindPartOfType(int) {   // 17: pure
        _purecall();
        return nullptr;
    }
    virtual void GetScale(float* x, float* y, float* z) { ServerExtraScale(x, y, z); }            // 18: 0051AC30
    virtual int DrawParts(void* context, int* dirty) { return Half()->DrawNothing(context, dirty); }   // 19: 0051AC60
    virtual int DrawPartsShadow(void* context) { return Half()->ReturnZero(context); }            // 20: 0051AC70
    virtual void ApplyMaterials() { SpecificBaseNothing(Half()); }                                 // 21: 0051AC80
    virtual int BoundsContainPoint(const long* point) { return Half()->ReturnZero(point); }   // 22: 0051AC90
    virtual int PartsIntersectRect(const long* rect) { return Half()->ReturnZero(rect); }     // 23: 0051ACB0
    virtual int PartsContainPoint(const long* point) { return Half()->ReturnZero(point); }    // 24: 0051ACA0
    virtual void Unused25(int value) { Half()->IgnoreArgument(value); }   // 25: 0051ACC0
    virtual void SetLit(int value) { Half()->IgnoreArgument(value); }     // 26: 0051ACD0

protected:
    CObject3DSpecific* Half() { return this->template As<CObject3DSpecific>(); }
};

// 0073DEFC: slot 0 is 0051ACE0, the base destructor body (SpecificBaseDestruct) and the memory freed.
class HalfBaseClass final : public HalfClass<HalfBaseClass, CObject3DSpecific> {
public:
    void* DeleteScalar(unsigned flags) override { return As<CObject3DClientSpecific>()->DeleteScalar(flags); }   // 0
};

class Object3DServerClass final : public HalfClass<Object3DServerClass, CObject3DSpecific> {
public:
    int Create() override { return Object3DServerCreate(Half()); }                      // 2: 0051F320
    void Destroy() override { Object3DServerDestroy(Half()); }                          // 3: 0051F330
    const char* ClassName() override { return Object3DServerClassName(Half()); }       // 11: 0051F2C0
    int ClassId() override { return Object3DServerClassId(Half()); }                    // 12: 0051F2D0
    int OnPartAdded(CFigureMesh* part) override { return Half()->ReturnZero(part); }   // 13: 0051F270
    CFigureMesh* RemoveFromPartSets(const char*) override { return nullptr; }          // 14: 0051F290 (0)
    int OnPartRemoved(CFigureMesh* part) override { return Half()->ReturnZero(part); }   // 15: 0051F280
    CFigureMesh* FindPart(const char*) override { return nullptr; }                      // 16: 0051F2B0 (0)
    CFigureMesh* FindPartOfType(int) override { return nullptr; }                        // 17: 0051F2A0 (0)
};

class Object3DClientSpecificClass final : public HalfClass<Object3DClientSpecificClass, CObject3DClientSpecific> {
public:
    int Create() override { return Client()->CreateClient(); }                       // 2: 0051AEC0
    void Destroy() override { Client()->DestroyClient(); }                           // 3: 0051AF70
    const char* ClassName() override { return ClientSpecificClassName(Client()); }   // 11: 0051AD50
    int ClassId() override { return ClientSpecificClassId(Client()); }               // 12: 0051AD60
    int OnPartAdded(CFigureMesh* part) override { return Client()->PartAdded(part); }                        // 13: 0051B050
    CFigureMesh* RemoveFromPartSets(const char* name) override { return Client()->RemovePartNamed(name); }   // 14: 0051B210
    int OnPartRemoved(CFigureMesh* part) override { return Client()->PartRemoved(part); }                    // 15: 0051B160
    CFigureMesh* FindPart(const char* name) override { return Client()->ObjectPartNamed(name); }             // 16: 0051B2E0
    CFigureMesh* FindPartOfType(int type) override { return Client()->ObjectPartOfType(type); }              // 17: 0051B2D0
    void GetScale(float* x, float* y, float* z) override { Client()->ExtraScale(x, y, z); }                  // 18: 0051AD20
    int DrawParts(void* context, int* dirty) override {   // 19: 0051B7E0
        return Client()->Draw(static_cast<CSceneRenderer*>(context), dirty);
    }
    int DrawPartsShadow(void* context) override { return Client()->DrawShadow(static_cast<CSceneRenderer*>(context)); }   // 20: 0051CE60
    void ApplyMaterials() override { Client()->ApplyWorldMaterials(); }   // 21: 0051CB30
    int BoundsContainPoint(const long* point) override { return Client()->DirtyRectContains(point); }   // 22: 0051C130
    int PartsIntersectRect(const long* rect) override {   // 23: 0051C1D0
        return Client()->PartRectsMeet(reinterpret_cast<const tagRECT*>(rect));
    }
    int PartsContainPoint(const long* point) override { return Client()->PartRectsContain(point); }   // 24: 0051C160
    void Unused25(int value) override { Client()->EmptySlot25(value); }   // 25: 0051C9D0
    void SetLit(int lit) override { Client()->SetLitSlot(lit); }          // 26: 0051C9E0

private:
    CObject3DClientSpecific* Client() { return As<CObject3DClientSpecific>(); }
};

} // namespace

void* CObject3DSpecific::BaseVtable() { return HalfBaseClass::Vtable(); }
void* CObject3DSpecific::ServerVtable() { return Object3DServerClass::Vtable(); }
void* CObject3DClientSpecific::Vtable() { return Object3DClientSpecificClass::Vtable(); }
