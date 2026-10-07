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

using BOOL = int;

class CItem;
class CItemConstructor;
class CParty;
class CResourceManager;
class CSpellConstructor;
class CSpellManager;
class CStream;   // core_streams.h
struct PartyUnitRecord;   // unit.h

// A unit record's item list (its data pointer, then count, capacity and grow-by) as the aligned
// pointer array it is.
inline AlignedPtrArray& ItemList(CEngineObject**& data) { return *reinterpret_cast<AlignedPtrArray*>(&data); }

// The party whose records are being loaded (set by CParty::LoadParty around the records).
inline CParty*& LoadingParty() { return *reinterpret_cast<CParty**>(std::uintptr_t(0x007C8344)); }

// 00664880 (__stdcall, RET 8; party_lifetime.cpp). MFC ConstructElements for the records: the
// memory zeroed, then each record constructed.
void __stdcall ConstructPartyUnitRecords(PartyUnitRecord* records, int count);

// ConstructElements / DestructElements of the records: 00664880, and each record's Destruct.
struct PartyUnitRecordElements {
    static void Construct(PartyUnitRecord* records, int count) { ConstructPartyUnitRecords(records, count); }
    static void Destruct(PartyUnitRecord* records, int count);   // party_lifetime.cpp
};
// A party's unit records: an MFC CArray<PartyUnitRecord> (CObject vtable 00742FF4). Its SetSize
// (00663FA0, RET 8) is registered on the template (party_lifetime.cpp).
using PartyMemberArray = mfc::CArray<PartyUnitRecord, PartyUnitRecordElements>;
constexpr std::uintptr_t kPartyMemberArrayVtable = std::uintptr_t(0x00742FF4);

// The party's named variables (the script's "gtime" and the like): an STLport hash_map<CString,
// float>. Its hash is not needed by the code here (declared only).
struct PartyVariableHash;
using PartyVariableMap = stlp::hash_map<mfc::CString, float, PartyVariableHash>;
static_assert(sizeof(PartyVariableMap) == 0x14);

