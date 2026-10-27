#pragma once

// The map object classes without members of their own (map_object_kinds.cpp): CTree (class id 0x52), CStone
// (0x53), CBuilding (0x54) and CStuffObject (0x56), all 0x238 bytes and made by the map object factory. Real
// virtual classes. Each overrides only the destructor, class name and class id.

#include "map_object.h"
#include "game_data.h"
#include "lib/crt.h"

#include <cstdint>

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

template <int ClassId>
class PlainMapObject : public CMapObject {
public:
    PlainMapObject();
    ~PlainMapObject() override;
    const char* GetClassName() override;
    int GetClassId() override;
};

// Constructor, destructor, deleting destructor; slots 11 / 12.
using CTree = PlainMapObject<0x52>;
using CStone = PlainMapObject<0x53>;
using CBuilding = PlainMapObject<0x54>;
using CStuffObject = PlainMapObject<0x56>;

// Slots 11 / 12's registrations: "CTree" 0x52, "CBuilding" 0x54, "CStone" 0x53, "CStuffObject" 0x56.
const char* __fastcall TreeClassName(CTree* self);
int __fastcall TreeClassId(CTree* self);
const char* __fastcall BuildingClassName(CBuilding* self);
int __fastcall BuildingClassId(CBuilding* self);
const char* __fastcall StoneClassName(CStone* self);
int __fastcall StoneClassId(CStone* self);
const char* __fastcall StuffClassName(CStuffObject* self);
int __fastcall StuffClassId(CStuffObject* self);
