// Checks for the registry reader (registry.cpp). The lookups only read the registry: both versions
// run (outputs into scratch copies for the original) and the results are compared. Opening and
// closing run ours only.
#include "verify.h"

#include "../../src/game/console.h"
#include "../../src/game/registry.h"
#include "../../src/lib/crt.h"

#include <cstring>
#include <new>
#include <vector>

namespace {
bool SameArray(const console::CStringArray& a, const console::CStringArray& b) {
    if (a.GetSize() != b.GetSize()) return false;
    for (int i = 0; i < a.GetSize(); ++i)
        if (std::strcmp(a[i], b[i]) != 0) return false;
    return true;
}

void FreeScratch(DwordArray& array) {
    if (array.data) gamecrt::operator_delete(reinterpret_cast<std::uint8_t**>(array.data)[-1]);
}

std::uint64_t Bits(double d) {
    std::uint64_t u;
    std::memcpy(&u, &d, 8);
    return u;
}
} // namespace

// 006946D0: PUSH ECX; PUSH EBX; PUSH EBP; MOV EBP,[ECX+0x14] (6 bytes). The original's scalar flag
// goes to a scratch int.
static const std::uint8_t*(__fastcall* s_findData)(CFileRegistry*, void*, const char*, const char*, unsigned, int*);
static const std::uint8_t* __fastcall FindDataVerify(CFileRegistry* self, void*, const char* section, const char* key,
                                                     unsigned type, int* isScalar) {
    int expectedFlag = -7, actualFlag = -7;
    const std::uint8_t* expected = s_findData(self, nullptr, section, key, type, isScalar ? &expectedFlag : nullptr);
    const std::uint8_t* actual = self->FindData(section, key, type, isScalar ? &actualFlag : nullptr);
    ei::verify::Check("CFileRegistry::FindData", true, expected == actual && expectedFlag == actualFlag,
                      "[%s] %s type %X: %p/%d vs %p/%d", section, key ? key : "(null)", type, expected, expectedFlag,
                      actual, actualFlag);
    if (isScalar && expectedFlag != -7) *isScalar = expectedFlag;   // written once the key is found
    return expected;
}
EI_VERIFY(0x006946D0, FindDataVerify, 6, s_findData);

// 00694880: PUSH -1; PUSH imm32 (7 bytes). The original fills a scratch array.
static BOOL(__fastcall* s_getSectionNames)(CFileRegistry*, void*, console::CStringArray*);
static BOOL __fastcall GetSectionNamesVerify(CFileRegistry* self, void*, console::CStringArray* out) {
    console::CStringArray scratch;
    BOOL expected = s_getSectionNames(self, nullptr, &scratch);
    BOOL actual = self->GetSectionNames(out);
    ei::verify::Check("CFileRegistry::GetSectionNames", true, expected == actual && SameArray(scratch, *out),
                      "%d sections vs %d", scratch.GetSize(), out->GetSize());
    return expected;
}
EI_VERIFY(0x00694880, GetSectionNamesVerify, 7, s_getSectionNames);

// 00694B90 / 00694BE0: PUSH EBX; MOV EBX,[ESP+8] (5 bytes).
static int(__fastcall* s_getInt)(CFileRegistry*, void*, const char*, const char*, int);
static int __fastcall GetIntVerify(CFileRegistry* self, void*, const char* section, const char* key, int def) {
    int expected = s_getInt(self, nullptr, section, key, def);
    ei::verify::Check("CFileRegistry::GetInt", expected, self->GetInt(section, key, def), "[%s] %s", section, key);
    return expected;
}
EI_VERIFY(0x00694B90, GetIntVerify, 5, s_getInt);

static int(__fastcall* s_getInt2)(CFileRegistry*, void*, const char*, const char*, int);
static int __fastcall GetInt2Verify(CFileRegistry* self, void*, const char* section, const char* key, int def) {
    int expected = s_getInt2(self, nullptr, section, key, def);
    ei::verify::Check("CFileRegistry::GetInt2", expected, self->GetInt2(section, key, def), "[%s] %s", section, key);
    return expected;
}
EI_VERIFY(0x00694BE0, GetInt2Verify, 5, s_getInt2);

