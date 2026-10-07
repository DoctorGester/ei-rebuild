// Differential checks for the unit AI brain and modes (src/game/unit_ai*.cpp).
//
// Lookups and geometry run both versions and compare. Functions that only write the mode (or the
// brain) run the original on a copy and ours on the real object. Load runs the original on a copy
// from the real stream (recording the bytes) and ours from the recording; Save writes both into
// capture streams. Everything that releases references, calls other AI code or changes game state
// runs ours only, followed by a consistency check.
#include "verify.h"

#include "../../src/game/unit_ai.h"

#include <cstring>
#include <vector>

namespace {
void* s_unused;

std::uint32_t Bits(float f) {
    std::uint32_t u;
    std::memcpy(&u, &f, sizeof(u));
    return u;
}

// Streams for Load / Save (the version at +8 is copied from the real stream).
class CaptureStream : public AIStream {
public:
    explicit CaptureStream(const AIStream* real) {
        unknown_04 = 0;
        version = real ? real->version : 0;
    }
    void Slot0() override {}
    void Read(void*, unsigned) override {}
    void Write(const void* buffer, unsigned size) override {
        auto* p = static_cast<const unsigned char*>(buffer);
        bytes.insert(bytes.end(), p, p + size);
        chunks.push_back(size);
    }
    void Forward(AIStream* stream) const {
        std::size_t pos = 0;
        for (unsigned size : chunks) {
            stream->Write(bytes.data() + pos, size);
            pos += size;
        }
    }
    bool Same(const CaptureStream& other) const { return bytes == other.bytes && chunks == other.chunks; }
    std::vector<unsigned char> bytes;
    std::vector<unsigned> chunks;
};

class RecordStream : public AIStream {
public:
    explicit RecordStream(AIStream* real) : real_(real) {
        unknown_04 = 0;
        version = real->version;
    }
    void Slot0() override {}
    void Read(void* buffer, unsigned size) override {
        real_->Read(buffer, size);
        auto* p = static_cast<const unsigned char*>(buffer);
        bytes.insert(bytes.end(), p, p + size);
    }
    void Write(const void*, unsigned) override {}
    std::vector<unsigned char> bytes;

private:
    AIStream* real_;
};

class ReplayStream : public AIStream {
public:
    ReplayStream(const AIStream* real, const std::vector<unsigned char>& bytes) : bytes_(bytes) {
        unknown_04 = 0;
        version = real->version;
    }
    void Slot0() override {}
    void Read(void* buffer, unsigned size) override {
        for (unsigned i = 0; i < size; ++i)
            static_cast<unsigned char*>(buffer)[i] = pos_ < bytes_.size() ? bytes_[pos_++] : 0;
    }
    void Write(const void*, unsigned) override {}
    bool Consumed() const { return pos_ == bytes_.size(); }

private:
    const std::vector<unsigned char>& bytes_;
    std::size_t pos_ = 0;
};

// Load: the original reads the real stream into a copy of the mode (recording the bytes), ours
// replays them into the real mode; the modes must end up equal and all bytes consumed.
template <typename Mode, typename Original>
void SameLoad(const char* name, Mode* self, AIStream* stream, Original original) {
    Mode scratch;
    std::memcpy(&scratch, self, sizeof(Mode));
    RecordStream record(stream);
    original(&scratch, &record);
    ReplayStream replay(stream, record.bytes);
    self->Load(&replay);
    bool same = std::memcmp(&scratch, self, sizeof(Mode)) == 0 && replay.Consumed();
    ei::verify::Check(name, true, same, "%p version %d, %u bytes", self, stream->version,
                      static_cast<unsigned>(record.bytes.size()));
}

// Save: both into capture streams; the original's writes go on to the real stream.
template <typename Mode, typename Original>
void SameSave(const char* name, Mode* self, AIStream* stream, Original original) {
    CaptureStream expected(stream), actual(stream);
    original(self, &expected);
    self->Save(&actual);
    ei::verify::Check(name, true, expected.Same(actual), "%p: %u vs %u bytes", self,
                      static_cast<unsigned>(expected.bytes.size()), static_cast<unsigned>(actual.bytes.size()));
    expected.Forward(stream);
}

// Functions that only write `size` bytes of `self`: ours into the real object, the original into a
// copy taken before; bytes and results compared.
template <typename R, typename Ours, typename Original>
R SameWrites(const char* name, void* self, std::size_t size, Ours ours, Original original) {
    std::vector<unsigned char> copy(static_cast<unsigned char*>(self), static_cast<unsigned char*>(self) + size);
    R expected = original(copy.data());
    R actual = ours();
    int firstDiff = -1;
    for (std::size_t i = 0; i < size; ++i) {
        if (copy[i] != static_cast<unsigned char*>(self)[i]) {
            firstDiff = static_cast<int>(i);
            break;
        }
    }
    ei::verify::Check(name, true, firstDiff < 0 && expected == actual, "%p: first difference at +0x%X, result %X vs %X", self,
                      firstDiff, static_cast<unsigned>(expected), static_cast<unsigned>(actual));
    return expected;
}

int CountModes(const AIBrain* brain) {
    int n = 0;
    for (auto* node = brain->modes.head; node; node = node->next) ++n;
    return n;
}
} // namespace

// ---- Brain ---------------------------------------------------------------------------------------

