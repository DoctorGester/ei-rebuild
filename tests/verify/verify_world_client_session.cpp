// A-W12: the client world's zone reset (00565090), CWorld::CloseSession (0055F890) and the session
// links (world_client_session.cpp). All ours only (they release objects and replace links), with
// consistency checks afterwards. Reached by every new game (StartZone / ProcessRequest call
// SetPlayer, which runs CloseSession, ClearMaps and ResetZoneObjects) and every frame (the
// application's pump 00566E20 / 00566E70); PlayRecording and JoinGameClient only by replays and
// network games.
#include "verify.h"

#include "../../src/game/net_link.h"
#include "../../src/game/player_messages.h"
#include "../../src/game/world.h"

namespace {
void* s_unused;

CPlayer* LocalPlayer(CWorldClient* self) { return reinterpret_cast<CPlayer*>(self->session_); }

// The second grid's cells are all empty.
bool SecondGridEmpty(CWorldClient* self) {
    SectorGrid& grid = self->secondGrid_;
    for (int x = 0; x < static_cast<int>(grid.width_); ++x)
        for (int y = 0; y < static_cast<int>(grid.height_); ++y)
            if (!grid.rows_[y][x].Empty()) return false;
    return true;
}

void __fastcall ResetZoneObjectsVerify(CWorldClient* self) {
    self->ResetZoneObjects();
    ei::verify::Check("CWorldClient::ResetZoneObjects", true, self->countedA0_.GetSize() == 0 && SecondGridEmpty(self),
                      "a0 %d", self->countedA0_.GetSize());
}

void __fastcall CloseSessionVerify(CWorld* self) {
    self->CloseSession();
    ei::verify::Check("CWorld::CloseSession", true, self->unknown60_ == nullptr, "");
}

// After a reconnect the local player holds a link (null only when the link could not be made) and
// the maps are empty.
void CheckReconnected(const char* name, CWorldClient* self) {
    ei::verify::Check(name, true, self->countedA0_.GetSize() == 0 && SecondGridEmpty(self), "link %p",
                      static_cast<void*>(LocalPlayer(self)->link_));
}

void __fastcall SetPlayerVerify(CWorldClient* self, void*, std::uint8_t* player) {
    self->SetPlayer(player);
    CheckReconnected("CWorldClient::SetPlayer", self);
    ei::verify::Check("CWorldClient::SetPlayer session", true, self->unknown60_ == nullptr, "");
}

void __fastcall PlayRecordingVerify(CWorldClient* self, void*, const char* path) {
    self->PlayRecording(path);
    CheckReconnected("CWorldClient::PlayRecording", self);
}

bool __fastcall JoinGameClientVerify(CWorldClient* self, void*, const char* unused) {
    bool result = self->JoinGameClient(unused);
    if (result) CheckReconnected("CWorldClient::JoinGameClient", self);
    return result;
}

// The answer must be what the link (asked again, without side effects) says.
bool __fastcall PumpHasIncomingVerify(CWorldClient* self) {
    bool result = self->PumpHasIncoming();
    netlink::NetLink* link = LocalPlayer(self)->link_;
    bool expected = link && !(link->flags7_ & CRefCounted::kDetached) ? link->HasIncoming() : false;
    ei::verify::Check("CWorldClient::PumpHasIncoming", expected, result, "link %p", static_cast<void*>(link));
    return result;
}

int __fastcall PumpIncomingCountVerify(CWorldClient* self) {
    int result = self->PumpIncomingCount();
    netlink::NetLink* link = LocalPlayer(self)->link_;
    int expected = link && !(link->flags7_ & CRefCounted::kDetached) ? link->IncomingCount() : 0;
    ei::verify::Check("CWorldClient::PumpIncomingCount", expected, result, "link %p", static_cast<void*>(link));
    return result;
}

} // namespace

EI_VERIFY(0x00565090, ResetZoneObjectsVerify, 0, s_unused);
EI_VERIFY(0x0055F890, CloseSessionVerify, 0, s_unused);
EI_VERIFY(0x00566CE0, SetPlayerVerify, 0, s_unused);
EI_VERIFY(0x00566D30, PlayRecordingVerify, 0, s_unused);
EI_VERIFY(0x00566D80, JoinGameClientVerify, 0, s_unused);
EI_VERIFY(0x00566E20, PumpHasIncomingVerify, 0, s_unused);
EI_VERIFY(0x00566E70, PumpIncomingCountVerify, 0, s_unused);
