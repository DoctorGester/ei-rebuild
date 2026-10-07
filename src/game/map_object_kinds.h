#pragma once

// The map object classes without members of their own (A-U6, map_object_kinds.cpp): CTree (class
// id 0x52, vtable 0073E178), CStone (0x53, 0073E360), CBuilding (0x54, 0073E26C) and CStuffObject
// (0x56, 0073E454), all 0x238 bytes and made by the factory 00512730. Each has a constructor (the
// map object's, the vtable, the map object's Create 0050E770 called directly; returns this), a
// destructor body (the vtable, the map object's Destroy 0050E840 and destructor body 0050E660) and
// slot 0 (RET 4: the destructor body, then operator delete when `flags` bit 0 is set; returns
// this); slots 11 / 12 return the class name and id.

#include "map_object.h"

#include <cstdint>

namespace mapkind {
constexpr std::uintptr_t kTreeVtable = std::uintptr_t(0x0073E178);
constexpr std::uintptr_t kBuildingVtable = std::uintptr_t(0x0073E26C);
constexpr std::uintptr_t kStoneVtable = std::uintptr_t(0x0073E360);
constexpr std::uintptr_t kStuffVtable = std::uintptr_t(0x0073E454);
} // namespace mapkind

template <std::uintptr_t Vtable>
class PlainMapObject : public CMapObject {
public:
    PlainMapObject* Construct();
    void DestructBody();
    PlainMapObject* DeletingDestruct(int flags);
};

using CTree = PlainMapObject<mapkind::kTreeVtable>;              // 005296E0, 00529770, 00529750
using CBuilding = PlainMapObject<mapkind::kBuildingVtable>;      // 005297C0, 00529850, 00529830
using CStone = PlainMapObject<mapkind::kStoneVtable>;            // 005298A0, 00529930, 00529910
using CStuffObject = PlainMapObject<mapkind::kStuffVtable>;      // 00529980, 00529A10, 005299F0

// Slots 11 / 12: 00529730 / 00529740 "CTree" 0x52, 00529810 / 00529820 "CBuilding" 0x54,
// 005298F0 / 00529900 "CStone" 0x53, 005299D0 / 005299E0 "CStuffObject" 0x56.
const char* __fastcall TreeClassName(CMapObject* self);
int __fastcall TreeClassId(CMapObject* self);
const char* __fastcall BuildingClassName(CMapObject* self);
int __fastcall BuildingClassId(CMapObject* self);
const char* __fastcall StoneClassName(CMapObject* self);
int __fastcall StoneClassId(CMapObject* self);
const char* __fastcall StuffClassName(CMapObject* self);
int __fastcall StuffClassId(CMapObject* self);
