// Checks for the constructors and destructors (map_object_lifetime.cpp).
//
// The constructors allocate nothing and link nothing (their sub-constructors only set fields), so
// both versions run: the original into a scratch copy of the raw memory, ours into the object;
// every byte and the returned pointers are compared. The destructors free memory and release
// references: ours only, with the base state they must leave.
#include "verify.h"

#include "../../src/game/combat.h"
#include "../../src/game/map_object.h"
#include "../../src/game/map_object_figures.h"
#include "../../src/game/unit.h"
#include "../../src/game/unit_ai.h"
#include "../../src/lib/crt.h"

#include <cstddef>
#include <cstring>
#include <vector>

namespace {
void* s_unused;

constexpr std::uintptr_t kEngineObjectVtable = 0x0073B8E0;

// Runs the original constructor on a scratch copy of `self`'s first N bytes and ours on `self`;
// compares the bytes and that each returned its own object.
template <std::size_t N, typename R>
R* SameConstruction(const char* name, R* self, R*(__fastcall* original)(void*), R* (R::*ours)()) {
    alignas(16) unsigned char scratch[N];
    std::memcpy(scratch, self, N);
    R* expected = original(scratch);
    R* actual = (self->*ours)();
    int diff = -1;
    for (std::size_t i = 0; i < N && diff < 0; ++i)
        if (scratch[i] != reinterpret_cast<unsigned char*>(self)[i]) diff = static_cast<int>(i);
    bool ok = diff < 0 && expected == reinterpret_cast<R*>(scratch) && actual == self;
    ei::verify::Check(name, true, ok, "first difference at +0x%X", diff);
    return actual;
}

// After a destructor body chain: the engine object's base vtable and no owner.
bool BaseState(const CEngineObject* object) {
    return *reinterpret_cast<const std::uintptr_t*>(object) == kEngineObjectVtable && object->owner_ == nullptr;
}
} // namespace

// 00515BD0: PUSH -1; PUSH 0x71D526 (7 bytes).
static CObject3D*(__fastcall* s_constructObject3D)(void*);
static CObject3D* __fastcall ConstructObject3DVerify(CObject3D* self) {
    return SameConstruction<sizeof(CObject3D)>("CObject3D::Construct", self, s_constructObject3D, &CObject3D::Construct);
}
EI_VERIFY(0x00515BD0, ConstructObject3DVerify, 7, s_constructObject3D);

// 0050E2D0: PUSH -1; PUSH 0x71CFB0 (7 bytes).
static CMapObject*(__fastcall* s_constructMapObject)(void*);
static CMapObject* __fastcall ConstructMapObjectVerify(CMapObject* self) {
    return SameConstruction<sizeof(CMapObject)>("CMapObject::Construct", self, s_constructMapObject,
                                                &CMapObject::Construct);
}
EI_VERIFY(0x0050E2D0, ConstructMapObjectVerify, 7, s_constructMapObject);

// 0052E110: PUSH -1; PUSH 0x71E7D0 (7 bytes).
static CUnit*(__fastcall* s_constructUnit)(void*);
static CUnit* __fastcall ConstructUnitVerify(CUnit* self) {
    return SameConstruction<sizeof(CUnit)>("CUnit::Construct", self, s_constructUnit, &CUnit::Construct);
}
EI_VERIFY(0x0052E110, ConstructUnitVerify, 7, s_constructUnit);

// 00515610: PUSH -1; PUSH 0x71D44C (7 bytes). The constructor writes up to +0x94.
static CMapObjectServer*(__fastcall* s_constructServer)(void*);
static CMapObjectServer* __fastcall ConstructServerVerify(CMapObjectServer* self) {
    return SameConstruction<0x94>("CMapObjectServer::Construct", self, s_constructServer, &CMapObjectServer::Construct);
}
EI_VERIFY(0x00515610, ConstructServerVerify, 7, s_constructServer);

// 00510450: MOV EAX,ECX; XOR ECX,ECX; MOV [EAX+4],ECX (7 bytes). Writes up to +0x54.
static CMapObjectClientSpecific*(__fastcall* s_constructClient)(void*);
static CMapObjectClientSpecific* __fastcall ConstructClientVerify(CMapObjectClientSpecific* self) {
    return SameConstruction<0x54>("CMapObjectClientSpecific::Construct", self, s_constructClient,
                                  &CMapObjectClientSpecific::Construct);
}
EI_VERIFY(0x00510450, ConstructClientVerify, 7, s_constructClient);

// 0052E5B0. Ours only (releases the controller, frees the carried array): the blank state.
static void __fastcall ResetUnitVerify(CUnit* self) {
    self->Reset();
    bool ok = !self->controller && !self->player && !self->carried && self->carriedCount == 0 &&
              self->prototype23C == -1 && self->fightAtWill == 1 && self->activeWeapon == 0xFFFFFFFFu &&
              !self->weapons[0] && !self->armor[6] && !self->spells[7];
    ei::verify::Check("CUnit::Reset", true, ok, "unit %d", self->id_);
}
EI_VERIFY(0x0052E5B0, ResetUnitVerify, 0, s_unused);

