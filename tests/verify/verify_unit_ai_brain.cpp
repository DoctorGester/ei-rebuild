// Checks for the brain's mode list and resets (src/game/unit_ai_brain.cpp). All of them release
// references and free list nodes, so they run ours only and check the resulting state.
#include "verify.h"

#include "../../src/game/combat.h"
#include "../../src/game/object_diplomacy.h"
#include "../../src/game/originals_ai.h"
#include "../../src/game/unit_ai.h"
#include "../../src/game/world.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {
void* s_unused;

// Nodes walked from the head; -1 when the walk disagrees with the count or the tail.
int ListLength(const mfc::CTypedPtrList<AIMode>& list) {
    int n = 0;
    const mfc::CTypedPtrList<AIMode>::Node* last = nullptr;
    for (auto* node = list.head; node; node = node->next) {
        if (node->prev != last) return -1;
        last = node;
        ++n;
    }
    return n == list.count && last == list.tail ? n : -1;
}

int CountOfType(AIBrain* brain, int type) {
    int n = 0;
    for (auto* node = brain->modes.head; node; node = node->next)
        if (node->data && node->data->GetType() == type) ++n;
    return n;
}

bool InList(AIBrain* brain, AIMode* mode) { return mode == nullptr || brain->modes.Find(mode) != nullptr; }

template <typename T>
bool Empty(const mfc::CTypedPtrList<T>& list) {
    return list.head == nullptr && list.tail == nullptr && list.count == 0 && list.free == nullptr && list.blocks == nullptr;
}

bool MemoryEmpty(const AIBrain* brain) {
    return Empty(brain->unknown_0F4) && Empty(brain->visible) && Empty(brain->unknown_12C) && Empty(brain->corpses) &&
           brain->noticedX == 0.0f && brain->noticedY == 0.0f && brain->noticedLevel == 0 && brain->noticedKind == 0 &&
           brain->nearbyCountdown == 0;
}

bool ModesCleared(const AIBrain* brain) {
    return Empty(brain->modes) && brain->current == nullptr && brain->unknown_44 == 100000.0f &&
           brain->unknown_48 == 100000.0f && brain->decay == 1.0f;
}
} // namespace

// 005D5F60 AddMode: ours only. Afterwards the list is consistent, its last mode has the added
// mode's type (RefPtr::Share may have cloned it), exactly one mode has that type, and the mode in
// charge is still listed (or was dropped with the mode it replaced).
static void __fastcall AddModeVerify(AIBrain* self, void*, AIMode* mode) {
    int type = mode->GetType();
    self->AddMode(mode);
    int length = ListLength(self->modes);
    bool ok = length > 0 && self->modes.tail->data && self->modes.tail->data->GetType() == type &&
              CountOfType(self, type) == 1 && InList(self, self->current);
    ei::verify::Check("AIBrain::AddMode", true, ok, "brain %p type %d: %d modes, current %p", self, type, length,
                      self->current);
}
EI_VERIFY(0x005D5F60, AddModeVerify, 0, s_unused);

// 005D6140 RemoveModesOfType: ours only; no mode of the type is left, the list is consistent.
static void __fastcall RemoveModesVerify(AIBrain* self, void*, int type) {
    self->RemoveModesOfType(type);
    int length = ListLength(self->modes);
    ei::verify::Check("AIBrain::RemoveModesOfType", true, length >= 0 && CountOfType(self, type) == 0,
                      "brain %p type %d: %d modes", self, type, length);
}
EI_VERIFY(0x005D6140, RemoveModesVerify, 0, s_unused);

// 005D4DF0 ClearModes: ours only; list empty with its blocks freed, no mode in charge, near point
// and decay reset.
static void __fastcall ClearModesVerify(AIBrain* self) {
    self->ClearModes();
    ei::verify::Check("AIBrain::ClearModes", true, ModesCleared(self), "brain %p: %d modes, current %p", self,
                      self->modes.count, self->current);
}
EI_VERIFY(0x005D4DF0, ClearModesVerify, 0, s_unused);

