#pragma once

// Original functions forwarded from several files (one declaration per address; the bodies
// jump to game.exe). Types are forward declared; include the class headers where members are used.

#include "ei/hook.h"

#include <cstddef>
#include <cstdint>
#include "lib/mfc.h"

class CEngineObject;
struct Projectile;
struct WorldPlayer;

__declspec(naked) inline void __fastcall PartyCopyFrom(void* /*party*/, void*, const void* /*from*/)
    EI_ORIGINAL(0x00661300)
__declspec(naked) inline void __fastcall PartyResetItems(void* /*party*/) EI_ORIGINAL(0x00661FB0)
// 004A91B0 (library, RET 4): the party manager's hash_map<int, CParty>::operator[] (a new key gets a
// default party).
__declspec(naked) inline void* __fastcall PartyMapAtOriginal(void* /*map*/, void*, const int* /*key*/) EI_ORIGINAL(0x004A91B0)
__declspec(naked) inline void __fastcall PlayerListRelease(CEngineObject*, void*, WorldPlayer*)
    EI_ORIGINAL(0x0066C1D0)
__declspec(naked) inline void __fastcall PlayerSendPair(void*, void*, int, float) EI_ORIGINAL(0x0066F2C0)
__declspec(naked) inline void __fastcall MissileSetTarget(CEngineObject*, void*, int) EI_ORIGINAL(0x0067AC70)
__declspec(naked) inline Projectile* __fastcall ProjectileConstruct(void*, void*) EI_ORIGINAL(0x0067E510)
__declspec(naked) inline void __fastcall ArmourTexture(mfc::CString*, std::uint32_t, std::uint32_t)
    EI_ORIGINAL(0x006A3480)
// 006AC970 (patched by Rebirth, so always through its address): the item manager's item by name (RET 4; EAX the item, or
// null).
__declspec(naked) inline CEngineObject* __fastcall ItemManagerCreateItemOriginal(void* /*manager*/, void*, const char* /*name*/)
    EI_ORIGINAL(0x006AC970)
// 00665470: Player::GiveItems(list, flag) (RET 8, EAX not a result).
__declspec(naked) inline void __fastcall PlayerGiveItemsOriginal(void* /*player*/, void*, void* /*items*/, int /*flag*/)
    EI_ORIGINAL(0x00665470)

// The item database (007AF60C) lists the names of each kind into a CStringArray: 006AD2F0 weapons, 006AD380 armors,
// 006AD410 quick items, 006AD260 the fourth kind (materials). The spell database (007AF604): 00689570 spells, 00689610
// modifiers. All __thiscall, RET 4, no result.
__declspec(naked) inline void __fastcall ItemDbListWeaponsOriginal(void* /*db*/, void*, mfc::CStringArray*) EI_ORIGINAL(0x006AD2F0)
__declspec(naked) inline void __fastcall ItemDbListArmorsOriginal(void* /*db*/, void*, mfc::CStringArray*) EI_ORIGINAL(0x006AD380)
__declspec(naked) inline void __fastcall ItemDbListQuickItemsOriginal(void* /*db*/, void*, mfc::CStringArray*)
    EI_ORIGINAL(0x006AD410)
__declspec(naked) inline void __fastcall ItemDbListFourthKindOriginal(void* /*db*/, void*, mfc::CStringArray*)
    EI_ORIGINAL(0x006AD260)
__declspec(naked) inline void __fastcall SpellDbListSpellsOriginal(void* /*db*/, void*, mfc::CStringArray*) EI_ORIGINAL(0x00689570)
__declspec(naked) inline void __fastcall SpellDbListModifiersOriginal(void* /*db*/, void*, mfc::CStringArray*)
    EI_ORIGINAL(0x00689610)

// C3h. 00661700: party Reset(a, b) (RET 8, EAX not a result); 00661DB0: party AddItem(item, flag1, flag2) (RET 0xC; the
// two flags are read as bytes; EAX is the item, or null).
__declspec(naked) inline void __fastcall PartyResetOriginal(void* /*party*/, void*, int /*a*/, int /*b*/)
    EI_ORIGINAL(0x00661700)
// 00619920: the insert overflow of a vector of party file entries (0x74 bytes: the records and a CString file name;
// the multiplayer heroes 007C82A4, the world's party list): `count` copies of `value` at `position` in a new block
// (RET 0xC, no result).
__declspec(naked) inline void __fastcall PartyEntriesInsertOverflowOriginal(void* /*vector*/, void*, void* /*position*/,
                                                                            const void* /*value*/, unsigned /*count*/)
    EI_ORIGINAL(0x00619920)
