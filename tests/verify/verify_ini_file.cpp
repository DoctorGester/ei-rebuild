// Checks for the .ini reader and writer (ini_file.cpp). Reading an .ini file can be repeated: the
// readers run both versions (outputs into scratch objects for the original) and compare. Writers
// and constructors run ours only.
#include "verify.h"

#include "../../src/game/console.h"
#include "../../src/game/ini_file.h"
#include "../../src/lib/crt.h"

#include <cstring>
#include <new>

namespace {
bool SameStrings(const console::CStringArray& a, int aFrom, const console::CStringArray& b, int bFrom) {
    if (a.GetSize() - aFrom != b.GetSize() - bFrom) return false;
    for (int i = 0; i < a.GetSize() - aFrom; ++i)
        if (std::strcmp(a[aFrom + i], b[bFrom + i]) != 0) return false;
    return true;
}

void FreeAlignedScratch(void* data) {
    if (data) gamecrt::operator_delete(static_cast<std::uint8_t**>(data)[-1]);
}

std::uint64_t Bits(double d) {
    std::uint64_t u;
    std::memcpy(&u, &d, 8);
    return u;
}

bool SameBytes(const void* a, int aSize, const void* b, int bSize, int element) {
    return aSize == bSize && (aSize == 0 || std::memcmp(a, b, static_cast<std::size_t>(aSize * element)) == 0);
}
} // namespace

// 00695D10: PUSH -1; MOV EAX,FS:[0] (8 bytes).
static BOOL(__fastcall* s_getKeyNames)(CIniFile*, void*, const char*, console::CStringArray*);
static BOOL __fastcall GetKeyNamesVerify(CIniFile* self, void*, const char* section, console::CStringArray* out) {
    console::CStringArray scratch;
    BOOL expected = s_getKeyNames(self, nullptr, section, &scratch);
    BOOL actual = self->GetKeyNames(section, out);
    ei::verify::Check("CIniFile::GetKeyNames", true, expected == actual && SameStrings(scratch, 0, *out, 0), "[%s] %d keys",
                      section, scratch.GetSize());
    return expected;
}
EI_VERIFY(0x00695D10, GetKeyNamesVerify, 8, s_getKeyNames);

// The destructor (a jump to ~CString; ours only, counted).
static void __fastcall IniDestructVerify(CIniFile* self) {
    self->Destruct();
    ei::verify::Check("CIniFile::~CIniFile", true, true, "");
}
static void* s_iniDestructUnused;
EI_VERIFY(0x00695BE0, IniDestructVerify, 0, s_iniDestructUnused);

// 00695F00 / 00695F20: MOV ECX,[ECX]; MOV EAX,[ESP+0xC] (6 bytes).
static unsigned(__fastcall* s_iniGetInt)(CIniFile*, void*, const char*, const char*, int);
static unsigned __fastcall IniGetIntVerify(CIniFile* self, void*, const char* section, const char* key, int def) {
    unsigned expected = s_iniGetInt(self, nullptr, section, key, def);
    ei::verify::Check("CIniFile::GetInt", expected, self->GetInt(section, key, def), "[%s] %s", section, key);
    return expected;
}
EI_VERIFY(0x00695F00, IniGetIntVerify, 6, s_iniGetInt);

static unsigned(__fastcall* s_iniGetInt2)(CIniFile*, void*, const char*, const char*, int);
static unsigned __fastcall IniGetInt2Verify(CIniFile* self, void*, const char* section, const char* key, int def) {
    unsigned expected = s_iniGetInt2(self, nullptr, section, key, def);
    ei::verify::Check("CIniFile::GetInt2", expected, self->GetInt2(section, key, def), "[%s] %s", section, key);
    return expected;
}
EI_VERIFY(0x00695F20, IniGetInt2Verify, 6, s_iniGetInt2);

