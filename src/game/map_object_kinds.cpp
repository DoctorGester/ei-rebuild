// The member-less map object classes (map_object_kinds.h, A-U6): CTree, CBuilding, CStone and
// CStuffObject.
#include "map_object_kinds.h"

#include "ei/hook.h"
#include "lib/crt.h"

#include <new>

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
PlainMapObject<ClassId>* PlainMapObject<ClassId>::Construct() {
    return new (this) PlainMapObject();
}

template <int ClassId>
PlainMapObject<ClassId>::~PlainMapObject() {
    DestroyMapObject();
}

template <int ClassId>
void PlainMapObject<ClassId>::DestructBody() {
    this->PlainMapObject::~PlainMapObject();
}

template <int ClassId>
PlainMapObject<ClassId>* PlainMapObject<ClassId>::DeletingDestruct(int flags) {
    this->PlainMapObject::~PlainMapObject();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
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

const char* __fastcall TreeClassName(CTree* self) { return self->CTree::GetClassName(); }
EI_REPLACE(0x00529730, TreeClassName);
int __fastcall TreeClassId(CTree* self) { return self->CTree::GetClassId(); }
EI_REPLACE(0x00529740, TreeClassId);
const char* __fastcall BuildingClassName(CBuilding* self) { return self->CBuilding::GetClassName(); }
EI_REPLACE(0x00529810, BuildingClassName);
int __fastcall BuildingClassId(CBuilding* self) { return self->CBuilding::GetClassId(); }
EI_REPLACE(0x00529820, BuildingClassId);
const char* __fastcall StoneClassName(CStone* self) { return self->CStone::GetClassName(); }
EI_REPLACE(0x005298F0, StoneClassName);
int __fastcall StoneClassId(CStone* self) { return self->CStone::GetClassId(); }
EI_REPLACE(0x00529900, StoneClassId);
const char* __fastcall StuffClassName(CStuffObject* self) { return self->CStuffObject::GetClassName(); }
EI_REPLACE(0x005299D0, StuffClassName);
int __fastcall StuffClassId(CStuffObject* self) { return self->CStuffObject::GetClassId(); }
EI_REPLACE(0x005299E0, StuffClassId);
