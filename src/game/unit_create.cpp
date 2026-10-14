// Unit setup and upkeep around creation: the prototype name index, the server controller's
// statistics hand-over and remarks, and dropping broken equipment after a hit. See
// docs/notes/units.md ("Fourth round").
#include "unit_create.h"

#include "combat.h"
#include "item.h"
#include "map_object.h"
#include "unit.h"

#include "lib/mfc.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "ei/x87.h"

#include <cstring>
#include "originals_gameplay.h"
#include "originals_units.h"

namespace {
constexpr int kPartStats = 4;          // MarkChanged parts
constexpr int kPartEquipment = 5;
constexpr int kPartWeapons = 6;

// Controller slots (vtable 0073EDBC and the classes deriving from it).
constexpr int kControllerWeaponsChanged = 0x64 / 4;   // 25 (-1: re-pick the active weapon)
constexpr int kControllerArmourRemoved = 0x6C / 4;    // 27 (the piece's armour type)
// CUnitServer slot 19 (0x4C), called around the slot refill.
constexpr int kServerStatsSlot = 0x4C / 4;

constexpr std::uint32_t kLastPlainRemark = 0x29;
constexpr std::uint32_t kExtraRemark = 0x30;

// The vtable of the CTypedPtrList<CPtrList, CItem*> the broken pieces are collected in.
constexpr std::uintptr_t kPtrListVtable = 0x0073BE80;
constexpr int kPtrListBlockSize = 10;

// Called through their game addresses, being rewritten elsewhere, so their verify wrappers stay in the
// path: 00525AF0 UnitStats::RefillSlots in unit_vitals.cpp, 0066EA70 / 0066D6E0 the player's
// message writers in player_messages.cpp.
__declspec(naked) void __fastcall SendPair16At(void*, void*, std::uint32_t, std::uint32_t) EI_ORIGINAL(0x0066EA70)
__declspec(naked) void __fastcall SendObject0BAt(void*, void*, const void*, std::uint32_t) EI_ORIGINAL(0x0066D6E0)
// 00665470. The player takes the items of `list`, a CPtrList. Not rewritten.
void __fastcall PlayerReceiveItems(char* a0, void*, void* a2, int a3) { PlayerGiveItemsOriginal((void*)a0, nullptr, a2, a3); }

// The player's message writer (CPlayerServer, player + 0x20).
void* PlayerMessages(const char* player) { return *reinterpret_cast<void* const*>(player + 0x20); }

bool IsBroken(const CEngineObject* item) {
    return x87::fistp(Field<float>(reinterpret_cast<const char*>(item), 0x20)) <= 0;
}

using ItemList = mfc::CTypedPtrList<CEngineObject>;
} // namespace

// --- Prototype names --------------------------------------------------------------------------

char* UnitPrototypeTable::FindByName(const char* name) {
    // The original builds the key twice (a string, then the pair it would insert) and frees both
    // afterwards; the pool sees the same allocations in the same order.
    stlp::string key(name);
    stlp::string pairKey(key);
    char* record = byName[pairKey];
    return record;
}
EI_REPLACE_METHOD(0x00514930, UnitPrototypeTable::FindByName);

// --- Server controller ------------------------------------------------------------------------

std::uintptr_t CUnitServer::SetStats(const UnitStats* source) {
    std::memcpy(&stats, source, sizeof(UnitStats));
    unit->MarkChanged(kPartStats);
    ei::VCall<int>(this, kServerStatsSlot, 0);
    UnitStatsRefillSlotsOriginal(&stats);
    ei::VCall<int>(this, kServerStatsSlot, 0);
    // The companion's vitals (UnitVitals at +0x20): hit points and mana at their maxima.
    char* vitals = reinterpret_cast<char*>(reinterpret_cast<CMapObject*>(unit)->companion);
    std::memcpy(vitals + 0x28, vitals + 0x2C, 4);
    vitals = reinterpret_cast<char*>(reinterpret_cast<CMapObject*>(unit)->companion);
    std::memcpy(vitals + 0x34, vitals + 0x38, 4);
    return reinterpret_cast<std::uintptr_t>(vitals);
}
EI_REPLACE_METHOD(0x00552F40, CUnitServer::SetStats);

void CUnitServer::Shout(std::uint32_t code) {
    if (code > kLastPlainRemark && code != kExtraRemark) return;
    if (code == 0xFFFFFFFFu) return;   // already excluded above; the original tests it anyway
    if (!unit->player) return;
    SendObject0BAt(PlayerMessages(unit->player), nullptr, unit, code);
}
EI_REPLACE_METHOD(0x00552780, CUnitServer::Shout);

// --- Broken equipment -------------------------------------------------------------------------

void CUnit::DropBrokenEquipment() {
    char* owner = player;
    if (!owner) return;   // the empty list's destructor (00429F10) has nothing to free
    ItemList broken{};
    broken.vtable = reinterpret_cast<void*>(kPtrListVtable);
    broken.blockSize = kPtrListBlockSize;
    void* messages = PlayerMessages(owner);

    for (CEngineObject*& slot : armor) {
        if (!slot || !IsBroken(slot)) continue;
        ++slot->refCount_;
        broken.AddTail(slot);
        CEngineObject* piece = slot;
        slot = nullptr;
        if (piece) {
            ei::VCall<int>(controller, kControllerArmourRemoved, static_cast<CItemArmor*>(piece)->GetType());
            piece->Release();
        }
        MarkChanged(kPartEquipment);
        MarkChanged(kPartStats);
    }

    bool weaponLost = false;
    for (int i = 3; i >= 0; --i) {
        CEngineObject*& slot = weapons[i];
        if (!slot || !IsBroken(slot)) continue;
        SendPair16At(messages, nullptr, static_cast<std::uint32_t>(id_), static_cast<std::uint32_t>(i));
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
    if (weaponLost) ei::VCall<int>(controller, kControllerWeaponsChanged, -1);

    if (broken.count != 0) {
        PlayerReceiveItems(owner, nullptr, &broken, 1);
        for (ItemList::Node* node = broken.head; node;) {
            ItemList::Node* next = node->next;
            node->data->Release();
            node = next;
        }
        MarkChanged(kPartEquipment);
    }
    broken.RemoveAll();
}
EI_REPLACE_METHOD(0x00532AC0, CUnit::DropBrokenEquipment);