// The destructor bodies. Ours only: the object ends in the engine object's base state.
static void __fastcall DestructObject3DVerify(CObject3D* self) {
    self->DestructBody();
    ei::verify::Check("CObject3D::DestructBody", true, BaseState(self), "");
}
EI_VERIFY(0x00515D70, DestructObject3DVerify, 0, s_unused);

static void __fastcall DestructMapObjectVerify(CMapObject* self) {
    self->DestructBody();
    ei::verify::Check("CMapObject::DestructBody", true, BaseState(self), "");
}
EI_VERIFY(0x0050E660, DestructMapObjectVerify, 0, s_unused);

static void __fastcall DestructUnitVerify(CUnit* self) {
    self->DestructBody();
    ei::verify::Check("CUnit::DestructBody", true, BaseState(self), "");
}
EI_VERIFY(0x0052E310, DestructUnitVerify, 0, s_unused);

// 0052E2A0. Ours only; the memory may be gone afterwards: counted.
static CUnit* __fastcall DeletingDestructorVerify(CUnit* self, void*, std::uint32_t flags) {
    CUnit* result = self->DeletingDestructor(flags);
    ei::verify::Check("CUnit::DeletingDestructor", true, result == self, "");
    return result;
}
EI_VERIFY(0x0052E2A0, DeletingDestructorVerify, 0, s_unused);

static void __fastcall DestructServerVerify(CMapObjectServer* self) {
    self->DestructBody();
    ei::verify::Check("CMapObjectServer::DestructBody", true, BaseState(self), "");
}
EI_VERIFY(0x00515720, DestructServerVerify, 0, s_unused);

static void __fastcall DestructClientVerify(CMapObjectClientSpecific* self) {
    self->DestructBody();
    bool ok = BaseState(self) && *reinterpret_cast<void**>(reinterpret_cast<char*>(self) + 0x1C) == nullptr;
    ei::verify::Check("CMapObjectClientSpecific::DestructBody", true, ok, "");
}
EI_VERIFY(0x00510560, DestructClientVerify, 0, s_unused);

static void __fastcall DestructUnitServerVerify(CUnitServer* self) {
    self->DestructBody();
    bool ok = BaseState(self) && self->unit == nullptr;
    ei::verify::Check("CUnitServer::DestructBody", true, ok, "");
}
EI_VERIFY(0x00546230, DestructUnitServerVerify, 0, s_unused);

// --- A-U7 (combat_lifetime.cpp) ------------------------------------------------------------------
// Scenarios: every unit spawn (newgame, unit_create, combat*) builds a controller (00545580) and its
// brain (00545990); a unit deleted with its controller (zone changes: zone_gz2g_traps, world_load)
// runs 00546230 and with it the memory destructor 00545CB0. 005457E0 is reached only from unwind
// funclets (an exception inside the controller's constructor or destructor). 00545110 is called only
// for a queued order of kind 10 (StartQueuedOrder), whose one setter 005557E0 is dead code: no
// scenario reaches it.

// 00545580: PUSH -1; PUSH 0x720633 (7 bytes). Allocates nothing; the whole 0xB20 bytes compared.
static CUnitServer*(__fastcall* s_constructUnitServer)(void*);
static CUnitServer* __fastcall ConstructUnitServerVerify(CUnitServer* self) {
    return SameConstruction<sizeof(CUnitServer)>("CUnitServer::Construct", self, s_constructUnitServer,
                                                 &CUnitServer::Construct);
}
EI_VERIFY(0x00545580, ConstructUnitServerVerify, 7, s_constructUnitServer);

// 00545990: PUSH -1; PUSH 0x7207C9 (7 bytes). The 0x190 bytes compared.
static AIBrain*(__fastcall* s_constructBrain)(void*);
static AIBrain* __fastcall ConstructBrainVerify(AIBrain* self) {
    return SameConstruction<sizeof(AIBrain)>("AIBrain::Construct", self, s_constructBrain, &AIBrain::Construct);
}
EI_VERIFY(0x00545990, ConstructBrainVerify, 7, s_constructBrain);

namespace {
bool ObjectListDestroyed(const mfc::CTypedPtrList<CEngineObject>& list) {
    return list.vtable == reinterpret_cast<void*>(0x0073E730) && !list.head && !list.tail && list.count == 0 &&
           !list.free && !list.blocks;
}
} // namespace

