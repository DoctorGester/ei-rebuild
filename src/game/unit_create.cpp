// Unit setup and upkeep around creation: the prototype name index, the server controller's
// statistics hand-over and remarks, and dropping broken equipment after a hit. See
// docs/notes/units.md ("Creation, databases, save").
#include "player_messages.h"
#include "unit_create.h"

#include "combat.h"
#include "item.h"
#include "map_object.h"
#include "unit.h"

#include "lib/mfc.h"

#include "ei/hook.h"
#include "ei/x87.h"

#include <bit>
#include <cstring>
#include "lib/mfc_list.h"
#include "lib/mfc_slots.h"

namespace {
constexpr int kPartStats = 4;          // MarkChanged parts
constexpr int kPartEquipment = 5;
constexpr int kPartWeapons = 6;

constexpr std::uint32_t kLastPlainRemark = 0x29;
constexpr std::uint32_t kExtraRemark = 0x30;

// The vtable of the CTypedPtrList<CPtrList, CItem*> the broken pieces are collected in.
constexpr int kPtrListBlockSize = 10;

// Called through their game addresses (rewritten elsewhere; config/direct_calls_skip.csv): 00525AF0
// UnitStats::RefillSlots in unit_vitals.cpp, 0066D6E0 a player's message writer in player_messages.cpp.
// 00665470. The player takes the items of `list`, a CPtrList. Not rewritten.

// The player's message writer (CPlayerServer, player + 0x20).
CPlayerServer* PlayerMessages(const CPlayer* player) { return player->Server(); }

// The item's condition (CItem +0x20, a float kept as a dword) rounds to nothing.
bool IsBroken(const CItem* item) {
    return x87::fistp(item->condition_) <= 0;
}

using UnitItems = mfc::CTypedPtrList<CItem>;
} // namespace

// --- Prototype names --------------------------------------------------------------------------

UnitPrototypeRecord* UnitPrototypeTable::FindByName(const char* name) {
    // The original builds the key twice (a string, then the pair it would insert) and frees both
    // afterwards; the pool sees the same allocations in the same order.
    stlp::string key(name);
    stlp::string pairKey(key);
    UnitPrototypeRecord* record = byName[pairKey];
    return record;
}
EI_REWRITTEN(0x00514930, UnitPrototypeTable::FindByName);

// --- Server controller ------------------------------------------------------------------------

std::uintptr_t CUnitServer::SetStats(const UnitStats* source) {
    std::memcpy(&stats, source, sizeof(UnitStats));
    unit->MarkChanged(kPartStats);
    RefreshVitals(0);
    stats.RefillSlots();
    RefreshVitals(0);
    // The companion's vitals: hit points and mana at their maxima (the companion read again).
    auto* companion = unit->ServerCompanion();
    std::memcpy(&companion->vitals.hitPoints, &companion->vitals.maxHitPoints, 4);
    companion = unit->ServerCompanion();
    std::memcpy(&companion->vitals.mana, &companion->vitals.maxMana, 4);
    return reinterpret_cast<std::uintptr_t>(companion);
}
EI_REWRITTEN(0x00552F40, CUnitServer::SetStats);

void CUnitServer::Shout(std::uint32_t code) {
    if (code > kLastPlainRemark && code != kExtraRemark) return;
    if (code == 0xFFFFFFFFu) return;   // already excluded above; the original tests it anyway
    if (!unit->player) return;
    PlayerMessages(unit->player)->SendObject0B(unit, code);
}
EI_REWRITTEN(0x00552780, CUnitServer::Shout);

// --- Broken equipment -------------------------------------------------------------------------

void CUnit::DropBrokenEquipment() {
    CPlayer* owner = player;
    if (!owner) return;   // the empty list's destructor (00429F10) has nothing to free
    UnitItems broken{};
    broken.vtable = mfc::impl::ItemListClass::Vtable();
    broken.blockSize = kPtrListBlockSize;
    CPlayerServer* messages = PlayerMessages(owner);

    for (CItemArmor*& slot : armor) {
        if (!slot || !IsBroken(slot)) continue;
        ++slot->refCount_;
        broken.AddTail(slot);
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
        broken.AddTail(slot);
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

    if (broken.count != 0) {
        owner->GiveItems(&broken, 1);
        for (UnitItems::Node* node = broken.head; node;) {
            UnitItems::Node* next = node->next;
            node->data->Release();
            node = next;
        }
        MarkChanged(kPartEquipment);
    }
    broken.RemoveAll();
}
EI_REWRITTEN(0x00532AC0, CUnit::DropBrokenEquipment);
