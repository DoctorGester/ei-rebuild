// The unit's link to its controller and player: the hostile mask, the motion rebuilt after a path
// change, the owning player, and the release / Destroy slots. See unit.h and docs/notes/units.md.
#include "player_messages.h"
#include "unit.h"

#include "ai_motion.h"
#include "map_object.h"

#include "lib/crt.h"

#include "ei/hook.h"
#include "originals_gameplay.h"

namespace {
constexpr std::uint32_t kFlagRelationsChanged = 0x80000;   // flags_
constexpr int kPartRelations = 11;                          // MarkChanged parts
constexpr int kPartPath = 10;

void ReleaseSlots(CEngineObject** slots, int count) {
    for (int i = 0; i < count; ++i) {
        if (slots[i]) slots[i]->Release();
        slots[i] = nullptr;
    }
}
} // namespace

void CUnit::SetHostileMask(std::uint32_t mask) {
    relationsA = mask;
    Controller()->HostileMaskChanged();
    flags_ |= kFlagRelationsChanged;
    MarkChanged(kPartRelations);
}
EI_REPLACE_METHOD(0x0052E970, CUnit::SetHostileMask);

void CUnit::RefreshMotion() {
    AIPath* path = Controller()->Path();
    path->ForgetObject(id_);
    float speed, turnRate;
    GetMoveSpeeds(&speed, &turnRate);
    UnitMotion* unitMotion = Motion();
    unitMotion->segment = -1;
    unitMotion->speedScale = speed;
    unitMotion->baseTurnRate = turnRate;
    unitMotion->Build(path);
    MarkChanged(kPartPath);
}
EI_REPLACE_METHOD(0x0052E9D0, CUnit::RefreshMotion);

void CUnit::SetPlayer(char* newPlayer) {
    char* old = player;
    if (old == newPlayer) return;
    player = newPlayer;
    if (old) reinterpret_cast<CPlayer*>(old)->RemoveUnit(this);
    if (player) reinterpret_cast<CPlayer*>(player)->AddUnit(this);
    MarkChanged(kPartRelations);
    if (controller) Controller()->PlayerChanged(newPlayer);
}
EI_REPLACE_METHOD(0x005300D0, CUnit::SetPlayer);

void CUnit::ReleaseUnit() {
    // The map object's slot 5 (0050E760) is empty.
    SetPlayer(nullptr);
    if (controller) controller->ClearState();
    ReleaseSlots(quickItems, 4);
    ReleaseSlots(weapons, 4);
    ReleaseSlots(spells, 8);
    ReleaseSlots(armor, 7);
    for (int i = 0; i < carriedCount; ++i) carried[i]->Release();
    if (carried) {
        gamecrt::operator_delete(carried);
        carried = nullptr;
    }
    carriedCapacity = 0;
    carriedCount = 0;
}
EI_REPLACE_METHOD(0x0052E740, CUnit::ReleaseUnit);

void CUnit::DestroyUnit() {
    ClearState();
    reinterpret_cast<CMapObject*>(this)->DestroyMapObject();
    Reset();
}
EI_REPLACE_METHOD(0x0052E590, CUnit::DestroyUnit);
