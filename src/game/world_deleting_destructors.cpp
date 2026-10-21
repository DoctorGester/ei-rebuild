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
#include "lib/mfc_slots.h"
#include "originals_library.h"
#include "game_lists.h"
#include "record_arrays.h"
#include "game/game_slots.h"

void __fastcall DestroyFigureManager(char* manager);   // figure_model_destroy.cpp (005AA1C0)

namespace {

// The shape of every one of them: the body, the memory freed when bit 0 is set, the object back.
template <typename T, typename Body>
T* DestroyAndFree(T* self, unsigned flags, Body body) {
    body(self);
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}

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

using mfc::impl::ScalarDelete;
EI_REWRITTEN(0x005754E0, ScalarDelete<&ObjectHolderRelease>);
EI_REWRITTEN(0x00575510, ScalarDelete<&ObjectHolderRelease>);
EI_REWRITTEN(0x005768A0, ScalarDelete<&ObjectHolderRelease>);
EI_REWRITTEN(0x0059DDB0, ScalarDelete<&ObjectHolderRelease>);

// ---- MFC collections (S13) ----------------------------------------------------------------------

EI_REWRITTEN(0x00571E50, ScalarDelete<&mfc::impl::PtrList::Destruct<mfc::impl::PlatformListClass>>);
EI_REWRITTEN(0x00571ED0, ScalarDelete<&recordarrays::WaterLevels::Destruct<mfc::impl::ClientMapArrayClass>>);
EI_REWRITTEN(0x0059B430, ScalarDelete<&CSectorMap::DestructSectors>);
EI_REWRITTEN(0x0059B4F0, ScalarDelete<&gamelists::SharedObjects::Destruct<gameslots::SharedObjectListClass>>);
EI_REWRITTEN(0x005B1B30, ScalarDelete<&mfc::impl::PtrList::Destruct<mfc::impl::BlockCostListClass>>);
EI_REWRITTEN(0x005B1B50, ScalarDelete<&mfc::impl::PtrList::Destruct<mfc::impl::AiMapListClass>>);
EI_REWRITTEN(0x005BE070, ScalarDelete<&mfc::impl::PtrList::Destruct<mfc::impl::WaypointListClass>>);