// 005D53B0 Think: ours only (it runs every mode and issues actions). Afterwards the corpse list is
// empty, nothing is remembered as seen, the mode in charge (if any) is in the mode list and holds a
// reference, and without a mode there is no action.
static void __fastcall ThinkVerify(AIBrain* self) {
    self->Think();
    // Think returns early (before choosing) for an inactive controller; then nothing is checked.
    bool ok = true;
    if (!(self->owner && self->owner->state == 5)) {
        bool inList = self->current == nullptr || self->modes.Find(self->current) != nullptr;
        ok = self->corpses.head == nullptr && self->corpses.count == 0 && self->seen == nullptr && inList &&
             (self->current == nullptr || self->current->refs > 0);
    }
    ei::verify::Check("AIBrain::Think", true, ok, "brain %p: corpses %d, seen %p, current %p (%d modes), action %d", self,
                      self->corpses.count, self->seen, self->current, CountModes(self), self->action);
}
EI_VERIFY(0x005D53B0, ThinkVerify, 0, s_unused);

// 005D5DD0: PUSH EBX; PUSH ESI; MOV ESI,[ECX+0x74] (5 bytes). Pure (GetType is a constant per mode).
static AIMode*(__fastcall* s_findMode)(AIBrain*, void*, int);
static AIMode* __fastcall FindModeVerify(AIBrain* self, void*, int type) {
    AIMode* expected = s_findMode(self, nullptr, type);
    ei::verify::Check("AIBrain::FindMode", expected, self->FindMode(type), "brain %p type %d", self, type);
    return expected;
}
EI_VERIFY(0x005D5DD0, FindModeVerify, 5, s_findMode);

// 005D5E30 / 005D5E60: PUSH ESI; MOV ESI,[ECX+0x74]; TEST ESI,ESI (6 bytes).
static AIMode*(__fastcall* s_findPatrol)(AIBrain*);
static AIMode* __fastcall FindPatrolVerify(AIBrain* self) {
    AIMode* expected = s_findPatrol(self);
    ei::verify::Check("AIBrain::FindPatrolMode", expected, self->FindPatrolMode(), "brain %p", self);
    return expected;
}
EI_VERIFY(0x005D5E30, FindPatrolVerify, 6, s_findPatrol);

static AIMode*(__fastcall* s_findPlayer)(AIBrain*);
static AIMode* __fastcall FindPlayerVerify(AIBrain* self) {
    AIMode* expected = s_findPlayer(self);
    ei::verify::Check("AIBrain::FindPlayerMode", expected, self->FindPlayerMode(), "brain %p", self);
    return expected;
}
EI_VERIFY(0x005D5E60, FindPlayerVerify, 6, s_findPlayer);

// 005D5E90: PUSH EBX; MOV EBX,ECX; PUSH EBP; PUSH ESI (5 bytes).
static CEngineObject*(__fastcall* s_leaderOrFollowed)(AIBrain*);
static CEngineObject* __fastcall LeaderOrFollowedVerify(AIBrain* self) {
    CEngineObject* expected = s_leaderOrFollowed(self);
    ei::verify::Check("AIBrain::LeaderOrFollowed", expected, self->LeaderOrFollowed(), "brain %p action %d", self,
                      self->action);
    return expected;
}
EI_VERIFY(0x005D5E90, LeaderOrFollowedVerify, 5, s_leaderOrFollowed);

// 005D5EE0: PUSH EBX; PUSH ESI; MOV ESI,[ECX+0x74] (5 bytes).
static CEngineObject*(__fastcall* s_playerLeader)(AIBrain*);
static CEngineObject* __fastcall PlayerLeaderVerify(AIBrain* self) {
    CEngineObject* expected = s_playerLeader(self);
    ei::verify::Check("AIBrain::PlayerModeLeader", expected, self->PlayerModeLeader(), "brain %p", self);
    return expected;
}
EI_VERIFY(0x005D5EE0, PlayerLeaderVerify, 5, s_playerLeader);

// 005D5F20: PUSH EBX; MOV EBX,ECX; PUSH ESI; PUSH EDI (5 bytes).
static CEngineObject*(__fastcall* s_aggressionEnemy)(AIBrain*);
static CEngineObject* __fastcall AggressionEnemyVerify(AIBrain* self) {
    CEngineObject* expected = s_aggressionEnemy(self);
    ei::verify::Check("AIBrain::AggressionEnemy", expected, self->AggressionEnemy(), "brain %p", self);
    return expected;
}
EI_VERIFY(0x005D5F20, AggressionEnemyVerify, 5, s_aggressionEnemy);

// 005D50E0: PUSH EBP; MOV EBP,ESP; SUB ESP,0x18 (6 bytes). Pure; the callers test AL only.
static std::uint32_t(__fastcall* s_isNearPath)(AIBrain*, void*, const Vector3*, float);
static std::uint32_t __fastcall IsNearPathVerify(AIBrain* self, void*, const Vector3* point, float radius) {
    std::uint32_t expected = s_isNearPath(self, nullptr, point, radius);
    bool actual = self->IsNearPath(point, radius);
    ei::verify::Check("AIBrain::IsNearPath", static_cast<bool>(expected & 0xFF), actual,
                      "point %08X,%08X radius %08X; position %08X,%08X previous %08X,%08X", Bits(point->x), Bits(point->y),
                      Bits(radius), Bits(self->position.x), Bits(self->position.y), Bits(self->previousPosition.x),
                      Bits(self->previousPosition.y));
    return expected;
}
EI_VERIFY(0x005D50E0, IsNearPathVerify, 6, s_isNearPath);

