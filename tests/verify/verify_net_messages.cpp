// Checks for net_messages.cpp. The message constructors that allocate string blocks run the
// original on a scratch copy of the object's bytes and ours on the real object; the two are
// compared with netverify::BuiltCompare (pointers to the string blocks pair up), then the scratch
// copy's blocks are returned to the pool.
#include "net_verify.h"

#include "../../src/game/net_classes.h"
#include "../../src/lib/stlp.h"

#include <cstring>

namespace {

using net::Message;
using CtorFn = Message*(__fastcall*)(Message*);

void* s_unused;

// 00413250: ours only (it allocates). The count must be cleared.
void* __fastcall NewRefObjectVerify(std::size_t size) {
    void* object = net::NewRefObject(size);
    ei::verify::Check("net::NewRefObject", true, object != nullptr && static_cast<std::uint32_t*>(object)[1] == 0,
                      "size %u", unsigned(size));
    return object;
}

// Constructors that only store fields: the original on a scratch copy, ours on the real object.
template <std::size_t N>
Message* CheckPlain(const char* name, CtorFn original, CtorFn ours, Message* self) {
    alignas(4) unsigned char scratch[N];
    std::memcpy(scratch, self, N);
    Message* expectedRet = original(reinterpret_cast<Message*>(scratch));
    Message* actualRet = ours(self);
    bool same = expectedRet == reinterpret_cast<Message*>(scratch) && actualRet == self &&
                std::memcmp(scratch, self, N) == 0;
    ei::verify::Check(name, true, same, "");
    return actualRet;
}

// Constructors that allocate string blocks at `strings` offsets.
template <std::size_t N>
Message* CheckBuilt(const char* name, CtorFn original, CtorFn ours, Message* self,
                    std::initializer_list<std::size_t> strings) {
    alignas(4) unsigned char scratch[N];
    std::memcpy(scratch, self, N);
    Message* expectedRet = original(reinterpret_cast<Message*>(scratch));
    Message* actualRet = ours(self);
    netverify::BuiltCompare cmp{scratch, reinterpret_cast<const std::uint8_t*>(self), N};
    cmp.Range(0, N);
    for (std::size_t s : strings) {   // each string: empty, NUL-terminated, 8 bytes of room
        auto* e = reinterpret_cast<net::StringRep*>(scratch + s);
        auto* a = reinterpret_cast<net::StringRep*>(reinterpret_cast<std::uint8_t*>(self) + s);
        bool shape = e->start == e->finish && e->eos == e->start + 8 && *e->start == 0 &&
                     a->start == a->finish && a->eos == a->start + 8 && *a->start == 0;
        if (!shape && cmp.ok) std::snprintf(cmp.detail, sizeof(cmp.detail), "string at +0x%X", unsigned(s));
        cmp.ok &= shape;
        stlp::deallocate(e->start, static_cast<std::size_t>(e->eos - e->start));
    }
    ei::verify::Check(name, true, cmp.ok && expectedRet == reinterpret_cast<Message*>(scratch) && actualRet == self,
                      "%s", cmp.detail);
    return actualRet;
}

// 004186E0: MOV EAX,ECX (2), MOV [EAX],imm32 (6).
CtorFn s_base;
Message* __fastcall BaseVerify(Message* self) {
    return CheckPlain<12>("net::MessageConstructBase", s_base, net::MessageConstructBase, self);
}

// 0041C870: MOV EAX,ECX (2), XOR ECX,ECX (2), MOV [EAX+8],1 (7).
CtorFn s_bcb8;
Message* __fastcall Bcb8Verify(Message* self) {
    return CheckPlain<0x14>("net::ConstructMessage73BCB8", s_bcb8, net::ConstructMessage73BCB8, self);
}

// The others: PUSH -1 (2), PUSH imm32 (5).
CtorFn s_bc70, s_bc88, s_bca0, s_bcd0;
Message* __fastcall Bc70Verify(Message* self) {
    return CheckBuilt<0x3C>("net::ConstructMessage73BC70", s_bc70, net::ConstructMessage73BC70, self, {0xC, 0x18});
}
Message* __fastcall Bc88Verify(Message* self) {
    // +0xC is not written by either.
    return CheckBuilt<0x20>("net::ConstructMessage73BC88", s_bc88, net::ConstructMessage73BC88, self, {0x10});
}
Message* __fastcall Bca0Verify(Message* self) {
    return CheckBuilt<0x24>("net::ConstructMessage73BCA0", s_bca0, net::ConstructMessage73BCA0, self, {0xC, 0x18});
}
Message* __fastcall Bcd0Verify(Message* self) {
    return CheckBuilt<0x18>("net::ConstructMessage73BCD0", s_bcd0, net::ConstructMessage73BCD0, self, {0xC});
}

// 004140B0: MOV EAX,ECX (2), XOR ECX,ECX (2), MOV [EAX+8],1 (7).
CtorFn s_bc58;
Message* __fastcall Bc58Verify(Message* self) {
    return CheckPlain<0x14>("net::ConstructMessage73BC58", s_bc58, net::ConstructMessage73BC58, self);
}

// PUSH -1 (2), PUSH imm32 (5).
CtorFn s_bbf8, s_bc28, s_bc40;
Message* __fastcall Bbf8Verify(Message* self) {
    return CheckBuilt<0x34>("net::ConstructMessage73BBF8", s_bbf8, net::ConstructMessage73BBF8, self, {0xC, 0x18});
}
Message* __fastcall Bc28Verify(Message* self) {
    return CheckBuilt<0x18>("net::ConstructMessage73BC28", s_bc28, net::ConstructMessage73BC28, self, {0xC});
}
Message* __fastcall Bc40Verify(Message* self) {
    return CheckBuilt<0x1C>("net::ConstructMessage73BC40", s_bc40, net::ConstructMessage73BC40, self, {0x10});
}

} // namespace

EI_VERIFY(0x004140B0, Bc58Verify, 11, s_bc58);
EI_VERIFY(0x00413350, Bbf8Verify, 7, s_bbf8);
EI_VERIFY(0x004138A0, Bc28Verify, 7, s_bc28);
EI_VERIFY(0x00413C90, Bc40Verify, 7, s_bc40);
EI_VERIFY(0x00413250, NewRefObjectVerify, 0, s_unused);
EI_VERIFY(0x004186E0, BaseVerify, 8, s_base);
EI_VERIFY(0x0041C870, Bcb8Verify, 11, s_bcb8);
EI_VERIFY(0x0041C680, Bc70Verify, 7, s_bc70);
EI_VERIFY(0x0041C740, Bc88Verify, 7, s_bc88);
EI_VERIFY(0x0041C7C0, Bca0Verify, 7, s_bca0);
EI_VERIFY(0x00418C30, Bcd0Verify, 7, s_bcd0);