// 00545CB0 (this = brain + 0xD0). Ours only: the destructors' vtables and the four lists emptied.
static void __fastcall DestructMemoryVerify(std::uint8_t* memory) {
    auto* brain = reinterpret_cast<AIBrain*>(memory - offsetof(AIBrain, memory));
    brain->DestructMemory();
    bool ok = brain->calls.vtable == reinterpret_cast<void*>(0x0073F064) &&
              brain->reports.vtable == reinterpret_cast<void*>(0x0073F058) &&
              ObjectListDestroyed(brain->nearby) && ObjectListDestroyed(brain->visible) &&
              ObjectListDestroyed(brain->seenCorpses) && ObjectListDestroyed(brain->corpses);
    ei::verify::Check("AIBrain::DestructMemory", true, ok, "");
}
EI_VERIFY(0x00545CB0, DestructMemoryVerify, 0, s_unused);

// 005457E0. Ours only: no modes, target or seen object, the looks' base vtable.
static void __fastcall DestructBrainVerify(AIBrain* self) {
    self->Destruct();
    bool ok = !self->modes.head && self->modes.count == 0 && !self->target && !self->seen &&
              self->looks.vtable == reinterpret_cast<void*>(kLookArrayBaseVtable);
    ei::verify::Check("AIBrain::Destruct", true, ok, "");
}
EI_VERIFY(0x005457E0, DestructBrainVerify, 0, s_unused);

namespace {
// `count` steps kept in our memory (empty for none).
std::vector<AnimationStep> KeepSteps(const AnimationStep* steps, int count) {
    if (!steps || count <= 0) return {};
    return std::vector<AnimationStep>(steps, steps + count);
}

// A copy of kept steps in a block of the game's heap (null when the list had none), for the
// original to free.
AnimationStep* CopySteps(bool had, const std::vector<AnimationStep>& kept) {
    if (!had) return nullptr;
    std::size_t n = kept.empty() ? 1 : kept.size();
    auto* copy = static_cast<AnimationStep*>(gamecrt::operator_new(static_cast<unsigned>(n * sizeof(AnimationStep))));
    if (!kept.empty()) std::memcpy(copy, kept.data(), kept.size() * sizeof(AnimationStep));
    return copy;
}
} // namespace

// 00545110: MOV EAX,FS:[0]; PUSH -1 (8 bytes). Output: ours on the plan first, then the original on
// a copy of the plan as it was (its steps duplicated) with a duplicate of the argument (it frees
// both); the copies are made after ours from bytes kept in our memory (SV2f). Every field but the
// steps pointer, and the copied steps, compared.
static void(__fastcall* s_takeSteps)(OrderPlan*, void*, AnimationSteps, int, int);
static void __fastcall TakeStepsVerify(OrderPlan* self, void*, AnimationSteps steps, int start, int duration) {
    OrderPlan scratch;
    std::memcpy(&scratch, self, sizeof(scratch));
    bool hadOwn = self->animation.steps != nullptr;
    std::vector<AnimationStep> ownSteps = KeepSteps(self->animation.steps, self->animation.capacity);
    bool hadArgument = steps.steps != nullptr;
    std::vector<AnimationStep> argumentSteps = KeepSteps(steps.steps, steps.count);
    self->TakeSteps(steps, start, duration);
    scratch.animation.steps = CopySteps(hadOwn, ownSteps);
    AnimationSteps argument;
    std::memcpy(&argument, &steps, sizeof(argument));
    argument.steps = CopySteps(hadArgument, argumentSteps);
    s_takeSteps(&scratch, nullptr, argument, start, duration);
    AnimationStep* expectedSteps = scratch.animation.steps;
    scratch.animation.steps = self->animation.steps;
    bool ok = std::memcmp(&scratch, self, sizeof(scratch)) == 0 &&
              (self->animation.count == 0 ||
               std::memcmp(expectedSteps, self->animation.steps,
                           static_cast<std::size_t>(self->animation.count) * sizeof(AnimationStep)) == 0);
    if (expectedSteps) gamecrt::operator_delete(expectedSteps);
    ei::verify::Check("OrderPlan::TakeSteps", true, ok, "steps %d start %d duration %d", steps.count, start, duration);
}
EI_VERIFY(0x00545110, TakeStepsVerify, 8, s_takeSteps);

// 00546100 (CPlayerServer::OnUseOrder's path, ui_battle_commands). Ours only: no points, both cell
// lists empty, their vtables.
static void __fastcall DestructPathVerify(AIPath* self) {
    self->Destruct();
    bool ok = self->excludedBlocks.vtable == reinterpret_cast<void*>(AIPath::kExcludedBlocksVtable) && !self->cells.head &&
              self->cells.count == 0 && !self->cells.blocks && !self->steps.head && self->steps.count == 0 &&
              !self->steps.blocks && self->cells.vtable == PathCellListVtable();
    ei::verify::Check("AIPath::Destruct", true, ok, "");
}
EI_VERIFY(0x00546100, DestructPathVerify, 0, s_unused);
