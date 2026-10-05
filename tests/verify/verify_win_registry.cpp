// Checks for the Windows registry wrapper (win_registry.cpp). The readers only read the registry:
// both versions run and the results are compared. The writers write the same value twice (both
// run, results compared). Open runs the original on a scratch object whose key is closed again;
// Close / Destroy run ours only. Reached at startup by every scenario (the options loader 0045B210
// reads every setting through these); the writers when the options are saved (0045C560).
#include "verify.h"

#include "../../src/game/win_registry.h"
#include "../../src/lib/gameapi.h"

#include <cstring>
#include <new>

namespace {
void* s_unused;

// The CString a wrapper received by value (as its raw bits); the wrapper destroys it.
mfc::CString& Received(char*& raw) { return *reinterpret_cast<mfc::CString*>(&raw); }

// A copy of `s` passed by value to the original (which destroys it).
char* RawCopy(const mfc::CString& s) {
    char* raw;
    new (&raw) mfc::CString(s);
    return raw;
}

std::uint64_t Bits(double d) {
    std::uint64_t u;
    std::memcpy(&u, &d, 8);
    return u;
}
} // namespace

// 006972A0: MOV EAX,ECX; MOV dword ptr [EAX],0 (8 bytes). The original on a scratch object.
static CWinRegistry*(__fastcall* s_construct)(CWinRegistry*, void*);
static CWinRegistry* __fastcall ConstructVerify(CWinRegistry* self, void*) {
    CWinRegistry scratch;
    std::memset(&scratch, 0x55, sizeof(scratch));
    CWinRegistry* expected = s_construct(&scratch, nullptr);
    CWinRegistry* actual = self->Construct();
    ei::verify::Check("CWinRegistry::Construct", true,
                      expected == &scratch && actual == self && std::memcmp(&scratch, self, sizeof(scratch)) == 0,
                      "returned %p/%p key %p", expected, actual, self->key_);
    return actual;
}
EI_VERIFY(0x006972A0, ConstructVerify, 8, s_construct);

// 006972B0 / 006973A0 (the same code): ours only; the key is closed afterwards.
static void __fastcall DestroyVerify(CWinRegistry* self, void*) {
    self->Destroy();
    ei::verify::Check("CWinRegistry::Destroy", true, self->key_ == nullptr, "key %p", self->key_);
}
EI_VERIFY(0x006972B0, DestroyVerify, 0, s_unused);

static void __fastcall CloseVerify(CWinRegistry* self, void*) {
    self->Close();
    ei::verify::Check("CWinRegistry::Close", true, self->key_ == nullptr, "key %p", self->key_);
}
EI_VERIFY(0x006973A0, CloseVerify, 0, s_unused);

// 00697340: PUSH ESI; MOV ESI,ECX; MOV EAX,[ESI]; TEST EAX,EAX (7 bytes). Ours on the object, the
// original on a closed scratch object whose key is closed again; results and success compared.
static BOOL(__fastcall* s_open)(CWinRegistry*, void*, const char*, std::uint32_t);
static BOOL __fastcall OpenVerify(CWinRegistry* self, void*, const char* path, std::uint32_t root) {
    BOOL actual = self->Open(path, root);
    CWinRegistry scratch;
    scratch.key_ = nullptr;
    BOOL expected = s_open(&scratch, nullptr, path, root);
    bool same = expected == actual && (scratch.key_ != nullptr) == (self->key_ != nullptr);
    ei::verify::Check("CWinRegistry::Open", true, same, "%s: %d/%p vs %d/%p", path, expected, scratch.key_, actual,
                      self->key_);
    if (scratch.key_) gameapi::RegCloseKey(static_cast<HKEY>(scratch.key_));
    return actual;
}
EI_VERIFY(0x00697340, OpenVerify, 7, s_open);

// 00697CA0 / 00697D40: PUSH ECX; MOV EAX,[ESP+0x10] (5 bytes). Both write the same value.
static BOOL(__fastcall* s_setDWord)(CWinRegistry*, void*, const char*, const char*, std::uint32_t);
static BOOL __fastcall SetDWordVerify(CWinRegistry* self, void*, const char* subkey, const char* name,
                                      std::uint32_t value) {
    BOOL expected = s_setDWord(self, nullptr, subkey, name, value);
    ei::verify::Check("CWinRegistry::SetDWord", expected, self->SetDWord(subkey, name, value), "%s %s", subkey, name);
    return expected;
}
EI_VERIFY(0x00697CA0, SetDWordVerify, 5, s_setDWord);

static BOOL(__fastcall* s_setInt)(CWinRegistry*, void*, const char*, const char*, int);
static BOOL __fastcall SetIntVerify(CWinRegistry* self, void*, const char* subkey, const char* name, int value) {
    BOOL expected = s_setInt(self, nullptr, subkey, name, value);
    ei::verify::Check("CWinRegistry::SetInt", expected, self->SetInt(subkey, name, value), "%s %s", subkey, name);
    return expected;
}
EI_VERIFY(0x00697D40, SetIntVerify, 5, s_setInt);

// 00697E80: PUSH ECX; MOV ECX,[ECX]; LEA EAX,[ESP+0] (7 bytes).
static BOOL(__fastcall* s_setFloat)(CWinRegistry*, void*, const char*, const char*, std::uint32_t);
static BOOL __fastcall SetFloatVerify(CWinRegistry* self, void*, const char* subkey, const char* name,
                                      std::uint32_t bits) {
    BOOL expected = s_setFloat(self, nullptr, subkey, name, bits);
    ei::verify::Check("CWinRegistry::SetFloat", expected, self->SetFloat(subkey, name, bits), "%s %s", subkey, name);
    return expected;
}
EI_VERIFY(0x00697E80, SetFloatVerify, 7, s_setFloat);

