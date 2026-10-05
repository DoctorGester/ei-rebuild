#pragma once

// CParty (vtable 00743000, GetClassName 0065F560 "CParty", 0x70 bytes): a party's data: its unit
// records (+0x2C), the items it owns (+0x40), named variables (+0x54), a zone name (+0x68) and
// its own item and spell managers and constructors. The player's inventory, the named party
// records (party_records.h) and the camp's lists are parties. Its virtual slots 13-26 are the
// manager getters CWorld has too (24: 0055E940 makes the party's own copy of an item). See
// docs/notes/items_spells.md ("Party").

#include "engine_object.h"
#include "map_object_figures.h"

#include "lib/mfc.h"
#include "lib/stlp.h"

#include <cstddef>
#include <cstdint>

class CItem;
class CItemConstructor;
class CSpellConstructor;
class CSpellManager;
struct PartyUnitRecord;   // unit.h

// A unit record's item list (its data pointer, then count, capacity and grow-by) as the aligned
// pointer array it is.
inline AlignedPtrArray& ItemList(CEngineObject**& data) { return *reinterpret_cast<AlignedPtrArray*>(&data); }

// 00664880 (__stdcall, RET 8; party_lifetime.cpp). MFC ConstructElements for the records: the
// memory zeroed, then each record constructed.
void __stdcall ConstructPartyUnitRecords(PartyUnitRecord* records, int count);

// A party's unit records: an MFC CArray<PartyUnitRecord> (CObject vtable 00742FF4).
struct PartyMemberArray {
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x00742FF4);

    // 00663FA0 (RET 8; party_lifetime.cpp). CArray::SetSize: new records are zeroed and
    // constructed, dropped ones destroyed; growing past the capacity moves the records to a new
    // block (grow-by 0: an eighth of the size, 4..1024); `growBy` replaces the grow-by unless -1.
    void SetSize(int newSize, int newGrowBy);

    void* vtable;
    PartyUnitRecord* data;          // +0x04
    int size;                       // +0x08
    int capacity;                   // +0x0C
    int growBy;                     // +0x10
};
static_assert(sizeof(PartyMemberArray) == 0x14);

class CParty : public CEngineObject {
public:
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x00743000);

    virtual void Slot13();
    virtual void Slot14();
    virtual void Slot15();
    virtual void Slot16();
    virtual void Slot17();
    virtual void Slot18();
    virtual void Slot19();
    virtual void Slot20();
    virtual void Slot21();
    virtual void Slot22();
    virtual void Slot23();
    // 24: 0055E940. A new item made like `item` (the item factory, slot 18) and given its values
    // (its slot 15 with the spell manager, slot 22).
    virtual CItem* CopyItem(CItem* item);

    // 00661DB0 (RET 0xC; party_items.cpp). Puts `item` among the party's items with a count of 1
    // (with `keepCount`, the item's own +0x48). With `merge`, an item the list already has that is
    // the same (slot 14) only gets the count added and is returned. Otherwise the party's copy
    // (slot 24) gets the count, a reference and the end of the list, and is returned. Null for
    // a null item.
    // The flags are read as bytes (their low bytes).
    CItem* AddItem(CItem* item, int keepCount, int merge);
    // 00661F30 (RET 8; party_items.cpp). Takes `item` out of the list: when its count is above 1
    // and not `all`, the count drops by one and the item is returned; otherwise it is released
    // and removed, and the result is null (also when it is not in the list).
    CItem* RemoveItem(CItem* item, int all);   // `all` read as a byte

    std::uint8_t unknown_1C[0x2C - 0x1C];
    PartyMemberArray members_;      // +0x2C
    AlignedPtrArray items_;         // +0x40 CItem*
};
static_assert(offsetof(CParty, members_) == 0x2C);
static_assert(offsetof(CParty, items_) == 0x40);
