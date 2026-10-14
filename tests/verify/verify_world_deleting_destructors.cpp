// Checks for world_deleting_destructors.cpp (A-W14): ours only (each destroys and may free its
// object, so the original cannot run beside it); the wrapper records that the destructor ran and
// that it returns its object.
// Reached: the figure manager's (slot 0 of 0073FB88) when the client world is torn down; the
// holder's 0059DDB0 from CArray<CRefPtr>::Copy 0059B8F0 while a save is loaded; 00560590 from the
// driver session's teardown; the others only from library array code and from destructors that run
// at process exit (005754E0 from the client world's, 0059DD70 from the server's).
#include "verify.h"

#include "../../src/game/map_spec.h"
#include "../../src/game/world_deleting_destructors.h"

namespace {

void* s_unused;

#define EI_DELETE_CHECK(Type, Function)                                                              \
    Type* __fastcall Function##Verify(Type* self, void*, unsigned flags) {                           \
        Type* result = Function(self, nullptr, flags);                                               \
        ei::verify::Check(#Function, true, result == self, "flags %u", flags);                       \
        return result;                                                                               \
    }

EI_DELETE_CHECK(AIRef, AIRefDelete)
EI_DELETE_CHECK(AICallRecord, AICallRecordDelete)
EI_DELETE_CHECK(RefPtr, RefPtrDestroyDelete)
EI_DELETE_CHECK(ScriptPointLight, ScriptPointLightDelete)
EI_DELETE_CHECK(CFigureManager, FigureManagerDelete)
EI_DELETE_CHECK(mapspec::Behaviour, BehaviourDelete)
EI_DELETE_CHECK(CEngineObject*, ObjectHolderDelete)

} // namespace

EI_VERIFY(0x00558CA0, AIRefDeleteVerify, 0, s_unused);
EI_VERIFY(0x00558CC0, AICallRecordDeleteVerify, 0, s_unused);
EI_VERIFY(0x00560590, RefPtrDestroyDeleteVerify, 0, s_unused);
EI_VERIFY(0x0059DD70, ScriptPointLightDeleteVerify, 0, s_unused);
EI_VERIFY(0x005AA1A0, FigureManagerDeleteVerify, 0, s_unused);
EI_VERIFY(0x005DA290, BehaviourDeleteVerify, 0, s_unused);
EI_VERIFY(0x005754E0, ObjectHolderDeleteVerify, 0, s_unused);
EI_VERIFY(0x00575510, ObjectHolderDeleteVerify, 0, s_unused);
EI_VERIFY(0x005768A0, ObjectHolderDeleteVerify, 0, s_unused);
EI_VERIFY(0x0059DDB0, ObjectHolderDeleteVerify, 0, s_unused);

// 0058EDD0 / 0058EDF0: the quick item effect's record (vtable 0073FA58) shares the object record's
// destructors (map_spec.cpp): ours only. No scenario reaches them: UseQuickItem (rewritten) keeps
// the record on its stack and destroys it through 0046AEB0.
namespace {
void __fastcall QuickSpecDestructVerify(mapspec::ObjectSpec* self) {
    self->ObjectSpecDestruct();
    ei::verify::Check("ObjectSpecDestruct@0058EDF0", true, true, "");
}
mapspec::ObjectSpec* __fastcall QuickSpecDeleteVerify(mapspec::ObjectSpec* self, void*, unsigned flags) {
    mapspec::ObjectSpec* result = self->ObjectSpecDelete(flags);
    ei::verify::Check("ObjectSpecDelete@0058EDD0", true, result == self, "flags %u", flags);
    return result;
}
} // namespace

EI_VERIFY(0x0058EDF0, QuickSpecDestructVerify, 0, s_unused);
EI_VERIFY(0x0058EDD0, QuickSpecDeleteVerify, 0, s_unused);
