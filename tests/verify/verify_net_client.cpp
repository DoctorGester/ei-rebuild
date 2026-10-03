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
#include "../../src/lib/crt.h"

#include "ei/vcall.h"

#include <cstring>

namespace {

using namespace net;

// 0041C420: PUSH ECX (1), MOV DL,[ESP+3] (4).
void*(__fastcall* s_vector)(std::uint32_t*);
void* __fastcall VectorVerify(std::uint32_t* self) {
    std::uint32_t scratch[4];
    std::memcpy(scratch, self, sizeof(scratch));
    void* expectedRet = s_vector(scratch);
    void* actualRet = ClientVectorConstruct(self);
    ei::verify::Check("ClientVectorConstruct", true,
                      expectedRet == scratch && actualRet == self && std::memcmp(scratch, self, sizeof(scratch)) == 0, "");
    return actualRet;
}

// 0041BC40: PUSH -1 (2), PUSH imm32 (5).
ClientSession*(__fastcall* s_construct)(ClientSession*);
ClientSession* __fastcall ClientConstructVerify(ClientSession* self) {
    constexpr std::size_t kSize = sizeof(ClientSession);
    auto* scratch = static_cast<std::uint8_t*>(gamecrt::operator_new(kSize));
    std::memcpy(scratch, self, kSize);
    ClientSession* actualRet = self->Construct();
    ClientSession* expectedRet = s_construct(reinterpret_cast<ClientSession*>(scratch));
    auto* actual = reinterpret_cast<const std::uint8_t*>(self);
    netverify::BuiltCompare cmp{scratch, actual, kSize};
    cmp.Range(0, kSize, {0x54});
    netverify::CompareSessionTables(cmp, scratch, actual);
    cmp.Nested(reinterpret_cast<ClientSession*>(scratch)->player, self->player, 0x34, "player record");
    ei::verify::Check("ClientSession::Construct", true,
                      cmp.ok && expectedRet == reinterpret_cast<ClientSession*>(scratch) && actualRet == self, "%s",
                      cmp.detail);
    ei::VCall<void*>(scratch, 0, 1);   // deleting destructor
    return actualRet;
}

} // namespace

EI_VERIFY(0x0041C420, VectorVerify, 5, s_vector);
EI_VERIFY(0x0041BC40, ClientConstructVerify, 7, s_construct);
