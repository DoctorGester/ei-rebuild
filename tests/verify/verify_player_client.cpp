// Checks for player_client.cpp (scenario: every one; the local client talks to the server over the
// loopback link every frame). All four use the link, the coders' windows or run the message
// handlers, so they run ours only:
// - SendToServer: the result is 0 or 1, the queue is empty after (when there is a link).
// - DispatchMessages: 1 (the errors are fatal).
// - ReceiveMessages: 0 or 1.
// - Pump: 0 or 1; a connecting client with a connected link is logged in after.
#include "verify.h"

#include "../../src/game/net_link.h"
#include "../../src/game/originals_engine.h"
#include "../../src/game/party.h"
#include "../../src/game/player_client.h"

#include <cstring>
#include <string>

namespace {
void* s_unused;

int __fastcall SendToServerVerify(CPlayerClient* self) {
    bool linked = self->Owner()->link_ != nullptr;
    int result = self->SendToServer();
    bool ok = (result == 0 || result == 1) && (!linked || self->out_.size_ == 0);
    ei::verify::Check("CPlayerClient::SendToServer", true, ok, "client %p: result %d", self, result);
    return result;
}

int __fastcall DispatchVerify(CPlayerClient* self) {
    int result = self->DispatchMessages();
    ei::verify::Check("CPlayerClient::DispatchMessages", 1, result, "client %p", self);
    return result;
}

int __fastcall ReceiveVerify(CPlayerClient* self) {
    int result = self->ReceiveMessages();
    ei::verify::Check("CPlayerClient::ReceiveMessages", true, result == 0 || result == 1, "client %p: result %d", self,
                      result);
    return result;
}

int __fastcall PumpVerify(CPlayerClient* self) {
    netlink::NetLink* link = self->Owner()->link_;
    bool loggingIn = self->state_ == 0 && link != nullptr && link->Connected();
    int result = self->Pump();
    bool ok = (result == 0 || result == 1) && (!loggingIn || self->state_ == 1);
    ei::verify::Check("CPlayerClient::Pump", true, ok, "client %p: result %d state %d", self, result, self->state_);
    return result;
}

// 00667D80: PUSH ECX; PUSH ESI; MOV ESI,[ESP+0Ch] (6). Only the stream's position and flags move:
// ours, stream restored, original; text and position compared.
void*(__fastcall* s_readText)(CPlayerClient*, void*, void*);
void* __fastcall ReadTextVerify(CPlayerClient* self, void*, void* out) {
    BitStream before;
    std::memcpy(&before, &self->in_, sizeof(before));
    alignas(4) unsigned char ours[sizeof(mfc::CString)];
    self->ReadText(ours);
    std::uint32_t oursPosition = self->in_.position_;
    std::memcpy(&self->in_, &before, sizeof(before));
    void* result = s_readText(self, nullptr, out);
    auto* text = reinterpret_cast<mfc::CString*>(ours);
    bool ok = result == out && oursPosition == self->in_.position_ &&
              std::strcmp(*text, *static_cast<mfc::CString*>(out)) == 0;
    ei::verify::Check("CPlayerClient::ReadText", true, ok, "client %p: position %u vs %u", self, self->in_.position_,
                      oursPosition);
    text->~CString();
    return result;
}

// The handlers change the variables, the music or the zone: ours only. Each returns 1 and moves the
// stream on; a set variable then reads back as the value sent.
const char* PendingText(CPlayerClient* self) {
    return reinterpret_cast<const char*>(self->in_.data_ + self->in_.position_);
}

int __fastcall SetVariableVerify(CPlayerClient* self) {
    std::string name = PendingText(self);
    std::uint32_t value;
    std::memcpy(&value, PendingText(self) + name.size() + 1, 4);
    int result = self->OnSetVariable();
    float now = NamedTableGet(&self->Owner()->Party()->variables_, nullptr, name.c_str());
    std::uint32_t bits;
    std::memcpy(&bits, &now, 4);
    ei::verify::Check("CPlayerClient::OnSetVariable", true, result == 1 && bits == value, "client %p: %s = %08X vs %08X",
                      self, name.c_str(), value, bits);
    return result;
}

template <typename Handler>
int CheckHandler(const char* label, CPlayerClient* self, Handler handler) {
    std::uint32_t position = self->in_.position_;
    int result = handler();
    ei::verify::Check(label, true, result == 1 && self->in_.position_ > position, "client %p", self);
    return result;
}
int __fastcall DeleteVariableVerify(CPlayerClient* self) {
    return CheckHandler("CPlayerClient::OnDeleteVariable", self, [&] { return self->OnDeleteVariable(); });
}
int __fastcall ZoneChangeVerify(CPlayerClient* self) {
    return CheckHandler("CPlayerClient::OnZoneChange", self, [&] { return self->OnZoneChange(); });
}
int __fastcall JingleVerify(CPlayerClient* self) {
    return CheckHandler("CPlayerClient::OnJingle", self, [&] { return self->OnJingle(); });
}

// The block readers (the world loads them; ours only): 1 and the stream moved, or 0.
template <typename Handler>
int CheckBlock(const char* label, CPlayerClient* self, Handler handler) {
    std::uint32_t position = self->in_.position_;
    int result = handler();
    ei::verify::Check(label, true, result == 0 || (result == 1 && self->in_.position_ > position), "client %p: %d", self,
                      result);
    return result;
}
int __fastcall ZoneVerify(CPlayerClient* self) {
    return CheckBlock("CPlayerClient::OnZone", self, [&] { return self->OnZone(); });
}
int __fastcall WorldBlock3Verify(CPlayerClient* self) {
    return CheckBlock("CPlayerClient::OnWorldBlock3", self, [&] { return self->OnWorldBlock3(); });
}
int __fastcall WorldBlock5Verify(CPlayerClient* self) {
    return CheckBlock("CPlayerClient::OnWorldBlock5", self, [&] { return self->OnWorldBlock5(); });
}
int __fastcall ItemRemovedVerify(CPlayerClient* self) {
    return CheckHandler("CPlayerClient::OnItemRemoved", self, [&] { return self->OnItemRemoved(); });
}

int __fastcall QuestItemsVerify(CPlayerClient* self) {
    int result = self->OnQuestItemsChanged();
    ei::verify::Check("CPlayerClient::OnQuestItemsChanged", 1, result, "client %p", self);
    return result;
}
int __fastcall ExperienceAllVerify(CPlayerClient* self) {
    return CheckHandler("CPlayerClient::OnExperienceAll", self, [&] { return self->OnExperienceAll(); });
}
int __fastcall ExperienceVerify(CPlayerClient* self) {
    return CheckHandler("CPlayerClient::OnExperience", self, [&] { return self->OnExperience(); });
}

// A-H10 (ours only: they load the party, give items, make particle sources, record).
int __fastcall ItemsReceivedVerify(CPlayerClient* self) {
    return CheckBlock("CPlayerClient::OnItemsReceived", self, [&] { return self->OnItemsReceived(); });
}
int __fastcall PartyVerify(CPlayerClient* self) {
    return CheckHandler("CPlayerClient::OnParty", self, [&] { return self->OnParty(); });
}
int __fastcall PartyUpdateVerify(CPlayerClient* self) {
    return CheckHandler("CPlayerClient::OnPartyUpdate", self, [&] { return self->OnPartyUpdate(); });
}
int __fastcall PathMarkerVerify(CPlayerClient* self) {
    return CheckHandler("CPlayerClient::OnPathMarker", self, [&] { return self->OnPathMarker(); });
}
// Ours only (commits the coders' output, frees the array): the coders start over, everything is
// empty.
void __fastcall CoderResetVerify(CoderState* self) {
    self->Reset();
    bool ok = self->matchPos == -1 && self->pos == 0 && self->out.sink == nullptr && self->out.bits == 0;
    ei::verify::Check("CoderState::Reset", true, ok, "coder %p", self);
}
void __fastcall ClearVerify(CPlayerClient* self) {
    self->Clear();
    bool ok = self->state_ == 0 && self->array50D8_.data == nullptr && self->array50D8_.size == 0 &&
              self->in_.size_ == 0 && self->out_.size_ == 0 && self->sendParty_ == 0 && self->compressor_.matchPos == -1;
    ei::verify::Check("CPlayerClient::Clear", true, ok, "client %p", self);
}
// Multiplayer only (no scenario): 1 and the stream moved, or 0.
int __fastcall NewObjectVerify(CPlayerClient* self) {
    return CheckBlock("CPlayerClient::OnNewObject", self, [&] { return self->OnNewObject(); });
}
int __fastcall StringEventVerify(CPlayerClient* self) {
    return CheckHandler("CPlayerClient::OnStringEvent", self, [&] { return self->OnStringEvent(); });
}
// With a link one request byte is queued.
void __fastcall StartRecordingVerify(CPlayerClient* self, void*, const char* path) {
    bool linked = self->Owner()->link_ != nullptr;
    std::uint32_t size = self->out_.size_;
    self->StartRecording(path);
    ei::verify::Check("CPlayerClient::StartRecording", true, self->out_.size_ == size + (linked ? 1u : 0u),
                      "client %p", self);
}
void __fastcall StopRecordingVerify(CPlayerClient* self) {
    self->StopRecording();
    ei::verify::Check("CPlayerClient::StopRecording", true, true, "client %p", self);
}

} // namespace