// 00695F60: FLD [ESP+0xC]; MOV EAX,[ESP+8] (8 bytes).
static double(__fastcall* s_iniGetFloat)(CIniFile*, void*, const char*, const char*, float);
static double __fastcall IniGetFloatVerify(CIniFile* self, void*, const char* section, const char* key, float def) {
    double expected = s_iniGetFloat(self, nullptr, section, key, def);
    double actual = self->GetFloat(section, key, def);
    ei::verify::Check("CIniFile::GetFloat", Bits(expected), Bits(actual), "[%s] %s", section, key);
    return expected;
}
EI_VERIFY(0x00695F60, IniGetFloatVerify, 8, s_iniGetFloat);

// 00695F80: PUSH -1; PUSH imm32 (7 bytes).
static double(__fastcall* s_iniGetDouble)(CIniFile*, void*, const char*, const char*, double);
static double __fastcall IniGetDoubleVerify(CIniFile* self, void*, const char* section, const char* key, double def) {
    double expected = s_iniGetDouble(self, nullptr, section, key, def);
    double actual = self->GetDouble(section, key, def);
    ei::verify::Check("CIniFile::GetDouble", Bits(expected), Bits(actual), "[%s] %s", section, key);
    return expected;
}
EI_VERIFY(0x00695F80, IniGetDoubleVerify, 7, s_iniGetDouble);

// 00696030: SUB ESP,0x404 (6 bytes).
static mfc::CString*(__fastcall* s_iniGetString)(CIniFile*, void*, mfc::CString*, const char*, const char*, const char*);
static mfc::CString* __fastcall IniGetStringVerify(CIniFile* self, void*, mfc::CString* result, const char* section,
                                                   const char* key, const char* def) {
    s_iniGetString(self, nullptr, result, section, key, def);
    mfc::CString actual = self->GetString(section, key, def);
    ei::verify::Check("CIniFile::GetString", true, std::strcmp(*result, actual) == 0, "[%s] %s", section, key);
    return result;
}
EI_VERIFY(0x00696030, IniGetStringVerify, 6, s_iniGetString);

// 00696090: MOV EAX,FS:[0] (6 bytes). Both append: the original to an empty scratch array, ours to
// the real one; the appended items compared.
static BOOL(__fastcall* s_iniGetList)(CIniFile*, void*, const char*, const char*, console::CStringArray*, int);
static BOOL __fastcall IniGetListVerify(CIniFile* self, void*, const char* section, const char* key,
                                        console::CStringArray* out, int separator) {
    console::CStringArray scratch;
    BOOL expected = s_iniGetList(self, nullptr, section, key, &scratch, separator);
    int before = out->GetSize();
    BOOL actual = self->GetList(section, key, out, static_cast<char>(separator));
    ei::verify::Check("CIniFile::GetList", true, expected == actual && SameStrings(scratch, 0, *out, before),
                      "[%s] %s", section, key);
    return expected;
}
EI_VERIFY(0x00696090, IniGetListVerify, 6, s_iniGetList);

