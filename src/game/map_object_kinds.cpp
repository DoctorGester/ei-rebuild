// The member-less map object classes (map_object_kinds.h, A-U6): CTree, CBuilding, CStone and CStuffObject.
#include "map_object_kinds.h"

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

namespace {
template <int ClassId> constexpr const char* kName = nullptr;
template <> constexpr const char* kName<0x52> = "CTree";
template <> constexpr const char* kName<0x53> = "CStone";
template <> constexpr const char* kName<0x54> = "CBuilding";
template <> constexpr const char* kName<0x56> = "CStuffObject";
} // namespace

template <int ClassId>
PlainMapObject<ClassId>::PlainMapObject() {
    CreateMapObject();
}

template <int ClassId>
PlainMapObject<ClassId>::~PlainMapObject() {
    DestroyMapObject();
}

template <int ClassId>
const char* PlainMapObject<ClassId>::GetClassName() {
    return kName<ClassId>;
}

template <int ClassId>
int PlainMapObject<ClassId>::GetClassId() {
    return ClassId;
}

template class PlainMapObject<0x52>;
template class PlainMapObject<0x53>;
template class PlainMapObject<0x54>;
template class PlainMapObject<0x56>;
