// Checks for net_session_pump.cpp. The record constructor (00477E60): the original builds the
// real object, ours the same bytes in a copy taken before (the CFile constructor allocates
// nothing); the 0x28 bytes are compared. The pump and the start (ours only): afterwards every
// driver of the owning list has a live player; after a start that list holds just the returned
// driver, for the given player, and the holders' list is empty.
#include "verify.h"

#include "../../src/game/core_refcount.h"

#include <cstdint>
#include <cstring>
#include <string>

namespace netsession {
void* __fastcall RecordConstruct(std::uint8_t* self);
struct Session;
void __fastcall SessionPump(Session* self, void*, CRefObject* world);
CRefObject* __fastcall SessionStart(Session* self, void*, CRefObject* player);
} // namespace netsession

namespace {
void* s_unused;

struct ListNode {
    ListNode* next;
    ListNode* prev;
    CRefObject* value;
};
ListNode* List(void* session, std::size_t offset) {
    return *reinterpret_cast<ListNode**>(static_cast<std::uint8_t*>(session) + offset);
}
CRefObject* PlayerOf(CRefObject* driver) {
    return *reinterpret_cast<CRefObject**>(reinterpret_cast<std::uint8_t*>(driver) + 0x50);
}
bool Alive(const CRefObject* o) { return o && !(o->flags7_ & CRefObject::kDetached); }

// 00477E60: PUSH -1 (2), PUSH imm32 (5).
void*(__fastcall* s_record)(std::uint8_t*);
void* __fastcall RecordVerify(std::uint8_t* self) {
    alignas(8) std::uint8_t copy[0x28];
    std::memcpy(copy, self, sizeof(copy));
    void* result = s_record(self);
    netsession::RecordConstruct(copy);
    std::size_t bad = 0;
    bool ok = result == self;
    for (std::size_t o = 0; ok && o < sizeof(copy); ++o)
        if (copy[o] != self[o]) {
            ok = false;
            bad = o;
        }
    ei::verify::Check("RecordConstruct", true, ok, std::to_string(bad).c_str());
    return result;
}

void __fastcall PumpVerify(netsession::Session* self, void*, CRefObject* world) {
    netsession::SessionPump(self, nullptr, world);
    bool ok = true;
    for (ListNode* n = List(self, 8)->next; ok && n != List(self, 8); n = n->next) ok = Alive(PlayerOf(n->value));
    ei::verify::Check("SessionPump", true, ok, "");
}

CRefObject* __fastcall StartVerify(netsession::Session* self, void*, CRefObject* player) {
    CRefObject* driver = netsession::SessionStart(self, nullptr, player);
    ListNode* drivers = List(self, 8);
    ListNode* holders = List(self, 0xC);
    bool ok = drivers->next != drivers && drivers->next->next == drivers && drivers->next->value == driver &&
              PlayerOf(driver) == player && holders->next == holders;
    ei::verify::Check("SessionStart", true, ok, "");
    return driver;
}
} // namespace

EI_VERIFY(0x00477E60, RecordVerify, 7, s_record);
EI_VERIFY(0x00479E30, PumpVerify, 0, s_unused);
EI_VERIFY(0x0047A080, StartVerify, 0, s_unused);