// The array readers: PUSH -1; PUSH imm32 (7 bytes). The original fills an empty scratch array.
#define EI_INI_ALIGNED_WRAPPER(Name, Method, ArrayType, Element)                                               \
    static BOOL(__fastcall* s_##Name)(CIniFile*, void*, const char*, const char*, ArrayType*);                 \
    static BOOL __fastcall Name##Verify(CIniFile* self, void*, const char* section, const char* key,          \
                                        ArrayType* out) {                                                      \
        ArrayType scratch{};                                                                                   \
        BOOL expected = s_##Name(self, nullptr, section, key, &scratch);                                       \
        BOOL actual = self->Method(section, key, out);                                                         \
        bool same = expected == actual && (!expected || SameBytes(scratch.data, scratch.size, out->data,      \
                                                                  out->size, Element));                        \
        ei::verify::Check("CIniFile::" #Method, true, same, "[%s] %s", section, key);                         \
        FreeAlignedScratch(scratch.data);                                                                      \
        return expected;                                                                                       \
    }

EI_INI_ALIGNED_WRAPPER(IniIntArrayCopy, GetIntArrayCopy, DwordArray, 4)
EI_INI_ALIGNED_WRAPPER(IniByteArray, GetByteArray, lib::AlignedArray<std::uint8_t>, 1)
EI_INI_ALIGNED_WRAPPER(IniShortArray, GetShortArray, lib::AlignedArray<std::uint16_t>, 2)
EI_INI_ALIGNED_WRAPPER(IniIntArray, GetIntArray, DwordArray, 4)
EI_VERIFY(0x006961D0, IniIntArrayCopyVerify, 7, s_IniIntArrayCopy);
EI_VERIFY(0x006963F0, IniByteArrayVerify, 7, s_IniByteArray);
EI_VERIFY(0x00696610, IniShortArrayVerify, 7, s_IniShortArray);
EI_VERIFY(0x00696830, IniIntArrayVerify, 7, s_IniIntArray);

// MFC arrays: the scratch has no vtable (SetSize does not use it); its data is freed with
// operator delete, as MFC's destructors do.
#define EI_INI_MFC_WRAPPER(Name, Method, Type)                                                                 \
    static BOOL(__fastcall* s_##Name)(CIniFile*, void*, const char*, const char*, mfc::CArray<Type>*);        \
    static BOOL __fastcall Name##Verify(CIniFile* self, void*, const char* section, const char* key,          \
                                        mfc::CArray<Type>* out) {                                              \
        mfc::CArray<Type> scratch{};                                                                           \
        BOOL expected = s_##Name(self, nullptr, section, key, &scratch);                                       \
        BOOL actual = self->Method(section, key, out);                                                         \
        bool same = expected == actual && (!expected || SameBytes(scratch.data, scratch.size, out->data,      \
                                                                  out->size, sizeof(Type)));                   \
        ei::verify::Check("CIniFile::" #Method, true, same, "[%s] %s", section, key);                         \
        if (scratch.data) gamecrt::operator_delete(scratch.data);                                              \
        return expected;                                                                                       \
    }

EI_INI_MFC_WRAPPER(IniMfcByteArray, GetMfcByteArray, std::uint8_t)
EI_INI_MFC_WRAPPER(IniMfcWordArray, GetMfcWordArray, std::uint16_t)
EI_INI_MFC_WRAPPER(IniMfcUIntArray, GetMfcUIntArray, std::uint32_t)
EI_INI_MFC_WRAPPER(IniMfcDWordArray, GetMfcDWordArray, std::uint32_t)
EI_VERIFY(0x006969F0, IniMfcByteArrayVerify, 7, s_IniMfcByteArray);
EI_VERIFY(0x00696B20, IniMfcWordArrayVerify, 7, s_IniMfcWordArray);
EI_VERIFY(0x00696C50, IniMfcUIntArrayVerify, 7, s_IniMfcUIntArray);
EI_VERIFY(0x00696D80, IniMfcDWordArrayVerify, 7, s_IniMfcDWordArray);

// Construction: ours only.
static void* s_iniUnused;
static CIniFile* __fastcall IniConstructVerify(CIniFile* self) {
    CIniFile* result = self->Construct();
    ei::verify::Check("CIniFile::Construct", true, result == self && self->path_ && !self->path_[0], "%p", self);
    return result;
}
EI_VERIFY(0x00695B80, IniConstructVerify, 0, s_iniUnused);

static CIniFile* __fastcall IniConstructWithPathVerify(CIniFile* self, void*, const char* path) {
    CIniFile* result = self->ConstructWithPath(path);
    ei::verify::Check("CIniFile::ConstructWithPath", true, result == self && std::strcmp(self->path_, path) == 0, "%s",
                      path);
    return result;
}
EI_VERIFY(0x00695B90, IniConstructWithPathVerify, 0, s_iniUnused);

static BOOL __fastcall IniSetPathVerify(CIniFile* self, void*, const char* path) {
    BOOL result = self->SetPath(path);
    ei::verify::Check("CIniFile::SetPath", true, result == 1 && std::strcmp(self->path_, path) == 0, "%s", path);
    return result;
}
EI_VERIFY(0x00695BF0, IniSetPathVerify, 0, s_iniUnused);
