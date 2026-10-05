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
__declspec(naked) inline void __fastcall PlayerListRelease(CEngineObject*, void*, WorldPlayer*)
    EI_ORIGINAL(0x0066C1D0)
__declspec(naked) inline void __fastcall PlayerSendPair(void*, void*, int, float) EI_ORIGINAL(0x0066F2C0)
__declspec(naked) inline void __fastcall MissileSetTarget(CEngineObject*, void*, int) EI_ORIGINAL(0x0067AC70)
__declspec(naked) inline Projectile* __fastcall ProjectileConstruct(void*, void*) EI_ORIGINAL(0x0067E510)
__declspec(naked) inline void __fastcall ArmourTexture(mfc::CString*, std::uint32_t, std::uint32_t)
    EI_ORIGINAL(0x006A3480)
// 00665470: Player::GiveItems(list, flag) (RET 8, EAX not a result).
__declspec(naked) inline void __fastcall PlayerGiveItemsOriginal(void* /*player*/, void*, void* /*items*/, int /*flag*/)
    EI_ORIGINAL(0x00665470)

// C3h. 00661700: party Reset(a, b) (RET 8, EAX not a result); 00661DB0: party AddItem(item, flag1, flag2) (RET 0xC; the
// two flags are read as bytes; EAX is the item, or null).
__declspec(naked) inline void __fastcall PartyResetOriginal(void* /*party*/, void*, int /*a*/, int /*b*/)
    EI_ORIGINAL(0x00661700)
__declspec(naked) inline void* __fastcall PartyAddItemOriginal(void* /*party*/, void*, void* /*item*/, int /*a*/,
                                                               int /*b*/) EI_ORIGINAL(0x00661DB0)

// C3i. 00661050: party record builder / embedded party constructor (plain RET, returns this). 006628D0: adds a member record
// (name, prototype, extra; RET 0xC, EAX is the new record).
__declspec(naked) inline void* __fastcall PartyRecordsConstructOriginal(void* /*party*/) EI_ORIGINAL(0x00661050)
__declspec(naked) inline void* __fastcall PartyAddMemberOriginal(void* /*party*/, void*, const char* /*name*/,
                                                                 const char* /*prototype*/, const char* /*extra*/)
    EI_ORIGINAL(0x006628D0)

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
__declspec(naked) inline void __fastcall PlayerStateRefreshOriginal(void* /*state*/) EI_ORIGINAL(0x0066EF80)
__declspec(naked) inline void __fastcall PlayerResendPartyOriginal(void* /*state*/) EI_ORIGINAL(0x0066F450)

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