// 00697460 / 00697FC0: PUSH -1; PUSH imm32 (7 bytes). The value CString is passed by value: the
// original gets a copy, ours another one (made by the call), and the wrapper destroys the one it
// received.
static BOOL(__fastcall* s_setStringInt)(CWinRegistry*, void*, const char*, const char*, char*);
static BOOL __fastcall SetStringIntVerify(CWinRegistry* self, void*, const char* subkey, const char* name, char* raw) {
    mfc::CString& value = Received(raw);
    BOOL expected = s_setStringInt(self, nullptr, subkey, name, RawCopy(value));
    ei::verify::Check("CWinRegistry::SetStringInt", expected, self->SetStringInt(subkey, name, value), "%s %s", subkey,
                      name);
    value.~CString();
    return expected;
}
EI_VERIFY(0x00697460, SetStringIntVerify, 7, s_setStringInt);

static BOOL(__fastcall* s_setString)(CWinRegistry*, void*, const char*, const char*, char*);
static BOOL __fastcall SetStringVerify(CWinRegistry* self, void*, const char* subkey, const char* name, char* raw) {
    mfc::CString& value = Received(raw);
    BOOL expected = s_setString(self, nullptr, subkey, name, RawCopy(value));
    ei::verify::Check("CWinRegistry::SetString", expected, self->SetString(subkey, name, value), "%s %s", subkey, name);
    value.~CString();
    return expected;
}
EI_VERIFY(0x00697FC0, SetStringVerify, 7, s_setString);

// 00698570 / 00698610: MOV EDX,[ESP+4]; MOV ECX,[ECX] (6 bytes).
static std::uint32_t(__fastcall* s_getDWord)(CWinRegistry*, void*, const char*, const char*, std::uint32_t);
static std::uint32_t __fastcall GetDWordVerify(CWinRegistry* self, void*, const char* subkey, const char* name,
                                               std::uint32_t def) {
    std::uint32_t expected = s_getDWord(self, nullptr, subkey, name, def);
    ei::verify::Check("CWinRegistry::GetDWord", expected, self->GetDWord(subkey, name, def), "%s %s", subkey, name);
    return expected;
}
EI_VERIFY(0x00698570, GetDWordVerify, 6, s_getDWord);

static int(__fastcall* s_getInt)(CWinRegistry*, void*, const char*, const char*, int);
static int __fastcall GetIntVerify(CWinRegistry* self, void*, const char* subkey, const char* name, int def) {
    int expected = s_getInt(self, nullptr, subkey, name, def);
    ei::verify::Check("CWinRegistry::GetInt", expected, self->GetInt(subkey, name, def), "%s %s", subkey, name);
    return expected;
}
EI_VERIFY(0x00698610, GetIntVerify, 6, s_getInt);

// 00698750: MOV EDX,[ESP+4]; MOV ECX,[ECX] (6 bytes). A float loaded into ST0: compared bitwise.
static double(__fastcall* s_getFloat)(CWinRegistry*, void*, const char*, const char*, std::uint32_t);
static double __fastcall GetFloatVerify(CWinRegistry* self, void*, const char* subkey, const char* name,
                                        std::uint32_t defaultBits) {
    double expected = s_getFloat(self, nullptr, subkey, name, defaultBits);
    double actual = self->GetFloat(subkey, name, defaultBits);
    ei::verify::Check("CWinRegistry::GetFloat", Bits(expected), Bits(actual), "%s %s: %g vs %g", subkey, name, expected,
                      actual);
    return expected;
}
EI_VERIFY(0x00698750, GetFloatVerify, 6, s_getFloat);

// 006976B0 / 006988A0: PUSH -1; PUSH imm32 (7 bytes). The original builds the result, ours a local;
// the default CString as for the writers.
static mfc::CString*(__fastcall* s_getStringInt)(CWinRegistry*, void*, mfc::CString*, const char*, const char*, char*);
static mfc::CString* __fastcall GetStringIntVerify(CWinRegistry* self, void*, mfc::CString* result,
                                                   const char* subkey, const char* name, char* raw) {
    mfc::CString& def = Received(raw);
    s_getStringInt(self, nullptr, result, subkey, name, RawCopy(def));
    mfc::CString actual = self->GetStringInt(subkey, name, def);
    ei::verify::Check("CWinRegistry::GetStringInt", true, std::strcmp(*result, actual) == 0, "%s %s: '%s' vs '%s'",
                      subkey, name, static_cast<const char*>(*result), static_cast<const char*>(actual));
    def.~CString();
    return result;
}
EI_VERIFY(0x006976B0, GetStringIntVerify, 7, s_getStringInt);

static mfc::CString*(__fastcall* s_getString)(CWinRegistry*, void*, mfc::CString*, const char*, const char*, char*);
static mfc::CString* __fastcall GetStringVerify(CWinRegistry* self, void*, mfc::CString* result, const char* subkey,
                                                const char* name, char* raw) {
    mfc::CString& def = Received(raw);
    s_getString(self, nullptr, result, subkey, name, RawCopy(def));
    mfc::CString actual = self->GetString(subkey, name, def);
    ei::verify::Check("CWinRegistry::GetString", true, std::strcmp(*result, actual) == 0, "%s %s: '%s' vs '%s'", subkey,
                      name, static_cast<const char*>(*result), static_cast<const char*>(actual));
    def.~CString();
    return result;
}
EI_VERIFY(0x006988A0, GetStringVerify, 7, s_getString);
