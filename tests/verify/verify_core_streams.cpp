// Differential checks for core_streams.cpp. Constructor and destructor bodies write only their own
// object: the original runs on a scratch copy of the bytes it may touch, ours on the real object,
// and the bytes and returned pointers are compared. Where the original would free memory (the
// buffer of a memory stream, the object itself), the scratch copy has the buffer pointer cleared
// and the original is called without the delete flag, so nothing is freed twice.
#include "verify.h"

#include "../../src/game/core_streams.h"

#include <cstring>

namespace {

template <std::size_t N>
struct Scratch {
    alignas(4) unsigned char bytes[N];
    explicit Scratch(const void* from) { std::memcpy(bytes, from, N); }
    void* get() { return bytes; }
    template <typename T>
    T* as() { return reinterpret_cast<T*>(bytes); }
    bool Same(const void* other) const { return std::memcmp(bytes, other, N) == 0; }
};

void* s_unused;   // registrations without a trampoline

constexpr std::size_t kStreamBytes = sizeof(CMemoryStream);
constexpr std::size_t kDataOffset = offsetof(CMemoryStream, data_);

// Equal apart from the buffer pointer (ours leaves it dangling, the scratch copy had it cleared).
bool SameButData(const void* a, const void* b) {
    const auto* x = static_cast<const unsigned char*>(a);
    const auto* y = static_cast<const unsigned char*>(b);
    return std::memcmp(x, y, kDataOffset) == 0 &&
           std::memcmp(x + kDataOffset + 4, y + kDataOffset + 4, kStreamBytes - kDataOffset - 4) == 0;
}

// --- Vtable-only bodies --------------------------------------------------------------------------

using VoidFn = void(__fastcall*)(void*);
using CtorFn = void*(__fastcall*)(void*);
using DeleteFn = void*(__fastcall*)(void*, void*, unsigned);

// 00412720 / 00413430 / 004174E0: MOV [ECX],imm32 (6).
VoidFn s_refDestruct1, s_refDestruct2, s_streamDestruct;
void CheckVoid(const char* name, VoidFn original, VoidFn ours, void* self) {
    Scratch<4> expected(self);
    original(expected.get());
    ours(self);
    ei::verify::Check(name, true, expected.Same(self), "");
}
void __fastcall RefDestruct1Verify(void* self) {
    CheckVoid("RefObjectDestructBase@00412720", s_refDestruct1, RefObjectDestructBase, self);
}
void __fastcall RefDestruct2Verify(void* self) {
    CheckVoid("RefObjectDestructBase@00413430", s_refDestruct2, RefObjectDestructBase, self);
}
VoidFn s_refDestruct3, s_refDestruct4;
void __fastcall RefDestruct3Verify(void* self) {
    CheckVoid("RefObjectDestructBase@00412960", s_refDestruct3, RefObjectDestructBase, self);
}
void __fastcall RefDestruct4Verify(void* self) {
    CheckVoid("RefObjectDestructBase@004368C0", s_refDestruct4, RefObjectDestructBase, self);
}
void __fastcall StreamDestructVerify(void* self) {
    CheckVoid("StreamDestructBase", s_streamDestruct, StreamDestructBase, self);
}

// 00419720 / 0047A200: MOV EAX,ECX (2), MOV [EAX],imm32 (6).
template <std::size_t N>
void* CheckCtor(const char* name, CtorFn original, CtorFn ours, void* self) {
    Scratch<N> expected(self);
    void* expectedRet = original(expected.get());
    void* actualRet = ours(self);
    bool same = expectedRet == expected.get() && actualRet == self && expected.Same(self);
    ei::verify::Check(name, true, same, "");
    return actualRet;
}
CtorFn s_refValueCtor, s_streamCtor;
void* __fastcall RefValueCtorVerify(void* self) {
    return CheckCtor<4>("RefValueConstructBase", s_refValueCtor, RefValueConstructBase, self);
}
void* __fastcall StreamCtorVerify(void* self) {
    return CheckCtor<0xC>("StreamConstructBase", s_streamCtor, StreamConstructBase, self);
}

// 00412730 / 00417400: MOV AL,[ESP+4] (4), PUSH ESI (1). The original runs without the delete flag.
template <std::size_t N>
void* CheckDelete(const char* name, DeleteFn original, DeleteFn ours, void* self, unsigned flags) {
    Scratch<N> expected(self);
    void* expectedRet = original(expected.get(), nullptr, flags & ~1u);
    // Compare before ours runs: with the flag set ours frees the object.
    Scratch<N> actual(self);
    void* actualRet = ours(actual.get(), nullptr, flags & ~1u);
    bool same = expectedRet == expected.get() && actualRet == actual.get() &&
                std::memcmp(expected.bytes, actual.bytes, N) == 0;
    ei::verify::Check(name, true, same, "flags %u", flags);
    return ours(self, nullptr, flags);
}
DeleteFn s_refDelete, s_streamDelete;
void* __fastcall RefDeleteVerify(void* self, void*, unsigned flags) {
    return CheckDelete<4>("RefObjectDeletingDestructor", s_refDelete, RefObjectDeletingDestructor, self, flags);
}
void* __fastcall StreamDeleteVerify(void* self, void*, unsigned flags) {
    return CheckDelete<4>("StreamDeletingDestructor", s_streamDelete, StreamDeletingDestructor, self, flags);
}

// 00412700: PUSH ESI, MOV ESI,ECX, then a CALL: no trampoline. Same check against a stand-in that
// runs the original's steps through the (separately verified) 00412720.
void* __fastcall RefValueDeleteVerify(void* self, void*, unsigned flags) {
    Scratch<4> expected(self);
    s_refDestruct1(expected.get());
    Scratch<4> actual(self);
    void* actualRet = RefValueDeletingDestructor(actual.get(), nullptr, flags & ~1u);
    ei::verify::Check("RefValueDeletingDestructor", true, actualRet == actual.get() && expected.Same(actual.bytes),
                      "flags %u", flags);
    return RefValueDeletingDestructor(self, nullptr, flags);
}

// --- CMemoryStream -------------------------------------------------------------------------------

// 0041DD70: MOV EAX,ECX (2), XOR ECX,ECX (2), MOV [EAX+8],imm32 (7).
CMemoryStream*(__fastcall* s_msCtor)(CMemoryStream*);
CMemoryStream* __fastcall MemoryStreamCtorVerify(CMemoryStream* self) {
    Scratch<kStreamBytes> expected(self);
    CMemoryStream* expectedRet = s_msCtor(expected.as<CMemoryStream>());
    CMemoryStream* actualRet = MemoryStreamConstruct(self);
    bool same = expectedRet == expected.as<CMemoryStream>() && actualRet == self && expected.Same(self);
    ei::verify::Check("MemoryStreamConstruct", true, same, "");
    return actualRet;
}

// 004173D0 / 004790A0: PUSH ESI (1), MOV ESI,ECX (2), MOV EAX,[ESI+0x18] (3).
using MsFn = void(__fastcall*)(CMemoryStream*);
MsFn s_msDestruct1, s_msDestruct2;
void CheckMsDestruct(const char* name, MsFn original, CMemoryStream* self) {
    Scratch<kStreamBytes> expected(self);
    expected.as<CMemoryStream>()->data_ = nullptr;
    original(expected.as<CMemoryStream>());
    MemoryStreamDestruct(self);
    ei::verify::Check(name, true, SameButData(expected.bytes, self), "");
}
void __fastcall MsDestruct1Verify(CMemoryStream* self) { CheckMsDestruct("MemoryStreamDestruct@004173D0", s_msDestruct1, self); }
void __fastcall MsDestruct2Verify(CMemoryStream* self) { CheckMsDestruct("MemoryStreamDestruct@004790A0", s_msDestruct2, self); }

// 004173B0 / 004790D0: PUSH ESI, MOV ESI,ECX, then a CALL: no trampoline. Ours runs on a scratch
// copy without the flag and must leave what the destructor body leaves.
void* CheckMsDelete(const char* name, CMemoryStream* self, unsigned flags) {
    Scratch<kStreamBytes> actual(self);
    actual.as<CMemoryStream>()->data_ = nullptr;
    Scratch<kStreamBytes> expected(actual.bytes);
    MemoryStreamDestruct(expected.as<CMemoryStream>());
    void* ret = MemoryStreamDeletingDestructor(actual.as<CMemoryStream>(), nullptr, flags & ~1u);
    ei::verify::Check(name, true, ret == actual.get() && expected.Same(actual.bytes), "flags %u", flags);
    return MemoryStreamDeletingDestructor(self, nullptr, flags);
}
void* __fastcall MsDelete1Verify(CMemoryStream* self, void*, unsigned flags) {
    return CheckMsDelete("MemoryStreamDeletingDestructor@004173B0", self, flags);
}
void* __fastcall MsDelete2Verify(CMemoryStream* self, void*, unsigned flags) {
    return CheckMsDelete("MemoryStreamDeletingDestructor@004790D0", self, flags);
}

// 00417350: PUSH ESI (1), MOV ESI,ECX (2), PUSH EDI (1), XOR EDI,EDI (2).
MsFn s_msClear;
void __fastcall MsClearVerify(CMemoryStream* self) {
    Scratch<kStreamBytes> expected(self);
    expected.as<CMemoryStream>()->data_ = nullptr;
    s_msClear(expected.as<CMemoryStream>());
    MemoryStreamClear(self);
    ei::verify::Check("MemoryStreamClear", true, expected.Same(self), "");
}

// --- Aligned arrays (allocate / free: ours only) -------------------------------------------------

void** __fastcall AlignedAllocVerify(int count) {
    void** result = AlignedPointerArrayAlloc(count);
    auto aligned = reinterpret_cast<std::uintptr_t>(result);
    auto block = reinterpret_cast<std::uintptr_t>(reinterpret_cast<void**>(result)[-1]);
    bool ok = (aligned & 0x1F) == 0 && aligned - block >= 4 && aligned - block <= 0x23;
    ei::verify::Check("AlignedPointerArrayAlloc", true, ok, "count %d", count);
    return result;
}

void __fastcall AlignedClearVerify(AlignedObjectArray* self) {
    self->Clear();
    ei::verify::Check("AlignedObjectArray::Clear", true, !self->data && !self->size && !self->capacity, "");
}

// --- Coder state ---------------------------------------------------------------------------------

// 0042FCA0: PUSH EBX (1), MOV EDX,ECX (2), XOR EBX,EBX (2). 10 KB: the scratch copy is static.
CoderState*(__fastcall* s_coderCtor)(CoderState*);
CoderState* __fastcall CoderCtorVerify(CoderState* self) {
    static Scratch<sizeof(CoderState)>* expected;
    if (!expected) expected = new Scratch<sizeof(CoderState)>(self);
    else std::memcpy(expected->bytes, self, sizeof(CoderState));
    CoderState* expectedRet = s_coderCtor(expected->as<CoderState>());
    CoderState* actualRet = self->Construct();
    bool same = expectedRet == expected->as<CoderState>() && actualRet == self && expected->Same(self);
    ei::verify::Check("CoderState::Construct", true, same, "");
    return actualRet;
}

} // namespace

