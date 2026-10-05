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
