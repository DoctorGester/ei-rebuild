#pragma once

// The player connections' tables as compiled tags (C19, engine_object_class.h) in place of game.exe's 007431E0
// (CPlayerConnection, the base: the constructors store it first and the destructors last), 00743180 (CPlayerClient,
// player_client.cpp) and 00743280 (CPlayerServer, player_messages.cpp). ConnectionClass declares the connection's
// slots 13-23 (player_messages.h's CPlayerConnection) with the base's bodies: 13 and 23 pure, the others empty
// (00666EE0 ... 00666F60, RET 4 / RET 8 where they take arguments). Each side overrides the slots its table changes.

#include "player_messages.h"

#include "engine_object_class.h"

template <typename Self, typename T>
class ConnectionClass : public EngineObjectClass<Self, T> {
public:
    virtual void ForgetSentObjects() { _purecall(); }                     // 13: pure
    virtual void UnitChanged(CUnit*) {}                                   // 14: 00666EE0 (RET 4)
    virtual void ResetState() {}                                          // 15: 00666EF0
    virtual void Leave() {}                                               // 16: 00666F00
    virtual void ItemsGiven(const mfc::CTypedPtrList<CItem>*, int) {}     // 17: 00666F10 (RET 8)
    virtual void ItemsTaken() {}                                          // 18: 00666F20
    virtual void ItemTaken(CEngineObject*) {}                             // 19: 00666F30 (RET 4)
    virtual void LeaderDied() {}                                          // 20: 00666F40
    virtual void TickDone() {}                                            // 21: 00666F50
    virtual void ItemUsed(CEngineObject*) {}                              // 22: 00666F60 (RET 4)
    virtual int Receive() { return _purecall(); }                         // 23: pure
};
