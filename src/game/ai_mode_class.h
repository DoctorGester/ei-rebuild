#pragma once

// The AI modes' tables as compiled tags (C19, lib/compiled_vtable.h; docs/notes/standalone.md, "Batch 17's
// families") in place of game.exe's: the abstract bases 0073D15C (ModeBaseClass) and 0073D22C (ActionBaseClass),
// Guard 0073D128, Sentry 0073D190, Follow 0073D1C4, Suspection 0073D1F8, Aggression 0073D260, CorpseWatcher
// 0073D294, Fear 0073FF8C, Patrol 0073FF40, Player 0073F0F8 and Tamed 0073F0C4. A mode keeps its table as a plain
// field (AIMode::vtable); its constructors, clones and destructors store `Tag::Vtable()`, and the brain's dispatch
// through AIModeVirtuals (unit_ai.h) reaches the tag's slots, which call the bodies game.exe's table reached
// (qualified: no dispatch). ModeClass declares the 13 slots with the abstract base 0073D15C's bodies; ActionModeClass
// those 0073D22C changes. The harness digest names a mode by its table (tests/verify/harness_digest.cpp: game.exe's
// address in game.exe, these tables in the image).

#include "unit_ai.h"

#include "lib/compiled_vtable.h"

#include <cstdint>

extern "C" int __cdecl _purecall();

