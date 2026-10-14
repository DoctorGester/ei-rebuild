// Checks for player_client_handlers.cpp (A-H23). Every scenario runs the client's messages over
// the loopback link: the game state (0x07) at each zone and screen change, the login (0x00), the
// camera placement (0x04), map and timed objects (0x41, 0x42, 0x50-0x52: effects, spells), the
// world time (0x71), unit control (0x19) and slot 14 for the party's units. The handlers change the
// world, the interface or the camera, so they run ours only and check what they leave:
// - each consumes its message: the stream position moves by the message's size (where fixed) and
//   the result is 1, or 0 / 1 for the handlers that can refuse;
// - the game state handler stores the state it read (unless +0x84 is set) and keeps the old one
//   at the player's +0x104; the login handler leaves state 2; the timed object handlers leave the
//   object filed / gone.
#include "verify.h"

#include "../../src/game/net_classes.h"
#include "../../src/game/net_messages.h"
#include "../../src/game/party.h"
#include "../../src/game/player_client.h"
#include "../../src/game/unit.h"
#include "../../src/game/world.h"

#include <cstring>

namespace {
void* s_unused;

std::uint32_t PendingDword(CPlayerClient* self, std::uint32_t offset = 0) {
    std::uint32_t value = 0;
    if (self->in_.position_ + offset + 4 <= self->in_.size_) std::memcpy(&value, self->in_.data_ + self->in_.position_ + offset, 4);
    return value;
}

// Runs `handler`; the result must be 1 (or 0 when `mayRefuse`) and the stream must have moved by
// `size` bytes (any amount when `size` is 0).
template <typename Handler>
int CheckHandler(const char* label, CPlayerClient* self, std::uint32_t size, bool mayRefuse, Handler handler) {
    const std::uint32_t position = self->in_.position_;
    const int result = handler();
    const std::uint32_t moved = self->in_.position_ - position;
    const bool resultOk = result == 1 || (mayRefuse && result == 0);
    const bool movedOk = size ? moved == size || !self->in_.ok_ : moved > 0 || result == 0;
    ei::verify::Check(label, true, resultOk && movedOk, "client %p: result %d, moved %u", self, result, moved);
    return result;
}

#define FIXED(Name, Label, Size)                                                                              \
    int __fastcall Name##Verify(CPlayerClient* self) {                                                        \
        return CheckHandler(Label, self, Size, false, [&] { return self->Name(); });                           \
    }
#define REFUSING(Name, Label, Size)                                                                           \
    int __fastcall Name##Verify(CPlayerClient* self) {                                                        \
        return CheckHandler(Label, self, Size, true, [&] { return self->Name(); });                            \
    }

FIXED(OnUnitSpeech, "CPlayerClient::OnUnitSpeech", 8)
REFUSING(OnObjectValues, "CPlayerClient::OnObjectValues", 0)
FIXED(OnFreeCamera, "CPlayerClient::OnFreeCamera", 25)
FIXED(OnWorldValue, "CPlayerClient::OnWorldValue", 5)
FIXED(OnItemValue, "CPlayerClient::OnItemValue", 8)
FIXED(OnWeaponRemoved, "CPlayerClient::OnWeaponRemoved", 8)
REFUSING(OnUnitControl, "CPlayerClient::OnUnitControl", 5)
REFUSING(OnObjectChanged, "CPlayerClient::OnObjectChanged", 0)
REFUSING(OnObjectRemoved, "CPlayerClient::OnObjectRemoved", 4)
FIXED(OnUnitDied, "CPlayerClient::OnUnitDied", 4)
REFUSING(OnTimedObjectChanged, "CPlayerClient::OnTimedObjectChanged", 0)
FIXED(OnWeather, "CPlayerClient::OnWeather", 3)
FIXED(OnWorldTime, "CPlayerClient::OnWorldTime", 8)
FIXED(OnWorldPoint, "CPlayerClient::OnWorldPoint", 16)

// OnDisconnect / OnGameLost read nothing; OnWorldStream leaves the reading to the client world.
int __fastcall OnDisconnectExactVerify(CPlayerClient* self) {
    const std::uint32_t position = self->in_.position_;
    const int result = self->OnDisconnect();
    ei::verify::Check("CPlayerClient::OnDisconnect", true, result == 1 && self->in_.position_ == position, "client %p", self);
    return result;
}
int __fastcall OnGameLostExactVerify(CPlayerClient* self) {
    const std::uint32_t position = self->in_.position_;
    const int result = self->OnGameLost();
    ei::verify::Check("CPlayerClient::OnGameLost", true, result == 1 && self->in_.position_ == position, "client %p", self);
    return result;
}

int __fastcall OnWorldStreamVerify(CPlayerClient* self) {
    const int result = self->OnWorldStream();
    ei::verify::Check("CPlayerClient::OnWorldStream", 1, result, "client %p", self);
    return result;
}

int __fastcall OnLoginAcceptedVerify(CPlayerClient* self) {
    const int result = CheckHandler("CPlayerClient::OnLoginAccepted", self, 4, false, [&] { return self->OnLoginAccepted(); });
    ei::verify::Check("CPlayerClient::OnLoginAccepted state", 2, self->state_, "client %p", self);
    return result;
}

