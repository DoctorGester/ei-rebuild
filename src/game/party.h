#pragma once

// CParty (class name "CParty", id 9): a party's data: its unit records, the items it owns, named variables,
// a zone name and its own item and spell managers and constructors. The player's inventory, the named party
// records (party_records.h) and the camp's lists are parties. Its manager context getters
// (manager_context.h) answer only the item and spell ones. See docs/notes/items_spells.md ("Party").

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
struct CItemManager;
class CParty;
class CResourceManager;
class CSpellConstructor;
class CSpellManager;
class ChunkArchive;
class CStream;
struct PartyUnitRecord;

// A party's item list (CParty::items_) and a unit record's (PartyUnitRecord::weapons, ...): an
// aligned array of item pointers.
using PartyItemArray = lib::AlignedArray<CItem*>;

// A unit record's item list (its data pointer, then count, capacity and grow-by) as the aligned
// pointer array it is.
inline PartyItemArray& ItemList(CItem**& data) { return *reinterpret_cast<PartyItemArray*>(&data); }

// The party whose records are being loaded (set by CParty::LoadParty around the records).
inline CParty* g_loadingParty = /*007C8344*/ {nullptr};

// MFC ConstructElements for the records: the memory zeroed, then each record constructed.
void __stdcall ConstructPartyUnitRecords(PartyUnitRecord* records, int count);

// MFC DestructElements for the records: each record's Destruct (a count of 0 does nothing).
void __stdcall DestructPartyUnitRecords(PartyUnitRecord* records, int count);

// ConstructElements / DestructElements of the records.
struct PartyUnitRecordElements {
    static void Construct(PartyUnitRecord* records, int count) { ConstructPartyUnitRecords(records, count); }
    static void Destruct(PartyUnitRecord* records, int count);   // party_lifetime.cpp
    // Moves records within or between storages (a record's texts are std::strings: moved, not copied as bytes).
    static void Relocate(PartyUnitRecord* to, PartyUnitRecord* from, int count);   // party_lifetime.cpp
};
// A party's unit records: an MFC CArray<PartyUnitRecord> (CObject vtable Vtable()). Its SetSize is
// registered on the template (party_lifetime.cpp).
struct PartyMembers : mfc::CArray<PartyUnitRecord, PartyUnitRecordElements> {
    // The compiled table (party_lifetime.cpp's PartyMemberArrayClass over mfc_slots.h's CompiledCollection).
    static void* Vtable();
    // The array's destructor body: its vtable, the records destroyed and the storage freed.
    void Destruct();
    // Slot 0 (operator delete with flags bit 0; returns the array).
    PartyMembers* DeletingDestruct(unsigned flags);
};
using PartyMemberArray = PartyMembers;

class CParty : public CManagerContext {
public:
    // Empty record and item arrays, an empty variable map with NextPrime(100) buckets (its functor bytes
    // are an uninitialised stack byte, left alone), an empty zone name, no managers. The money and the
    // entrance are left as they are.
    CParty();
    // A copy of `source`, as Assign makes it (it reads its source non-const: saving it
    // makes its managers when it has none).
    explicit CParty(CParty& source);
    ~CParty() override;
    CParty& operator=(const CParty&) = delete;

    // Makes and counts the party's own item manager and spell manager (attached to this
    // party), item constructor and spell constructor (created), then Clear(1, 0). Returns 1.
    int Create() override;
    // Empties the record array, releases the items and frees their storage, then drops
    // (count, delete) the item constructor, spell constructor, item manager and spell manager.
    void Destroy() override;
    const char* GetClassName() override;
    int GetClassId() override;             // class id 9

    CItemConstructor* ItemConstructor() override;
    CSpellConstructor* SpellConstructor() override;
    CItemManager* ItemFactory() override;
    CSpellManager* SpellManager() override;
    // The item factory under the party's name.
    CItemManager* ItemManager() { return ItemFactory(); }
    // A new item made like `item` (DuplicateItem: MakeCopyOf).
    CItem* CopyItem(CItem* item) { return DuplicateItem(item); }

