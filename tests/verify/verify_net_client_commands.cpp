// Checks for net_client_commands.cpp: ours only (they allocate messages and change the timers
// list and *out).
//  - ClientAddTimed / ClientSendCommand: the timers list ends with an entry for the object /
//    command (the command holding the kind and the text).
//  - ClientDescribe: *out is an entry of kind 4.
//  - ClientBeginGame: ours only (it starts the game).
//  - ClientBufferRatio: side by side for the game's two buffer classes when the first dword lies
//    in memory (the original on a copy of the 0x1C-byte header: Seek(0) and the read only move
//    its pointers), within a few ULPs (plain C++ division, rewrite-guide rule 7), and the headers
//    must end equal; ours only otherwise.
#include "net_verify.h"

#include "../../src/game/net_classes.h"
#include "../../src/game/core_refcount.h"
#include "../../src/game/net_coder.h"

#include <cstring>

namespace net {
double __fastcall ClientBufferRatio(ClientSession* self, void*, GameBuffer* buffer);
}

namespace {
void* s_unused;

std::uint8_t* LastTimed(net::ClientSession* self, std::uint32_t* seconds) {
    void** sentinel = static_cast<void**>(self->timers_);
    auto** last = static_cast<void**>(sentinel[1]);
    if (last == sentinel) return nullptr;
    std::memcpy(seconds, &last[2], 4);
    return static_cast<std::uint8_t*>(last[3]);
}

void __fastcall AddTimedVerify(net::ClientSession* self, void*, CRefObject* object, std::uint32_t seconds) {
    self->AddTimed(object, seconds);
    std::uint32_t stored = 0;
    LastTimed(self, &stored);
    ei::verify::Check("ClientAddTimed", true, stored == seconds, "");
}

void __fastcall SendCommandVerify(net::ClientSession* self, void*, int kind, const char* text) {
    char copy[256];
    std::size_t length = std::strlen(text);
    bool fits = length < sizeof(copy);
    if (fits) std::memcpy(copy, text, length);
    self->SendCommand(kind, text);
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
    self->ClientSession::CreateEntry(out);
    bool ok = out->ptr != nullptr;
    if (ok) {
        std::uint32_t kind;
        std::memcpy(&kind, reinterpret_cast<const std::uint8_t*>(out->ptr) + 0x34, 4);
        ok = kind == 4;
    }
    ei::verify::Check("ClientDescribe", true, ok, "");
}
// 0041D9A0: PUSH ECX (1), PUSH ESI (1), MOV ESI,[ESP+0xC] (4).
double(__fastcall* s_bufferRatio)(net::ClientSession*, void*, GameBuffer*);
double __fastcall BufferRatioVerify(net::ClientSession* self, void*, GameBuffer* buffer) {
    std::uintptr_t vtable;
    std::memcpy(&vtable, buffer, 4);
    std::uint8_t* first = buffer->begin + 4;
    bool known = vtable == kGrowingBufferVtable || vtable == kBufferViewVtable;
    if (!known || first > buffer->end || first > (buffer->cur > buffer->high ? buffer->cur : buffer->high)) {
        double result = net::ClientBufferRatio(self, nullptr, buffer);
        ei::verify::Check("ClientBufferRatio (ours only)", true, true, "");
        return result;
    }
    GameBuffer copy = *buffer;
    double expected = s_bufferRatio(self, nullptr, &copy);
    double actual = net::ClientBufferRatio(self, nullptr, buffer);
    ei::verify::CheckNear("ClientBufferRatio", expected, actual, "");
    ei::verify::Check("ClientBufferRatio header", true, std::memcmp(&copy, buffer, sizeof(copy)) == 0, "");
    return actual;
}
void __fastcall BeginGameVerify(net::ClientSession* self) {
    self->ClientSession::OnJoined();
    ei::verify::Check("ClientBeginGame", true, true, "");
}
} // namespace

EI_VERIFY(0x0041D1A0, AddTimedVerify, 0, s_unused);
EI_VERIFY(0x0041D260, SendCommandVerify, 0, s_unused);
EI_VERIFY(0x0041D420, DescribeVerify, 0, s_unused);
EI_VERIFY(0x0041D9A0, BufferRatioVerify, 6, s_bufferRatio);
EI_VERIFY(0x0041D410, BeginGameVerify, 0, s_unused);
