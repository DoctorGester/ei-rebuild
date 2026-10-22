#pragma once

// CParty (game.exe's vtable 00743000, class name "CParty", id 9): a party's data: its unit records,
// the items it owns, named variables, a zone name and its own item and spell managers and
// constructors. The player's inventory, the named party records (party_records.h) and the camp's
// lists are parties. Its manager context getters (manager_context.h) answer only the item and spell
// ones. See docs/notes/items_spells.md ("Party").

#include "engine_object.h"
#include "manager_context.h"
#include "map_object_figures.h"
#include "object_tables.h"

#include "lib/mfc.h"
#include "lib/stlp.h"
#include "game_data.h"

#include <cstddef>
#include <cstdint>

using BOOL = int;

class CItem;
class CItemConstructor;
struct CItemManager;   // item.h
class CParty;
class CResourceManager;
class CSpellConstructor;
class CSpellManager;
class ChunkArchive;
class CStream;   // core_streams.h
struct PartyUnitRecord;   // unit.h

// A party's item list (CParty::items_) and a unit record's (PartyUnitRecord::weapons, ...): an
// aligned array of item pointers.
using PartyItemArray = lib::AlignedArray<CItem*>;

// A unit record's item list (its data pointer, then count, capacity and grow-by) as the aligned
// pointer array it is.
inline PartyItemArray& ItemList(CItem**& data) { return *reinterpret_cast<PartyItemArray*>(&data); }

// The party whose records are being loaded (set by CParty::LoadParty around the records).
inline CParty* g_loadingParty = /*007C8344*/ {nullptr};

// 00664880 (__stdcall, RET 8; party_lifetime.cpp). MFC ConstructElements for the records: the
// memory zeroed, then each record constructed.
void __stdcall ConstructPartyUnitRecords(PartyUnitRecord* records, int count);

// 00664850 (__stdcall, RET 8; party_lifetime.cpp). MFC DestructElements for the records: each
// record's Destruct (a count of 0 does nothing).
void __stdcall DestructPartyUnitRecords(PartyUnitRecord* records, int count);

// ConstructElements / DestructElements of the records: 00664880, 00664850.
struct PartyUnitRecordElements {
    static void Construct(PartyUnitRecord* records, int count) { ConstructPartyUnitRecords(records, count); }
    static void Destruct(PartyUnitRecord* records, int count);   // party_lifetime.cpp
};
// A party's unit records: an MFC CArray<PartyUnitRecord> (CObject vtable Vtable(), in place of game.exe's
// 00742FF4). Its SetSize (00663FA0, RET 8) is registered on the template (party_lifetime.cpp).
struct PartyMembers : mfc::CArray<PartyUnitRecord, PartyUnitRecordElements> {
    // The compiled table (C19, party_lifetime.cpp's PartyMemberArrayClass over mfc_slots.h's CompiledCollection).
    static void* Vtable();
    // 00664160 (party_lifetime.cpp). The array's destructor body: its vtable, the records destroyed
    // and the storage freed.
    void Destruct();
    // 00664470: slot 0 (operator delete with flags bit 0; returns the array).
    PartyMembers* DeletingDestruct(unsigned flags);
};
using PartyMemberArray = PartyMembers;

class CParty : public CManagerContext {
public:
    // 00661050. Empty record and item arrays, an empty variable map with NextPrime(100) buckets
    // (its functor bytes are the original's uninitialised stack byte, left alone here), an empty
    // zone name, no managers. The money and the entrance are left as they are.
    CParty();
    // 006613C0. A copy of `source`, as Assign makes it (it reads its source non-const: saving it
    // makes its managers when it has none).
    explicit CParty(CParty& source);
    // 006611D0 (the deleting destructor 00661190). Clear(1, 1), Destroy, then the members.
    ~CParty() override;
    CParty& operator=(const CParty&) = delete;

    // 006614F0. Makes and counts the party's own item manager and spell manager (attached to this
    // party), item constructor and spell constructor (created), then Clear(1, 0). Returns 1.
    int Create() override;
    // 00661640. Empties the record array, releases the items and frees their storage, then drops
    // (count, delete) the item constructor, spell constructor, item manager and spell manager.
    void Destroy() override;
    const char* GetClassName() override;   // 0065F560 "CParty"
    int GetClassId() override;             // 0065F570 9

    CItemConstructor* ItemConstructor() override;     // 0065F540
    CSpellConstructor* SpellConstructor() override;   // 0065F550
    CItemManager* ItemFactory() override;             // 0065F520
    CSpellManager* SpellManager() override;           // 0065F510
    // The item factory under the party's name.
    CItemManager* ItemManager() { return ItemFactory(); }
    // A new item made like `item` (DuplicateItem: MakeCopyOf).
    CItem* CopyItem(CItem* item) { return DuplicateItem(item); }

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
    // 00661FB0 (party_items.cpp). Every quest item (class 0x14) taken out with RemoveItem(item, 1),
    // from the back of the list.
    void RemoveQuestItems();
    // 00661FE0 (party_items.cpp). Every item released (slot 4; the count re-read after each), the
    // list's storage freed and the list emptied (what Destroy and Clear inline).
    void ReleaseAllItems();

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
    // 006625B0 (RET 4; party_members.cpp). The multiplayer hero's starting equipment: `record`'s
    // spells and weapons released and removed (from the front), then, from the NPC record of its
    // prototype (text 5; NpcTable's operator[], which adds an empty entry for an unknown name),
    // copies of the start spells (appended), quick items and weapons (each at its list's count,
    // SetAtGrow), the start money added to the party's, the start experience to the record's
    // statistics, and text 6 = text 5. 0 without an NPC record (after the lists were emptied),
    // else 1.
    BOOL EquipFromPrototype(PartyUnitRecord* record);
    // 006621B0 (player_messages.cpp). The record of the unit with id `unitId`, or null.
    PartyUnitRecord* Find(int unitId) const;
    // 006620F0 (RET 4). Destroys the first record whose key (+0x1C) is `key` and closes the gap;
    // 1 when there was one, else 0.
    BOOL RemoveMember(int key);
    // 006629D0 (party_members.cpp, A-H17; the single-player login). When the party's zone (+0x68)
    // is an edge zone (map.txt kind 1), every exit's target zone of kind 0 gets its variable
    // "z.<name>" set to 1 unless it is already nonzero (0 or NaN is set).
    void OpenEdgeExits();

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
    // 006617F0 (RET 4; party_save.cpp, A-H25). The party in an archive: a memory stream holding
    // SaveParty's output (with variables) as stream field 1, read back with the archive's version.
    // ArchivePartyThroughHook goes through the address (party_manager.cpp), so its check runs.
    void ArchiveParty(ChunkArchive* ar);
    // 00661300 (RET 4). Clear(1, 0), then `source` saved into a memory stream and loaded back into
    // this party (variables included). Returns this.
    CParty* Assign(CParty* source);
    // The money (+0x50; the camp's deals and the trade read it).
    int& Money() { return money_; }

    CItemManager* itemManager_;
    CItemConstructor* itemConstructor_;
    CSpellManager* spellManager_;
    CSpellConstructor* spellConstructor_;
    PartyMemberArray members_;
    PartyItemArray items_;
    int money_;                 // the party's money (Money(); cleared by Clear, saved masked)
    NamedFloatTable variables_;   // named variables (the script's "gtime" and the like)
    mfc::CString zone_;           // the zone name ("gz1g" after a new game)
    std::uint32_t entrance_;      // the zone exit the party arrives by (cleared by Clear)
};
