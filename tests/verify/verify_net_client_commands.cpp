// Checks for net_client_commands.cpp: ours only (they allocate messages and change the timers
// list and *out).
//  - ClientAddTimed / ClientSendCommand: the timers list ends with an entry for the object /
//    command (the command holding the kind and the text).
//  - ClientDescribe: *out is an entry of kind 4.
#include "net_verify.h"

#include "../../src/game/net_classes.h"
#include "../../src/game/core_refcount.h"

#include <cstring>

namespace net {
void __fastcall ClientAddTimed(ClientSession* self, void*, CRefObject* object, std::uint32_t seconds);
void __fastcall ClientSendCommand(ClientSession* self, void*, int kind, const char* text);
void __fastcall ClientDescribe(ClientSession* self, void*, RefPtr* out);
}

namespace {
void* s_unused;

std::uint8_t* LastTimed(net::ClientSession* self, std::uint32_t* seconds) {
    void** sentinel = static_cast<void**>(self->list_2604);
    auto** last = static_cast<void**>(sentinel[1]);
    if (last == sentinel) return nullptr;
    std::memcpy(seconds, &last[2], 4);
    return static_cast<std::uint8_t*>(last[3]);
}

void __fastcall AddTimedVerify(net::ClientSession* self, void*, CRefObject* object, std::uint32_t seconds) {
    net::ClientAddTimed(self, nullptr, object, seconds);
    std::uint32_t stored = 0;
    LastTimed(self, &stored);
    ei::verify::Check("ClientAddTimed", true, stored == seconds, "");
}

void __fastcall SendCommandVerify(net::ClientSession* self, void*, int kind, const char* text) {
    char copy[256];
    std::size_t length = std::strlen(text);
    bool fits = length < sizeof(copy);
    if (fits) std::memcpy(copy, text, length);
    net::ClientSendCommand(self, nullptr, kind, text);
    std::uint32_t seconds = 0;
    std::uint8_t* command = LastTimed(self, &seconds);
    bool ok = command != nullptr && seconds == 0x42700000u;
    if (ok && fits) {
        int stored;
        std::memcpy(&stored, command + 0xC, 4);
        auto* string = reinterpret_cast<const net::StringRep*>(command + 0x10);
        ok = stored == kind && static_cast<std::size_t>(string->finish - string->start) == length &&
             std::memcmp(string->start, copy, length) == 0;
    }
    ei::verify::Check("ClientSendCommand", true, ok, "kind %d", kind);
}

void __fastcall DescribeVerify(net::ClientSession* self, void*, RefPtr* out) {
    net::ClientDescribe(self, nullptr, out);
    bool ok = out->ptr != nullptr;
    if (ok) {
        std::uint32_t kind;
        std::memcpy(&kind, reinterpret_cast<const std::uint8_t*>(out->ptr) + 0x34, 4);
        ok = kind == 4;
    }
    ei::verify::Check("ClientDescribe", true, ok, "");
}
} // namespace

EI_VERIFY(0x0041D1A0, AddTimedVerify, 0, s_unused);
EI_VERIFY(0x0041D260, SendCommandVerify, 0, s_unused);
EI_VERIFY(0x0041D420, DescribeVerify, 0, s_unused);
