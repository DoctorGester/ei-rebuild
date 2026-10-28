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

#include "ei/x87.h"

#include <bit>
#include <list>

namespace {
constexpr int kPartStats = 4;          // MarkChanged parts
constexpr int kPartEquipment = 5;
constexpr int kPartWeapons = 6;

CPlayerServer* PlayerMessages(const CPlayer* player) { return player->Server(); }

// The item's condition rounds to nothing.
bool IsBroken(const CItem* item) {
    return x87::fistp(item->condition_) <= 0;
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

void CUnitServer::SetStats(const UnitStats* source) {
    std::memcpy(&stats, source, sizeof(UnitStats));
    unit->MarkChanged(kPartStats);
    RefreshVitals(0);
    stats.RefillSlots();
    RefreshVitals(0);
    // The companion's vitals: hit points and mana at their maxima (the companion read again).
    auto* companion = unit->ServerCompanion();
    companion->vitals.hitPoints = companion->vitals.maxHitPoints;
    companion = unit->ServerCompanion();
    companion->vitals.mana = companion->vitals.maxMana;
}

void CUnitServer::Shout(AckKind kind) {
    if (!IsShoutable(kind)) return;
    if (!unit->player) return;
    PlayerMessages(unit->player)->SendObject0B(unit, kind);
}

// --- Broken equipment -------------------------------------------------------------------------

void CUnit::DropBrokenEquipment() {
    CPlayer* owner = player;
    if (!owner) return;   // the empty list's destructor has nothing to free
    std::list<CItem*> broken{};
    CPlayerServer* messages = PlayerMessages(owner);

    for (CItemArmor*& slot : armor) {
        if (!slot || !IsBroken(slot)) continue;
        ++slot->refCount_;
        broken.push_back(slot);
        CItemArmor* piece = slot;
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
        CItem*& slot = weapons[i];
        if (!slot || !IsBroken(slot)) continue;
        messages->SendPair16(static_cast<std::uint32_t>(id_), static_cast<std::uint32_t>(i));
        ++slot->refCount_;
        broken.push_back(slot);
        CEngineObject* weapon = slot;
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
        owner->GiveItems(&broken, 1);
        for (auto node = broken.begin(); node != broken.end();) {
            auto next = std::next(node);
            (*node)->Release();
            node = next;
        }
        MarkChanged(kPartEquipment);
    }
    broken.clear();
}