// 005D5080: MOV EAX,[ESP+4]; MOV EDX,[ECX] (6 bytes). Writes only the brain.
static std::uint32_t(__fastcall* s_setOwner)(void*, void*, UnitController*);
static std::uint32_t __fastcall SetOwnerVerify(AIBrain* self, void*, UnitController* owner) {
    return SameWrites<std::uint32_t>("AIBrain::SetOwner", self, sizeof(AIBrain), [&] { return self->SetOwner(owner); },
                                     [&](void* copy) { return s_setOwner(copy, nullptr, owner); });
}
EI_VERIFY(0x005D5080, SetOwnerVerify, 6, s_setOwner);

// 005D58D0 OnObjectDeleted: ours only; the object is neither target nor seen afterwards.
static void __fastcall BrainObjectDeletedVerify(AIBrain* self, void*, CEngineObject* object) {
    self->OnObjectDeleted(object);
    bool ok = object == nullptr || (self->target != object && self->seen != object);
    ei::verify::Check("AIBrain::OnObjectDeleted", true, ok, "brain %p object %p", self, object);
}
EI_VERIFY(0x005D58D0, BrainObjectDeletedVerify, 0, s_unused);

// 005D7520 UpdateMove: ours only. Afterwards the action is 0 or 2, the look count is not negative,
// and a finished move (action 0) holds no target.
static void __fastcall UpdateMoveVerify(AIBrain* self) {
    int looksBefore = self->looks.size;
    self->UpdateMove();
    bool ok = (self->action == AIBrain::kActionNone || self->action == AIBrain::kActionMove) && self->looks.size >= 0 &&
              self->looks.size >= looksBefore - 1 && (self->action != AIBrain::kActionNone || self->target == nullptr);
    ei::verify::Check("AIBrain::UpdateMove", true, ok, "brain %p action %d looks %d->%d time %d", self, self->action,
                      looksBefore, self->looks.size, self->actionTime);
}
EI_VERIFY(0x005D7520, UpdateMoveVerify, 0, s_unused);

// 005D76A0 UpdateAttack: ours only. The action stays 3 or becomes 0, the target is kept.
static void __fastcall UpdateAttackVerify(AIBrain* self) {
    CEngineObject* target = self->target;
    self->UpdateAttack();
    bool ok = (self->action == AIBrain::kActionAttack || self->action == AIBrain::kActionNone) && self->target == target;
    ei::verify::Check("AIBrain::UpdateAttack", true, ok, "brain %p action %d target %p", self, self->action, target);
}
EI_VERIFY(0x005D76A0, UpdateAttackVerify, 0, s_unused);

static void __fastcall StopActionVerify(AIBrain* self) {
    self->StopAction();
    ei::verify::Check("AIBrain::StopAction", true, self->action == 0 && self->target == nullptr, "brain %p", self);
}
EI_VERIFY(0x005D7790, StopActionVerify, 0, s_unused);

// ---- Shared mode slots ---------------------------------------------------------------------------

// 005D0A80: MOV EAX,0x32 (5 bytes).
static int(__fastcall* s_priority50)(AIMode*, void*, AIBrain*);
static int __fastcall Priority50Verify(AIMode* self, void*, AIBrain* brain) {
    int expected = s_priority50(self, nullptr, brain);
    ei::verify::Check("aimode::Priority50", expected, aimode::Priority50(self, nullptr, brain), "mode %p", self);
    return expected;
}
EI_VERIFY(0x005D0A80, Priority50Verify, 5, s_priority50);

// 005D0AF0 EnterIdle: ours only (releases the target, draws a random number). The decay must be
// 0.8 + 0.4 * r for some 32-bit r, i.e. within [0.8, 1.2].
static void __fastcall EnterIdleVerify(AIMode* self, void*, AIBrain* brain) {
    aimode::EnterIdle(self, nullptr, brain);
    bool ok = brain->target == nullptr && brain->action == 0 && brain->owner->busy == 0 && brain->decay >= 0.79f &&
              brain->decay <= 1.21f;
    ei::verify::Check("aimode::EnterIdle", true, ok, "brain %p decay %08X", brain, Bits(brain->decay));
}
EI_VERIFY(0x005D0AF0, EnterIdleVerify, 0, s_unused);

static void __fastcall ClearActionVerify(AIMode* self, void*, AIBrain* brain) {
    aimode::ClearAction(self, nullptr, brain);
    ei::verify::Check("aimode::ClearAction", true, brain->target == nullptr && brain->action == 0, "brain %p", brain);
}
EI_VERIFY(0x005D0A50, ClearActionVerify, 0, s_unused);

// ---- Guard ---------------------------------------------------------------------------------------

// 005D0B70: MOV EDX,[ESP+4]; PUSH ESI (5 bytes). Writes only the mode.
static int(__fastcall* s_guardSet)(void*, void*, const Vector3*, float, float);
static int __fastcall GuardSetVerify(GuardMode* self, void*, const Vector3* point, float radius, float wander) {
    return SameWrites<int>("GuardMode::Set", self, sizeof(GuardMode),
                           [&] {
                               self->Set(point, radius, wander);
                               return 0;
                           },
                           [&](void* copy) {
                               s_guardSet(copy, nullptr, point, radius, wander);
                               return 0;
                           });
}
EI_VERIFY(0x005D0B70, GuardSetVerify, 5, s_guardSet);