int __fastcall OnCameraPlacementVerify(CPlayerClient* self) {
    const std::uint32_t position = self->in_.position_;
    const int result = self->OnCameraPlacement();
    const bool ok = self->in_.position_ - position == 20 && result == 4;
    ei::verify::Check("CPlayerClient::OnCameraPlacement", true, ok || !self->in_.ok_, "client %p: result %d", self, result);
    return result;
}

int __fastcall OnGameStateVerify(CPlayerClient* self) {
    CPlayer* player = self->Owner();
    const int before = player->sessionKind100_;
    const int state = static_cast<int>(PendingDword(self));
    const bool keyOnly = self->unknown_84 != 0;
    const int result = CheckHandler("CPlayerClient::OnGameState", self, 8, false, [&] { return self->OnGameState(); });
    const bool ok = player->unknown_104 == static_cast<std::uint32_t>(before) &&
                    player->sessionKind100_ == (keyOnly ? before : state);
    ei::verify::Check("CPlayerClient::OnGameState state", true, ok, "client %p: state %d (was %d), stored %d", self, state,
                      before, player->sessionKind100_);
    return result;
}

int __fastcall OnTimedObjectAddedVerify(CPlayerClient* self) {
    const int id = static_cast<int>(PendingDword(self));
    const int result = CheckHandler("CPlayerClient::OnTimedObjectAdded", self, 0, true, [&] { return self->OnTimedObjectAdded(); });
    CPlayer* player = self->Owner();
    const bool ok = result == 0 || (player->knownTimed_.Find(id) != nullptr && player->maxTimedId_ >= id);
    ei::verify::Check("CPlayerClient::OnTimedObjectAdded filed", true, ok, "client %p: id %d", self, id);
    return result;
}

int __fastcall OnTimedObjectRemovedVerify(CPlayerClient* self) {
    const int id = static_cast<int>(PendingDword(self));
    const int result = CheckHandler("CPlayerClient::OnTimedObjectRemoved", self, 4, true, [&] { return self->OnTimedObjectRemoved(); });
    ei::verify::Check("CPlayerClient::OnTimedObjectRemoved gone", true, self->Owner()->knownTimed_.Find(id) == nullptr,
                      "client %p: id %d", self, id);
    return result;
}

// Slot 14: a multiplayer client whose party is this one unit keeps its id (-1 when it left) in the
// login record; otherwise the record is left alone.
void __fastcall NotifyUnitChangedVerify(CPlayerClient* self, void*, CUnit* unit) {
    net::LoginMessage* login = nullptr;
    if (IsMultiplayerClient()) login = static_cast<net::LoginMessage*>(net::GameClient()->player_.ptr);
    const std::int32_t idBefore = login ? login->id_ : 0;
    const std::uint32_t versionBefore = login ? login->version_ : 0;
    self->NotifyUnitChanged(unit);
    bool ok = true;
    if (login) {
        CParty* party = self->Owner()->Party();
        const bool leader = party->members_.size == 1 && static_cast<int>(party->members_.data[0].unitId) == unit->id_;
        ok = leader ? login->id_ == (unit->player ? unit->id_ : -1) && login->version_ == versionBefore + 1
                    : login->id_ == idBefore && login->version_ == versionBefore;
    }
    ei::verify::Check("CPlayerClient::NotifyUnitChanged", true, ok, "client %p unit %p", self, unit);
}

} // namespace

EI_VERIFY(0x00667DA0, OnLoginAcceptedVerify, 0, s_unused);
EI_VERIFY(0x00667DE0, OnDisconnectExactVerify, 0, s_unused);
EI_VERIFY(0x00667C20, OnCameraPlacementVerify, 0, s_unused);
EI_VERIFY(0x00669D50, OnGameStateVerify, 0, s_unused);
EI_VERIFY(0x00669F30, OnGameLostExactVerify, 0, s_unused);
EI_VERIFY(0x00667DF0, OnUnitSpeechVerify, 0, s_unused);
EI_VERIFY(0x006696A0, OnObjectValuesVerify, 0, s_unused);
EI_VERIFY(0x00667D10, OnFreeCameraVerify, 0, s_unused);
EI_VERIFY(0x00667E20, OnWorldValueVerify, 0, s_unused);
EI_VERIFY(0x0066A420, OnItemValueVerify, 0, s_unused);
EI_VERIFY(0x00669400, OnWeaponRemovedVerify, 0, s_unused);
EI_VERIFY(0x0066A620, OnUnitControlVerify, 0, s_unused);
EI_VERIFY(0x006687B0, OnObjectChangedVerify, 0, s_unused);
EI_VERIFY(0x00668850, OnObjectRemovedVerify, 0, s_unused);
EI_VERIFY(0x0066A3B0, OnUnitDiedVerify, 0, s_unused);
EI_VERIFY(0x00668940, OnTimedObjectAddedVerify, 0, s_unused);
EI_VERIFY(0x00668A30, OnTimedObjectChangedVerify, 0, s_unused);
EI_VERIFY(0x00668AA0, OnTimedObjectRemovedVerify, 0, s_unused);
EI_VERIFY(0x00669760, OnWeatherVerify, 0, s_unused);
EI_VERIFY(0x006697B0, OnWorldTimeVerify, 0, s_unused);
EI_VERIFY(0x00669800, OnWorldPointVerify, 0, s_unused);
EI_VERIFY(0x0066A400, OnWorldStreamVerify, 0, s_unused);
EI_VERIFY(0x00667BA0, NotifyUnitChangedVerify, 0, s_unused);