// 00662AC0 (RET 4, no result): a multiplayer hero record (MpHeroRecord, ui_screens.h) writes its party file into
// the folder (a CString; the heroes' folder 007C82A0) passed as an int; rewritten as MpHeroRecord::WriteFile
// (party_file.cpp), called through its address so the verify wrapper runs.
__declspec(naked) inline void __fastcall PartySendTransferOriginal(void* /*party*/, void*, int /*transfer*/) EI_ORIGINAL(0x00662AC0)
__declspec(naked) inline void* __fastcall PartyAddItemOriginal(void* /*party*/, void*, void* /*item*/, int /*a*/,
                                                               int /*b*/) EI_ORIGINAL(0x00661DB0)

// C3i. 00661050: party record builder / embedded party constructor (plain RET, returns this). 006628D0: adds a member record
// (name, prototype, extra; RET 0xC, EAX is the new record).
__declspec(naked) inline void* __fastcall PartyRecordsConstructOriginal(void* /*party*/) EI_ORIGINAL(0x00661050)
__declspec(naked) inline void* __fastcall PartyAddMemberOriginal(void* /*party*/, void*, const char* /*name*/,
                                                                 const char* /*prototype*/, const char* /*extra*/)
    EI_ORIGINAL(0x006628D0)

// 006625B0 (RET 4): the party gives `record` its prototype's starting equipment (its weapons and spells released
// first; weapons, quick items and spells from the prototype's lists (by text 5), its experience). 1 in EAX, 0
// without a prototype.
__declspec(naked) inline int __fastcall PartyEquipFromPrototypeOriginal(void* /*party*/, void*, void* /*record*/)
    EI_ORIGINAL(0x006625B0)

// C3i part 2. 00613820: the quick item record by name (RET 4). 006AD000: item factory MakeItem (RET 0x10, returns the item);
// 006AD1E0: CreateSpell(name, owner) (RET 8, returns the spell). 006611D0: party record builder destructor, 00665200: player
// clear, 00665A10: player disconnect, 0066EF80: player state refresh, 0066F450: resend the party (all plain RET, EAX not a
// result). 0066C180: creates a player object for a session (plain RET, EAX is the player or null).
__declspec(naked) inline void* __fastcall FindQuickItemOriginal(void* /*map*/, void*, const char* /*name*/) EI_ORIGINAL(0x00613820)
__declspec(naked) inline void* __fastcall FactoryMakeItemOriginal(void* /*factory*/, void*, int, int, int, int)
    EI_ORIGINAL(0x006AD000)
__declspec(naked) inline void* __fastcall FactoryCreateSpellOriginal(void* /*factory*/, void*, const char* /*name*/,
                                                                     void* /*owner*/) EI_ORIGINAL(0x006AD1E0)
__declspec(naked) inline void __fastcall PartyRecordsDestructOriginal(void* /*party*/) EI_ORIGINAL(0x006611D0)
__declspec(naked) inline void __fastcall PlayerClearOriginal(void* /*player*/) EI_ORIGINAL(0x00665200)
__declspec(naked) inline void __fastcall PlayerDisconnectOriginal(void* /*player*/) EI_ORIGINAL(0x00665A10)
__declspec(naked) inline void* __fastcall PlayerCreateOriginal(void* /*session*/) EI_ORIGINAL(0x0066C180)
// 0066D730 (CPlayerServer::SendValue12, RET 8; player_messages.cpp): message 0x12. Through its
// address, so that its verify wrapper runs (CPlayerManager::SendValueToAll is its only caller).
__declspec(naked) inline void __fastcall PlayerServerSend12Original(void* /*server*/, void*, int /*unused*/, int /*value*/)
    EI_ORIGINAL(0x0066D730)
__declspec(naked) inline void __fastcall PlayerStateRefreshOriginal(void* /*state*/) EI_ORIGINAL(0x0066EF80)
// 0059C3D0 (RET 8, no result): SetSize(count, growBy) of the unit placements (an MFC CArray of 0x28-byte records whose
// new elements 004CEE20 builds), the player state's +0x74 and the zone entry's list.
__declspec(naked) inline void __fastcall PlacementsSetSizeOriginal(void* /*array*/, void*, int /*count*/, int /*growBy*/)
    EI_ORIGINAL(0x0059C3D0)
