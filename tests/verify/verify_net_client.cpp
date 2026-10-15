// Checks for net_client.cpp.
//  - ClientVectorConstruct: scratch copy, bytes compared.
//  - ClientSession::Construct: ours builds the real client; the original builds a second one in a
//    scratch block (a copy of the real object's bytes before construction, so fields neither
//    writes match). They are compared with BuiltCompare (allocations pair up; the socket handles at
//    +0x54 differ), including the handler tables, the type registry and the local player's record.
//    The scratch client is then destroyed by its own deleting destructor (slot 0): it has no
//    connections, so that only frees and closes its socket.
#include "net_verify.h"

#include "../../src/game/net_classes.h"
#include "../../src/game/net_messages.h"
#include "../../src/lib/crt.h"

#include "ei/vcall.h"

#include <cstring>
#include <vector>

namespace {

using namespace net;

// 0041C420: PUSH ECX (1), MOV DL,[ESP+3] (4).
ClientQueries*(__fastcall* s_vector)(ClientQueries*);
ClientQueries* __fastcall VectorVerify(ClientQueries* self) {
    // Raw bytes: the vector has no copy (and the original's constructor overwrites them all).
    alignas(ClientQueries) std::uint8_t storage[sizeof(ClientQueries)];
    std::memcpy(storage, self, sizeof(storage));
    auto* scratch = reinterpret_cast<ClientQueries*>(storage);
    ClientQueries* expectedRet = s_vector(scratch);
    ClientQueries* actualRet = ClientVectorConstruct(self);
    ei::verify::Check("ClientVectorConstruct", true,
                      expectedRet == scratch && actualRet == self && std::memcmp(storage, self, sizeof(storage)) == 0, "");
    return actualRet;
}

// 0041BC40: PUSH -1 (2), PUSH imm32 (5).
ClientSession*(__fastcall* s_construct)(ClientSession*);
ClientSession* __fastcall ClientConstructVerify(ClientSession* self) {
    constexpr std::size_t kSize = sizeof(ClientSession);
    // The bytes are kept in our memory and the scratch block allocated after ours ran, so that ours'
    // blocks land where the original's would without the check (SV2f).
    std::vector<std::uint8_t> before(reinterpret_cast<const std::uint8_t*>(self),
                                     reinterpret_cast<const std::uint8_t*>(self) + kSize);
    ClientSession* actualRet = self->Construct();
    auto* scratch = static_cast<std::uint8_t*>(gamecrt::operator_new(kSize));
    std::memcpy(scratch, before.data(), kSize);
    ClientSession* expectedRet = s_construct(reinterpret_cast<ClientSession*>(scratch));
    auto* actual = reinterpret_cast<const std::uint8_t*>(self);
    netverify::BuiltCompare cmp{scratch, actual, kSize};
    for (std::size_t o : netverify::kRegistryFunctors) cmp.skipped.push_back(o);
    cmp.Range(0, kSize, {0x54});
    netverify::CompareSessionTables(cmp, scratch, actual);
    // The record is a LoginMessage (0x34 bytes) of each one's allocation: +0x24 is its host_ byte
    // and three bytes of padding nobody writes, so only the byte is compared.
    auto* expectedRecord = static_cast<const LoginMessage*>(reinterpret_cast<ClientSession*>(scratch)->player_.ptr);
    auto* actualRecord = static_cast<const LoginMessage*>(self->player_.ptr);
    cmp.Nested(expectedRecord, actualRecord, 0x34, "player record", {0x24});
    if (expectedRecord && actualRecord && expectedRecord->host_ != actualRecord->host_) cmp.Note("player record host_");
    ei::verify::Check("ClientSession::Construct", true,
                      cmp.ok && expectedRet == reinterpret_cast<ClientSession*>(scratch) && actualRet == self, "%s",
                      cmp.detail);
    delete reinterpret_cast<ClientSession*>(scratch);   // the deleting destructor (slot 0)
    return actualRet;
}

// 0041D050: SUB ESP,8 (3), PUSH EBX (1), PUSH EBP (1).
void(__fastcall* s_collect)(ClientSession*, void*, ReplicatedVector**);
void __fastcall ClientCollectVerify(ClientSession* self, void*, ReplicatedVector** out) {
    auto ours = [](ClientSession* session, void*, ReplicatedVector** to) { session->ClientSession::CollectReplicatedImpl(to); };
    netverify::CheckCollect("ClientCollectReplicated", self, out, ours, s_collect);
}

} // namespace

EI_VERIFY(0x0041C420, VectorVerify, 5, s_vector);
EI_VERIFY(0x0041BC40, ClientConstructVerify, 7, s_construct);
EI_VERIFY(0x0041D050, ClientCollectVerify, 5, s_collect);
