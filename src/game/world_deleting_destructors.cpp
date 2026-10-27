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
#include "game_lists.h"
#include "record_arrays.h"
#include "game/game_slots.h"

namespace {

// The shape of every one of them: the body, the memory freed when bit 0 is set, the object back.
template <typename T, typename Body>
T* DestroyAndFree(T* self, unsigned flags, Body body) {
    body(self);
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}

} // namespace

template <>
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

EI_REWRITTEN_SPECIAL(0x005AA1A0, CFigureManager::~CFigureManager);   // deleting

mapspec::Behaviour* mapspec::Behaviour::DeleteScalar(unsigned deleteFlags) {
    return DestroyAndFree(this, deleteFlags, [](mapspec::Behaviour* behaviour) { mapspec::BehaviourDestruct(behaviour); });
}
EI_REWRITTEN(0x005DA290, mapspec::Behaviour::DeleteScalar);

using mfc::impl::ScalarDeleting;
EI_REWRITTEN(0x005754E0, ScalarDeleting<&ObjectHolderRelease>::Delete);
EI_REWRITTEN(0x00575510, ScalarDeleting<&ObjectHolderRelease>::Delete);
EI_REWRITTEN(0x005768A0, ScalarDeleting<&ObjectHolderRelease>::Delete);
EI_REWRITTEN(0x0059DDB0, ScalarDeleting<&ObjectHolderRelease>::Delete);

// ---- MFC collections ----------------------------------------------------------------------

EI_REWRITTEN(0x00571E50, ScalarDeleting<&mfc::impl::PtrList::Destruct<mfc::impl::PlatformListClass>>::Delete);
EI_REWRITTEN(0x00571ED0, ScalarDeleting<&recordarrays::WaterLevels::Destruct<mfc::impl::ClientMapArrayClass>>::Delete);
EI_REWRITTEN(0x0059B430, ScalarDeleting<&CSectorMap::DestructSectors>::Delete);
EI_REWRITTEN(0x0059B4F0, ScalarDeleting<&gamelists::SharedObjects::Destruct<gameslots::SharedObjectListClass>>::Delete);
EI_REWRITTEN(0x005B1B30, ScalarDeleting<&mfc::impl::PtrList::Destruct<mfc::impl::BlockCostListClass>>::Delete);
EI_REWRITTEN(0x005B1B50, ScalarDeleting<&mfc::impl::PtrList::Destruct<mfc::impl::AiMapListClass>>::Delete);
EI_REWRITTEN(0x005BE070, ScalarDeleting<&mfc::impl::PtrList::Destruct<mfc::impl::WaypointListClass>>::Delete);