EI_VERIFY(0x00669490, ItemsReceivedVerify, 0, s_unused);
EI_VERIFY(0x00669A30, PartyVerify, 0, s_unused);
EI_VERIFY(0x00669C90, PartyUpdateVerify, 0, s_unused);
EI_VERIFY(0x00669FA0, PathMarkerVerify, 0, s_unused);
EI_VERIFY(0x00667E60, StringEventVerify, 0, s_unused);
EI_VERIFY(0x00667EC0, NewObjectVerify, 0, s_unused);
EI_VERIFY(0x006671A0, CoderResetVerify, 0, s_unused);
EI_VERIFY(0x00667510, ClearVerify, 0, s_unused);
EI_VERIFY(0x00667600, ClearVerify, 0, s_unused);
EI_VERIFY(0x00669710, StartRecordingVerify, 0, s_unused);
EI_VERIFY(0x00669750, StopRecordingVerify, 0, s_unused);

EI_VERIFY(0x00669350, QuestItemsVerify, 0, s_unused);
EI_VERIFY(0x0066A480, ExperienceAllVerify, 0, s_unused);
EI_VERIFY(0x0066A5A0, ExperienceVerify, 0, s_unused);
EI_VERIFY(0x00669010, ZoneVerify, 0, s_unused);
EI_VERIFY(0x006690F0, WorldBlock3Verify, 0, s_unused);
EI_VERIFY(0x006691C0, WorldBlock5Verify, 0, s_unused);
EI_VERIFY(0x00669290, ItemRemovedVerify, 0, s_unused);
EI_VERIFY(0x00667D80, ReadTextVerify, 6, s_readText);
EI_VERIFY(0x00669850, SetVariableVerify, 0, s_unused);
EI_VERIFY(0x00669960, DeleteVariableVerify, 0, s_unused);
EI_VERIFY(0x00669EB0, ZoneChangeVerify, 0, s_unused);
EI_VERIFY(0x00669F40, JingleVerify, 0, s_unused);
EI_VERIFY(0x00667810, SendToServerVerify, 0, s_unused);
EI_VERIFY(0x00667970, DispatchVerify, 0, s_unused);
EI_VERIFY(0x00667A30, ReceiveVerify, 0, s_unused);
EI_VERIFY(0x00667680, PumpVerify, 0, s_unused);
