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
#include "originals_library.h"

void __fastcall DestroyFigureManager(char* manager);   // figure_model_destroy.cpp (005AA1C0)
void __fastcall ObjectHolderRelease(CEngineObject** self);   // refptr_copies.cpp

namespace {

// The shape of every one of them: the body, the memory freed when bit 0 is set, the object back.
template <typename T, typename Body>
T* DestroyAndFree(T* self, unsigned flags, Body body) {
    body(self);
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}

// The MFC collection destructors the deleting destructors below run (library rows).
__declspec(naked) void __fastcall PlatformListDestruct(void*) EI_ORIGINAL(0x0056FE40)
__declspec(naked) void __fastcall ClientArrayDestruct(void*) EI_ORIGINAL(0x005711E0)
void __fastcall SectorArrayDestruct(void* array) { SectorArrayDestructOriginal(array); }
__declspec(naked) void __fastcall ArrayElementListDestruct(void*) EI_ORIGINAL(0x0059A3C0)
__declspec(naked) void __fastcall BlockCostListDestruct(void*) EI_ORIGINAL(0x005B1810)
__declspec(naked) void __fastcall AIMapListDestruct(void*) EI_ORIGINAL(0x005B19A0)
__declspec(naked) void __fastcall WaypointListDestruct(void*) EI_ORIGINAL(0x005BDEE0)

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
    return DestroyAndFree(self, flags, [](CEngineObject** holder) { ObjectHolderRelease(holder); });
}
EI_REPLACE(0x005754E0, ObjectHolderDelete);
EI_REPLACE(0x00575510, ObjectHolderDelete);
EI_REPLACE(0x005768A0, ObjectHolderDelete);
EI_REPLACE(0x0059DDB0, ObjectHolderDelete);

// ---- MFC collections (S13) ----------------------------------------------------------------------

void* __fastcall PlatformListDelete(void* self, void*, unsigned flags) { return DestroyAndFree(self, flags, PlatformListDestruct); }
EI_REPLACE(0x00571E50, PlatformListDelete);

void* __fastcall ClientArrayDelete(void* self, void*, unsigned flags) { return DestroyAndFree(self, flags, ClientArrayDestruct); }
EI_REPLACE(0x00571ED0, ClientArrayDelete);

void* __fastcall SectorArrayDelete(void* self, void*, unsigned flags) { return DestroyAndFree(self, flags, SectorArrayDestruct); }
EI_REPLACE(0x0059B430, SectorArrayDelete);

void* __fastcall ArrayElementListDelete(void* self, void*, unsigned flags) { return DestroyAndFree(self, flags, ArrayElementListDestruct); }
EI_REPLACE(0x0059B4F0, ArrayElementListDelete);

void* __fastcall BlockCostListDelete(void* self, void*, unsigned flags) { return DestroyAndFree(self, flags, BlockCostListDestruct); }
EI_REPLACE(0x005B1B30, BlockCostListDelete);

void* __fastcall AIMapListDelete(void* self, void*, unsigned flags) { return DestroyAndFree(self, flags, AIMapListDestruct); }
EI_REPLACE(0x005B1B50, AIMapListDelete);

void* __fastcall WaypointListDelete(void* self, void*, unsigned flags) { return DestroyAndFree(self, flags, WaypointListDestruct); }
EI_REPLACE(0x005BE070, WaypointListDelete);