// 00694C80: PUSH EBX; MOV EBX,[ESP+8] (5 bytes). Returned in ST0, compared as doubles.
static double(__fastcall* s_getFloat)(CFileRegistry*, void*, const char*, const char*, float);
static double __fastcall GetFloatVerify(CFileRegistry* self, void*, const char* section, const char* key, float def) {
    double expected = s_getFloat(self, nullptr, section, key, def);
    double actual = self->GetFloat(section, key, def);
    ei::verify::Check("CFileRegistry::GetFloat", Bits(expected), Bits(actual), "[%s] %s", section, key);
    return expected;
}
EI_VERIFY(0x00694C80, GetFloatVerify, 5, s_getFloat);

// 00694D20: PUSH -1; PUSH imm32 (7 bytes). The default CString is passed by value and destroyed
// by the callee: each version gets its own copy, and this wrapper destroys the one it received.
static mfc::CString*(__fastcall* s_getString)(CFileRegistry*, void*, mfc::CString*, const char*, const char*, char*);
static mfc::CString* __fastcall GetStringVerify(CFileRegistry* self, void*, mfc::CString* result, const char* section,
                                                const char* key, char* defaultData) {
    mfc::CString& received = *reinterpret_cast<mfc::CString*>(&defaultData);
    alignas(4) char copy[sizeof(mfc::CString)];
    new (copy) mfc::CString(received);
    s_getString(self, nullptr, result, section, key, *reinterpret_cast<char**>(copy));
    mfc::CString actual = self->GetString(section, key, received);
    ei::verify::Check("CFileRegistry::GetString", true, std::strcmp(*result, actual) == 0, "[%s] %s", section, key);
    received.~CString();
    return result;
}
EI_VERIFY(0x00694D20, GetStringVerify, 7, s_getString);

// 00694DC0: MOV EAX,[ESP+8]; MOV EDX,[ESP+4] (8 bytes). The original writes a scratch buffer of
// the same size.
static void(__fastcall* s_getStringBuffer)(CFileRegistry*, void*, const char*, const char*, char*, int*, const char*);
static void __fastcall GetStringBufferVerify(CFileRegistry* self, void*, const char* section, const char* key,
                                             char* buffer, int* length, const char* def) {
    int size = *length;
    std::vector<char> scratch(static_cast<std::size_t>(size > 0 ? size : 0) + 1, '\x55');
    int scratchLength = size;
    s_getStringBuffer(self, nullptr, section, key, scratch.data(), &scratchLength, def);
    self->GetStringBuffer(section, key, buffer, length, def);
    bool same = scratchLength == *length && std::memcmp(scratch.data(), buffer, static_cast<std::size_t>(size > 0 ? size : 0)) == 0;
    ei::verify::Check("CFileRegistry::GetStringBuffer", true, same, "[%s] %s size %d", section, key, size);
}
EI_VERIFY(0x00694DC0, GetStringBufferVerify, 8, s_getStringBuffer);

// 00695410 / 00695600: PUSH ECX; MOV EDX,[ESP+0xC] (5 bytes). The original fills an empty scratch
// array; result, size and values compared.
static BOOL CompareArrays(const char* name, BOOL expected, BOOL actual, const DwordArray& scratch,
                          const DwordArray& out, const char* section, const char* key) {
    bool same = expected == actual && scratch.size == out.size &&
                (scratch.size == 0 || std::memcmp(scratch.data, out.data, static_cast<std::size_t>(scratch.size) * 4) == 0);
    ei::verify::Check(name, true, same, "[%s] %s: %d/%d vs %d/%d", section, key, expected, scratch.size, actual, out.size);
    return expected;
}

static BOOL(__fastcall* s_getIntArray)(CFileRegistry*, void*, const char*, const char*, DwordArray*);
static BOOL __fastcall GetIntArrayVerify(CFileRegistry* self, void*, const char* section, const char* key,
                                         DwordArray* out) {
    DwordArray scratch{nullptr, 0, 0, 0};
    BOOL expected = s_getIntArray(self, nullptr, section, key, &scratch);
    BOOL actual = self->GetIntArray(section, key, out);
    CompareArrays("CFileRegistry::GetIntArray", expected, actual, scratch, *out, section, key);
    FreeScratch(scratch);
    return expected;
}
EI_VERIFY(0x00695410, GetIntArrayVerify, 5, s_getIntArray);

