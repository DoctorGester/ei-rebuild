// Unit setup and upkeep around creation: the prototype name index, the server controller's
// statistics hand-over and remarks, and dropping broken equipment after a hit. See
// docs/notes/units.md ("Creation, databases, save").
#include "game/units/unit_create.h"

#include "game/combat/combat.h"
#include "engine/core/engine_object.h"
#include "game/items/item.h"
#include "game/objects/map_object.h"
#include "game/players/player_messages.h"
#include "game/players/player.h"
#include "game/units/unit.h"
#include "game/units/unit_db.h"
#include "game/world/world.h"

#include "lib/crt.h"
#include "lib/rounding.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace {
constexpr float kTenth = 0.1f;

PlayerServer* PlayerMessages(const Player* player) { return player->Server(); }

// The item's condition rounds to nothing.
bool IsBroken(const Item* item) {
    return lib::RoundEven(item->condition) <= 0;
}

} // namespace

// --- Prototype names --------------------------------------------------------------------------

UnitPrototypeRecord* UnitPrototypeTable::FindByName(std::string_view name) {
    auto found = byName.find(std::string(name));
    return found != byName.end() ? found->second : nullptr;
}

// --- Server controller ------------------------------------------------------------------------

void UnitServer::SetStats(const UnitStats* source) {
    std::memcpy(&stats, source, sizeof(UnitStats));
    unit->MarkChanged(NetField::Stats);
    RefreshVitals(false);
    stats.RefillSlots();
    RefreshVitals(false);
    // The companion's vitals: hit points and mana at their maxima (the companion read again).
    auto* companion = unit->ServerCompanion();
    companion->vitals.hitPoints = companion->vitals.maxHitPoints;
    companion = unit->ServerCompanion();
    companion->vitals.mana = companion->vitals.maxMana;
}

void UnitServer::Shout(AckKind kind) {
    if (!IsShoutable(kind)) return;
    if (!unit->player) return;
    PlayerMessages(unit->player)->SendUnitSpeech(unit, kind);
}

// --- Broken equipment -------------------------------------------------------------------------

void Unit::DropBrokenEquipment() {
    Player* ownerObject = player;
    if (!ownerObject) return;
    // Each broken piece leaves its slot with the slot's reference, is given to the player, then released.
    std::vector<Item*> broken;
    PlayerServer* messages = PlayerMessages(ownerObject);

    for (ItemArmor*& slot : armor) {
        if (!slot || !IsBroken(slot)) continue;
        ItemArmor* piece = std::exchange(slot, nullptr);
        broken.push_back(piece);
        controller->ArmourTakenOff(static_cast<std::uint32_t>(piece->GetType()));
        MarkChanged(NetField::Equipment);
        MarkChanged(NetField::Stats);
    }

    bool weaponLost = false;
    for (int i = 3; i >= 0; --i) {
        Item*& slot = weapons[i];
        if (!slot || !IsBroken(slot)) continue;
        messages->SendWeaponRemoved(static_cast<std::uint32_t>(id), static_cast<std::uint32_t>(i));
        broken.push_back(std::exchange(slot, nullptr));
        MarkChanged(NetField::Equipment);
        for (int k = i; k < 3; ++k) weapons[k] = weapons[k + 1];
        weapons[3] = nullptr;
        MarkChanged(NetField::Modes);
        weaponLost = true;
    }
    if (weaponLost) controller->ShowWeapon(0xFFFFFFFFu);   // re-pick the active weapon

    if (!broken.empty()) {
        ownerObject->GiveItems(broken, 1);
        for (Item* item : broken) item->Release();
        MarkChanged(NetField::Equipment);
    }
}

// --- The unit's lifetime ---------------------------------------------------------------------------

Unit::Unit() {
    // The unit's motion: two arrays, then the step speeds and the segment part.
    UnitMotion* m = &motion;
    // (The segment part's constructor only returns it.)
    std::memset(&m->curveX, 0, sizeof(m->curveX));
    std::memset(&m->curveY, 0, sizeof(m->curveY));
    m->segment = -1;
    m->Clear();
    m->speedScale = kTenth;
    m->baseTurnRate = kTenth;
    Reset();
}

void Unit::Reset() {
    scriptName.clear();
    secondPrototype = -1;
    heightOffset = 0.0f;
    sizeClass = 3;
    unitRadius = kDefaultUnitRadius;
    standingHeight = kDefaultStandingHeight;
    relationsA = 0;
    relationsB = 0xFFFFFFFFu;
    side = 0;
    SetController(nullptr);
    animationEntry = -1;
    secondEntry = -1;
    player = nullptr;
    actionSound = 0;
    animationStart = 0;
    motionMode = 0;
    pathTimeShift = 0;
    moveMode = MoveMode::Walk;
    moveMode2 = MoveMode::Walk;
    pendingWeapon = -1;
    dead = 0;
    obstacle = 0;
    activeWeapon = 0xFFFFFFFFu;
    params.Reset();
    // The item slots and the carried list are empty here: they start so, and ReleaseUnit empties them before
    // every later Reset (Destroy, the destructor).
    altPassCosts = 0;
    AsMapObject()->netState.Reset();
    fightAtWill = kFightAtWill;
}

Unit::~Unit() {
    // The release-from-the-player virtual, called directly like the original (through the vtable's memory, not
    // the object's).
    ReleaseUnit();
    MapObject::Destroy();
    Reset();
}
