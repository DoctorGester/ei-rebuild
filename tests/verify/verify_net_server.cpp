// Checks for net_server.cpp.
//  - ServerSession::Construct: ours builds the real server first (so its socket gets the game's
//    port); then the original builds a second one in a scratch block holding a copy of the real
//    object's bytes before construction (its bind fails: the port is taken; harmless). The two are
//    compared with BuiltCompare (allocations pair up; the socket handles at +0x54 differ), including
//    the handler tables and the type registry, then the scratch server is destroyed by its own
//    deleting destructor (slot 0; nothing is connected, so it only frees).
//    The original's 00418530 rebuilds the game's multiplayer map list (it clears it first), so the
//    second construction leaves that list as the first one did.
#include "net_verify.h"

#include "../../src/game/net_classes.h"
#include "../../src/lib/crt.h"

#include "ei/vcall.h"

#include <cstring>
#include <vector>

namespace {

using namespace net;

// 00412D80: PUSH -1 (2), PUSH imm32 (5).
ServerSession*(__fastcall* s_construct)(ServerSession*, void*, std::uint32_t);
ServerSession* __fastcall ServerConstructVerify(ServerSession* self, void*, std::uint32_t arg) {
    constexpr std::size_t kSize = sizeof(ServerSession);
    // The bytes are kept in our memory and the scratch block allocated after ours ran, so that ours'
    // blocks land where the original's would without the check (SV2f).
    std::vector<std::uint8_t> before(reinterpret_cast<const std::uint8_t*>(self),
                                     reinterpret_cast<const std::uint8_t*>(self) + kSize);
    ServerSession* actualRet = self->Construct(arg);
    auto* scratch = static_cast<std::uint8_t*>(gamecrt::operator_new(kSize));
    std::memcpy(scratch, before.data(), kSize);
    ServerSession* expectedRet = s_construct(reinterpret_cast<ServerSession*>(scratch), nullptr, arg);
    auto* actual = reinterpret_cast<const std::uint8_t*>(self);
    netverify::BuiltCompare cmp{scratch, actual, kSize};
    for (std::size_t o : netverify::kRegistryFunctors) cmp.skipped.push_back(o);
    cmp.Range(0, kSize, {0x54});
    netverify::CompareSessionTables(cmp, scratch, actual);
    ei::verify::Check("ServerSession::Construct", true,
                      cmp.ok && expectedRet == reinterpret_cast<ServerSession*>(scratch) && actualRet == self, "%s",
                      cmp.detail);
    ei::VCall<void*>(scratch, 0, 1);   // deleting destructor
    return actualRet;
}

// 00417EB0: SUB ESP,0x10 (3), PUSH EBX (1), PUSH EBP (1).
void(__fastcall* s_collect)(ServerSession*, void*, ReplicatedVector**);
void __fastcall ServerCollectVerify(ServerSession* self, void*, ReplicatedVector** out) {
    auto ours = [](ServerSession* session, void*, ReplicatedVector** to) { session->ServerSession::CollectReplicatedImpl(to); };
    netverify::CheckCollect("ServerCollectReplicated", self, out, ours, s_collect);
}

} // namespace

EI_VERIFY(0x00412D80, ServerConstructVerify, 7, s_construct);
EI_VERIFY(0x00417EB0, ServerCollectVerify, 5, s_collect);

namespace {
// 004186F0: ours only (it loads the map and changes the server's lists); the name is kept at +0x25D4.
void __fastcall SetMapVerify(net::ServerSession* self, void*, const net::StringRep* name) {
    self->SetMap(name);
    std::size_t n = static_cast<std::size_t>(name->finish - name->start);
    ei::verify::Check("ServerSession::SetMap", true,
                      static_cast<std::size_t>(self->mapName_.finish - self->mapName_.start) == n &&
                          std::memcmp(self->mapName_.start, name->start, n) == 0,
                      "");
}
void* s_unusedSetMap;
} // namespace
EI_VERIFY(0x004186F0, SetMapVerify, 0, s_unusedSetMap);

// QueueMapList (net_server_map.cpp): ours only (it loads quests and queues messages); it only
// adds to the queued messages.
namespace {
void __fastcall QueueMapListVerify(net::ServerSession* self) {
    std::uint32_t before = 0;
    void** sentinel = static_cast<void**>(self->queuedMaps_);
    for (void** node = static_cast<void**>(sentinel[0]); node != sentinel; node = static_cast<void**>(node[0])) ++before;
    self->QueueMapList();
    std::uint32_t after = 0;
    sentinel = static_cast<void**>(self->queuedMaps_);
    for (void** node = static_cast<void**>(sentinel[0]); node != sentinel; node = static_cast<void**>(node[0])) ++after;
    ei::verify::Check("ServerSession::QueueMapList", true, after >= before, "");
}
void* s_unusedQueueMapList;
} // namespace
EI_VERIFY(0x00418530, QueueMapListVerify, 0, s_unusedQueueMapList);

// FinishMap (net_server_map.cpp): ours only (it releases and queues messages); when it reports
// success no map message is held.
namespace {
bool __fastcall FinishMapVerify(net::ServerSession* self, void*, const net::StringRep* name) {
    bool result = self->FinishMap(name);
    ei::verify::Check("ServerSession::FinishMap", true, !result || self->map_.ptr == nullptr, "");
    return result;
}
void* s_unusedFinishMap;
} // namespace
EI_VERIFY(0x004193F0, FinishMapVerify, 0, s_unusedFinishMap);
