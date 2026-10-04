#include "object.h"

#include "ei/hook.h"

// 00404920: First(manager). The reference moves from the previous manager to `manager`; returns the
// list's first object (the position then names the next node), or null.
CEngineObject* ObjectListIterator::First(CBaseManager* list) {
    if (manager) --manager->refCount_;
    manager = list;
    if (list) ++list->refCount_;
    void** node = FieldAt<void**>(manager, 0x38);
    position = node;
    if (!node) return nullptr;
    position = node[0];
    return static_cast<CEngineObject*>(node[2]);
}
EI_REPLACE_METHOD(0x00404920, ObjectListIterator::First);

// 00404960: Next(). The object at the position (moving it on), or null at the end.
CEngineObject* ObjectListIterator::Next() {
    auto** node = static_cast<void**>(position);
    if (!node) return nullptr;
    position = node[0];
    return static_cast<CEngineObject*>(node[2]);
}
EI_REPLACE_METHOD(0x00404960, ObjectListIterator::Next);

// --- Forwarders (not rewritten) ----------------------------------------------------------------

__declspec(naked) void __fastcall UnitSetHostileMask(CUnit*, void*, std::uint32_t) EI_ORIGINAL(0x0052E970)
__declspec(naked) void __fastcall UnitAddHostileMask(CUnit*, void*, std::uint32_t) EI_ORIGINAL(0x0052E940)

namespace {
void SetVtable(void* object, std::uintptr_t vtable) { std::memcpy(object, &vtable, sizeof(vtable)); }
} // namespace

// --- CEngineObject ------------------------------------------------------------------------------

void* CEngineObject::ManagerContext() const {
    return static_cast<const CBaseManager*>(owner_)->owner_AC;
}
EI_REPLACE_METHOD(0x00455940, CEngineObject::ManagerContext);

void* CEngineObject::SetOwner(void* owner) {
    void* previous = owner_;
    owner_ = owner;
    return previous;
}
EI_REPLACE_METHOD(0x00455970, CEngineObject::SetOwner);

void CEngineObject::DestructBase() {
    SetVtable(this, kEngineObjectVtable);
    owner_ = nullptr;
}
EI_REPLACE_METHOD(0x00455930, CEngineObject::DestructBase);

// --- CTransformObject ---------------------------------------------------------------------------

CTransformObject* CTransformObject::ConstructBase() {
    refCount_ = 0;
    id_ = -1;
    unknown_18 = 0;
    type_ = 0;
    flags_ = 0;
    owner_ = nullptr;
    // Identity rotations (w first); the positions are not initialised.
    localRotation_.w = 1.0f;
    localRotation_.x = 0.0f;
    localRotation_.y = 0.0f;
    localRotation_.z = 0.0f;
    worldRotation_.w = 1.0f;
    worldRotation_.x = 0.0f;
    worldRotation_.y = 0.0f;
    worldRotation_.z = 0.0f;
    SetVtable(this, kTransformObjectVtable);
    return this;
}
EI_REPLACE_METHOD(0x00453400, CTransformObject::ConstructBase);

// --- CHoover ------------------------------------------------------------------------------------

CHoover* CHoover::Construct() {
    refCount_ = 0;
    id_ = -1;
    unknown_18 = 0;
    type_ = 0;
    flags_ = 0;
    owner_ = nullptr;
    SetVtable(this, kHooverVtable);
    return this;
}
EI_REPLACE_METHOD(0x00461C10, CHoover::Construct);

void CHoover::Destruct() {
    SetVtable(this, kHooverVtable);
    DestructBase();
}
EI_REPLACE_METHOD(0x00461C80, CHoover::Destruct);

// --- Unit and figure getters --------------------------------------------------------------------

const char* __fastcall UnitGetName(const CUnit* unit) { return FieldAt<const char*>(unit, unitfield::kName); }
EI_REPLACE(0x00446820, UnitGetName);

int __fastcall UnitGetFlag278(const CUnit* unit) { return FieldAt<int>(unit, unitfield::kFlag278); }
EI_REPLACE(0x00446890, UnitGetFlag278);

int __fastcall UnitGetFlag274(const CUnit* unit) { return FieldAt<int>(unit, unitfield::kFlag274); }
EI_REPLACE(0x00446880, UnitGetFlag274);

float __fastcall UnitGetValue328(const CUnit* unit) { return FieldAt<float>(unit, unitfield::kValue328); }
EI_REPLACE(0x004468A0, UnitGetValue328);

const char* __fastcall FigureGetName(const CEngineObject* figure) { return FieldAt<const char*>(figure, 0x120); }
EI_REPLACE(0x00453440, FigureGetName);

const char* __fastcall FakeFigureGetName(const CEngineObject* figure) { return FieldAt<const char*>(figure, 0x94); }
EI_REPLACE(0x004547C0, FakeFigureGetName);

// --- CMemoryStream ------------------------------------------------------------------------------

void CMemoryStream::Reset() {
    ok_ = 1;
    data_ = nullptr;
    unknown_1C = 0;
    position_ = 0;
    size_ = 0;
    growBy_ = 0x80;
    unknown_14 = -1;
}
EI_REPLACE_METHOD(0x00468C20, CMemoryStream::Reset);
