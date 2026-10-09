#pragma once

// The world's player manager (vtable 0073F328; a CBaseManager of CPlayer objects, the world's +0x38,
// also the script commands' event dispatcher 007AFF98). player_manager.cpp (A-H15). Its slot 2 and
// 14 bodies are registered by address under their own names (rule 4: they are CBaseManager
// virtuals). 0066C180 (a player for a lobby session) is Rebirth-patched and stays original.

#include "base_manager.h"

#include <cstdint>

class CPlayerManager : public CBaseManager {
public:
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x0073F328);

    // 0066C0F0 (slot 2). CBaseManager's Create, its result made 0 / 1.
    BOOL CreatePlayers();
    // 0066C110 (slot 14, RET 4). A new CPlayer (0x134 bytes, CPlayer::Construct) whose type_ is
    // `type`; null when the allocation fails.
    CEngineObject* NewPlayer(int type);
    // 0066C1D0 (RET 4). `player`'s Release (slot 4); `this` is not used.
    void ReleasePlayer(CEngineObject* player);
    // 0066C1E0 (RET 8; the client's message 0x00, the server's player id). `object` is filed
    // under `id`: its old id's entry erased, its id_ set, maxId_ raised, the table entry set
    // (positions_ and the object list are left alone).
    void ChangeId(CEngineObject* object, int id);
    // 0066C340 / 0066C3C0 (RET 8). Every player's connection (a CPlayerServer) gets message
    // 0x12 (0066D730: one uninitialised byte, then `value`) / 0x13 (SendText13, `text`).
    void SendValueToAll(int unused, int value);
    void SendTextToAll(int unused, const char* text);
};
static_assert(sizeof(CPlayerManager) == 0xB0);
