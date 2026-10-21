#pragma once

// The world's player manager (a CBaseManager of CPlayer objects, the world's +0x38, also the script
// commands' event dispatcher 007AFF98; a real virtual class: the compiler makes its vtable, C19,
// game.exe's 0073F328 is retired). player_manager.cpp (A-H15), its lifetime and class slots
// world_managers.cpp. Its slot 2 and 14 bodies are registered by member pointer under their own
// names. 0066C180 (a player for a lobby session) is Rebirth-patched (PlayerNumbers, reproduced
// behind its flag); callers go through its address (PlayerCreateOriginal, rule 9).

#include "base_manager.h"

#include <cstdint>

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

class CPlayerManager : public CBaseManager {
public:
    // CBaseManager's constructor (inlined in WorldCreateManagers 0055EE40).
    CPlayerManager();
    CPlayerManager* Construct();   // the constructor in place; returns this
    // 0055F350. CBaseManager's destructor.
    ~CPlayerManager() override;
    // 0055F330 (slot 0's registration): the destructor, then operator delete when bit 0 of
    // `flags` is set. Returns this.
    CPlayerManager* DeleteScalar(unsigned flags);
    int Create() override;                         // 2: CreatePlayers
    const char* GetClassName() override;           // 11: 0055F310 "CPlayerManager"
    int GetClassId() override;                     // 12: 0055F320 (0x2E)
    CEngineObject* NewObject(int type) override;   // 14: NewPlayer

    // 0066C0F0 (slot 2). CBaseManager's Create, its result made 0 / 1.
    BOOL CreatePlayers();
    // 0066C110 (slot 14, RET 4). A new CPlayer (0x134 bytes, CPlayer::Construct) whose type_ is
    // `type`; null when the allocation fails.
    CEngineObject* NewPlayer(int type);
    // 0066C180 (A-H26). A new player (type 0x300A, a free id) with a new connection from the
    // world's side-specific manager (type 0x49: a CPlayerServer or a CPlayerClient), one reference
    // taken, player number 0 (Rebirth's PlayerNumbers: the server world's highest other + 1). Null
    // when the player cannot be made (the connection is not checked).
    CEngineObject* CreateSessionPlayer();
    // 0066C1D0 (RET 4). `player`'s Release (slot 4); `this` is not used.
    void ReleasePlayer(CEngineObject* player);
    // 0066C1E0 (RET 8; the client's message 0x00, the server's player id). `object` is filed
    // under `id`: its old id's entry erased, its id_ set, maxId_ raised, the table entry set
    // (positions_ and the object list are left alone).
    void ChangeId(CEngineObject* object, int id);
    // 0066C340 / 0066C3C0 (RET 8). Every player's connection (a CPlayerServer) gets message
    // 0x12 (SendValue12 through its address: a byte, then `value`) / 0x13 (SendText13, `text`).
    void SendValueToAll(int unused, int value);
    void SendTextToAll(int unused, const char* text);
};
static_assert(sizeof(CPlayerManager) == 0xB0);
