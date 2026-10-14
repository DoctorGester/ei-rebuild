// Checks for net_session_pump.cpp and the link base's constructor. The base constructor (00477E60):
// the original builds the real object, ours the same bytes in a copy taken before (the CFile
// constructor allocates nothing); the 0x28 bytes are compared. The pump and the start (ours only):
// afterwards every driver of the owning list has a live player; after a start that list holds just
// the returned driver, for the given player, and the holders' list is empty.
#include "verify.h"

#include "../../src/game/net_link.h"

#include <cstdint>
#include <cstring>
#include <string>

namespace {

using netlink::DriverNode;
using netlink::DriverSession;
using netlink::NetLink;

void* s_unused;

bool Alive(const CRefObject* o) { return o && !(o->flags7_ & CRefObject::kDetached); }

// 00477E60: PUSH -1 (2), PUSH imm32 (5).
NetLink*(__fastcall* s_record)(NetLink*);
NetLink* __fastcall RecordVerify(NetLink* self) {
    alignas(8) std::uint8_t copy[sizeof(NetLink)];
    std::memcpy(copy, self, sizeof(copy));
    NetLink* result = s_record(self);
    reinterpret_cast<NetLink*>(copy)->Construct();
    const auto* real = reinterpret_cast<const std::uint8_t*>(self);
    std::size_t bad = 0;
    bool ok = result == self;
    for (std::size_t o = 0; ok && o < sizeof(copy); ++o)
        if (copy[o] != real[o]) {
            ok = false;
            bad = o;
        }
    ei::verify::Check("RecordConstruct", true, ok, std::to_string(bad).c_str());
    return result;
}

void __fastcall PumpVerify(DriverSession* self, void*, net::SessionBase* session) {
    self->PumpDrivers(session);
    bool ok = true;
    for (DriverNode* n = self->drivers_->next; ok && n != self->drivers_; n = n->next) ok = Alive(n->Driver()->player_);
    ei::verify::Check("SessionPump", true, ok, "");
}

netlink::DriverLink* __fastcall StartVerify(DriverSession* self, void*, net::Connection* player) {
    netlink::DriverLink* driver = self->Start(player);
    DriverNode* drivers = self->drivers_;
    DriverNode* holders = self->holders_;
    bool ok = drivers->next != drivers && drivers->next->next == drivers && drivers->next->Driver() == driver &&
              driver->player_ == player && holders->next == holders;
    ei::verify::Check("SessionStart", true, ok, "");
    return driver;
}
} // namespace

EI_VERIFY(0x00477E60, RecordVerify, 7, s_record);
EI_VERIFY(0x00479E30, PumpVerify, 0, s_unused);
EI_VERIFY(0x0047A080, StartVerify, 0, s_unused);
