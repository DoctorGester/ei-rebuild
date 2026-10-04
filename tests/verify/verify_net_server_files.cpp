// Checks for net_server_files.cpp: ours only (it allocates and queues messages and reads the
// buffer). Afterwards the files list holds a file of that name.
#include "net_verify.h"

#include "../../src/game/net_classes.h"

#include <cstring>

namespace {
void* s_unused;

void __fastcall ShareFileVerify(net::ServerSession* self, void*, const net::StringRep* name, GameBuffer* data) {
    // The name may be the caller's temporary; copy it first.
    std::size_t length = static_cast<std::size_t>(name->finish - name->start);
    char copy[260];
    bool fits = length < sizeof(copy);
    if (fits) std::memcpy(copy, name->start, length);
    self->ShareFile(name, data);
    bool found = !fits;
    void** sentinel = static_cast<void**>(self->list_2598);
    for (void** node = static_cast<void**>(sentinel[0]); !found && node != sentinel; node = static_cast<void**>(node[0])) {
        auto* other = reinterpret_cast<const net::StringRep*>(static_cast<std::uint8_t*>(node[2]) + 0x14);
        found = static_cast<std::size_t>(other->finish - other->start) == length && std::memcmp(other->start, copy, length) == 0;
    }
    ei::verify::Check("ServerSession::ShareFile", true, found, "");
}
} // namespace

EI_VERIFY(0x00419A00, ShareFileVerify, 0, s_unused);
