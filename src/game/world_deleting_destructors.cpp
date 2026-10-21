// The scalar deleting destructors of the world range whose bodies are rewritten elsewhere (A-W14).
// See world_deleting_destructors.h.
#include "world_deleting_destructors.h"

#include "core_refcount.h"
#include "figure_manager.h"
#include "map_spec.h"
#include "script.h"
#include "sector_map.h"
#include "unit_ai.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "originals_library.h"
#include "game_lists.h"
#include "record_arrays.h"
#include "game/game_slots.h"

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

// The MFC collection destructors the deleting destructors below run: the instantiations' own
// (0056FE40, 005711E0, 0059A3C0, 005B1810, 005B19A0, 005BDEE0).
template <typename Abi, typename Class>
void ListDestruct(void* list) { Abi::template Destruct<Class>(static_cast<typename Abi::List*>(list)); }
template <typename Abi, typename Class>
void ArrayDestruct(void* array) { Abi::template Destruct<Class>(static_cast<typename Abi::Array*>(array)); }
constexpr auto PlatformListDestruct = ListDestruct<mfc::impl::PtrList, mfc::impl::PlatformListClass>;
constexpr auto ClientArrayDestruct = ArrayDestruct<recordarrays::WaterLevels, mfc::impl::ClientMapArrayClass>;
void __fastcall SectorArrayDestruct(void* array) {
    CSectorMap::DestructSectors(static_cast<mfc::CArray<Sector, SectorElements>*>(array));
}
constexpr auto ArrayElementListDestruct = ListDestruct<gamelists::SharedObjects, gameslots::SharedObjectListClass>;
constexpr auto BlockCostListDestruct = ListDestruct<mfc::impl::PtrList, mfc::impl::BlockCostListClass>;
constexpr auto AIMapListDestruct = ListDestruct<mfc::impl::PtrList, mfc::impl::AiMapListClass>;
constexpr auto WaypointListDestruct = ListDestruct<mfc::impl::PtrList, mfc::impl::WaypointListClass>;

} // namespace

AIRef* AIRef::DeleteScalar(unsigned flags) {
    return DestroyAndFree(this, flags, [](AIRef* ref) { ref->Reset(); });
}
EI_REWRITTEN(0x00558CA0, AIRef::DeleteScalar);

AICallRecord* AICallRecord::DeleteScalar(unsigned flags) {
    return DestroyAndFree(this, flags, [](AICallRecord* call) { call->Destruct(); });
}
EI_REWRITTEN(0x00558CC0, AICallRecord::DeleteScalar);

RefPtr* RefPtr::DeleteDestroyed(unsigned flags) {
    return DestroyAndFree(this, flags, [](RefPtr* pointer) { pointer->Destroy(); });
}
EI_REWRITTEN(0x00560590, RefPtr::DeleteDestroyed);

ScriptPointLight* ScriptPointLight::DeleteScalar(unsigned flags) {
    return DestroyAndFree(this, flags, [](ScriptPointLight* light) { light->ScriptPointLightDestruct(); });
}
EI_REWRITTEN(0x0059DD70, ScriptPointLight::DeleteScalar);

CFigureManager* CFigureManager::DeleteScalar(unsigned flags) {
    return DestroyAndFree(this, flags, [](CFigureManager* manager) { DestroyFigureManager(reinterpret_cast<char*>(manager)); });
}
EI_REWRITTEN(0x005AA1A0, CFigureManager::DeleteScalar);

mapspec::Behaviour* mapspec::Behaviour::DeleteScalar(unsigned deleteFlags) {
    return DestroyAndFree(this, deleteFlags, [](mapspec::Behaviour* behaviour) { mapspec::BehaviourDestruct(behaviour); });
}
EI_REWRITTEN(0x005DA290, mapspec::Behaviour::DeleteScalar);

CEngineObject** __fastcall ObjectHolderDelete(CEngineObject** self, void*, unsigned flags) {
    return DestroyAndFree(self, flags, [](CEngineObject** holder) { ObjectHolderRelease(holder); });
}
EI_REWRITTEN(0x005754E0, ObjectHolderDelete);
EI_REWRITTEN(0x00575510, ObjectHolderDelete);
EI_REWRITTEN(0x005768A0, ObjectHolderDelete);
EI_REWRITTEN(0x0059DDB0, ObjectHolderDelete);

// ---- MFC collections (S13) ----------------------------------------------------------------------

void* __fastcall PlatformListDelete(void* self, void*, unsigned flags) { return DestroyAndFree(self, flags, PlatformListDestruct); }
EI_REPLACE(0x00571E50, PlatformListDelete);

void* __fastcall ClientArrayDelete(void* self, void*, unsigned flags) { return DestroyAndFree(self, flags, ClientArrayDestruct); }
EI_REPLACE(0x00571ED0, ClientArrayDelete);

void* __fastcall SectorArrayDelete(void* self, void*, unsigned flags) { return DestroyAndFree(self, flags, SectorArrayDestruct); }
EI_REPLACE(0x0059B430, SectorArrayDelete);

void* __fastcall ArrayElementListDelete(void* self, void*, unsigned flags) { return DestroyAndFree(self, flags, ArrayElementListDestruct); }
EI_REWRITTEN(0x0059B4F0, ArrayElementListDelete);

void* __fastcall BlockCostListDelete(void* self, void*, unsigned flags) { return DestroyAndFree(self, flags, BlockCostListDestruct); }
EI_REPLACE(0x005B1B30, BlockCostListDelete);

void* __fastcall AIMapListDelete(void* self, void*, unsigned flags) { return DestroyAndFree(self, flags, AIMapListDestruct); }
EI_REPLACE(0x005B1B50, AIMapListDelete);

void* __fastcall WaypointListDelete(void* self, void*, unsigned flags) { return DestroyAndFree(self, flags, WaypointListDestruct); }
EI_REPLACE(0x005BE070, WaypointListDelete);