// 005D0EB0: PUSH ESI; MOV ESI,[ESP+8] (5 bytes).
static void(__fastcall* s_guardLoad)(GuardMode*, void*, AIStream*);
static void __fastcall GuardLoadVerify(GuardMode* self, void*, AIStream* stream) {
    SameLoad("GuardMode::Load", self, stream, [](GuardMode* m, AIStream* s) { s_guardLoad(m, nullptr, s); });
}
EI_VERIFY(0x005D0EB0, GuardLoadVerify, 5, s_guardLoad);

// 005D0F00: PUSH ECX; PUSH ESI; MOV ESI,[ESP+0xC] (6 bytes).
static void(__fastcall* s_guardSave)(GuardMode*, void*, AIStream*);
static void __fastcall GuardSaveVerify(GuardMode* self, void*, AIStream* stream) {
    SameSave("GuardMode::Save", self, stream, [](GuardMode* m, AIStream* s) { s_guardSave(m, nullptr, s); });
}
EI_VERIFY(0x005D0F00, GuardSaveVerify, 6, s_guardSave);

// ---- Patrol --------------------------------------------------------------------------------------

// 005D16B0: PUSH EDI; MOV EDI,ECX; MOV EAX,[EDI+0x10] (6 bytes). With points it is pure; without,
// ours only (it adds a point): -1000 and one point afterwards.
static int(__fastcall* s_patrolEvaluate)(PatrolMode*, void*, AIBrain*);
static int __fastcall PatrolEvaluateVerify(PatrolMode* self, void*, AIBrain* brain) {
    if (self->points.size == 0) {
        int result = self->Evaluate(brain);
        ei::verify::Check("PatrolMode::Evaluate", true, result == -1000 && self->points.size == 1, "mode %p points %d", self,
                          self->points.size);
        return result;
    }
    int expected = s_patrolEvaluate(self, nullptr, brain);
    ei::verify::Check("PatrolMode::Evaluate", expected, self->Evaluate(brain), "mode %p points %d", self, self->points.size);
    return expected;
}
EI_VERIFY(0x005D16B0, PatrolEvaluateVerify, 6, s_patrolEvaluate);

// 005D1690: MOV EAX,[ECX+0x10]; TEST EAX,EAX (5 bytes).
static PatrolPoint*(__fastcall* s_lastPoint)(PatrolMode*);
static PatrolPoint* __fastcall LastPointVerify(PatrolMode* self) {
    PatrolPoint* expected = s_lastPoint(self);
    ei::verify::Check("PatrolMode::LastPoint", expected, self->LastPoint(), "mode %p points %d", self, self->points.size);
    return expected;
}
EI_VERIFY(0x005D1690, LastPointVerify, 5, s_lastPoint);

// ---- Sentry --------------------------------------------------------------------------------------

// 005D1B50: MOV EAX,[ESP+4]; ADD ECX,8 (7 bytes).
static int(__fastcall* s_sentrySet)(void*, void*, const Vector3*);
static int __fastcall SentrySetVerify(SentryMode* self, void*, const Vector3* point) {
    return SameWrites<int>("SentryMode::Set", self, sizeof(SentryMode),
                           [&] {
                               self->Set(point);
                               return 0;
                           },
                           [&](void* copy) {
                               s_sentrySet(copy, nullptr, point);
                               return 0;
                           });
}
EI_VERIFY(0x005D1B50, SentrySetVerify, 7, s_sentrySet);

// 005D1DA0 / 005D1DC0: MOV EAX,ECX; PUSH 0xC; MOV ECX,[ESP+8] (8 bytes).
static void(__fastcall* s_sentryLoad)(SentryMode*, void*, AIStream*);
static void __fastcall SentryLoadVerify(SentryMode* self, void*, AIStream* stream) {
    SameLoad("SentryMode::Load", self, stream, [](SentryMode* m, AIStream* s) { s_sentryLoad(m, nullptr, s); });
}
EI_VERIFY(0x005D1DA0, SentryLoadVerify, 8, s_sentryLoad);

static void(__fastcall* s_sentrySave)(SentryMode*, void*, AIStream*);
static void __fastcall SentrySaveVerify(SentryMode* self, void*, AIStream* stream) {
    SameSave("SentryMode::Save", self, stream, [](SentryMode* m, AIStream* s) { s_sentrySave(m, nullptr, s); });
}
EI_VERIFY(0x005D1DC0, SentrySaveVerify, 8, s_sentrySave);

// ---- Follow --------------------------------------------------------------------------------------

// 005D1E10 Execute: ours only. An idle brain with a leader ends up following it.
static void __fastcall FollowExecuteVerify(FollowMode* self, void*, AIBrain* brain) {
    bool idle = brain->action == 0;
    self->Execute(brain);
    bool ok = !idle || !self->leader || (brain->action == AIBrain::kActionFollow && brain->target == self->leader);
    ei::verify::Check("FollowMode::Execute", true, ok, "mode %p leader %p action %d target %p", self, self->leader,
                      brain->action, brain->target);
}
EI_VERIFY(0x005D1E10, FollowExecuteVerify, 0, s_unused);

static void __fastcall FollowObjectDeletedVerify(FollowMode* self, void*, CEngineObject* object) {
    self->OnObjectDeleted(object);
    ei::verify::Check("FollowMode::OnObjectDeleted", true, !object || self->leader != object, "mode %p", self);
}
EI_VERIFY(0x005D1DE0, FollowObjectDeletedVerify, 0, s_unused);

