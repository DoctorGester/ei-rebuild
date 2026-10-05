// Checks for net_server_chat.cpp: ours only (it allocates and queues a line). Afterwards the last
// line holds the text and the previous line number, and there are at most 50 lines.
#include "net_verify.h"

#include "../../src/game/net_classes.h"

#include <cstring>

namespace {
void* s_unused;

bool LastLineIs(net::ServerSession* self, const char* text, std::size_t length, std::uint32_t number) {
    void** sentinel = static_cast<void**>(self->lines_);
    std::uint32_t count = 0;
    for (void** node = static_cast<void**>(sentinel[0]); node != sentinel; node = static_cast<void**>(node[0])) ++count;
    auto* line = static_cast<const std::uint8_t*>(static_cast<void**>(sentinel[1])[2]);
    auto* stored = reinterpret_cast<const net::StringRep*>(line + 0x10);
    std::uint32_t stamp;
    std::memcpy(&stamp, line + 0xC, 4);
    return count <= 50 && stamp == number && static_cast<std::size_t>(stored->finish - stored->start) == length &&
           std::memcmp(stored->start, text, length) == 0;
}

void __fastcall AnnounceVerify(net::ServerSession* self, void*, const char* text, CRefObject* player) {
    char copy[512];
    std::size_t length = std::strlen(text);
    bool fits = length < sizeof(copy);
    if (fits) std::memcpy(copy, text, length);
    std::uint32_t number = self->nextLine_;
    self->Announce(text, player);
    ei::verify::Check("ServerSession::Announce", true, !fits || LastLineIs(self, copy, length, number), "");
}

void __fastcall AnnounceTextVerify(net::ServerSession* self, void*, int kind, const char* text, CRefObject* player) {
    std::uint32_t number = self->nextLine_;
    self->AnnounceText(kind, text, player);
    ei::verify::Check("ServerSession::AnnounceText", true, self->nextLine_ == number + 1, "");
}
} // namespace

EI_VERIFY(0x0041A310, AnnounceVerify, 0, s_unused);
EI_VERIFY(0x0041A490, AnnounceTextVerify, 0, s_unused);
