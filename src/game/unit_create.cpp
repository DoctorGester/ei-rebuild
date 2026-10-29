// Unit setup and upkeep around creation: the prototype name index, the server controller's
// statistics hand-over and remarks, and dropping broken equipment after a hit. See
// docs/notes/units.md ("Creation, databases, save").
#include "player_messages.h"
#include "unit_create.h"

#include "combat.h"
#include "item.h"
#include "map_object.h"
#include "unit.h"

#include <cstdint>
#include <cstring>
#include <string>

#include "lib/rounding.h"

#include <bit>
#include <vector>

namespace {
constexpr int kPartStats = 4;          // MarkChanged parts
constexpr int kPartEquipment = 5;
constexpr int kPartWeapons = 6;
constexpr float kTenth = 0.1f;
constexpr float kThreeQuarters = 0.75f;
constexpr float kHalf = 0.5f;

PlayerServer* PlayerMessages(const CPlayer* player) { return player->Server(); }

// The item's condition rounds to nothing.
bool IsBroken(const Item* item) {
    return lib::RoundEven(item->condition) <= 0;
}

} // namespace

// --- Prototype names --------------------------------------------------------------------------

UnitPrototypeRecord* UnitPrototypeTable::FindByName(std::string_view name) {
    // The key is built twice (a string, then the pair it would insert) and both freed
    // afterwards; the pool sees the same allocations in the same order.
    std::string key(name);
    std::string pairKey(key);
    UnitPrototypeRecord* record = byName[pairKey];
    return record;
}

// --- Server controller ------------------------------------------------------------------------

void UnitServer::SetStats(const UnitStats* source) {
    std::memcpy(&stats, source, sizeof(UnitStats));
    unit->MarkChanged(kPartStats);
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
    PlayerMessages(unit->player)->SendObject0B(unit, kind);
}

// --- Broken equipment -------------------------------------------------------------------------

void CUnit::DropBrokenEquipment() {
    CPlayer* ownerObject = player;
    if (!ownerObject) return;   // the empty list's destructor has nothing to free
    std::vector<Item*> broken{};
    PlayerServer* messages = PlayerMessages(ownerObject);

    for (ItemArmor*& slot : armor) {
        if (!slot || !IsBroken(slot)) continue;
        slot->AddRef();
        broken.push_back(slot);
        ItemArmor* piece = slot;
        slot = nullptr;
        if (piece) {
            Controller()->ArmourTakenOff(static_cast<std::uint32_t>(piece->GetType()));
            piece->Release();
        }
        MarkChanged(kPartEquipment);
        MarkChanged(kPartStats);
    }

    bool weaponLost = false;
    for (int i = 3; i >= 0; --i) {
        Item*& slot = weapons[i];
        if (!slot || !IsBroken(slot)) continue;
        messages->SendPair16(static_cast<std::uint32_t>(id), static_cast<std::uint32_t>(i));
        slot->AddRef();
        broken.push_back(slot);
        EngineObject* weapon = slot;
        slot = nullptr;
        if (weapon) weapon->Release();
        MarkChanged(kPartEquipment);
        for (int k = i; k < 3; ++k) weapons[k] = weapons[k + 1];
        weapons[3] = nullptr;
        MarkChanged(kPartWeapons);
        weaponLost = true;
    }
    if (weaponLost) Controller()->ShowWeapon(0xFFFFFFFFu);   // re-pick the active weapon

    if (!broken.empty()) {
        ownerObject->GiveItems(&broken, 1);
        for (auto node = broken.begin(); node != broken.end();) {
            auto next = std::next(node);
            (*node)->Release();
            node = next;
        }
        MarkChanged(kPartEquipment);
    }
    broken.clear();
}

// --- The unit's lifetime ---------------------------------------------------------------------------

CUnit::CUnit() {
    // The unit's motion: two arrays, then the step speeds and the segment part.
    UnitMotion* m = &motion;
    // (The segment part's constructor only returns it.)
    std::memset(&m->curveX, 0, sizeof(m->curveX));
    std::memset(&m->curveY, 0, sizeof(m->curveY));
    m->segment = -1;
    m->Clear();
    m->speedScale = kTenth;
    m->baseTurnRate = kTenth;
    controller = nullptr;
    player = nullptr;
    Reset();
}

void CUnit::Reset() {
    scriptName.clear();
    secondPrototype = -1;
    heightOffset = 0.0f;
    sizeClass = 3;
    unitRadius = kThreeQuarters;
    standingHeight = kHalf;
    relationsA = 0;
    relationsB = 0xFFFFFFFFu;
    side = 0;
    if (controller) {
        ServerController()->unit = nullptr;
        controller->Release();
    }
    animationEntry = -1;
    secondEntry = -1;
    controller = nullptr;
    player = nullptr;
    actionSound = 0;
    animationStart = 0;
    motionMode = 0;
    pathTimeShift = 0;
    moveMode = 2;
    moveMode2 = 2;
    pendingWeapon = -1;
    dead = 0;
    obstacle = 0;
    activeWeapon = 0xFFFFFFFFu;
    params.Reset();
    std::memset(weapons, 0, sizeof(weapons));
    std::memset(quickItems, 0, sizeof(quickItems));
    std::memset(armor, 0, sizeof(armor));
    std::memset(spells, 0, sizeof(spells));
    carried.clear();
    altPassCosts = 0;
    MapObject()->netState.Reset();
    fightAtWill = 1;
}

CUnit::~CUnit() {
    // The release-from-the-player virtual, called directly like the original (through the vtable's memory, not
    // the object's).
    ReleaseUnit();
    DestroyMapObject();
    Reset();
}
