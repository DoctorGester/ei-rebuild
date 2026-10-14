// The scalar deleting destructors of the world range whose bodies are rewritten elsewhere (A-W14).
// See world_deleting_destructors.h.
#include "world_deleting_destructors.h"

#include "core_refcount.h"
#include "figure_manager.h"
#include "map_spec.h"
#include "script.h"
#include "unit_ai.h"

#include "ei/hook.h"
#include "lib/crt.h"

void __fastcall DestroyFigureManager(char* manager);   // figure_model_destroy.cpp (005AA1C0)
void __fastcall ObjectHolderRelease(void** self);      // refptr_copies.cpp

namespace {

// The shape of every one of them: the body, the memory freed when bit 0 is set, the object back.
template <typename T, typename Body>
T* DestroyAndFree(T* self, unsigned flags, Body body) {
    body(self);
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}

} // namespace

AIRef* __fastcall AIRefDelete(AIRef* self, void*, unsigned flags) {
    return DestroyAndFree(self, flags, [](AIRef* ref) { ref->Reset(); });
}
EI_REPLACE(0x00558CA0, AIRefDelete);

AICallRecord* __fastcall AICallRecordDelete(AICallRecord* self, void*, unsigned flags) {
    return DestroyAndFree(self, flags, [](AICallRecord* call) { call->Destruct(); });
}
EI_REPLACE(0x00558CC0, AICallRecordDelete);

RefPtr* __fastcall RefPtrDestroyDelete(RefPtr* self, void*, unsigned flags) {
    return DestroyAndFree(self, flags, [](RefPtr* pointer) { pointer->Destroy(); });
}
EI_REPLACE(0x00560590, RefPtrDestroyDelete);

ScriptPointLight* __fastcall ScriptPointLightDelete(ScriptPointLight* self, void*, unsigned flags) {
    return DestroyAndFree(self, flags, [](ScriptPointLight* light) { light->ScriptPointLightDestruct(); });
}
EI_REPLACE(0x0059DD70, ScriptPointLightDelete);

CFigureManager* __fastcall FigureManagerDelete(CFigureManager* self, void*, unsigned flags) {
    return DestroyAndFree(self, flags, [](CFigureManager* manager) { DestroyFigureManager(reinterpret_cast<char*>(manager)); });
}
EI_REPLACE(0x005AA1A0, FigureManagerDelete);

mapspec::Behaviour* __fastcall BehaviourDelete(mapspec::Behaviour* self, void*, unsigned flags) {
    return DestroyAndFree(self, flags, [](mapspec::Behaviour* behaviour) { mapspec::BehaviourDestruct(behaviour); });
}
EI_REPLACE(0x005DA290, BehaviourDelete);

CEngineObject** __fastcall ObjectHolderDelete(CEngineObject** self, void*, unsigned flags) {
    return DestroyAndFree(self, flags, [](CEngineObject** holder) { ObjectHolderRelease(reinterpret_cast<void**>(holder)); });
}
EI_REPLACE(0x005754E0, ObjectHolderDelete);
EI_REPLACE(0x00575510, ObjectHolderDelete);
EI_REPLACE(0x005768A0, ObjectHolderDelete);
EI_REPLACE(0x0059DDB0, ObjectHolderDelete);