__declspec(naked) inline void __fastcall PlayerResendPartyOriginal(void* /*state*/) EI_ORIGINAL(0x0066F450)
// 00664B70 (RET 4): formats a message error ("... prevID=%x curID=%x") with the previous and the current message
// type (bytes) and raises the fatal error 00480080; returns its result.
// 00665870 (RET 4): the player drops the item with this id from its item list.
__declspec(naked) inline void __fastcall PlayerRemoveItemOriginal(void* /*player*/, void*, int /*id*/) EI_ORIGINAL(0x00665870)
__declspec(naked) inline int __fastcall PlayerMessageErrorOriginal(const char* /*format*/, int /*previous*/, int /*type*/)
    EI_ORIGINAL(0x00664B70)

// C3l. The unit record's four item lists (arrays of counted entries at +0x730 quick, +0x740 belt, +0x750 worn, +0x760 spells;
// data / count / capacity at +0, +4, +8): Add(item) makes the item's record entry (0069FF70 with the item's manager context),
// adds a reference (+4), appends it to the list and returns it (RET 4); Remove(entry) releases the entry through slot 4 and
// closes the gap (RET 4, no result). Add: 006608D0 quick, 00660AB0 belt, 00660C90 worn, 00660E70 spells; Remove: 00660A50 quick,
// 00660C30 belt, 00660E10 worn, 00660FF0 spells (world_quests.cpp used the names of the other lists for the removers).
__declspec(naked) inline void* __fastcall RecordAddQuickEntryOriginal(void* /*record*/, void*, void* /*item*/)
    EI_ORIGINAL(0x006608D0)
__declspec(naked) inline void __fastcall RecordRemoveQuickEntryOriginal(void* /*record*/, void*, void* /*entry*/)
    EI_ORIGINAL(0x00660A50)
__declspec(naked) inline void* __fastcall RecordAddBeltEntryOriginal(void* /*record*/, void*, void* /*item*/)
    EI_ORIGINAL(0x00660AB0)
__declspec(naked) inline void __fastcall RecordRemoveBeltEntryOriginal(void* /*record*/, void*, void* /*entry*/)
    EI_ORIGINAL(0x00660C30)
__declspec(naked) inline void* __fastcall RecordAddWornEntryOriginal(void* /*record*/, void*, void* /*item*/)
    EI_ORIGINAL(0x00660C90)
__declspec(naked) inline void __fastcall RecordRemoveWornEntryOriginal(void* /*record*/, void*, void* /*entry*/)
    EI_ORIGINAL(0x00660E10)
__declspec(naked) inline void* __fastcall RecordAddSpellEntryOriginal(void* /*record*/, void*, void* /*item*/)
    EI_ORIGINAL(0x00660E70)
__declspec(naked) inline void __fastcall RecordRemoveSpellEntryOriginal(void* /*record*/, void*, void* /*entry*/)
    EI_ORIGINAL(0x00660FF0)
// 00661F30: takes an item out of the party's item list (RET 8: item, flag read as a byte; when the stack count at +0x48 is above
// 1 and the flag is clear it only lowers the count and EAX is the item, otherwise the item is released and removed and EAX is
// 0; no caller reads it). 006620F0: removes the member record (0x784 bytes) whose key at +0x1C equals the argument (RET 4; EAX 1
// when found, else 0). 00665440: the unit belongs to the player (RET 4; EAX 0/1). 00665780: erases the quest item (type 0x3009)
// with a number (RET 4; EAX 1 when found, else 0). 00665A70 / 00665B50: add / remove a unit of the player (RET 4, EAX is not a
// result).
__declspec(naked) inline void* __fastcall PartyRemoveItemOriginal(void* /*party*/, void*, void* /*item*/, int /*flag*/)
    EI_ORIGINAL(0x00661F30)
__declspec(naked) inline int __fastcall PartyRemoveMemberOriginal(void* /*party*/, void*, unsigned /*id*/)
    EI_ORIGINAL(0x006620F0)
__declspec(naked) inline int __fastcall PlayerOwnsUnitOriginal(const void* /*player*/, void*, const void* /*unit*/)
    EI_ORIGINAL(0x00665440)
__declspec(naked) inline int __fastcall PlayerEraseQuestItemOriginal(void* /*player*/, void*, int /*number*/)
    EI_ORIGINAL(0x00665780)
__declspec(naked) inline void __fastcall PlayerAddUnitOriginal(void* /*player*/, void*, void* /*unit*/) EI_ORIGINAL(0x00665A70)
__declspec(naked) inline void __fastcall PlayerRemoveUnitOriginal(void* /*player*/, void*, void* /*unit*/)
    EI_ORIGINAL(0x00665B50)
