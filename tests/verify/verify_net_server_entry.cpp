// Checks for net_server_entry.cpp: ours only (it allocates the entry and replaces *out). The entry
// carries the server's name, kind and limits afterwards.
#include "net_verify.h"

#include "../../src/game/net_classes.h"
#include "../../src/game/core_refcount.h"

#include <cstring>

namespace {
void* s_unused;

void __fastcall DescribeVerify(net::ServerSession* self, void*, RefPtr* out) {
    self->ServerSession::Describe(out);
    auto* entry = reinterpret_cast<const std::uint8_t*>(out->ptr);
    bool ok = entry != nullptr;
    if (ok) {
        auto* name = reinterpret_cast<const net::StringRep*>(entry + 0x28);
        std::size_t length = static_cast<std::size_t>(self->name_.finish - self->name_.start);
        std::uint32_t kind;
        std::memcpy(&kind, entry + 0x34, 4);
        ok = static_cast<std::size_t>(name->finish - name->start) == length &&
             std::memcmp(name->start, self->name_.start, length) == 0 && kind == self->kind_ &&
             entry[0x45] == static_cast<std::uint8_t>(self->maxPlayers_);
    }
    ei::verify::Check("ServerSession::Describe", true, ok, "");
}

void __fastcall SetPublicVerify(net::ServerSession* self, void*, bool announce) {
    self->SetPublic(announce);
    ei::verify::Check("ServerSession::SetPublic", true, self->public_ == static_cast<std::uint8_t>(announce), "");
}
} // namespace

EI_VERIFY(0x0041A7E0, DescribeVerify, 0, s_unused);
EI_VERIFY(0x00417E10, SetPublicVerify, 0, s_unused);