class CParty : public CEngineObject {
public:
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x00743000);
    // The base class's vtable, which the constructor stores first.
    static constexpr std::uintptr_t kBaseVtableAddress = std::uintptr_t(0x0074306C);

    // Slots 13-24: the manager context's getters, as CWorld's (world.h); a party has only the item
    // and spell ones. Slots declared `void Name()` keep their place in the order only (their
    // return types are not declared; docs/notes/drafts/DR-C2.md).
    virtual void ThisWorld();                  // 13: 0065F4B0 (null)
    virtual void TimedObjectManager();         // 14: 0065F530 (null)
    virtual void FigureManager();              // 15: 0065F4C0 (null)
    virtual void ItemConstructor();            // 16: 0065F540 (itemConstructor_)
    virtual void SpellConstructor();           // 17: 0065F550 (spellConstructor_)
    virtual CResourceManager* ItemManager();   // 18: itemManager_
    virtual void MapObjectManager();           // 19: 0065F4D0 (null)
    virtual void PlayerList();                 // 20: 0065F4E0 (null)
    virtual void SpecificManager();            // 21: 0065F4F0 (null)
    virtual CSpellManager* SpellManager();     // 22: spellManager_
    virtual void FigureCache();                // 23: 0065F500 (null)
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

    // 006628D0 (RET 0xC; party_members.cpp). Appends a unit record: Create (slot 2) when there is
    // no item manager; a constructed record assigned to the new entry (its statistics are the
    // stack's leftover bytes, as in the original); `name` as text 1 and its text key (-1 when
    // empty) as the key (+0x1C), `prototype` as text 5, `extra` as text 0; then
    // FillFromPrototype. Returns the record.
    PartyUnitRecord* AddMember(const char* name, const char* prototype, const char* extra);
    // 006621F0 (RET 4). Gives `record` what its prototype (text 5) says: text 2 from its model,
    // the NPC statistics, the three scales (+0x724..+0x72C), copies (CItem::Clone) of the starting
    // armour, of the two weapons unless empty or "none", and of the spells; text 6 = text 5.
    // Returns 1. (The original reads the lists through 005143E0, whose only failure, a missing
    // prototype, cannot happen after the record was read; here they come from the record.)
    BOOL FillFromPrototype(PartyUnitRecord* record);
    // 006620F0 (RET 4). Destroys the first record whose key (+0x1C) is `key` and closes the gap;
    // 1 when there was one, else 0.
    BOOL RemoveMember(int key);

    // 00661050 (party_lifetime.cpp). Constructor body: CEngineObject's fields, empty record and
    // item arrays, an empty variable map with NextPrime(100) buckets (its functor bytes are the
    // original's uninitialised stack byte, left alone here), an empty zone name, no managers; the
    // base vtable first, then the party's. +0x50 and +0x6C are left as they are. Returns this.
    CParty* Construct();
    // 006611D0. Destructor body: Clear(1, 1), DestroyParty, then the members' destructors (zone
    // name, variable map, the item array's storage, the record array) and CEngineObject's.
    void DestructBody();
    // 00661190 (slot 0, RET 4). DestructBody, then operator delete when bit 0 of `flags` is set.
    CParty* DeletingDestruct(unsigned flags);
    // 006614F0 (slot 2). Makes and counts the party's own item manager and spell manager
    // (attached with slot 13 to this party), item constructor and spell constructor (slot 2),
    // then Clear(1, 0). Returns 1.
    BOOL CreateParty();
    // 00661640 (slot 3). Empties the record array, releases the items and frees their storage,
    // then drops (count, delete) the item constructor, spell constructor, item manager and spell
    // manager.
    void DestroyParty();
    // 00661700 (RET 8). Releases the items and frees their storage, empties the item manager
    // (slot 15) and the spell manager (slot 3; attached again with slot 13 unless `keepSpells`),
    // empties the record array, the variables (with `clearVariables`) and the zone name, and
    // clears +0x50 and +0x6C.
    void Clear(int clearVariables, int keepSpells);

    // 006618C0 (RET 8; party_save.cpp). Reads what SaveParty writes: Create (slot 2) when there
    // is no item manager, Clear(loadVariables, 0) with the party marked as being loaded
    // (LoadingParty, which the records' item lookups use), the zone name, +0x6C, both managers
    // (slot 9, then LoadObjects), the item ids (resolved in the item manager and counted), the
    // records (PartyUnitRecord::Load), the variables (with `loadVariables`), +0x50 (from version
    // 0x6C stored xor a key that follows it); then both managers' ReleaseUnused.
    void LoadParty(CStream* stream, int loadVariables);
    // 00661C10 (RET 8). Create when there is no item manager; the zone name with its zero, +0x6C,
    // both managers (slot 10, then SaveObjects), the item ids (-1 for an empty entry), the record
    // count and records, the variables (with `saveVariables`), then +0x50 xor an ISAAC key and
    // the key.
    void SaveParty(CStream* stream, int saveVariables);
    // 00661300 (RET 4). operator=: Clear(1, 0), then `source` saved into a memory stream and
    // loaded back into this party (variables included). Returns this.
    CParty* Assign(CParty* source);
    // 006613C0 (RET 4). Copy constructor: Construct, then `source` copied as Assign does
    // (without the Clear). Returns this.
    CParty* CopyConstruct(CParty* source);
    // The money (+0x50; the camp's deals and the trade read it).
    int& Money() { return reinterpret_cast<int&>(unknown_50); }

    CResourceManager* itemManager_;         // +0x1C (CItemManager's vtable; slot 18 returns it)
    CItemConstructor* itemConstructor_;     // +0x20 (slot 16)
    CSpellManager* spellManager_;           // +0x24 (slot 22)
    CSpellConstructor* spellConstructor_;   // +0x28 (slot 17)
    PartyMemberArray members_;              // +0x2C
    AlignedPtrArray items_;                 // +0x40 CItem*
    std::uint32_t unknown_50;               // +0x50 cleared by Clear (the party's money)
    PartyVariableMap variables_;            // +0x54
    mfc::CString zone_;                     // +0x68 the zone name ("gz1g" after a new game)
    std::uint32_t unknown_6C;               // +0x6C cleared by Clear
};
static_assert(offsetof(CParty, itemManager_) == 0x1C);
static_assert(offsetof(CParty, unknown_50) == 0x50);
static_assert(offsetof(CParty, unknown_6C) == 0x6C);
static_assert(offsetof(CParty, members_) == 0x2C);
static_assert(offsetof(CParty, items_) == 0x40);
static_assert(offsetof(CParty, variables_) == 0x54);
static_assert(offsetof(CParty, zone_) == 0x68);
static_assert(sizeof(CParty) == 0x70);