    // Puts `item` among the party's items with a count of 1 (with `keepCount`, the item's own count_). With
    // `merge`, an item the list already has that is the same only gets the count added and is returned.
    // Otherwise the party's copy gets the count, a reference and the end of the list, and is returned. Null
    // for a null item. The flags are read as bytes (their low bytes).
    CItem* AddItem(CItem* item, int keepCount, int merge);
    // Takes `item` out of the list: when its count is above 1 and not `all`, the count drops by one and the
    // item is returned; otherwise it is released and removed, and the result is null (also when it is not
    // in the list).
    CItem* RemoveItem(CItem* item, int all);   // `all` read as a byte
    // Every quest item (class 0x14) taken out with RemoveItem(item, 1), from the back of the list.
    void RemoveQuestItems();
    // Every item released, the list's storage freed and the list emptied (what Destroy and Clear inline).
    void ReleaseAllItems();

    // Appends a unit record: Create when there is no item manager; a constructed record assigned to the new
    // entry (its statistics are the stack's leftover bytes); `name` as text 1 and its text key (-1 when
    // empty) as the key, `prototype` as text 5, `extra` as text 0; then FillFromPrototype. Returns
    // the record.
    PartyUnitRecord* AddMember(const char* name, const char* prototype, const char* extra);
    // Gives `record` what its prototype (text 5) says: text 2 from its model, the NPC statistics, the three
    // scales, copies (CItem::Clone) of the starting armour, of the two weapons unless
    // empty or "none", and of the spells; text 6 = text 5. Returns 1. (The lists come from the record: the
    // lookup that could fail, a missing prototype, cannot happen after the record was read.)
    BOOL FillFromPrototype(PartyUnitRecord* record);
    // The multiplayer hero's starting equipment: `record`'s spells and weapons released and removed (from
    // the front), then, from the NPC record of its prototype (text 5; NpcTable's operator[], which adds an
    // empty entry for an unknown name), copies of the start spells (appended), quick items and weapons
    // (each at its list's count, SetAtGrow), the start money added to the party's, the start experience to
    // the record's statistics, and text 6 = text 5. 0 without an NPC record (after the lists were emptied),
    // else 1.
    BOOL EquipFromPrototype(PartyUnitRecord* record);
    // The record of the unit with id `unitId`, or null.
    PartyUnitRecord* Find(int unitId) const;
    // Destroys the first record whose key is `key` and closes the gap; 1 when there was one, else 0.
    BOOL RemoveMember(int key);
    // When the party's zone is an edge zone (map.txt kind 1), every exit's target zone of kind 0
    // gets its variable "z.<name>" set to 1 unless it is already nonzero (0 or NaN is set).
    void OpenEdgeExits();

    // Releases the items and frees their storage, empties the item manager and the spell manager (attached
    // again unless `keepSpells`), empties the record array, the variables (with `clearVariables`) and the
    // zone name, and clears money_ and entrance_.
    void Clear(int clearVariables, int keepSpells);

    // Reads what SaveParty writes: Create when there is no item manager, Clear(loadVariables, 0) with the
    // party marked as being loaded (LoadingParty, which the records' item lookups use), the zone name,
    // entrance_, both managers (their Load, then LoadObjects), the item ids (resolved in the item manager and
    // counted), the records (PartyUnitRecord::Load), the variables (with `loadVariables`), money_ (from
    // version 0x6C stored xor a key that follows it); then both managers' ReleaseUnused.
    void LoadParty(CStream* stream, int loadVariables);
    // Create when there is no item manager; the zone name with its zero, entrance_, both managers (their Save,
    // then SaveObjects), the item ids (-1 for an empty entry), the record count and records, the variables
    // (with `saveVariables`), then money_ xor an ISAAC key and the key.
    void SaveParty(CStream* stream, int saveVariables);
    // The party in an archive: a memory stream holding SaveParty's output (with variables) as stream field
    // 1, read back with the archive's version.
    void ArchiveParty(ChunkArchive* ar);
    // Clear(1, 0), then `source` saved into a memory stream and loaded back (variables included).
    CParty* Assign(CParty* source);
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
