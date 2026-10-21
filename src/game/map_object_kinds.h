#pragma once

// The map object classes without members of their own (A-U6, map_object_kinds.cpp): CTree (class
// id 0x52), CStone (0x53), CBuilding (0x54) and CStuffObject (0x56), all 0x238 bytes and made by the
// factory 00512730. Real virtual classes: the compiler makes their vtables (C19; game.exe's
// 0073E178, 0073E360, 0073E26C and 0073E454 are retired). Each overrides only slots 0, 11 and 12.

#include "map_object.h"
#include "game_data.h"
#include "lib/crt.h"

#include <cstdint>

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

template <int ClassId>
class PlainMapObject : public CMapObject {
public:
    // The map object's constructor, then its Create (0050E770) called directly.
    PlainMapObject();
    // The map object's Destroy (0050E840), then its destructor.
    ~PlainMapObject() override;
    // The deleting destructor (slot 0) frees into the game's heap.
    static void operator delete(void* block) { gamecrt::operator_delete(block); }
    const char* GetClassName() override;   // 11: the class name
    int GetClassId() override;             // 12: ClassId

    // The registrations (C20 retires them): the constructor in place (returns this), the
    // destructor, slot 0 (RET 4: the destructor, then operator delete when `flags` bit 0 is set;
    // returns this).
    PlainMapObject* Construct();
    void DestructBody();
    PlainMapObject* DeletingDestruct(int flags);
};

using CTree = PlainMapObject<0x52>;          // 005296E0, 00529770, 00529750; 11 / 12: 00529730 / 00529740
using CStone = PlainMapObject<0x53>;         // 005298A0, 00529930, 00529910; 005298F0 / 00529900
using CBuilding = PlainMapObject<0x54>;      // 005297C0, 00529850, 00529830; 00529810 / 00529820
using CStuffObject = PlainMapObject<0x56>;   // 00529980, 00529A10, 005299F0; 005299D0 / 005299E0

// Slots 11 / 12's registrations: "CTree" 0x52, "CBuilding" 0x54, "CStone" 0x53, "CStuffObject" 0x56.
const char* __fastcall TreeClassName(CTree* self);
int __fastcall TreeClassId(CTree* self);
const char* __fastcall BuildingClassName(CBuilding* self);
int __fastcall BuildingClassId(CBuilding* self);
const char* __fastcall StoneClassName(CStone* self);
int __fastcall StoneClassId(CStone* self);
const char* __fastcall StuffClassName(CStuffObject* self);
int __fastcall StuffClassId(CStuffObject* self);