EI_VERIFY(0x00412720, RefDestruct1Verify, 6, s_refDestruct1);
EI_VERIFY(0x00413430, RefDestruct2Verify, 6, s_refDestruct2);
EI_VERIFY(0x00412960, RefDestruct3Verify, 6, s_refDestruct3);
EI_VERIFY(0x004368C0, RefDestruct4Verify, 6, s_refDestruct4);
EI_VERIFY(0x00412730, RefDeleteVerify, 5, s_refDelete);
EI_VERIFY(0x00419720, RefValueCtorVerify, 8, s_refValueCtor);
EI_VERIFY(0x00412700, RefValueDeleteVerify, 0, s_unused);
EI_VERIFY(0x0047A200, StreamCtorVerify, 8, s_streamCtor);
EI_VERIFY(0x004174E0, StreamDestructVerify, 6, s_streamDestruct);
EI_VERIFY(0x00417400, StreamDeleteVerify, 5, s_streamDelete);
EI_VERIFY(0x0041DD70, MemoryStreamCtorVerify, 11, s_msCtor);
EI_VERIFY(0x004173D0, MsDestruct1Verify, 6, s_msDestruct1);
EI_VERIFY(0x004790A0, MsDestruct2Verify, 6, s_msDestruct2);
EI_VERIFY(0x004173B0, MsDelete1Verify, 0, s_unused);
EI_VERIFY(0x004790D0, MsDelete2Verify, 0, s_unused);
EI_VERIFY(0x00417350, MsClearVerify, 6, s_msClear);
EI_VERIFY(0x0047B940, AlignedAllocVerify, 0, s_unused);
EI_VERIFY(0x0047B620, AlignedClearVerify, 0, s_unused);
EI_VERIFY(0x0042FCA0, CoderCtorVerify, 5, s_coderCtor);
