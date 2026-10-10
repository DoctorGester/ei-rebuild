// Checks for player_server_orders.cpp (A-H23). The server runs the client's messages every frame:
// 0x04 at every zone start (newgame), 0x40 with the camera, 0x34 / 0x35 / 0x3B from the unit panel
// (ui_battle_commands), 0x32 / 0x33 from casts (spells_cast through the interface), 0x01 when
// leaving. They change units, the world or the session, so they run ours only and check that each
// consumed its message (the received stream's position moved by the message's size, unless the
// read ran out) and returned 1; 0x04 also leaves state 2 and message 0x07 at the end of the
// outgoing stream, 0x40 the position read.
#include "verify.h"

#include "../../src/game/core_streams.h"
#include "../../src/game/player_messages.h"

#include <cstring>

namespace {
void* s_unused;

CMemoryStream& Received() { return *reinterpret_cast<CMemoryStream*>(std::uintptr_t(0x007C83D0)); }

template <typename Handler>
int CheckConsumed(const char* label, CPlayerServer* self, std::uint32_t size, Handler handler) {
    CMemoryStream& in = Received();
    const std::uint32_t position = in.position_;
    const int result = handler();
    const std::uint32_t moved = in.position_ - position;
    ei::verify::Check(label, true, result == 1 && (moved == size || !in.ok_), "server %p: result %d, moved %u", self, result,
                      moved);
    return result;
}

#define CONSUMES(Name, Label, Size)                                                                           \
    int __fastcall Name##Verify(CPlayerServer* self) {                                                        \
        return CheckConsumed(Label, self, Size, [&] { return self->Name(); });                                 \
    }

CONSUMES(OnLeave, "CPlayerServer::OnLeave", 0)
CONSUMES(OnPause, "CPlayerServer::OnPause", 1)
CONSUMES(OnCastAtObject, "CPlayerServer::OnCastAtObject", 0xD)
CONSUMES(OnCastAtPoint, "CPlayerServer::OnCastAtPoint", 0x15)
CONSUMES(OnWantedWeapon, "CPlayerServer::OnWantedWeapon", 5)
CONSUMES(OnMoveStyle, "CPlayerServer::OnMoveStyle", 5)
CONSUMES(OnUnitLookup, "CPlayerServer::OnUnitLookup", 8)
CONSUMES(OnFollow, "CPlayerServer::OnFollow", 8)
CONSUMES(OnUnitFlag438, "CPlayerServer::OnUnitFlag438", 5)

int __fastcall OnGameStateRequestVerify(CPlayerServer* self) {
    const bool joined = self->state_ == 1;
    const std::uint32_t position = Received().position_;
    const int result = self->OnGameStateRequest();
    bool ok;
    if (!joined) {
        ok = result == 0 && Received().position_ == position;
    } else {
        // The last 9 bytes written: type 0x07, the state, the tick.
        const std::uint32_t end = self->out_.position_;
        ok = result == 1 && self->state_ == 2 && end >= 9 && self->out_.data_[end - 9] == 0x07 &&
             Received().position_ - position == 4;
    }
    ei::verify::Check("CPlayerServer::OnGameStateRequest", true, ok, "server %p: result %d state %d", self, result,
                      self->state_);
    return result;
}

int __fastcall OnLastPositionVerify(CPlayerServer* self) {
    std::uint32_t expected[2] = {};
    CMemoryStream& in = Received();
    if (in.position_ + 8 <= in.size_) std::memcpy(expected, in.data_ + in.position_, 8);
    const int result = CheckConsumed("CPlayerServer::OnLastPosition", self, 8, [&] { return self->OnLastPosition(); });
    std::uint32_t actual[2];
    std::memcpy(&actual[0], &self->lastPosition_.x, 4);
    std::memcpy(&actual[1], &self->lastPosition_.y, 4);
    ei::verify::Check("CPlayerServer::OnLastPosition value", true, !in.ok_ || (actual[0] == expected[0] && actual[1] == expected[1]),
                      "server %p", self);
    return result;
}

} // namespace

EI_VERIFY(0x0066FA90, OnLeaveVerify, 0, s_unused);
EI_VERIFY(0x00670CE0, OnGameStateRequestVerify, 0, s_unused);
EI_VERIFY(0x0066FAC0, OnPauseVerify, 0, s_unused);
EI_VERIFY(0x00670820, OnCastAtObjectVerify, 0, s_unused);
EI_VERIFY(0x006708D0, OnCastAtPointVerify, 0, s_unused);
EI_VERIFY(0x00670960, OnWantedWeaponVerify, 0, s_unused);
EI_VERIFY(0x006709D0, OnMoveStyleVerify, 0, s_unused);
EI_VERIFY(0x00670A40, OnUnitLookupVerify, 0, s_unused);
EI_VERIFY(0x00670A90, OnFollowVerify, 0, s_unused);
EI_VERIFY(0x0066FB70, OnUnitFlag438Verify, 0, s_unused);
EI_VERIFY(0x00670B20, OnLastPositionVerify, 0, s_unused);