// 005D1E80: MOV EAX,[ESP+4]; ADD ECX,8 (7 bytes).
static void(__fastcall* s_followSave)(FollowMode*, void*, AIStream*);
static void __fastcall FollowSaveVerify(FollowMode* self, void*, AIStream* stream) {
    SameSave("FollowMode::Save", self, stream, [](FollowMode* m, AIStream* s) { s_followSave(m, nullptr, s); });
}
EI_VERIFY(0x005D1E80, FollowSaveVerify, 7, s_followSave);

// ---- Suspection ----------------------------------------------------------------------------------

// 005D1E90: SUB ESP,0xC; PUSH ESI; MOV ESI,[ESP+0x14] (8 bytes). Without a seen object it only writes the mode
// (original on a copy); with one it may add to the perception memory, so ours only, and the result
// is the stored level.
static int(__fastcall* s_suspectionEvaluate)(void*, void*, AIBrain*);
static int __fastcall SuspectionEvaluateVerify(SuspectionMode* self, void*, AIBrain* brain) {
    if (!brain->seen) {
        return SameWrites<int>("SuspectionMode::Evaluate", self, sizeof(SuspectionMode), [&] { return self->Evaluate(brain); },
                               [&](void* copy) { return s_suspectionEvaluate(copy, nullptr, brain); });
    }
    int result = self->Evaluate(brain);
    ei::verify::Check("SuspectionMode::Evaluate", true, result == self->level, "mode %p level %d", self, self->level);
    return result;
}
EI_VERIFY(0x005D1E90, SuspectionEvaluateVerify, 8, s_suspectionEvaluate);

static void __fastcall SuspectionLeaveVerify(SuspectionMode* self, void*, AIBrain* brain) {
    self->Leave(brain);
    ei::verify::Check("SuspectionMode::Leave", true, brain->owner->busy == 0, "mode %p", self);
}
EI_VERIFY(0x005D2310, SuspectionLeaveVerify, 0, s_unused);

// 005D2330 / 005D2360: PUSH ESI; MOV ESI,[ESP+8] (5 bytes).
static void(__fastcall* s_suspectionLoad)(SuspectionMode*, void*, AIStream*);
static void __fastcall SuspectionLoadVerify(SuspectionMode* self, void*, AIStream* stream) {
    SameLoad("SuspectionMode::Load", self, stream, [](SuspectionMode* m, AIStream* s) { s_suspectionLoad(m, nullptr, s); });
}
EI_VERIFY(0x005D2330, SuspectionLoadVerify, 5, s_suspectionLoad);

static void(__fastcall* s_suspectionSave)(SuspectionMode*, void*, AIStream*);
static void __fastcall SuspectionSaveVerify(SuspectionMode* self, void*, AIStream* stream) {
    SameSave("SuspectionMode::Save", self, stream, [](SuspectionMode* m, AIStream* s) { s_suspectionSave(m, nullptr, s); });
}
EI_VERIFY(0x005D2360, SuspectionSaveVerify, 5, s_suspectionSave);

// ---- Aggression ----------------------------------------------------------------------------------

static void __fastcall AggressionEnterVerify(AggressionMode* self, void*, AIBrain* brain) {
    self->Enter(brain);
    bool ok = brain->owner->busy == 1 && brain->target == nullptr && brain->action == 0;
    ei::verify::Check("AggressionMode::Enter", true, ok, "mode %p", self);
}
EI_VERIFY(0x005D26D0, AggressionEnterVerify, 0, s_unused);

static void __fastcall AggressionLeaveVerify(AggressionMode* self, void*, AIBrain* brain) {
    self->Leave(brain);
    ei::verify::Check("AggressionMode::Leave", true, self->enemy == nullptr && brain->owner->busy == 0, "mode %p", self);
}
EI_VERIFY(0x005D2710, AggressionLeaveVerify, 0, s_unused);

// 005D27A0: PUSH ESI; PUSH EDI; MOV EDI,[ESP+0xC] (6 bytes).
static void(__fastcall* s_aggressionSave)(AggressionMode*, void*, AIStream*);
static void __fastcall AggressionSaveVerify(AggressionMode* self, void*, AIStream* stream) {
    SameSave("AggressionMode::Save", self, stream, [](AggressionMode* m, AIStream* s) { s_aggressionSave(m, nullptr, s); });
}
EI_VERIFY(0x005D27A0, AggressionSaveVerify, 6, s_aggressionSave);

// ---- CorpseWatcher -------------------------------------------------------------------------------

// 005D27D0: ours only (takes a corpse off the list). -1000, one corpse fewer.
static int __fastcall CorpseEvaluateVerify(CorpseWatcherMode* self, void*, AIBrain* brain) {
    int before = brain->corpses.count;
    int result = self->Evaluate(brain);
    bool ok = result == -1000 && brain->corpses.count == (before > 0 ? before - 1 : 0);
    ei::verify::Check("CorpseWatcherMode::Evaluate", true, ok, "mode %p corpses %d -> %d", self, before, brain->corpses.count);
    return result;
}
EI_VERIFY(0x005D27D0, CorpseEvaluateVerify, 0, s_unused);

// ---- Fear ----------------------------------------------------------------------------------------

