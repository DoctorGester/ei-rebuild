// The member-less map object classes (map_object_kinds.h, A-U6): CTree, CBuilding, CStone and
// CStuffObject.
#include "map_object_kinds.h"

#include "ei/hook.h"
#include "lib/crt.h"

namespace {
const char* const kTreeName = reinterpret_cast<const char*>(0x0078F6F8);       // "CTree"
const char* const kBuildingName = reinterpret_cast<const char*>(0x0078F700);   // "CBuilding"
const char* const kStoneName = reinterpret_cast<const char*>(0x0078F70C);      // "CStone"
const char* const kStuffName = reinterpret_cast<const char*>(0x0078F714);      // "CStuffObject"
constexpr int kClassTree = 0x52;
constexpr int kClassStone = 0x53;
constexpr int kClassBuilding = 0x54;
constexpr int kClassStuff = 0x56;
} // namespace

template <std::uintptr_t Vtable>
PlainMapObject<Vtable>* PlainMapObject<Vtable>::Construct() {
    CMapObject::Construct();
    *reinterpret_cast<std::uintptr_t*>(this) = Vtable;
    CreateMapObject();
    return this;
}

template <std::uintptr_t Vtable>
void PlainMapObject<Vtable>::DestructBody() {
    *reinterpret_cast<std::uintptr_t*>(this) = Vtable;
    DestroyMapObject();
    CMapObject::DestructBody();
}

template <std::uintptr_t Vtable>
PlainMapObject<Vtable>* PlainMapObject<Vtable>::DeletingDestruct(int flags) {
    DestructBody();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}

template class PlainMapObject<mapkind::kTreeVtable>;
template class PlainMapObject<mapkind::kBuildingVtable>;
template class PlainMapObject<mapkind::kStoneVtable>;
template class PlainMapObject<mapkind::kStuffVtable>;

EI_REPLACE_METHOD(0x005296E0, CTree::Construct);
EI_REPLACE_METHOD(0x00529770, CTree::DestructBody);
EI_REPLACE_METHOD(0x00529750, CTree::DeletingDestruct);
EI_REPLACE_METHOD(0x005297C0, CBuilding::Construct);
EI_REPLACE_METHOD(0x00529850, CBuilding::DestructBody);
EI_REPLACE_METHOD(0x00529830, CBuilding::DeletingDestruct);
EI_REPLACE_METHOD(0x005298A0, CStone::Construct);
EI_REPLACE_METHOD(0x00529930, CStone::DestructBody);
EI_REPLACE_METHOD(0x00529910, CStone::DeletingDestruct);
EI_REPLACE_METHOD(0x00529980, CStuffObject::Construct);
EI_REPLACE_METHOD(0x00529A10, CStuffObject::DestructBody);
EI_REPLACE_METHOD(0x005299F0, CStuffObject::DeletingDestruct);

const char* __fastcall TreeClassName(CMapObject*) { return kTreeName; }
EI_REPLACE(0x00529730, TreeClassName);
int __fastcall TreeClassId(CMapObject*) { return kClassTree; }
EI_REPLACE(0x00529740, TreeClassId);
const char* __fastcall BuildingClassName(CMapObject*) { return kBuildingName; }
EI_REPLACE(0x00529810, BuildingClassName);
int __fastcall BuildingClassId(CMapObject*) { return kClassBuilding; }
EI_REPLACE(0x00529820, BuildingClassId);
const char* __fastcall StoneClassName(CMapObject*) { return kStoneName; }
EI_REPLACE(0x005298F0, StoneClassName);
int __fastcall StoneClassId(CMapObject*) { return kClassStone; }
EI_REPLACE(0x00529900, StoneClassId);
const char* __fastcall StuffClassName(CMapObject*) { return kStuffName; }
EI_REPLACE(0x005299D0, StuffClassName);
int __fastcall StuffClassId(CMapObject*) { return kClassStuff; }
EI_REPLACE(0x005299E0, StuffClassId);
