// Checks for net_server_players.cpp.
//  - The lookups (LeaderId, FindGuestConnection, FindConnectionBySerial, FindPlayer): side by side
//    (they only read).
//  - Admit, SetPassword, AnnounceToGuests, Kick, Ban and the departure reports: ours only (they
//    move the password pointer, write the password, queue lines and remove clients).
#include "net_verify.h"

#include "../../src/game/net_classes.h"

#include <cstring>

namespace {
void* s_unused;

// 0041A020: MOV ECX,[ECX+0x2590] (6).
int(__fastcall* s_leader)(net::ServerSession*);
int __fastcall LeaderVerify(net::ServerSession* self) {
    int expected = s_leader(self);
    int actual = self->LeaderId();
    ei::verify::Check("ServerSession::LeaderId", expected, actual, "");
    return actual;
}

// 0041A170: PUSH ECX, PUSH EBX, PUSH EBP, PUSH ESI, PUSH EDI (1 each).
void*(__fastcall* s_guest)(net::ServerSession*, void*, const net::StringRep*);
void* __fastcall GuestVerify(net::ServerSession* self, void*, const net::StringRep* name) {
    void* expected = s_guest(self, nullptr, name);
    void* actual = self->FindGuestConnection(name);
    ei::verify::Check("ServerSession::FindGuestConnection", expected, actual, "");
    return actual;
}

// 0041A270: MOV ECX,[ECX+0x25A4] (6).
void*(__fastcall* s_bySerial)(net::ServerSession*, void*, int);
void* __fastcall BySerialVerify(net::ServerSession* self, void*, int serial) {
    void* expected = s_bySerial(self, nullptr, serial);
    void* actual = self->FindConnectionBySerial(serial);
    ei::verify::Check("ServerSession::FindConnectionBySerial", expected, actual, "");
    return actual;
}

// 0041A2E0: MOV ECX,[ECX+0x2590] (6).
void*(__fastcall* s_player)(net::ServerSession*, void*, int);
void* __fastcall PlayerVerify(net::ServerSession* self, void*, int id) {
    void* expected = s_player(self, nullptr, id);
    void* actual = self->FindPlayer(id);
    ei::verify::Check("ServerSession::FindPlayer", expected, actual, "");
    return actual;
}

bool __fastcall AdmitVerify(net::ServerSession* self, void*, std::uint32_t address, const std::uint8_t** password, std::uint8_t* reason) {
    bool result = self->Admit(address, password, reason);
    ei::verify::Check("ServerSession::Admit", true, result ? *reason == 1 : (*reason >= 1 && *reason <= 3), "");
    return result;
}

void __fastcall SetPasswordVerify(net::ServerSession* self, void*, const char* text) {
    self->SetPassword(text);
    ei::verify::Check("ServerSession::SetPassword", true,
                      std::strncmp(reinterpret_cast<const char*>(self->unknown_25E8), text, 0x14) == 0, "");
}

void __fastcall AnnounceToGuestsVerify(net::ServerSession* self, void*, int kind, const char* text) {
    self->AnnounceToGuests(kind, text);
    ei::verify::Check("ServerSession::AnnounceToGuests", true, true, "");
}
void __fastcall KickVerify(net::ServerSession* self, void*, void* connection) {
    self->Kick(connection);
    ei::verify::Check("ServerSession::Kick", true, true, "");
}
void __fastcall BanVerify(net::ServerSession* self, void*, void* connection) {
    self->Ban(connection);
    ei::verify::Check("ServerSession::Ban", true, true, "");
}
void __fastcall LostVerify(net::ServerSession* self, void*, void* connection) {
    self->ReportDeparture(connection, 4);
    ei::verify::Check("ServerSession::ReportDeparture(lost)", true, true, "");
}
void __fastcall LeftVerify(net::ServerSession* self, void*, void* connection) {
    self->ReportDeparture(connection, 1);
    ei::verify::Check("ServerSession::ReportDeparture(left)", true, true, "");
}
} // namespace

EI_VERIFY(0x0041A020, LeaderVerify, 6, s_leader);
EI_VERIFY(0x0041A170, GuestVerify, 5, s_guest);
EI_VERIFY(0x0041A270, BySerialVerify, 6, s_bySerial);
EI_VERIFY(0x0041A2E0, PlayerVerify, 6, s_player);
EI_VERIFY(0x0041A050, AdmitVerify, 0, s_unused);
EI_VERIFY(0x0041A140, SetPasswordVerify, 0, s_unused);
EI_VERIFY(0x0041A500, AnnounceToGuestsVerify, 0, s_unused);
EI_VERIFY(0x0041A550, KickVerify, 0, s_unused);
EI_VERIFY(0x0041A620, BanVerify, 0, s_unused);
EI_VERIFY(0x0041A700, LostVerify, 0, s_unused);
EI_VERIFY(0x0041A770, LeftVerify, 0, s_unused);