namespace aimode {

template <typename Self, typename T>
class ModeClass : public ei::CompiledTag<Self> {
public:
    virtual void* DeleteScalar(unsigned flags) { return Mode()->DeletingDestructBase(static_cast<int>(flags)); }   // 0
    virtual void Serialize(ChunkArchive*) {}                           // 1: 00412580 (RET 4)
    virtual int OnShutdown() { return _purecall(); }                   // 2: pure
    virtual CRefObject* Clone() {                                      // 3: pure
        _purecall();
        return nullptr;
    }
    virtual int Evaluate(AIBrain* brain) { return Mode()->Priority50(brain); }    // 4: 005D0A80
    virtual void Execute(AIBrain* brain) { Mode()->LookAroundExecute(brain); }   // 5: 005D0A90
    virtual void Enter(AIBrain* brain) { Mode()->EnterIdle(brain); }             // 6: 005D0AF0
    virtual void Leave(AIBrain*) {}                                     // 7: 004BA220 (RET 4)
    virtual int GetType() { return _purecall(); }                       // 8: pure
    virtual bool Exclusive() { return true; }                           // 9: 004BA240
    virtual void OnObjectDeleted(CEngineObject*) {}                     // 10: 004BA230 (RET 4)
    virtual void Load(CStream*) {}                                      // 11: 004BA2B0 (RET 4)
    virtual void Save(CStream*) {}                                      // 12: 004BA2C0 (RET 4)

protected:
    AIMode* Mode() { return this->template ObjectAs<AIMode>(); }
    T* Derived() { return this->template ObjectAs<T>(); }
    // A slot 3 result as CRefObject's slot returns it.
    static CRefObject* AsRefObject(AIMode* mode) { return reinterpret_cast<CRefObject*>(mode); }
};

// The abstract action base 0073D22C: slot 0 is 004BA680 (the same body as 004BA340), 4 / 5 pure, 6 ClearAction,
// 9 false.
template <typename Self, typename T>
class ActionModeClass : public ModeClass<Self, T> {
public:
    int Evaluate(AIBrain*) override { return _purecall(); }                          // 4: pure
    void Execute(AIBrain*) override { _purecall(); }                                 // 5: pure
    void Enter(AIBrain* brain) override { this->Mode()->ClearAction(brain); }        // 6: 005D0A50
    bool Exclusive() override { return false; }                                      // 9: 004BA620
};

// Slot 2's result where the original's body leaves EAX as it was: nothing reads it (CRefObject::Shutdown's callers
// drop it).
constexpr int kNoResult = 0;

class ModeBaseClass final : public ModeClass<ModeBaseClass, AIMode> {};          // 0073D15C
class ActionBaseClass final : public ActionModeClass<ActionBaseClass, AIMode> {};   // 0073D22C

// 0073D128.
class GuardClass final : public ModeClass<GuardClass, GuardMode> {
public:
    void* DeleteScalar(unsigned flags) override { return Derived()->GuardDeletingDestruct(static_cast<int>(flags)); }   // 0: 004BA300
    int OnShutdown() override {   // 2: 004BA2D0
        Derived()->GuardReset();
        return kNoResult;
    }
    CRefObject* Clone() override { return AsRefObject(Derived()->GuardClone()); }   // 3: 004BA250
    void Execute(AIBrain* brain) override { Derived()->GuardMode::Execute(brain); }   // 5: 005D0BE0
    int GetType() override { return kModeGuard; }                                  // 8: 004BA2F0
    void Load(CStream* stream) override { Derived()->GuardMode::Load(stream); }      // 11: 005D0EB0
    void Save(CStream* stream) override { Derived()->GuardMode::Save(stream); }      // 12: 005D0F00
};

// 0073D190.
class SentryClass final : public ModeClass<SentryClass, SentryMode> {
public:
    void* DeleteScalar(unsigned flags) override { return Derived()->SentryDeletingDestruct(static_cast<int>(flags)); }   // 0: 004BA420
    int OnShutdown() override {   // 2: 004BA3F0
        Derived()->SentryReset();
        return kNoResult;
    }
    CRefObject* Clone() override { return AsRefObject(Derived()->SentryClone()); }    // 3: 004BA3B0
    void Execute(AIBrain* brain) override { Derived()->SentryMode::Execute(brain); }   // 5: 005D1B70
    int GetType() override { return Mode()->SentryType(); }                          // 8: 004BA410
    void Load(CStream* stream) override { Derived()->SentryMode::Load(stream); }       // 11: 005D1DA0
    void Save(CStream* stream) override { Derived()->SentryMode::Save(stream); }       // 12: 005D1DC0
};

// 0073D1C4.
class FollowClass final : public ModeClass<FollowClass, FollowMode> {
public:
    void* DeleteScalar(unsigned flags) override { return Derived()->FollowDeletingDestruct(static_cast<int>(flags)); }   // 0: 004BA590
    int OnShutdown() override {   // 2: 004BA510
        Derived()->FollowReset();
        return kNoResult;
    }
    CRefObject* Clone() override { return AsRefObject(Derived()->FollowClone()); }    // 3: 004BA4A0
    void Execute(AIBrain* brain) override { Derived()->FollowMode::Execute(brain); }   // 5: 005D1E10
    int GetType() override { return Mode()->FollowType(); }                          // 8: 004BA580
    void OnObjectDeleted(CEngineObject* object) override { Derived()->FollowMode::OnObjectDeleted(object); }   // 10: 005D1DE0
    void Load(CStream* stream) override { Derived()->FollowMode::Load(stream); }       // 11: 005D1E60
    void Save(CStream* stream) override { Derived()->FollowMode::Save(stream); }       // 12: 005D1E80
};

// 0073D1F8.
class SuspectionClass final : public ActionModeClass<SuspectionClass, SuspectionMode> {
public:
    void* DeleteScalar(unsigned flags) override {   // 0: 004BA6E0
        return Derived()->SuspectionDeletingDestruct(static_cast<int>(flags));
    }
    int OnShutdown() override {   // 2: 004BA6B0
        Derived()->SuspectionReset();
        return kNoResult;
    }
    CRefObject* Clone() override { return AsRefObject(Derived()->SuspectionClone()); }          // 3: 004BA630
    int Evaluate(AIBrain* brain) override { return Derived()->SuspectionMode::Evaluate(brain); }   // 4: 005D1E90
    void Execute(AIBrain* brain) override { Derived()->SuspectionMode::Execute(brain); }         // 5: 005D1F80
    void Enter(AIBrain* brain) override { Derived()->SuspectionMode::Enter(brain); }             // 6: 005D21A0
    void Leave(AIBrain* brain) override { Derived()->SuspectionMode::Leave(brain); }             // 7: 005D2310
    int GetType() override { return Mode()->SuspectionType(); }                                // 8: 004BA6D0
    void Load(CStream* stream) override { Derived()->SuspectionMode::Load(stream); }             // 11: 005D2330
    void Save(CStream* stream) override { Derived()->SuspectionMode::Save(stream); }             // 12: 005D2360
};

// 0073D260.
class AggressionClass final : public ActionModeClass<AggressionClass, AggressionMode> {
public:
    void* DeleteScalar(unsigned flags) override {   // 0: 004BA8A0
        return Derived()->AggressionDeletingDestruct(static_cast<int>(flags));
    }
    int OnShutdown() override {   // 2: 004BA7C0
        Derived()->AggressionReset();
        return kNoResult;
    }
    CRefObject* Clone() override { return AsRefObject(Derived()->AggressionClone()); }          // 3: 004BA730
    int Evaluate(AIBrain* brain) override { return Derived()->AggressionMode::Evaluate(brain); }   // 4: 005D2390
    void Execute(AIBrain* brain) override { Derived()->AggressionMode::Execute(brain); }         // 5: 005D2580
    void Enter(AIBrain* brain) override { Derived()->AggressionMode::Enter(brain); }             // 6: 005D26D0
    void Leave(AIBrain* brain) override { Derived()->AggressionMode::Leave(brain); }             // 7: 005D2710
    int GetType() override { return Mode()->AggressionType(); }                                // 8: 004BA860
    void OnObjectDeleted(CEngineObject* object) override { Derived()->AggressionObjectDeleted(object); }   // 10: 004BA870
    void Load(CStream* stream) override { Derived()->AggressionMode::Load(stream); }             // 11: 005D2770
    void Save(CStream* stream) override { Derived()->AggressionMode::Save(stream); }             // 12: 005D27A0
};

// 0073D294 (slots 11 / 12 the abstract base's empty ones).
class CorpseWatcherClass final : public ActionModeClass<CorpseWatcherClass, CorpseWatcherMode> {
public:
    void* DeleteScalar(unsigned flags) override {   // 0: 004BA990
        return Derived()->CorpseWatcherDeletingDestruct(static_cast<int>(flags));
    }
    int OnShutdown() override {   // 2: 004BA950
        Derived()->CorpseWatcherReset();
        return kNoResult;
    }
    CRefObject* Clone() override { return AsRefObject(Derived()->CorpseWatcherClone()); }          // 3: 004BA930
    int Evaluate(AIBrain* brain) override { return Derived()->CorpseWatcherMode::Evaluate(brain); }   // 4: 005D27D0
    void Execute(AIBrain*) override {}                                 // 5: 004BA970 (RET 4)
    int GetType() override { return Mode()->CorpseWatcherType(); }    // 8: 004BA980
};

// 0073FF8C.
class FearClass final : public ActionModeClass<FearClass, FearMode> {
public:
    void* DeleteScalar(unsigned flags) override { return Derived()->DeletingDestruct(static_cast<int>(flags)); }   // 0: 005D2930
    int OnShutdown() override { return Derived()->Reset(); }                                 // 2: 005D28F0
    CRefObject* Clone() override { return AsRefObject(Derived()->FearMode::Clone()); }       // 3: 005D2880
    int Evaluate(AIBrain* brain) override { return Derived()->FearMode::Evaluate(brain); }   // 4: 005D2960
    void Execute(AIBrain* brain) override { Derived()->FearMode::Execute(brain); }           // 5: 005D30A0
    void Enter(AIBrain* brain) override { Derived()->FearMode::Enter(brain); }               // 6: 005D3650
    void Leave(AIBrain* brain) override { Derived()->FearMode::Leave(brain); }               // 7: 005D3680
    int GetType() override { return FearType(Mode()); }                                    // 8: 005D2920
    void Load(CStream* stream) override { Derived()->FearMode::Load(stream); }               // 11: 005D3690
    void Save(CStream* stream) override { Derived()->FearMode::Save(stream); }               // 12: 005D36F0
};

// 0073FF40.
class PatrolClass final : public ModeClass<PatrolClass, PatrolMode> {
public:
    void* DeleteScalar(unsigned flags) override { return Derived()->DeletingDestruct(static_cast<int>(flags)); }   // 0: 005D1220
    int OnShutdown() override {   // 2: 005D1140
        Derived()->Rebuild();
        return kNoResult;
    }
    CRefObject* Clone() override { return AsRefObject(Derived()->PatrolMode::Clone()); }       // 3: 005D0F90
    int Evaluate(AIBrain* brain) override { return Derived()->PatrolMode::Evaluate(brain); }   // 4: 005D16B0
    void Execute(AIBrain* brain) override { Derived()->PatrolMode::Execute(brain); }           // 5: 005D1700
    int GetType() override { return PatrolType(Mode()); }                                    // 8: 005D1210
    void Load(CStream* stream) override { Derived()->PatrolMode::Load(stream); }               // 11: 005D1930
    void Save(CStream* stream) override { Derived()->PatrolMode::Save(stream); }               // 12: 005D1A40
};

// The player's and the tamed mode's slots: 0073F0F8 (PlayerClass) and 0073F0C4 (TamedClass), which differs in
// slots 0 (00552160, the same body), 2, 3, 4 and 8.
template <typename Self>
class PlayerModeClass : public ActionModeClass<Self, PlayerMode> {
public:
    void* DeleteScalar(unsigned flags) override { return Player()->DeletingDestruct(static_cast<int>(flags)); }   // 0: 005520B0
    int OnShutdown() override {   // 2: 00552000 (EAX: the mode)
        return static_cast<int>(reinterpret_cast<std::uintptr_t>(Player()->ResetPlayer()));
    }
    CRefObject* Clone() override { return this->AsRefObject(Player()->ClonePlayer()); }      // 3: 00551F50
    int Evaluate(AIBrain* brain) override { return this->Mode()->PlayerPriority(brain); }     // 4: 00552090
    void Execute(AIBrain* brain) override { Player()->PlayerMode::Execute(brain); }          // 5: 005D4A90
    int GetType() override { return PlayerType(this->Mode()); }                               // 8: 005520A0
    void OnObjectDeleted(CEngineObject* object) override { Player()->PlayerMode::OnObjectDeleted(object); }   // 10: 005D37A0
    void Load(CStream* stream) override { Player()->PlayerMode::Load(stream); }               // 11: 005D4CD0
    void Save(CStream* stream) override { Player()->PlayerMode::Save(stream); }               // 12: 005D4DB0

protected:
    PlayerMode* Player() { return this->template ObjectAs<PlayerMode>(); }
};

class PlayerClass final : public PlayerModeClass<PlayerClass> {};   // 0073F0F8

class TamedClass final : public PlayerModeClass<TamedClass> {   // 0073F0C4
public:
    int OnShutdown() override {   // 2: 00551EC0 (EAX: the mode)
        return static_cast<int>(reinterpret_cast<std::uintptr_t>(Player()->ResetTamed()));
    }
    CRefObject* Clone() override { return AsRefObject(Player()->CloneTamed()); }   // 3: 00551E10
    int Evaluate(AIBrain* brain) override { return Mode()->TamedPriority(brain); }   // 4: 00552140
    int GetType() override { return TamedType(Mode()); }                            // 8: 00552150
};

} // namespace aimode