static void __fastcall FearEnterVerify(FearMode* self, void*, AIBrain* brain) {
    self->Enter(brain);
    bool ok = brain->owner->busy == 1 && brain->target == nullptr && brain->action == 0;
    ei::verify::Check("FearMode::Enter", true, ok, "mode %p", self);
}
EI_VERIFY(0x005D3650, FearEnterVerify, 0, s_unused);

static void __fastcall FearLeaveVerify(FearMode* self, void*, AIBrain* brain) {
    self->Leave(brain);
    ei::verify::Check("FearMode::Leave", true, brain->owner->busy == 0, "mode %p", self);
}
EI_VERIFY(0x005D3680, FearLeaveVerify, 0, s_unused);

// 005D3690: PUSH ESI; MOV ESI,[ESP+8] (5 bytes). 005D36F0: PUSH ECX; PUSH ESI; MOV ESI,[ESP+0xC] (6).
static void(__fastcall* s_fearLoad)(FearMode*, void*, AIStream*);
static void __fastcall FearLoadVerify(FearMode* self, void*, AIStream* stream) {
    SameLoad("FearMode::Load", self, stream, [](FearMode* m, AIStream* s) { s_fearLoad(m, nullptr, s); });
}
EI_VERIFY(0x005D3690, FearLoadVerify, 5, s_fearLoad);

static void(__fastcall* s_fearSave)(FearMode*, void*, AIStream*);
static void __fastcall FearSaveVerify(FearMode* self, void*, AIStream* stream) {
    SameSave("FearMode::Save", self, stream, [](FearMode* m, AIStream* s) { s_fearSave(m, nullptr, s); });
}
EI_VERIFY(0x005D36F0, FearSaveVerify, 6, s_fearSave);

// ---- Player --------------------------------------------------------------------------------------

static void __fastcall PlayerObjectDeletedVerify(PlayerMode* self, void*, CEngineObject* object) {
    self->OnObjectDeleted(object);
    bool ok = !object || (self->target != object && self->leader != object);
    ei::verify::Check("PlayerMode::OnObjectDeleted", true, ok, "mode %p", self);
}
EI_VERIFY(0x005D37A0, PlayerObjectDeletedVerify, 0, s_unused);

// 005D3DE0 SetLeader: changes references, so ours only: the leader is the one asked (none for the
// unit itself) and a walk in progress was dropped. Reached by party units' leader changes
// (CreatePartyUnits / zone entry: every new game; ui_battle_commands).
static void __fastcall PlayerSetLeaderVerify(PlayerMode* self, void*, AIBrain* brain, CEngineObject* leader) {
    self->SetLeader(brain, leader);
    bool isSelf = leader == brain->owner->unit;
    bool ok = isSelf ? self->leader == nullptr : (self->leader == leader && brain->action != AIBrain::kActionWalk);
    ei::verify::Check("PlayerMode::SetLeader", true, ok, "mode %p leader %p (asked %p) action %d", self, self->leader, leader,
                      brain->action);
}
EI_VERIFY(0x005D3DE0, PlayerSetLeaderVerify, 0, s_unused);

// 005D4DB0: PUSH ESI; MOV ESI,[ESP+8] (5 bytes).
static void(__fastcall* s_playerSave)(PlayerMode*, void*, AIStream*);
static void __fastcall PlayerSaveVerify(PlayerMode* self, void*, AIStream* stream) {
    SameSave("PlayerMode::Save", self, stream, [](PlayerMode* m, AIStream* s) { s_playerSave(m, nullptr, s); });
}
EI_VERIFY(0x005D4DB0, PlayerSaveVerify, 5, s_playerSave);

// ---- Mode executes (ours only) -------------------------------------------------------------------

namespace {
// After an idle mode's Execute: a busy brain is left alone; an idle one either stays idle or has
// started a move (action 2, no target, no look in progress).
bool MoveStartedOrIdle(const AIBrain* brain, int actionBefore) {
    if (actionBefore != AIBrain::kActionNone) return brain->action == actionBefore;
    if (brain->action == AIBrain::kActionNone) return true;
    return brain->action == AIBrain::kActionMove && brain->target == nullptr && brain->actionTime == AIBrain::kNoTime;
}
} // namespace

static void __fastcall GuardExecuteVerify(GuardMode* self, void*, AIBrain* brain) {
    int before = brain->action;
    self->Execute(brain);
    bool ok = MoveStartedOrIdle(brain, before) && self->failures >= 0;
    ei::verify::Check("GuardMode::Execute", true, ok, "mode %p action %d -> %d failures %d", self, before, brain->action,
                      self->failures);
}
EI_VERIFY(0x005D0BE0, GuardExecuteVerify, 0, s_unused);

static void __fastcall PatrolExecuteVerify(PatrolMode* self, void*, AIBrain* brain) {
    int before = brain->action;
    self->Execute(brain);
    bool ok = MoveStartedOrIdle(brain, before) && (self->points.size == 0 || (self->current >= 0 && self->current < self->points.size));
    if (ok && before == AIBrain::kActionNone && self->points.size != 0) {
        // The move goes to the current point.
        ok = brain->action == AIBrain::kActionMove &&
             std::memcmp(&brain->actionPoint, &self->points[self->current].position, sizeof(Vector3)) == 0;
    }
    ei::verify::Check("PatrolMode::Execute", true, ok, "mode %p action %d -> %d point %d of %d", self, before, brain->action,
                      self->current, self->points.size);
}
EI_VERIFY(0x005D1700, PatrolExecuteVerify, 0, s_unused);