// 005D4E80 Reset: ours only; ClearModes' state, no action, memory empty, no target or seen object.
static void __fastcall ResetVerify(AIBrain* self) {
    self->Reset();
    bool ok = ModesCleared(self) && self->action == AIBrain::kActionNone && MemoryEmpty(self) && self->target == nullptr &&
              self->seen == nullptr && self->looks.size == 0 && self->actionPoint.x == 0.0f;
    ei::verify::Check("AIBrain::Reset", true, ok, "brain %p: action %d, target %p, seen %p", self, self->action,
                      self->target, self->seen);
}
EI_VERIFY(0x005D4E80, ResetVerify, 0, s_unused);

// 005D4F70 Init: ours only; Reset's state plus no owner, alarm countdown 1, no look running,
// +0x38 = 10.
static void __fastcall InitVerify(AIBrain* self) {
    self->Init();
    bool ok = ModesCleared(self) && MemoryEmpty(self) && self->owner == nullptr && self->target == nullptr &&
              self->seen == nullptr && self->alarmCountdown == 1 && self->actionTime == AIBrain::kNoTime &&
              self->callRadius == 10.0f && self->alertMode == 0 && self->looks.size == 0;
    ei::verify::Check("AIBrain::Init", true, ok, "brain %p", self);
}
EI_VERIFY(0x005D4F70, InitVerify, 0, s_unused);

// 005CB0F0 ResetMemory (thiscall on brain + 0xD0): ours only; the four lists are empty, the noticed
// point and level zero.
static void __fastcall ResetMemoryVerify(std::uint8_t* memory) {
    auto* brain = reinterpret_cast<AIBrain*>(memory - offsetof(AIBrain, memory));
    brain->ResetMemory();
    ei::verify::Check("AIBrain::ResetMemory", true, MemoryEmpty(brain), "brain %p", brain);
}
EI_VERIFY(0x005CB0F0, ResetMemoryVerify, 0, s_unused);

// ---- Standard mode sets ---------------------------------------------------------------------------

namespace {
AIMode* Tail(AIBrain* brain) { return brain->modes.tail ? brain->modes.tail->data : nullptr; }
bool TailIs(AIBrain* brain, int type) { return ListLength(brain->modes) > 0 && Tail(brain) && Tail(brain)->GetType() == type; }
bool SamePoint(const Vector3& a, const Vector3* b) { return std::memcmp(&a, b, sizeof(Vector3)) == 0; }

// The five modes of a standard set, in order: guard (0) or sentry (2), then 3, 4, 5, 7.
bool StandardSet(AIBrain* brain, int first, const Vector3* point) {
    static const int kTail[] = {3, 4, 5, 7};
    auto* node = brain->modes.head;
    if (ListLength(brain->modes) != 5 || node->data->GetType() != first) return false;
    const Vector3& at = first == 0 ? static_cast<GuardMode*>(node->data)->point : static_cast<SentryMode*>(node->data)->point;
    if (!SamePoint(at, point)) return false;
    for (int type : kTail) {
        node = node->next;
        if (node->data->GetType() != type) return false;
    }
    auto* fear = static_cast<FearMode*>(node->data);
    auto* aggression = static_cast<AggressionMode*>(node->prev->prev->data);
    return fear->flag == 0 && fear->spread == 3.0f && fear->shelter.x == -1313.0f && aggression->enemy == nullptr &&
           aggression->enemyPart == -1 && brain->current == nullptr && brain->target == nullptr &&
           brain->action == AIBrain::kActionNone;
}
} // namespace

// 005D6230 / 005D64E0 StandardGuard / StandardSentry: ours only; the list holds exactly the five
// modes of the set in order with the given point, nothing in charge, no target or action. These are
// the only callers of the six adders below, whose wrappers therefore do not run in practice.
static void __fastcall StandardGuardVerify(AIBrain* self, void*, const Vector3* point, float radius) {
    self->StandardGuard(point, radius);
    ei::verify::Check("AIBrain::StandardGuard", true, StandardSet(self, 0, point), "brain %p: %d modes", self,
                      self->modes.count);
}
EI_VERIFY(0x005D6230, StandardGuardVerify, 0, s_unused);

static void __fastcall StandardSentryVerify(AIBrain* self, void*, const Vector3* point) {
    self->StandardSentry(point);
    ei::verify::Check("AIBrain::StandardSentry", true, StandardSet(self, 2, point), "brain %p: %d modes", self,
                      self->modes.count);
}
EI_VERIFY(0x005D64E0, StandardSentryVerify, 0, s_unused);

