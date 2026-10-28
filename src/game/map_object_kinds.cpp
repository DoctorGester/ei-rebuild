// The member-less map object classes (map_object_kinds.h, A-U6): CTree, CBuilding, CStone and CStuffObject.
#include "map_object_kinds.h"

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

namespace {
template <ClassId Id> constexpr const char* kName = nullptr;
template <> constexpr const char* kName<kClassTree> = "CTree";
template <> constexpr const char* kName<kClassStone> = "CStone";
template <> constexpr const char* kName<kClassBuilding> = "CBuilding";
template <> constexpr const char* kName<kClassStuffObject> = "CStuffObject";
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
const char* PlainMapObject<Id>::GetClassName() {
    return kName<Id>;
}

template <ClassId Id>
ClassId PlainMapObject<Id>::GetClassId() {
    return Id;
}

template class PlainMapObject<kClassTree>;
template class PlainMapObject<kClassStone>;
template class PlainMapObject<kClassBuilding>;
template class PlainMapObject<kClassStuffObject>;
