#pragma once

// Original functions forwarded from several files (one declaration per address; the bodies
// jump to game.exe). Types are forward declared; include the class headers where members are used.

#include "ei/hook.h"

#include <cstddef>
#include <cstdint>
#include "lib/mfc.h"

class CEngineObject;
class CItem;
class CItemArmor;
class CItemConstructor;
class CSpell;
struct Projectile;
struct WorldPlayer;

__declspec(naked) inline void __fastcall PartyResetItems(void* /*party*/) EI_ORIGINAL(0x00661FB0)
// 006AC970 (patched by Rebirth, so always through its address): the item manager's item by name (RET 4; EAX the item, or
// null).
__declspec(naked) inline CItem* __fastcall ItemManagerCreateItemOriginal(void* /*manager*/, void*, const char* /*name*/)
    EI_ORIGINAL(0x006AC970)

// C3h. 00661700: party Reset(a, b) (RET 8, EAX not a result); 00661DB0: party AddItem(item, flag1, flag2) (RET 0xC; the
// two flags are read as bytes; EAX is the item, or null).
__declspec(naked) inline void __fastcall PartyResetOriginal(void* /*party*/, void*, int /*a*/, int /*b*/)
    EI_ORIGINAL(0x00661700)

// C3i part 2. 00613820: the quick item record by name (RET 4). 006AD000: item factory MakeItem (RET 0x10, returns the item);
// 006AD1E0: CreateSpell(name, owner) (RET 8, returns the spell). 006611D0: party record builder destructor, 00665200: player
// clear, 00665A10: player disconnect (all plain RET, EAX not a result). 0066C180: creates a player object for a session (plain RET, EAX is the player or null).
__declspec(naked) inline void __fastcall PartyRecordsDestructOriginal(void* /*party*/) EI_ORIGINAL(0x006611D0)
__declspec(naked) inline void __fastcall PlayerClearOriginal(void* /*player*/) EI_ORIGINAL(0x00665200)
__declspec(naked) inline void __fastcall PlayerDisconnectOriginal(void* /*player*/) EI_ORIGINAL(0x00665A10)
__declspec(naked) inline void* __fastcall PlayerCreateOriginal(void* /*session*/) EI_ORIGINAL(0x0066C180)
// 00664B70 (RET 4): formats a message error ("... prevID=%x curID=%x") with the previous and the current message
// type (bytes) and raises the fatal error 00480080; returns its result.
__declspec(naked) inline int __fastcall PlayerMessageErrorOriginal(const char* /*format*/, int /*previous*/, int /*type*/)
    EI_ORIGINAL(0x00664B70)

// C3l. The unit record's four item lists (arrays of counted entries at +0x730 quick, +0x740 belt, +0x750 worn, +0x760 spells;
// data / count / capacity at +0, +4, +8): Add(item) makes the item's record entry (0069FF70 with the item's manager context),
// adds a reference (+4), appends it to the list and returns it (RET 4); Remove(entry) releases the entry through slot 4 and
// closes the gap (RET 4, no result). Add: 006608D0 quick, 00660AB0 belt, 00660C90 worn, 00660E70 spells; Remove: 00660A50 quick,
// 00660C30 belt, 00660E10 worn, 00660FF0 spells (world_quests.cpp used the names of the other lists for the removers).
// 00661F30: takes an item out of the party's item list (RET 8: item, flag read as a byte; when the stack count at +0x48 is above
// 1 and the flag is clear it only lowers the count and EAX is the item, otherwise the item is released and removed and EAX is
// 0; no caller reads it). 006620F0: removes the member record (0x784 bytes) whose key at +0x1C equals the argument (RET 4; EAX 1
// when found, else 0). 00665780: erases the quest item (type 0x3009)
// with a number (RET 4; EAX 1 when found, else 0). 00665A70 / 00665B50: add / remove a unit of the player (RET 4, EAX is not a
// result).
__declspec(naked) inline int __fastcall PlayerEraseQuestItemOriginal(void* /*player*/, void*, int /*number*/)
    EI_ORIGINAL(0x00665780)

// 00613920: the spell prototype named `name` (thiscall on the spell table 007C8510, RET 4), or null.
// C3m. 0069D290: a record of the material table by name; 0069D380: the class-0x15 prototype table's record by name
// (thiscall on 007C8778, item_db.h's QuestItems() table). RET 4; null when missing.

// C3n batch 4. The player and its server half (thiscall), through their addresses so the wrappers run: 00665DE0
// (rewritten as CPlayer::Tick; plain RET) the player's update before the tick; 0066D6E0 (CPlayerServer::SendObject0B; RET 8)
// the message (player + 0x20) about an object with a code / value; 0066EE10 / 0066EE80 (CPlayerServer::SetVariable /
// SetVariableMax; RET 8) a quest variable of the name, set / raised to the value.
__declspec(naked) inline void __fastcall PlayerTickOriginal(void* /*player*/) EI_ORIGINAL(0x00665DE0)
__declspec(naked) inline void __fastcall PlayerSendObject0BOriginal(void* /*messages*/, void*, const void* /*object*/,
                                                                    std::uint32_t /*value*/) EI_ORIGINAL(0x0066D6E0)
__declspec(naked) inline void __fastcall PlayerStateSetVariableMaxOriginal(void* /*state*/, void*, const char* /*name*/,
                                                                           float /*value*/) EI_ORIGINAL(0x0066EE80)