// The adders: ours only; the new last mode has the type and the fields set.
static void __fastcall AddGuardModeVerify(AIBrain* self, void*, const Vector3* point, float radius, float wander) {
    self->AddGuardMode(point, radius, wander);
    bool ok = TailIs(self, 0) && SamePoint(static_cast<GuardMode*>(Tail(self))->point, point);
    ei::verify::Check("AIBrain::AddGuardMode", true, ok, "brain %p", self);
}
EI_VERIFY(0x005D6300, AddGuardModeVerify, 0, s_unused);

static void __fastcall AddSuspectionModeVerify(AIBrain* self) {
    self->AddSuspectionMode();
    ei::verify::Check("AIBrain::AddSuspectionMode", true, TailIs(self, 3), "brain %p", self);
}
EI_VERIFY(0x005D63B0, AddSuspectionModeVerify, 0, s_unused);

static void __fastcall AddAggressionModeVerify(AIBrain* self) {
    self->AddAggressionMode();
    bool ok = TailIs(self, 4) && static_cast<AggressionMode*>(Tail(self))->enemyPart == -1;
    ei::verify::Check("AIBrain::AddAggressionMode", true, ok, "brain %p", self);
}
EI_VERIFY(0x005D63F0, AddAggressionModeVerify, 0, s_unused);

static void __fastcall AddCorpseWatcherModeVerify(AIBrain* self) {
    self->AddCorpseWatcherMode();
    ei::verify::Check("AIBrain::AddCorpseWatcherMode", true, TailIs(self, 5), "brain %p", self);
}
EI_VERIFY(0x005D6440, AddCorpseWatcherModeVerify, 0, s_unused);

static void __fastcall AddFearModeVerify(AIBrain* self, void*, std::uint8_t flag) {
    self->AddFearMode(flag);
    bool ok = TailIs(self, 7) && static_cast<FearMode*>(Tail(self))->flag == flag;
    ei::verify::Check("AIBrain::AddFearMode", true, ok, "brain %p flag %d", self, flag);
}
EI_VERIFY(0x005D6480, AddFearModeVerify, 0, s_unused);

static void __fastcall AddSentryModeVerify(AIBrain* self, void*, const Vector3* point) {
    self->AddSentryMode(point);
    bool ok = TailIs(self, 2) && SamePoint(static_cast<SentryMode*>(Tail(self))->point, point);
    ei::verify::Check("AIBrain::AddSentryMode", true, ok, "brain %p", self);
}
EI_VERIFY(0x005D65B0, AddSentryModeVerify, 0, s_unused);

// ---- Calls for help -------------------------------------------------------------------------------

// 005D5990 CallAllies: ours only. The units in range are listed first with the same sector-map
// query (read only); afterwards every allied one (not the caller) has the call as its last heard
// record: the point, kind 0x1A, flag 0.
static void __fastcall CallAlliesVerify(AIBrain* self, void*, const Vector3* point) {
    mfc::CTypedPtrList<CEngineObject> units{};
    units.vtable = reinterpret_cast<void*>(0x0073D454);
    units.blockSize = 10;
    CUnit* unit = self->owner->unit;
    SectorMapFindUnitsInRadius(WorldServer()->Sectors(), nullptr, &unit->positionX, &units, self->callRadius);
    self->CallAllies(point);
    int allies = 0;
    bool ok = true;
    for (auto* node = units.head; node; node = node->next) {
        auto* other = static_cast<CUnit*>(node->data);
        if (other == unit || !(GameDiplomacy()->allied_[unit->side] & (1u << (other->side & 31)))) continue;
        ++allies;
        auto* brain = reinterpret_cast<AIBrain*>(static_cast<CUnitServer*>(other->controller)->brain);
        const AICallArray& calls = brain->calls;
        const AICallRecord* last = calls.size > 0 ? &calls.data[calls.size - 1] : nullptr;
        ok = ok && last && SamePoint(last->point, point) && last->kind == 0x1A && last->unknown_10 == 0;
    }
    units.RemoveAll();
    ei::verify::Check("AIBrain::CallAllies", true, ok, "brain %p: %d allies", self, allies);
}
EI_VERIFY(0x005D5990, CallAlliesVerify, 0, s_unused);

