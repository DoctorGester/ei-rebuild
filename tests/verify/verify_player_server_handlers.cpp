// Checks for player_server_handlers.cpp.
// - OnLeaderDied (0066F4C0; scenario hero_death: the hero killed in single player): ours only. Single
//   player: the stream grew by 1, or 10 with the 0x0B message (0x0B, id, 0xFF = 9 bytes, after the 0x09). A network game (no scenario): flag70_ is
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
#include "../../src/game/party.h"
#include "../../src/lib/crt.h"
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
        bool ok = grown == 1 || (grown == 10 && self->out_.data_[before + 1] == kMessageObject0B);
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

// --- A-H14 ----------------------------------------------------------------------------------------
// The client's other messages (ours only; they read the received stream, so they cannot run twice):
// result 0 or 1 and, where the payload has a fixed shape, the stream moved past it (when the read did
// not run out). Scenarios: every new game sends 0x05 (OnJoined); ui_battle_commands' ground clicks
// 0x30 (OnWalk) and its attack clicks 0x31 (OnAttack); the `zone` harness step and the zone
// scenarios run LeaveToZone (0066F310) directly. 0x02 / 0x03 / 0x06 / 0x07 / 0x08 / 0x09 / 0x3A / 0x50
// are not known to be reached.
// FormationOffsets (0066FD70; OnWalk / OnGoTo): side by side, the original into a scratch array
// (SUB ESP,0x1C; PUSH EBX; PUSH ESI; MOV ESI,ECX: 7 bytes), offsets within a few ULPs.
std::int32_t PeekInt(const CMemoryStream& in, std::uint32_t at) {
    std::int32_t v = 0;
    if (at + 4 <= in.size_) std::memcpy(&v, in.data_ + at, 4);
    return v;
}
using HandlerFn = int(CPlayerServer::*)();
int RunHandler(const char* name, CPlayerServer* self, HandlerFn handler, int fixedBytes, int perUnit) {
    CMemoryStream& in = Received();
    std::uint32_t before = in.position_;
    std::int32_t count = perUnit ? PeekInt(in, before) : 0;
    int result = (self->*handler)();
    bool ok = result == 0 || result == 1;
    if (fixedBytes >= 0 && in.ok_ != 0) {
        std::uint32_t expected = before + static_cast<std::uint32_t>(fixedBytes) +
                                 (perUnit ? 4u * static_cast<std::uint32_t>((count > 0 ? count : 0) + 1) : 0u);
        ok = ok && in.position_ == expected;
    }
    ei::verify::Check(name, true, ok, "server %p: result %d moved %u", self, result, in.position_ - before);
    return result;
}
int __fastcall RefreshPartyVerify(CPlayerServer* self) {
    return RunHandler("CPlayerServer::OnRefreshParty", self, &CPlayerServer::OnRefreshParty, 0, 0);
}
int __fastcall TakeExitVerify(CPlayerServer* self) {
    return RunHandler("CPlayerServer::OnTakeExit", self, &CPlayerServer::OnTakeExit, 4, 0);
}
int __fastcall JoinedVerify(CPlayerServer* self) {
    int state = self->state_;
    int result = RunHandler("CPlayerServer::OnJoined", self, &CPlayerServer::OnJoined, 0, 0);
    bool ok = result == (state == 2 ? 1 : 0) &&
              (result == 0 || (self->state_ == 1 && (self->Owner()->dirty_ & 0x10) != 0));
    ei::verify::Check("CPlayerServer::OnJoined state", true, ok, "state %d -> %d", state, self->state_);
    return result;
}
int __fastcall LeaveToZoneMessageVerify(CPlayerServer* self) {
    return RunHandler("CPlayerServer::OnLeaveToZone", self, &CPlayerServer::OnLeaveToZone, -1, 0);
}
int __fastcall BriefingDoneVerify(CPlayerServer* self) {
    return RunHandler("CPlayerServer::OnBriefingDone", self, &CPlayerServer::OnBriefingDone, -1, 0);
}
int __fastcall NewPartyVerify(CPlayerServer* self) {
    return RunHandler("CPlayerServer::OnNewParty", self, &CPlayerServer::OnNewParty, -1, 0);
}
int __fastcall ResumeVerify(CPlayerServer* self) {
    int state = self->state_;
    int result = RunHandler("CPlayerServer::OnResume", self, &CPlayerServer::OnResume, 0, 0);
    bool ok = result == (state == 4 ? 1 : 0) && (result == 0 || (self->state_ == 1 && self->flag70_ == 1));
    ei::verify::Check("CPlayerServer::OnResume state", true, ok, "state %d -> %d", state, self->state_);
    return result;
}
int __fastcall WalkVerify(CPlayerServer* self) {
    return RunHandler("CPlayerServer::OnWalk", self, &CPlayerServer::OnWalk, 16, 1);
}
int __fastcall GoToVerify(CPlayerServer* self) {
    return RunHandler("CPlayerServer::OnGoTo", self, &CPlayerServer::OnGoTo, 16, 1);
}
int __fastcall AttackVerify(CPlayerServer* self) {
    return RunHandler("CPlayerServer::OnAttack", self, &CPlayerServer::OnAttack, 12, 1);
}
int __fastcall PartyUpdateVerify(CPlayerServer* self) {
    return RunHandler("CPlayerServer::OnPartyUpdate", self, &CPlayerServer::OnPartyUpdate, -1, 0);
}

