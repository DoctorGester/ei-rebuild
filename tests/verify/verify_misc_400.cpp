// Checks for misc_400.cpp.
//  - ManagerConstruct / ManagerDestruct: ours only (the base manager's constructor and destructor
//    are verified on their own); the vtable is checked.
//  - ManagerNewObject: ours only (it constructs objects); null exactly for the types it does not make
//    (or a failed allocation).
//  - CursorStart, ClearPointee: side by side on copies.
#include "verify.h"

#include "../../src/game/base_manager.h"
#include "../../src/game/net_coder.h"

#include <cstdint>
#include <cstring>

namespace misc400 {
CBaseManager* __fastcall ManagerConstruct(CBaseManager* self);
void __fastcall ManagerDestruct(CBaseManager* self);
std::uint32_t __fastcall CursorStart(BufferCursor* self, void*, std::uint8_t* p, std::uint32_t, std::uint32_t);
void __fastcall ClearPointee(void** self);
CEngineObject* __fastcall ManagerNewObject(CBaseManager* self, void*, int type);
} // namespace misc400

namespace {
void* s_unused;

std::uintptr_t VtableOf(const void* p) {
    std::uintptr_t v;
    std::memcpy(&v, p, 4);
    return v;
}

CBaseManager* __fastcall ConstructVerify(CBaseManager* self) {
    CBaseManager* result = misc400::ManagerConstruct(self);
    ei::verify::Check("Manager73BECC::Construct", true, result == self && VtableOf(self) == 0x0073BECC, "");
    return result;
}
void __fastcall DestructVerify(CBaseManager* self) {
    misc400::ManagerDestruct(self);
    ei::verify::Check("Manager73BECC::Destruct", true, true, "");
}

// 00430FC0: MOV EAX,[ESP+4] (4), MOV [ECX],EAX (2).
std::uint32_t(__fastcall* s_cursorStart)(BufferCursor*, void*, std::uint8_t*, std::uint32_t, std::uint32_t);
std::uint32_t __fastcall CursorStartVerify(BufferCursor* self, void*, std::uint8_t* p, std::uint32_t a, std::uint32_t b) {
    BufferCursor expected = *self;
    std::uint32_t expectedResult = s_cursorStart(&expected, nullptr, p, a, b);
    std::uint32_t result = misc400::CursorStart(self, nullptr, p, a, b);
    ei::verify::Check("CursorStart", true, result == expectedResult && std::memcmp(&expected, self, sizeof(expected)) == 0, "");
    return result;
}

// 00433F10: MOV EAX,[ECX] (2), MOV [EAX],0 (6).
void(__fastcall* s_clearPointee)(void**);
void __fastcall ClearPointeeVerify(void** self) {
    void* target = *static_cast<void**>(*self);
    void* holder = &target;
    s_clearPointee(reinterpret_cast<void**>(&holder));
    misc400::ClearPointee(self);
    ei::verify::Check("ClearPointee", true, target == nullptr && *static_cast<void**>(*self) == nullptr, "");
}

CEngineObject* __fastcall NewObjectVerify(CBaseManager* self, void*, int type) {
    CEngineObject* object = misc400::ManagerNewObject(self, nullptr, type);
    bool makes = type >= 'E' && type <= 'K' && type != 'J';
    ei::verify::Check("Manager73BECC::NewObject", makes, object != nullptr, "type %d", type);
    return object;
}
} // namespace

EI_VERIFY(0x0042D690, ConstructVerify, 0, s_unused);
EI_VERIFY(0x0042D6F0, DestructVerify, 0, s_unused);
EI_VERIFY(0x00430FC0, CursorStartVerify, 6, s_cursorStart);
EI_VERIFY(0x00433F10, ClearPointeeVerify, 8, s_clearPointee);
EI_VERIFY(0x0042D700, NewObjectVerify, 0, s_unused);
