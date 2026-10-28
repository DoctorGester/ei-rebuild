// The member-less map object classes (map_object_kinds.h, A-U6): CTree, CBuilding, CStone and CStuffObject.
#include "map_object_kinds.h"

namespace {
template <ClassId Id> constexpr const char* kName = nullptr;
template <> constexpr const char* kName<ClassId::Tree> = "CTree";
template <> constexpr const char* kName<ClassId::Stone> = "CStone";
template <> constexpr const char* kName<ClassId::Building> = "CBuilding";
template <> constexpr const char* kName<ClassId::StuffObject> = "CStuffObject";
} // namespace

template <ClassId Id>
PlainMapObject<Id>::PlainMapObject() {
    CreateMapObject();
}

template <ClassId Id>
PlainMapObject<Id>::~PlainMapObject() {
    DestroyMapObject();
}

template <ClassId Id>
const char* PlainMapObject<Id>::ClassName() {
    return kName<Id>;
}

template <ClassId Id>
ClassId PlainMapObject<Id>::GetClassId() {
    return Id;
}

template class PlainMapObject<ClassId::Tree>;
template class PlainMapObject<ClassId::Stone>;
template class PlainMapObject<ClassId::Building>;
template class PlainMapObject<ClassId::StuffObject>;