// 0066F890 (A-H19): ours only (it reads the received stream). The stream moved past the 0x88-byte
// message; refused in any state but 0 with nothing written; a wrong version writes 0x11 and leaves
// state 3; a login writes 0x00 and the player's id (then SendParty's 0x80) and leaves state 1.
// Reached by every new game and zone change (the client logs in to the local server).
int __fastcall LoginVerify(CPlayerServer* self) {
    int state = self->state_;
    std::uint32_t before = self->out_.position_;
    CMemoryStream& in = Received();
    std::int32_t version = PeekInt(in, in.position_);
    int result = RunHandler("CPlayerServer::OnLogin", self, &CPlayerServer::OnLogin, 0x88, 0);
    std::uint32_t grown = self->out_.position_ - before;
    bool ok;
    if (state != 0) {
        ok = result == 0 && grown == 0 && self->state_ == state;
    } else if (version != 0x64) {
        ok = result == 0 && self->state_ == 3 && grown >= 2 && self->out_.data_[before] == 0x11;
    } else if (result == 1) {
        std::int32_t id = 0;
        if (grown >= 5) std::memcpy(&id, self->out_.data_ + before + 1, 4);
        ok = self->state_ == 1 && grown > 5 && self->out_.data_[before] == 0 && id == self->Owner()->id_;
    } else {
        ok = self->state_ == 0 && grown == 0;
    }
    ei::verify::Check("CPlayerServer::OnLogin state", true, ok, "state %d -> %d, version %X, result %d, wrote %u", state,
                      self->state_, version, result, grown);
    return result;
}

// 0047ADA0 (A-H19; the login's name check): against the original. PUSH EBX; PUSH ESI; MOV EBX,EDX;
// XOR ESI,ESI (6 bytes).
BOOL(__fastcall* s_printable)(const char*, int, int);
BOOL __fastcall PrintableVerify(const char* text, int maxLength, int minLength) {
    BOOL expected = s_printable(text, maxLength, minLength);
    BOOL actual = IsPrintableText(text, maxLength, minLength);
    ei::verify::Check("IsPrintableText", expected, actual, "'%.63s' %d %d", text, maxLength, minLength);
    return expected;
}

// 0066F310: ours only. A joined player that leaves is in state 3 with the party's zone and
// entrance set, message 0x08 written and flushed; otherwise (not joined, or the leader gone in a
// single-player campaign) nothing changes but the leader's death.
void __fastcall LeaveToZoneVerify(CPlayerServer* self, void*, const char* zone, int entrance) {
    int state = self->state_;
    self->LeaveToZone(zone, entrance);
    bool ok = true;
    if (self->state_ == 3 && state == 1) {
        CParty* party = self->Owner()->Party();
        ok = std::strcmp(party->zone_, zone) == 0 && static_cast<int>(party->unknown_6C) == entrance;
    } else {
        ok = self->state_ == state;
    }
    ei::verify::Check("CPlayerServer::LeaveToZone", true, ok, "%s %d: state %d -> %d", zone, entrance, state,
                      self->state_);
}

