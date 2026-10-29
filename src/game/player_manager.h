#pragma once

// The world's player manager (a BaseManager of CPlayer objects, the world's +0x38, also the script commands'
// event dispatcher; a real virtual class); player_manager.cpp holds its lifetime and class virtuals. Creating a
// player for a lobby session is Rebirth-patched (PlayerNumbers, reproduced behind its flag).

#include "base_manager.h"

#include <cstdint>

class CPlayer;

class PlayerManager : public BaseManager {
public:
    PlayerManager();
    ~PlayerManager() override;
    const char* ClassName() override;
    ClassId GetClassId() override;
    EngineObject* NewObject(int type) override;

    // A new CPlayer (0x134 bytes, CPlayer::Construct) whose type is `type`; null when the allocation fails.
    EngineObject* NewPlayer(int type);
    // A new player (type 0x300A, a free id) with a new connection from the
    // world's side-specific manager (SideObjectType::Player: a PlayerServer or a PlayerClient), one reference
    // taken, player number 0 (Rebirth's PlayerNumbers: the server world's highest other + 1). Null
    // when the player cannot be made (the connection is not checked).
    CPlayer* CreateSessionPlayer();
    // `player`'s Release; `this` is not used.
    void ReleasePlayer(EngineObject* player);
    // Called for the client's message 0x00 and the server's player id. `object` is filed
    // under `id`: its old id's entry erased, its id set, maxId raised, the table entry set
    // (positions and the object list are left alone).
    void ChangeId(EngineObject* object, int id);
    // Every player's connection (a PlayerServer) gets message
    // 0x12 (SendValue12 through its address: a byte, then `value`) / 0x13 (SendText13, `text`).
    void SendValueToAll(int value);
    void SendTextToAll(const char* text);
};
