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
void __fastcall ClearPointee(void** self);
const char* __fastcall ManagerClassName(const CBaseManager* self);
int __fastcall ManagerClassId(const CBaseManager* self);
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
    std::uint32_t result = self->Start(p, a, b);
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

// 0042D6B0 / 0042D6C0: MOV EAX,imm32 (5). Pure.
const char*(__fastcall* s_className)(const CBaseManager*);
const char* __fastcall ClassNameVerify(const CBaseManager* self) {
    const char* expected = s_className(self);
    const char* actual = misc400::ManagerClassName(self);
    ei::verify::Check("Manager73BECC::ClassName", expected, actual, "");
    return actual;
}
int(__fastcall* s_classId)(const CBaseManager*);
int __fastcall ClassIdVerify(const CBaseManager* self) {
    int expected = s_classId(self);
    int actual = misc400::ManagerClassId(self);
    ei::verify::Check("Manager73BECC::ClassId", expected, actual, "");
    return actual;
}

CEngineObject* __fastcall NewObjectVerify(CBaseManager* self, void*, int type) {
    CEngineObject* object = static_cast<CClientSpecificManager*>(self)->NewObjectImpl(type);
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
EI_VERIFY(0x0042D6B0, ClassNameVerify, 5, s_className);
EI_VERIFY(0x0042D6C0, ClassIdVerify, 5, s_classId);
