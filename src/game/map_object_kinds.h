#pragma once

// The map object classes without members of their own (map_object_kinds.cpp): CTree, CStone, CBuilding and
// CStuffObject, all 0x238 bytes and made by the map object factory. Real
// virtual classes. Each overrides only the destructor, class name and class id.

#include "map_object.h"

#include <cstdint>

template <ClassId Id>
class PlainMapObject : public CMapObject {
public:
    PlainMapObject();
    ~PlainMapObject() override;
    const char* ClassName() override;
    ClassId GetClassId() override;
};

// Kept: the map object kinds are the class names (one instance of PlainMapObject per class id).
// Constructor, destructor, deleting destructor and two more virtuals.
using CTree = PlainMapObject<ClassId::Tree>;
using CStone = PlainMapObject<ClassId::Stone>;
using CBuilding = PlainMapObject<ClassId::Building>;
using CStuffObject = PlainMapObject<ClassId::StuffObject>;