static BOOL(__fastcall* s_getFloatArray)(CFileRegistry*, void*, const char*, const char*, DwordArray*);
static BOOL __fastcall GetFloatArrayVerify(CFileRegistry* self, void*, const char* section, const char* key,
                                           DwordArray* out) {
    DwordArray scratch{nullptr, 0, 0, 0};
    BOOL expected = s_getFloatArray(self, nullptr, section, key, &scratch);
    BOOL actual = self->GetFloatArray(section, key, out);
    CompareArrays("CFileRegistry::GetFloatArray", expected, actual, scratch, *out, section, key);
    FreeScratch(scratch);
    return expected;
}
EI_VERIFY(0x00695600, GetFloatArrayVerify, 5, s_getFloatArray);

// 006958E0: MOV EAX,FS:[0] (6 bytes). The original fills a scratch array.
static BOOL(__fastcall* s_getStringArray)(CFileRegistry*, void*, const char*, const char*, console::CStringArray*);
static BOOL __fastcall GetStringArrayVerify(CFileRegistry* self, void*, const char* section, const char* key,
                                            console::CStringArray* out) {
    console::CStringArray scratch;
    BOOL expected = s_getStringArray(self, nullptr, section, key, &scratch);
    BOOL actual = self->GetStringArray(section, key, out);
    ei::verify::Check("CFileRegistry::GetStringArray", true, expected == actual && SameArray(scratch, *out),
                      "[%s] %s: %d vs %d", section, key, scratch.GetSize(), out->GetSize());
    return expected;
}
EI_VERIFY(0x006958E0, GetStringArrayVerify, 6, s_getStringArray);

// Opening and closing (files, mappings): ours only, with the resulting state checked.
static void* s_unused;
static CFileRegistry* __fastcall ConstructVerify(CFileRegistry* self) {
    CFileRegistry* result = self->Construct();
    ei::verify::Check("CFileRegistry::Construct", true, result == self && !self->attached_ && !self->data_, "%p", self);
    return result;
}
EI_VERIFY(0x00694520, ConstructVerify, 0, s_unused);

static void __fastcall DestroyVerify(CFileRegistry* self) {
    self->Destroy();
    ei::verify::Check("CFileRegistry::Destroy", true, !self->data_ && !self->file_.view_, "%p", self);
}
EI_VERIFY(0x00694540, DestroyVerify, 0, s_unused);

static BOOL __fastcall OpenVerify(CFileRegistry* self, void*, const char* path) {
    BOOL result = self->Open(path);
    bool ok = result ? (self->data_ && *reinterpret_cast<const std::uint32_t*>(self->data_) == kRegistryMagic)
                     : !self->data_;
    ei::verify::Check("CFileRegistry::Open", true, ok, "%s", path);
    return result;
}
EI_VERIFY(0x006945A0, OpenVerify, 0, s_unused);

static BOOL __fastcall AttachVerify(CFileRegistry* self, void*, const void* image) {
    BOOL result = self->Attach(image);
    bool ok = result ? self->attached_ == 1 && self->data_ == image : !self->data_ && !self->attached_;
    ei::verify::Check("CFileRegistry::Attach", true, ok, "%p", image);
    return result;
}
EI_VERIFY(0x00694610, AttachVerify, 0, s_unused);

static BOOL __fastcall IsValidVerify(CFileRegistry* self) {
    BOOL result = self->IsValid();
    ei::verify::Check("CFileRegistry::IsValid", true, result == 1 || !self->data_, "%p", self);
    return result;
}
EI_VERIFY(0x00694660, IsValidVerify, 0, s_unused);

static void __fastcall CloseVerify(CFileRegistry* self) {
    self->Close();
    ei::verify::Check("CFileRegistry::Close", true, !self->data_ && !self->attached_, "%p", self);
}
EI_VERIFY(0x006946B0, CloseVerify, 0, s_unused);