// 0066D490: writes a path message about a unit to the message stream in ECX (the member at the player's +0x20; RET 0x18: unit,
// motion order, point, kind, flag, limit; the limit is copied as a dword; EAX is not a result). 0066D7F0: StateObjectsAdded,
// walks the pointer array it is given (data at +0, count at +4) (RET 4; EAX is always 1, no caller reads it).
__declspec(naked) inline void __fastcall PlayerPathMessageOriginal(void* /*sink*/, void*, void* /*unit*/,
                                                                   const void* /*motionOrder*/, const float* /*point*/,
                                                                   int /*kind*/, int /*flag*/, std::uint32_t /*limitBits*/)
    EI_ORIGINAL(0x0066D490)
__declspec(naked) inline int __fastcall StateObjectsAddedOriginal(void* /*state*/, void*, const void* /*objects*/)
    EI_ORIGINAL(0x0066D7F0)

// A-I7. A class template's stock lists (+0x11C all, +0x1D4 spells and modifiers; thiscall on the list, RET 4): each
// appends the record to its vector unless already there (no result): 00699080 armour, 006990E0 weapon, 00699140 quick
// item, 006991A0 spell, 00699200 modifier.
__declspec(naked) inline void __fastcall StockAddArmorOriginal(void* /*list*/, void*, void* /*record*/) EI_ORIGINAL(0x00699080)
__declspec(naked) inline void __fastcall StockAddWeaponOriginal(void* /*list*/, void*, void* /*record*/) EI_ORIGINAL(0x006990E0)
__declspec(naked) inline void __fastcall StockAddQuickItemOriginal(void* /*list*/, void*, void* /*record*/)
    EI_ORIGINAL(0x00699140)
__declspec(naked) inline void __fastcall StockAddSpellOriginal(void* /*list*/, void*, void* /*record*/) EI_ORIGINAL(0x006991A0)
__declspec(naked) inline void __fastcall StockAddModifierOriginal(void* /*list*/, void*, void* /*record*/)
    EI_ORIGINAL(0x00699200)
// 00613920: the spell prototype named `name` (thiscall on the spell table 007C8510, RET 4), or null.
__declspec(naked) inline char* __fastcall SpellPrototypeByNameOriginal(void* /*table*/, void*, const char* /*name*/)
    EI_ORIGINAL(0x00613920)
// C3m. The spell and modifier records by code (thiscall on their tables; null when missing): 0068C820 on 007C8510
// (0x84-byte records, the code a CString at +4), 0068C920 on 007C8530 (0x24-byte records, the code inline at +4).
// Both rewritten as itemdb::CodeIndexedTable::FindByCode; through their addresses.
__declspec(naked) inline char* __fastcall FindSpellByCode(void*, void*, const char*) EI_ORIGINAL(0x0068C820)
__declspec(naked) inline char* __fastcall FindModifierByCode(void*, void*, const char*) EI_ORIGINAL(0x0068C920)
// C3m. 0069D290: a record of the material table by name; 0069D380: the class-0x15 prototype table's record by name
// (thiscall on 007C8778, item_db.h's QuestItems() table). RET 4; null when missing.
__declspec(naked) inline char* __fastcall FindMaterialNamed(void*, void*, const char*) EI_ORIGINAL(0x0069D290)
__declspec(naked) inline char* __fastcall FindTable15Named(void*, void*, const char*) EI_ORIGINAL(0x0069D380)
// C3m. 0059C7A0 (the world server's missile list, an MFC CTypedPtrList of counted Projectile pointers): NewNode(prev,
// next), a node taken from the free list or carved from a new block.
__declspec(naked) inline mfc::CTypedPtrList<Projectile>::Node* __fastcall ProjectileNewNode(
    mfc::CTypedPtrList<Projectile>*, void*, mfc::CTypedPtrList<Projectile>::Node* /*prev*/,
    mfc::CTypedPtrList<Projectile>::Node* /*next*/) EI_ORIGINAL(0x0059C7A0)

// C3m. 006A5C00 (rewritten as CItemConstructor::SetManager; thiscall): the item constructor takes its manager. Base of
// the views of the constructor.
struct ItemConstructorManagerOriginal {
    void SetManager(void* manager);
};
__declspec(naked) inline void ItemConstructorManagerOriginal::SetManager(void*) EI_ORIGINAL(0x006A5C00)
