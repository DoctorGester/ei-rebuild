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