// ---- Perception memory ----------------------------------------------------------------------------

namespace {
AIBrain* BrainOfMemory(std::uint8_t* memory) { return reinterpret_cast<AIBrain*>(memory - offsetof(AIBrain, memory)); }

// The noticed point, level and kind (+0xE0..+0xEF).
struct Noticed {
    float x, y;
    int level, kind;
};
Noticed NoticedOf(const AIBrain* brain) { return {brain->noticedX, brain->noticedY, brain->noticedLevel, brain->noticedKind}; }
void Restore(AIBrain* brain, const Noticed& n) {
    brain->noticedX = n.x;
    brain->noticedY = n.y;
    brain->noticedLevel = n.level;
    brain->noticedKind = n.kind;
}
} // namespace

// 005CBBE0 Notice: MOV EAX,[ECX+0x18]; SUB ESP,0xC (6 bytes). Differential: the original runs first
// (its result and the noticed fields kept), the fields are put back, ours runs; both compared.
static int(__fastcall* s_notice)(std::uint8_t*, void*, const Vector3*, int, int);
static int __fastcall NoticeVerify(std::uint8_t* memory, void*, const Vector3* point, int level, int kind) {
    AIBrain* brain = BrainOfMemory(memory);
    Noticed before = NoticedOf(brain);
    int expected = s_notice(memory, nullptr, point, level, kind);
    Noticed theirs = NoticedOf(brain);
    Restore(brain, before);
    int actual = brain->Notice(point, level, kind);
    Noticed ours = NoticedOf(brain);
    ei::verify::Check("AIBrain::Notice", expected, actual, "brain %p level %d -> %d", brain, before.level, level);
    ei::verify::Check("AIBrain::Notice fields", theirs, ours, "brain %p level %d -> %d", brain, before.level, level);
    return expected;
}
EI_VERIFY(0x005CBBE0, NoticeVerify, 6, s_notice);

// 005CC020 Forget: ours only; the object is in none of the four lists nor the reports.
static void __fastcall ForgetVerify(std::uint8_t* memory, void*, CEngineObject* object) {
    AIBrain* brain = BrainOfMemory(memory);
    brain->Forget(object);
    bool ok = !brain->unknown_0F4.Find(object) && !brain->visible.Find(object) && !brain->unknown_12C.Find(object) &&
              !brain->corpses.Find(object) && !ReportsContains(&brain->reports, nullptr, object);
    ei::verify::Check("AIBrain::Forget", true, ok || object == nullptr, "brain %p object %p", brain, object);
}
EI_VERIFY(0x005CC020, ForgetVerify, 0, s_unused);

// 005CC620 AIReportArray::Add: ours only; a non-null object is listed exactly once with at least
// `what`.
static void __fastcall ReportAddVerify(AIReportArray* self, void*, CEngineObject* object, int what) {
    ReportsAdd(self, nullptr, object, what);
    int found = 0;
    bool ok = true;
    for (int i = 0; i < self->size; ++i) {
        if (self->data[i].object != object) continue;
        ++found;
        ok = ok && self->data[i].what >= what;
    }
    ei::verify::Check("AIReportArray::Add", true, object == nullptr || (ok && found == 1), "array %p object %p what %d", self,
                      object, what);
}
EI_VERIFY(0x005CC620, ReportAddVerify, 0, s_unused);

// 005CC560 AIReportArray::Contains: MOV EDX,[ECX+8]; XOR EAX,EAX (5 bytes). Pure (AL compared).
static std::uint32_t(__fastcall* s_contains)(AIReportArray*, void*, CEngineObject*);
static std::uint32_t __fastcall ContainsVerify(AIReportArray* self, void*, CEngineObject* object) {
    std::uint32_t expected = s_contains(self, nullptr, object);
    bool actual = ReportsContains(self, nullptr, object);
    ei::verify::Check("AIReportArray::Contains", static_cast<bool>(expected & 0xFF), actual, "array %p object %p", self,
                      object);
    return expected;
}
EI_VERIFY(0x005CC560, ContainsVerify, 5, s_contains);
