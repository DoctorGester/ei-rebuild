// Checks for CEffectMoshka (world_effect_moshka.cpp), the client world's bird. Reached by every
// zone the client draws once the bird follows the leader (00566C20 / 00565380 call Follow; the draw
// pass calls Update every frame): newgame scenarios (units, effects, ...).
// - FindPerch (0067CD90) draws random numbers and asks the AI map's line of sight: ours with the
//   ISAAC generator (007A9D40) saved, the generator put back, the original; the perch (CheckNear:
//   cos / sin of the random angle) and the generator compared.
// - Unpack (0067D640) returns 1: both, compared.
// - Reset, Tick, Update and Follow move the figure in the client's maps, advance its animation or
//   make objects: ours only, with what must hold afterwards.
// Construct / Destruct / slot 0 are in verify_world_effect_lifetime.cpp.
#include "verify.h"

#include "../../src/game/random.h"
#include "../../src/game/world_effect.h"
#include "../../src/game/world.h"

#include <cstdint>
#include <cstring>

namespace {

void* s_unused;

std::uint32_t Bits(float f) {
    std::uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

bool Reset(const CEffectMoshka* e) {
    return e->bird_ == nullptr && e->source_ == nullptr && e->followedId_ == -1 && Bits(e->lastTime_) == 0 &&
           e->changeStamp_ == -1 && e->moving_ == 0 && e->startTick_ == 0;
}

// Ours only (takes the figure out of the client's maps, releases it and the source).
void __fastcall MoshkaResetVerify(CEffectMoshka* self) {
    self->Reset();
    ei::verify::Check("CEffectMoshka::Reset", true, Reset(self), "object %p", self);
}

// Ours only (Update moves the figure): the result is 0.
int __fastcall MoshkaTickVerify(CEffectMoshka* self) {
    int result = self->Tick();
    ei::verify::Check("CEffectMoshka::Tick", true, result == 0, "object %p result %d", self, result);
    return result;
}

// 0067CD90: PUSH EBP; MOV EBP,ESP; SUB ESP,2Ch (6).
Vector3*(__fastcall* s_findPerch)(CEffectMoshka*, void*, Vector3*, const Vector3*);
Vector3* __fastcall MoshkaFindPerchVerify(CEffectMoshka* self, void*, Vector3* out, const Vector3* from) {
    IsaacRandom& random = IsaacRandom::Global();
    static IsaacRandom before, after;   // 0x814 bytes each: kept off the stack
    std::memcpy(&before, &random, sizeof(IsaacRandom));
    Vector3 start = *from;   // `out` may alias `from`
    Vector3 ours;
    Vector3* returned = self->FindPerch(&ours, &start);
    std::memcpy(&after, &random, sizeof(IsaacRandom));
    std::memcpy(&random, &before, sizeof(IsaacRandom));
    Vector3* expected = s_findPerch(self, nullptr, out, &start);
    bool sameRandom = std::memcmp(&after, &random, sizeof(IsaacRandom)) == 0;
    ei::verify::Check("CEffectMoshka::FindPerch", true, sameRandom && returned == &ours && expected == out,
                      "generator or result differs (draws %u -> %u / %u)", before.count_, after.count_, random.count_);
    ei::verify::CheckNear("CEffectMoshka::FindPerch", out->x, ours.x, "x");
    ei::verify::CheckNear("CEffectMoshka::FindPerch", out->y, ours.y, "y");
    ei::verify::CheckNear("CEffectMoshka::FindPerch", out->z, ours.z, "z");
    return expected;
}

// Ours only: with a figure and a followed object the time is taken (or the bird was reset);
// otherwise nothing changes.
void __fastcall MoshkaUpdateVerify(CEffectMoshka* self, void*, float now) {
    bool active = self->bird_ != nullptr && WorldClient()->ObjectWithId(self->followedId_) != nullptr;
    std::uint32_t lastBefore = Bits(self->lastTime_);
    self->Update(now);
    bool ok;
    if (!active) ok = Bits(self->lastTime_) == lastBefore;
    else if (self->bird_ == nullptr) ok = Reset(self);
    else ok = Bits(self->lastTime_) == Bits(now) && self->state_ >= 0 && self->state_ <= 2 && !(self->timer_ > 200.0f);
    ei::verify::Check("CEffectMoshka::Update", true, ok, "object %p state %d active %d", self, self->state_,
                      active ? 1 : 0);
}

// Ours only (makes the figure and the source): the bird perches by an existing object.
void __fastcall MoshkaFollowVerify(CEffectMoshka* self, void*, int objectId) {
    bool exists = WorldClient()->ObjectWithId(objectId) != nullptr;
    self->Follow(objectId);
    bool ok = self->followedId_ == objectId;
    if (exists) ok = ok && self->bird_ != nullptr && self->state_ == 0 && Bits(self->timer_) == Bits(200.0f);
    ei::verify::Check("CEffectMoshka::Follow", true, ok, "object %p follows %d (exists %d)", self, objectId,
                      exists ? 1 : 0);
}

// 0067D640: MOV EAX,1 (5).
int(__fastcall* s_unpack)(CEffectMoshka*, void*, int, DeltaField*);
int __fastcall MoshkaUnpackVerify(CEffectMoshka* self, void*, int update, DeltaField* field) {
    int expected = s_unpack(self, nullptr, update, field);
    int actual = self->Unpack(update, field);
    ei::verify::Check("CEffectMoshka::Unpack", expected, actual, "object %p", self);
    return expected;
}

} // namespace

EI_VERIFY(0x0067CC90, MoshkaResetVerify, 0, s_unused);
EI_VERIFY(0x0067CD70, MoshkaTickVerify, 0, s_unused);
EI_VERIFY(0x0067CD90, MoshkaFindPerchVerify, 6, s_findPerch);
EI_VERIFY(0x0067CEF0, MoshkaUpdateVerify, 0, s_unused);
EI_VERIFY(0x0067D420, MoshkaFollowVerify, 0, s_unused);
EI_VERIFY(0x0067D640, MoshkaUnpackVerify, 5, s_unpack);