void(__fastcall* s_formation)(const mfc::CArray<CUnit*>*, mfc::CArray<Vector3>*);
void __fastcall FormationVerify(const mfc::CArray<CUnit*>* units, mfc::CArray<Vector3>* offsets) {
    mfc::CArray<Vector3> scratch{offsets->vtable, nullptr, 0, 0, 0};
    s_formation(units, &scratch);
    FormationOffsets(units, offsets);
    bool sized = scratch.size == offsets->size;
    ei::verify::Check("FormationOffsets", true, sized, "%d units: %d vs %d offsets", units->size, scratch.size,
                      offsets->size);
    for (int i = 0; sized && i < scratch.size; ++i) {
        const Vector3& a = scratch.data[i];
        const Vector3& b = offsets->data[i];
        ei::verify::CheckNear("FormationOffsets x", a.x, b.x, "unit %d of %d", i, units->size);
        ei::verify::CheckNear("FormationOffsets y", a.y, b.y, "unit %d of %d", i, units->size);
        ei::verify::Check("FormationOffsets z", a.z, b.z, "unit %d of %d", i, units->size);
    }
    if (scratch.data) gamecrt::operator_delete(scratch.data);
}

// 0066F0C0 (before a zone change, 00590A40), 0066F450 (the player state tick), 0066F840 (the
// briefing screen's answers; ui_base_dialogue): ours only, the messages appended.
void __fastcall PartyUpdateSendVerify(CPlayerServer* self) {
    std::uint32_t before = self->out_.position_;
    self->SendPartyUpdate();
    bool ok = self->out_.position_ > before + 1 && self->out_.data_[before] == 0x83;
    ei::verify::Check("CPlayerServer::SendPartyUpdate", true, ok, "grew %u", self->out_.position_ - before);
}
void __fastcall ResendPartyVerify(CPlayerServer* self) {
    std::uint32_t before = self->out_.position_;
    bool sends = self->state_ == 1 && self->flag70_ != 0;
    self->ResendParty();
    ei::verify::Check("CPlayerServer::ResendParty", true, sends || self->out_.position_ == before, "state %d flag %d",
                      self->state_, self->flag70_);
}
void __fastcall Pair19Verify(CPlayerServer* self, void*, std::uint8_t a, std::uint32_t b) {
    std::uint32_t before = self->out_.position_;
    self->SendPair19(a, b);
    bool ok = self->out_.position_ == before + 6;
    if (ok) {
        std::uint32_t written = 0;
        std::memcpy(&written, self->out_.data_ + before + 2, 4);
        ok = self->out_.data_[before] == 0x19 && self->out_.data_[before + 1] == a && written == b;
    }
    ei::verify::Check("CPlayerServer::SendPair19", true, ok, "%u %u", a, b);
}
} // namespace

EI_VERIFY(0x0066F0C0, PartyUpdateSendVerify, 0, s_unused);
EI_VERIFY(0x0066F450, ResendPartyVerify, 0, s_unused);
EI_VERIFY(0x0066F840, Pair19Verify, 0, s_unused);
EI_VERIFY(0x0066FAA0, RefreshPartyVerify, 0, s_unused);
EI_VERIFY(0x00670C20, TakeExitVerify, 0, s_unused);
EI_VERIFY(0x00670DA0, JoinedVerify, 0, s_unused);
EI_VERIFY(0x00670E00, LeaveToZoneMessageVerify, 0, s_unused);
EI_VERIFY(0x00670EB0, BriefingDoneVerify, 0, s_unused);
EI_VERIFY(0x00670F50, NewPartyVerify, 0, s_unused);
EI_VERIFY(0x00671060, ResumeVerify, 0, s_unused);
EI_VERIFY(0x0066FFF0, WalkVerify, 0, s_unused);
EI_VERIFY(0x0066FBD0, GoToVerify, 0, s_unused);
EI_VERIFY(0x00670190, AttackVerify, 0, s_unused);
EI_VERIFY(0x00670B60, PartyUpdateVerify, 0, s_unused);
EI_VERIFY(0x0066F310, LeaveToZoneVerify, 0, s_unused);
EI_VERIFY(0x0066F890, LoginVerify, 0, s_unused);
EI_VERIFY(0x0047ADA0, PrintableVerify, 6, s_printable);
EI_VERIFY(0x0066FD70, FormationVerify, 7, s_formation);
EI_VERIFY(0x0066F4C0, LeaderDiedVerify, 0, s_unused);
EI_VERIFY(0x00670310, UseOrderVerify, 0, s_unused);
EI_VERIFY(0x00670670, DistanceVerify, 7, s_distance);
EI_VERIFY(0x00670730, TargetPointVerify, 6, s_targetPoint);
EI_VERIFY(0x006707D0, AimPointVerify, 6, s_aimPoint);
EI_VERIFY(0x006706A0, ConstructVerify, 0, s_unused);
EI_VERIFY(0x00670760, MakeUseVerify, 0, s_unused);
