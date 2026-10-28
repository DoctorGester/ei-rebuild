// The unit's link to its controller and player: the hostile mask, the motion rebuilt after a path
// change, the owning player, and the release / Destroy slots. See unit.h and docs/notes/units.md.
#include "player_messages.h"
#include "unit.h"

#include "ai_motion.h"
#include "map_object.h"

#include "lib/crt.h"

namespace {
constexpr std::uint32_t kFlagRelationsChanged = 0x80000;   // flags_
constexpr int kPartRelations = 11;                          // MarkChanged parts
constexpr int kPartPath = 10;

template <typename Item>
void ReleaseSlots(Item** slots, int count) {
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

void CUnit::RefreshMotion() {
    PathOrder* path = Controller()->Path();
    path->ForgetObject(id_);
    float speed, turnRate;
    GetMoveSpeeds(&speed, &turnRate);
    UnitMotion* unitMotion = &motion;
    unitMotion->segment = -1;
    unitMotion->speedScale = speed;
    unitMotion->baseTurnRate = turnRate;
    unitMotion->Build(path);
    MarkChanged(kPartPath);
}

void CUnit::SetPlayer(CPlayer* newPlayer) {
    CPlayer* old = player;
    if (old == newPlayer) return;
    player = newPlayer;
    if (old) old->RemoveUnit(this);
    if (player) player->AddUnit(this);
    MarkChanged(kPartRelations);
    if (controller) Controller()->PlayerChanged(newPlayer);
}

void CUnit::ReleaseUnit() {
    // The map object's slot 5 is empty.
    SetPlayer(nullptr);
    if (controller) controller->ClearState();
    ReleaseSlots(quickItems, 4);
    ReleaseSlots(weapons, 4);
    ReleaseSlots(spells, 8);
    ReleaseSlots(armor, 7);
    for (CItem* item : carried) item->Release();
    carried.clear();
}

void CUnit::DestroyUnit() {
    ClearState();
    DestroyMapObject();
    Reset();
}
