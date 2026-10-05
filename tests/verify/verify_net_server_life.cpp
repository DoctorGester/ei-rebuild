// Checks for net_server_life.cpp: ours only (the destructors free the server's members).
#include "net_verify.h"

#include "../../src/game/net_classes.h"

namespace net {
}

namespace {
void* s_unused;

void __fastcall DestructVerify(net::ServerSession* self) {
    self->Destruct();
    ei::verify::Check("ServerSession::Destruct", true, true, "");
}
net::ServerSession* __fastcall DeleteVerify(net::ServerSession* self, void*, unsigned flags) {
    ei::verify::Check("ServerDeletingDestructor", true, true, "");
    return self->DeletingDestruct(flags);
}
void __fastcall BaseDestructVerify(net::ServerSession* self) {
    self->DestructBase();
    ei::verify::Check("ServerBaseDestruct", true, true, "");
}
} // namespace

EI_VERIFY(0x00417BC0, DestructVerify, 0, s_unused);
EI_VERIFY(0x00417A10, DeleteVerify, 0, s_unused);
EI_VERIFY(0x004132F0, BaseDestructVerify, 0, s_unused);