static void __fastcall SentryExecuteVerify(SentryMode* self, void*, AIBrain* brain) {
    int before = brain->action;
    self->Execute(brain);
    bool ok = MoveStartedOrIdle(brain, before);
    if (ok && before == AIBrain::kActionNone) ok = brain->action == AIBrain::kActionMove;
    ei::verify::Check("SentryMode::Execute", true, ok, "mode %p action %d -> %d", self, before, brain->action);
}
EI_VERIFY(0x005D1B70, SentryExecuteVerify, 0, s_unused);

static void __fastcall SuspectionExecuteVerify(SuspectionMode* self, void*, AIBrain* brain) {
    int before = brain->action;
    self->Execute(brain);
    // Either unchanged, or a (new) move without a target.
    bool ok = brain->action == before || (brain->action == AIBrain::kActionMove && brain->target == nullptr);
    ei::verify::Check("SuspectionMode::Execute", true, ok, "mode %p action %d -> %d level %d", self, before, brain->action,
                      self->level);
}
EI_VERIFY(0x005D1F80, SuspectionExecuteVerify, 0, s_unused);

static void __fastcall SuspectionEnterVerify(SuspectionMode* self, void*, AIBrain* brain) {
    self->Enter(brain);
    bool ok = brain->owner->busy == 1 && brain->action == AIBrain::kActionMove && brain->target == nullptr &&
              brain->actionTime == AIBrain::kNoTime;
    ei::verify::Check("SuspectionMode::Enter", true, ok, "mode %p action %d", self, brain->action);
}
EI_VERIFY(0x005D21A0, SuspectionEnterVerify, 0, s_unused);

static int __fastcall AggressionEvaluateVerify(AggressionMode* self, void*, AIBrain* brain) {
    int result = self->Evaluate(brain);
    bool ok = (result == 10000 && self->chosen != nullptr && self->enemy == self->chosen) ||
              (result == -1000 && self->chosen == nullptr);
    ei::verify::Check("AggressionMode::Evaluate", true, ok, "mode %p result %d chosen %p enemy %p fixed %d", self, result,
                      self->chosen, self->enemy, self->fixed);
    return result;
}
EI_VERIFY(0x005D2390, AggressionEvaluateVerify, 0, s_unused);

static void __fastcall AggressionExecuteVerify(AggressionMode* self, void*, AIBrain* brain) {
    int before = brain->action;
    self->Execute(brain);
    bool ok = brain->action == before || brain->action == AIBrain::kActionAttack || brain->action == AIBrain::kActionCast;
    ei::verify::Check("AggressionMode::Execute", true, ok, "mode %p action %d -> %d part %d", self, before, brain->action,
                      self->enemyPart);
}
EI_VERIFY(0x005D2580, AggressionExecuteVerify, 0, s_unused);

// 005D4A90 Player::Execute: ours only. The state stays in 0..2, a following brain has a target,
// and walking to a point (state 2, z = 1) means action 1 or a fight started on the way.
static void __fastcall PlayerExecuteVerify(PlayerMode* self, void*, AIBrain* brain) {
    int stateBefore = self->state;
    self->Execute(brain);
    bool ok = self->state >= 0 && self->state <= 2 && (brain->action != AIBrain::kActionFollow || brain->target != nullptr);
    if (stateBefore != self->state) ok = ok && self->state == PlayerMode::kStateFollow;
    ei::verify::Check("PlayerMode::Execute", true, ok, "mode %p state %d -> %d action %d target %p", self, stateBefore,
                      self->state, brain->action, brain->target);
}
EI_VERIFY(0x005D4A90, PlayerExecuteVerify, 0, s_unused);

// ---- Commands and attacks (ours only) ------------------------------------------------------------

static void __fastcall CommandWalkVerify(PlayerMode* self, void*, AIBrain* brain, const Vector3* to, int unknown, int run,
                                         const Vector3* offset) {
    self->CommandWalk(brain, to, unknown, run, offset);
    bool ok = self->state == PlayerMode::kStateCommand && brain->action == AIBrain::kActionWalk && self->target == nullptr &&
              self->run == static_cast<std::uint8_t>(run);
    ei::verify::Check("PlayerMode::CommandWalk", true, ok, "mode %p state %d action %d", self, self->state, brain->action);
}
EI_VERIFY(0x005D39E0, CommandWalkVerify, 0, s_unused);

static void __fastcall CommandGoToVerify(PlayerMode* self, void*, AIBrain* brain, const Vector3* to, int unknown, int run,
                                         const Vector3* offset) {
    self->CommandGoTo(brain, to, unknown, run, offset);
    std::uint32_t z;
    std::memcpy(&z, &self->point.z, 4);
    bool ok = self->state == PlayerMode::kStateMoveTo && self->target == nullptr &&
              ((z == 0x3F800000u && brain->action == AIBrain::kActionWalk) || z == 0);
    ei::verify::Check("PlayerMode::CommandGoTo", true, ok, "mode %p action %d z %08X", self, brain->action, z);
}
EI_VERIFY(0x005D3AE0, CommandGoToVerify, 0, s_unused);

