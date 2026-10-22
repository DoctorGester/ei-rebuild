// The member-less map object classes (map_object_kinds.h, A-U6): CTree, CBuilding, CStone and
// CStuffObject.
#include "map_object_kinds.h"

#include "ei/hook.h"

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

EI_REWRITTEN_SPECIAL(0x005296E0, CTree::CTree);
EI_REWRITTEN_SPECIAL(0x00529770, CTree::~CTree);
EI_REWRITTEN_SPECIAL(0x00529750, CTree::~CTree);   // deleting
EI_REWRITTEN_SPECIAL(0x005297C0, CBuilding::CBuilding);
EI_REWRITTEN_SPECIAL(0x00529850, CBuilding::~CBuilding);
EI_REWRITTEN_SPECIAL(0x00529830, CBuilding::~CBuilding);   // deleting
EI_REWRITTEN_SPECIAL(0x005298A0, CStone::CStone);
EI_REWRITTEN_SPECIAL(0x00529930, CStone::~CStone);
EI_REWRITTEN_SPECIAL(0x00529910, CStone::~CStone);   // deleting
EI_REWRITTEN_SPECIAL(0x00529980, CStuffObject::CStuffObject);
EI_REWRITTEN_SPECIAL(0x00529A10, CStuffObject::~CStuffObject);
EI_REWRITTEN_SPECIAL(0x005299F0, CStuffObject::~CStuffObject);   // deleting

const char* __fastcall TreeClassName(CTree* self) { return self->CTree::GetClassName(); }
EI_REWRITTEN(0x00529730, TreeClassName);
int __fastcall TreeClassId(CTree* self) { return self->CTree::GetClassId(); }
EI_REWRITTEN(0x00529740, TreeClassId);
const char* __fastcall BuildingClassName(CBuilding* self) { return self->CBuilding::GetClassName(); }
EI_REWRITTEN(0x00529810, BuildingClassName);
int __fastcall BuildingClassId(CBuilding* self) { return self->CBuilding::GetClassId(); }
EI_REWRITTEN(0x00529820, BuildingClassId);
const char* __fastcall StoneClassName(CStone* self) { return self->CStone::GetClassName(); }
EI_REWRITTEN(0x005298F0, StoneClassName);
int __fastcall StoneClassId(CStone* self) { return self->CStone::GetClassId(); }
EI_REWRITTEN(0x00529900, StoneClassId);
const char* __fastcall StuffClassName(CStuffObject* self) { return self->CStuffObject::GetClassName(); }
EI_REWRITTEN(0x005299D0, StuffClassName);
int __fastcall StuffClassId(CStuffObject* self) { return self->CStuffObject::GetClassId(); }
EI_REWRITTEN(0x005299E0, StuffClassId);
