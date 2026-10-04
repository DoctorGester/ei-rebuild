// Checks for net_client_life.cpp: ours only (the destructors free the client's members).
#include "net_verify.h"

#include "../../src/game/net_classes.h"

namespace net {
ClientSession* __fastcall ClientDeletingDestructor(ClientSession* self, void*, unsigned flags);
void __fastcall ClientBaseDestruct(ClientSession* self);
}

namespace {
void* s_unused;

void __fastcall DestructVerify(net::ClientSession* self) {
    self->Destruct();
    ei::verify::Check("ClientSession::Destruct", true, true, "");
}
net::ClientSession* __fastcall DeleteVerify(net::ClientSession* self, void*, unsigned flags) {
    ei::verify::Check("ClientDeletingDestructor", true, true, "");
    return net::ClientDeletingDestructor(self, nullptr, flags);
}
void __fastcall BaseDestructVerify(net::ClientSession* self) {
    net::ClientBaseDestruct(self);
    ei::verify::Check("ClientBaseDestruct", true, true, "");
}
} // namespace

EI_VERIFY(0x0041CA10, DestructVerify, 0, s_unused);
EI_VERIFY(0x0041C8B0, DeleteVerify, 0, s_unused);
EI_VERIFY(0x0041C530, BaseDestructVerify, 0, s_unused);
