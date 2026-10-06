// Checks for player_server_handlers.cpp.
// - OnLeaderDied (0066F4C0; scenario hero_death: the hero killed in single player): ours only. Single
//   player: the stream grew by 1, or 9 with the 0x0B message. A network game (no scenario): flag70_ is
//   1 and lastPosition_.z -1313 after.
// - OnUseOrder (00670310; ui_battle_commands: the clicks on the dead wolves send 0x36 with the one
//   selected hero, so only the one-unit path runs): ours only, result 1 and the stream moved past the
//   ids and the four values (when the read did not run out).
// - UseOrderDistanceSquared (00670670), AIPath::TargetPoint (00670730), AIOrder::AimPoint (006707D0):
//   against the original (the two-or-more-units path of OnUseOrder; no scenario selects two heroes).
// - AIPath::Construct (006706A0), AIOrder::MakeUse (00670760): ours only (fields, the reference).
#include "verify.h"

#include "../../src/game/ai_map.h"
#include "../../src/game/core_streams.h"
#include "../../src/game/map_object.h"
#include "../../src/game/player_messages.h"
#include "../../src/game/unit_ai.h"
#include "../../src/game/world.h"

#include <cstring>

namespace {
void* s_unused;
constexpr std::uint8_t kMessageObject0B = 0x0B;

CMemoryStream& Received() { return *reinterpret_cast<CMemoryStream*>(std::uintptr_t(0x007C83D0)); }
bool NetworkGame() { return IsMultiplayerClient() != 0; }

void __fastcall LeaderDiedVerify(CPlayerServer* self) {
    bool network = NetworkGame();
    std::uint32_t before = self->out_.position_;
    self->OnLeaderDied();
    std::uint32_t grown = self->out_.position_ - before;
    if (network) {
        ei::verify::Check("CPlayerServer::OnLeaderDied", true, self->flag70_ == 1 && self->lastPosition_.z == -1313.0f,
                          "server %p: flag %d", self, self->flag70_);
    } else {
        bool ok = grown == 1 || (grown == 9 && self->out_.data_[before + 1] == kMessageObject0B);
        ei::verify::Check("CPlayerServer::OnLeaderDied", true, ok, "server %p: grew %u", self, grown);
    }
}

int __fastcall UseOrderVerify(CPlayerServer* self) {
    CMemoryStream& in = Received();
    std::uint32_t before = in.position_;
    std::int32_t count = 0;
    if (before + 4 <= in.size_) std::memcpy(&count, in.data_ + before, 4);
    int result = self->OnUseOrder();
    bool ran = in.ok_ != 0;
    std::uint32_t expected = before + 4u * static_cast<std::uint32_t>((count > 0 ? count : 0) + 5);
    ei::verify::Check("CPlayerServer::OnUseOrder", true, result == 1 && (!ran || in.position_ == expected),
                      "server %p: result %d count %d moved %u", self, result, count, in.position_ - before);
    return result;
}

// 00670670: FLD [ECX]; FSUB [EDX]; FLD [ECX+4] (7 bytes).
double(__fastcall* s_distance)(const Vector3*, const Vector3*);
double __fastcall DistanceVerify(const Vector3* a, const Vector3* b) {
    double expected = s_distance(a, b);
    double actual = UseOrderDistanceSquared(a, b);
    ei::verify::CheckNear("UseOrderDistanceSquared", expected, actual, "(%g %g %g) (%g %g %g)", a->x, a->y, a->z, b->x,
                          b->y, b->z);
    return expected;
}

// 00670730: FLD [0074326C] (6 bytes, an absolute operand).
Vector3*(__fastcall* s_targetPoint)(const AIPath*, void*, Vector3*);
Vector3* __fastcall TargetPointVerify(const AIPath* self, void*, Vector3* out) {
    Vector3 expected{};
    s_targetPoint(self, nullptr, &expected);
    Vector3* result = self->TargetPoint(out);
    ei::verify::CheckNear("AIPath::TargetPoint x", expected.x, out->x, "path %p", self);
    ei::verify::CheckNear("AIPath::TargetPoint y", expected.y, out->y, "path %p", self);
    ei::verify::Check("AIPath::TargetPoint", true, out->z == 0.0f && result == out, "path %p", self);
    return result;
}

// 006707D0: MOV EAX,[ECX+8]; PUSH ESI; TEST EAX,EAX (6 bytes).
Vector3*(__fastcall* s_aimPoint)(const AIOrder*, void*, Vector3*);
Vector3* __fastcall AimPointVerify(const AIOrder* self, void*, Vector3* out) {
    Vector3 expected{};
    s_aimPoint(self, nullptr, &expected);
    Vector3* result = self->AimPoint(out);
    bool same = std::memcmp(&expected, out, sizeof(Vector3)) == 0 && result == out;
    ei::verify::Check("AIOrder::AimPoint", true, same, "order %p: (%g %g %g) / (%g %g %g)", self, expected.x, expected.y,
                      expected.z, out->x, out->y, out->z);
    return result;
}

AIPath* __fastcall ConstructVerify(AIPath* self) {
    AIPath* result = self->Construct();
    bool ok = result == self && self->steps.count == 0 && self->cells.count == 0 && self->steps.blockSize == 0x80 &&
              self->cells.blockSize == 0x80 && self->points == nullptr && self->pointCount == 0;
    ei::verify::Check("AIPath::Construct", true, ok, "path %p", self);
    return result;
}

void __fastcall MakeUseVerify(AIOrder* self, void*, CEngineObject* object, int what) {
    int references = object ? object->refCount_ : 0;
    bool same = object != nullptr && self->object == object;
    self->MakeUse(object, what);
    bool ok = self->kind == 6 && self->what == what && self->object == object && self->flag == 0 &&
              (!object || object->refCount_ == references + (same ? 0 : 1));
    ei::verify::Check("AIOrder::MakeUse", true, ok, "order %p", self);
}
} // namespace

EI_VERIFY(0x0066F4C0, LeaderDiedVerify, 0, s_unused);
EI_VERIFY(0x00670310, UseOrderVerify, 0, s_unused);
EI_VERIFY(0x00670670, DistanceVerify, 7, s_distance);
EI_VERIFY(0x00670730, TargetPointVerify, 6, s_targetPoint);
EI_VERIFY(0x006707D0, AimPointVerify, 6, s_aimPoint);
EI_VERIFY(0x006706A0, ConstructVerify, 0, s_unused);
EI_VERIFY(0x00670760, MakeUseVerify, 0, s_unused);
