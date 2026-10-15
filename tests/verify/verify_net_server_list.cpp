// Checks for net_server_list.cpp: ours only (it creates server entries and adds them to the
// client); the reply buffer is read to its end or left short.
#include "net_verify.h"

#include "../../src/game/net_classes.h"

namespace {
void* s_unused;
void __fastcall ServerListReadVerify(void* client, void*, GameBuffer* in) {
    static_cast<net::ClientSession*>(client)->ReadServerListImpl(in);
    ei::verify::Check("ServerListRead", true, true, "");
}
} // namespace

EI_VERIFY(0x0041D600, ServerListReadVerify, 0, s_unused);