static void __fastcall CommandAttackVerify(PlayerMode* self, void*, AIBrain* brain, CEngineObject* object, int how, int unknown) {
    bool self_ = object == brain->owner->unit;
    self->CommandAttack(brain, object, how, unknown);
    bool ok = self_ || (self->state == PlayerMode::kStateFollow && self->target == object &&
                        (brain->action != AIBrain::kActionAttack || brain->target == object));
    ei::verify::Check("PlayerMode::CommandAttack", true, ok, "mode %p object %p action %d", self, object, brain->action);
}
EI_VERIFY(0x005D3C30, CommandAttackVerify, 0, s_unused);

static void __fastcall CommandUseVerify(PlayerMode* self, void*, AIBrain* brain, CEngineObject* object, int kind, int a, int b) {
    self->CommandUse(brain, object, kind, a, b);
    bool ok = self->state == PlayerMode::kStateFollow && (!object || (brain->action == AIBrain::kActionUse && !brain->target));
    ei::verify::Check("PlayerMode::CommandUse", true, ok, "mode %p kind %d action %d", self, kind, brain->action);
}
EI_VERIFY(0x005D3CF0, CommandUseVerify, 0, s_unused);

static void __fastcall CommandCastVerify(PlayerMode* self, void*, AIBrain* brain, int spell, CEngineObject* object, int unused) {
    self->CommandCast(brain, spell, object, unused);
    bool ok = self->state == PlayerMode::kStateFollow && brain->target == object && brain->actionTime == spell;
    ei::verify::Check("PlayerMode::CommandCast", true, ok, "mode %p spell %X", self, spell);
}
EI_VERIFY(0x005D3D20, CommandCastVerify, 0, s_unused);

static void __fastcall CommandCastAtVerify(PlayerMode* self, void*, AIBrain* brain, int spell, const Vector3* at, int unused) {
    self->CommandCastAt(brain, spell, at, unused);
    bool ok = self->state == PlayerMode::kStateFollow && brain->actionTime == spell;
    ei::verify::Check("PlayerMode::CommandCastAt", true, ok, "mode %p spell %X", self, spell);
}
EI_VERIFY(0x005D3D90, CommandCastAtVerify, 0, s_unused);

static void __fastcall AttackTargetVerify(AIBrain* self, void*, CEngineObject* object) {
    self->AttackTarget(object);
    ei::verify::Check("AIBrain::AttackTarget", true, self->action == AIBrain::kActionAttack && self->target == object,
                      "brain %p object %p", self, object);
}
EI_VERIFY(0x005D7B50, AttackTargetVerify, 0, s_unused);

static void __fastcall AttackWithVerify(AIBrain* self, void*, CEngineObject* object, int how, int unknown) {
    int before = self->action;
    self->AttackWith(object, how, unknown);
    bool ok = (self->action == AIBrain::kActionAttack && self->target == object) || self->action == before;
    ei::verify::Check("AIBrain::AttackWith", true, ok, "brain %p object %p action %d", self, object, self->action);
}
EI_VERIFY(0x005D7C60, AttackWithVerify, 0, s_unused);

static void __fastcall UseOnVerify(AIBrain* self, void*, CEngineObject* object, int kind, int a, int b) {
    int before = self->action;
    self->UseOn(object, kind, a, b);
    bool ok = object ? (self->action == AIBrain::kActionUse && self->target == nullptr) : self->action == before;
    ei::verify::Check("AIBrain::UseOn", true, ok, "brain %p kind %d action %d", self, kind, self->action);
}
EI_VERIFY(0x005D7CE0, UseOnVerify, 0, s_unused);

static void __fastcall WalkVerify(AIBrain* self, void*, const Vector3* point, int run, int unknown) {
    self->Walk(point, run, unknown);
    ei::verify::Check("AIBrain::Walk", true, self->action == AIBrain::kActionWalk, "brain %p", self);
}
EI_VERIFY(0x005D7AE0, WalkVerify, 0, s_unused);

static void __fastcall TurnTowardsVerify(AIBrain* self, void*, const Vector3* direction, float time) {
    self->TurnTowards(direction, time);
    ei::verify::Check("AIBrain::TurnTowards", true, self->action == AIBrain::kActionWalk, "brain %p", self);
}
EI_VERIFY(0x005D7B30, TurnTowardsVerify, 0, s_unused);

static void __fastcall CastAtVerify(AIBrain* self, void*, int spell, const Vector3* point, int run) {
    int before = self->action;
    self->CastAt(spell, point, run);
    bool ok = self->actionTime == spell && (self->action == before || self->action == AIBrain::kActionCast);
    ei::verify::Check("AIBrain::CastAt", true, ok, "brain %p spell %X action %d", self, spell, self->action);
}
EI_VERIFY(0x005D7DA0, CastAtVerify, 0, s_unused);

static void __fastcall FollowVerify(AIBrain* self, void*, CEngineObject* object) {
    self->Follow(object);
    ei::verify::Check("AIBrain::Follow", true, self->action == AIBrain::kActionFollow && self->target == object, "brain %p", self);
}
EI_VERIFY(0x005D7E40, FollowVerify, 0, s_unused);

static void __fastcall SetSeenVerify(AIBrain* self, void*, CEngineObject* object) {
    bool alertWanted = self->alertMode == 1;
    self->SetSeen(object);
    bool ok = self->seen == object && (!alertWanted || self->alerted);
    ei::verify::Check("AIBrain::SetSeen", true, ok, "brain %p object %p", self, object);
}
EI_VERIFY(0x005D7E90, SetSeenVerify, 0, s_unused);
