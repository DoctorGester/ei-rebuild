// Checks for MFC's CArchive buffer and string operators (src/lib/mfc_archive.cpp, ticket S8). The
// archive functions move the buffer and the file, so they run ours only, with checks of what they
// leave (the bytes a store put and a load took, the buffer's bounds). CString's assignment from a wide
// string and _wcstombsz are pure: ours runs, then the original into scratch storage, and the
// results are compared. A start-up check stores values of every width, counts and strings (short, 0xFF+, 0xFFFE+)
// through a small buffer into a file in a private temporary folder, loads them back, loads a wide
// string and runs off the end (endOfFile).
#include "verify.h"

#include "../../src/lib/gameapi.h"
#include "../../src/lib/mfc_archive.h"

#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include <vector>

// The handler type named as the game's class (mfc_try.h declares CException and CFileException).
class CArchiveException {
    CArchiveException() = delete;
};

#pragma warning(disable : 4740) // naked forwarders

namespace {

using ei::verify::Check;
using mfc::impl::CArchive;
using mfc::impl::CFile;
using mfc::impl::CString;
void* s_unused;

const char* Text(const CString& s) { return static_cast<const char*>(s); }

bool InBuffer(const CArchive* ar) { return ar->bufferCur >= ar->bufferStart && ar->bufferCur <= ar->bufferMax; }

template <class T>
T Before(const CArchive* ar) {
    T value;
    std::memcpy(&value, ar->bufferCur - sizeof(T), sizeof(T));
    return value;
}

// ---- The buffer ------------------------------------------------------------------------------------

unsigned __fastcall ReadVerify(CArchive* self, void*, void* buffer, unsigned count) {
    const unsigned result = self->Read(buffer, count);
    Check("CArchive::Read", true, result <= count && InBuffer(self), "%u of %u", result, count);
    return result;
}

void __fastcall WriteVerify(CArchive* self, void*, const void* buffer, unsigned count) {
    self->Write(buffer, count);
    Check("CArchive::Write", true, InBuffer(self), "%u", count);
}

void __fastcall FlushVerify(CArchive* self) {
    self->Flush();
    Check("CArchive::Flush", true, self->bufferCur == (self->IsLoading() ? self->bufferMax : self->bufferStart), "");
}

void __fastcall FillBufferVerify(CArchive* self, void*, unsigned needed) {
    const unsigned total = needed + static_cast<unsigned>(self->bufferMax - self->bufferCur);
    self->FillBuffer(needed);   // throws endOfFile when the file has too little
    Check("CArchive::FillBuffer", true, static_cast<unsigned>(self->bufferMax - self->bufferCur) >= total && InBuffer(self), "%u", needed);
}

void __fastcall WriteCountVerify(CArchive* self, void*, unsigned long count) {
    self->WriteCount(count);
    const bool ok = count < 0xFFFF ? Before<unsigned short>(self) == count : Before<unsigned long>(self) == count;
    Check("CArchive::WriteCount", true, ok, "%lu", count);
}

unsigned long __fastcall ReadCountVerify(CArchive* self) {
    const unsigned long result = self->ReadCount();
    const bool ok = result < 0xFFFF ? Before<unsigned short>(self) == result : Before<unsigned long>(self) == result;
    Check("CArchive::ReadCount", true, ok, "%lu", result);
    return result;
}

// The operators: the value is the bytes just before the buffer's position.
template <class T, int Id>
struct Store {
    static inline CArchive* (CArchive::*ours)(T);
    static inline const char* name;
    static CArchive* __fastcall Verify(CArchive* self, void*, T value) {
        CArchive* result = (self->*ours)(value);
        Check(name, true, result == self && Before<T>(self) == value && InBuffer(self), "%lu", static_cast<unsigned long>(value));
        return result;
    }
};
using StoreByte = Store<unsigned char, 0>;
using StoreWord = Store<unsigned short, 0>;
using StoreDword = Store<unsigned long, 0>;

template <class T, int Id>
struct Load {
    static inline CArchive* (CArchive::*ours)(T&);
    static inline const char* name;
    static CArchive* __fastcall Verify(CArchive* self, void*, T& value) {
        CArchive* result = (self->*ours)(value);
        Check(name, true, result == self && Before<T>(self) == value && InBuffer(self), "%lu", static_cast<unsigned long>(value));
        return result;
    }
};
using LoadByte = Load<unsigned char, 0>;
using LoadWord = Load<unsigned short, 0>;
using LoadDword = Load<unsigned long, 0>;

unsigned __stdcall ReadStringLengthVerify(CArchive& ar) {
    const unsigned result = mfc::impl::ReadStringLength(ar);
    Check("AfxReadStringLength", true, InBuffer(&ar), "%u", result);
    return result;
}

CArchive& __stdcall StoreTextVerify(CArchive& ar, const CString& text) {
    CArchive& result = mfc::impl::StoreText(ar, text);
    Check("operator<<(CArchive&, const CString&)", true, &result == &ar && InBuffer(&ar), "%d", text.GetLength());
    return result;
}

CArchive& __stdcall LoadTextVerify(CArchive& ar, CString& text) {
    CArchive& result = mfc::impl::LoadText(ar, text);
    Check("operator>>(CArchive&, CString&)", true,
          &result == &ar && InBuffer(&ar) && static_cast<int>(std::strlen(Text(text))) <= text.GetLength(), "%d", text.GetLength());
    return result;
}

// ---- Wide strings ----------------------------------------------------------------------------------

// 00700B20: PUSH EBX; MOV EBX,[ESP+8] (5 bytes). Ours into the string, then the original into a fresh
// one (SV2h1: no block of the original's alive while ours allocates).
CString*(__fastcall* s_assignWide)(CString*, void*, const wchar_t*);
CString* __fastcall AssignWideVerify(CString* self, void*, const wchar_t* text) {
    CString* result = mfc::impl::AssignWide(self, nullptr, text);
    CString expected;
    s_assignWide(&expected, nullptr, text);
    Check("CString::operator=(LPCWSTR)", true, result == self && std::strcmp(Text(expected), Text(*self)) == 0,
          "\"%.80s\" (original \"%.80s\")", Text(*self), Text(expected));
    return result;
}

// 00700F63: PUSH EBX; XOR EBX,EBX; CMP [ESP+0x10],EBX (7 bytes). The original into scratch.
int(__cdecl* s_wideToMultiByte)(char*, const wchar_t*, unsigned);
int __cdecl WideToMultiByteVerify(char* to, const wchar_t* from, unsigned count) {
    std::vector<char> scratch(to && count ? count : 1);
    const int expected = s_wideToMultiByte(to ? scratch.data() : nullptr, from, count);
    const int actual = mfc::impl::WideToMultiByte(to, from, count);
    const bool ok = actual == expected && (!to || actual <= 0 || std::memcmp(to, scratch.data(), static_cast<std::size_t>(actual)) == 0);
    Check("_wcstombsz", true, ok, "%d (original %d), room %u", actual, expected, count);
    return actual;
}

// ---- Start-up check --------------------------------------------------------------------------------

// The functions through their addresses (the wrappers above).
__declspec(naked) CArchive* __fastcall StoreByteAt(CArchive*, void*, unsigned char) EI_ORIGINAL(0x006E6214)
__declspec(naked) CArchive* __fastcall StoreWordAt(CArchive*, void*, unsigned short) EI_ORIGINAL(0x006E6237)
__declspec(naked) CArchive* __fastcall StoreDwordAt(CArchive*, void*, unsigned long) EI_ORIGINAL(0x006E625E)
__declspec(naked) CArchive* __fastcall LoadByteAt(CArchive*, void*, unsigned char&) EI_ORIGINAL(0x006E6284)
__declspec(naked) CArchive* __fastcall LoadWordAt(CArchive*, void*, unsigned short&) EI_ORIGINAL(0x006E62B3)
__declspec(naked) CArchive* __fastcall LoadDwordAt(CArchive*, void*, unsigned long&) EI_ORIGINAL(0x006E62E6)
__declspec(naked) void __fastcall WriteCountAt(CArchive*, void*, unsigned long) EI_ORIGINAL(0x00708B0F)
__declspec(naked) unsigned long __fastcall ReadCountAt(CArchive*) EI_ORIGINAL(0x00708B3D)
__declspec(naked) void __fastcall WriteAt(CArchive*, void*, const void*, unsigned) EI_ORIGINAL(0x00708941)
__declspec(naked) unsigned __fastcall ReadAt(CArchive*, void*, void*, unsigned) EI_ORIGINAL(0x00708832)
__declspec(naked) void __fastcall FlushAt(CArchive*) EI_ORIGINAL(0x007089D1)
__declspec(naked) CArchive& __stdcall StoreTextAt(CArchive&, const CString&) EI_ORIGINAL(0x00708447)
__declspec(naked) CArchive& __stdcall LoadTextAt(CArchive&, CString&) EI_ORIGINAL(0x00708517)
__declspec(naked) void __fastcall DeleteExceptionAt(void*) EI_ORIGINAL(0x00702069)

constexpr unsigned kBufferSize = 16;   // small, so the values cross buffer refills

// An archive as CArchive's constructor sets one up (not ported: the game builds none in our code).
struct Archive {
    CArchive ar{};   // the name constructed and destroyed with it
    unsigned char buffer[kBufferSize];
    Archive(CFile* file, bool loading) {
        ar.mode = loading ? CArchive::load : CArchive::store;
        ar.bufferSize = kBufferSize;
        ar.file = file;
        ar.bufferStart = buffer;
        ar.bufferMax = buffer + kBufferSize;
        ar.bufferCur = loading ? ar.bufferMax : ar.bufferStart;
    }
    Archive(const Archive&) = delete;
    Archive& operator=(const Archive&) = delete;
};

// A CFile constructed and destroyed through its library functions (storage only here).
struct OpenFile {
    alignas(CFile) unsigned char raw[sizeof(CFile)];
    CFile& file = *reinterpret_cast<CFile*>(raw);
    OpenFile(const char* path, unsigned flags) { file.ConstructOpen(path, flags); }
    ~OpenFile() { file.Destruct(); }
    OpenFile(const OpenFile&) = delete;
    OpenFile& operator=(const OpenFile&) = delete;
};

std::vector<unsigned char> Block() {
    std::vector<unsigned char> block(100);
    for (std::size_t i = 0; i < block.size(); ++i) block[i] = static_cast<unsigned char>(i * 7);
    return block;
}

void StoreAll(const char* path) {
    OpenFile file(path, CFile::modeCreate | CFile::modeWrite);
    Archive a(&file.file, false);
    StoreByteAt(&a.ar, nullptr, 0xA5);
    StoreWordAt(&a.ar, nullptr, 0x1234);
    StoreDwordAt(&a.ar, nullptr, 0xDEADBEEF);
    WriteCountAt(&a.ar, nullptr, 7);
    WriteCountAt(&a.ar, nullptr, 0x12345);
    CString text("short");
    StoreTextAt(a.ar, text);
    text = std::string(300, 'm').c_str();
    StoreTextAt(a.ar, text);
    text = std::string(70000, 'l').c_str();
    StoreTextAt(a.ar, text);
    const std::vector<unsigned char> block = Block();
    WriteAt(&a.ar, nullptr, block.data(), static_cast<unsigned>(block.size()));
    // A wide string as a UNICODE build stores one: 0xFF, 0xFFFE, then the length and UTF-16.
    StoreByteAt(&a.ar, nullptr, 0xFF);
    StoreWordAt(&a.ar, nullptr, 0xFFFE);
    StoreByteAt(&a.ar, nullptr, 2);
    StoreWordAt(&a.ar, nullptr, L'H');
    StoreWordAt(&a.ar, nullptr, L'i');
    FlushAt(&a.ar);
}

bool LoadAll(const char* path) {
    OpenFile file(path, CFile::modeRead);
    Archive a(&file.file, true);
    unsigned char byte = 0;
    unsigned short word = 0;
    unsigned long dword = 0;
    LoadByteAt(&a.ar, nullptr, byte);
    LoadWordAt(&a.ar, nullptr, word);
    LoadDwordAt(&a.ar, nullptr, dword);
    bool ok = byte == 0xA5 && word == 0x1234 && dword == 0xDEADBEEF;
    ok = ok && ReadCountAt(&a.ar) == 7 && ReadCountAt(&a.ar) == 0x12345;
    CString text;
    LoadTextAt(a.ar, text);
    ok = ok && std::strcmp(Text(text), "short") == 0;
    LoadTextAt(a.ar, text);
    ok = ok && std::string(300, 'm') == Text(text);
    LoadTextAt(a.ar, text);
    ok = ok && std::string(70000, 'l') == Text(text);
    std::vector<unsigned char> block(100);
    ok = ok && ReadAt(&a.ar, nullptr, block.data(), static_cast<unsigned>(block.size())) == block.size() && block == Block();
    LoadTextAt(a.ar, text);
    ok = ok && std::strcmp(Text(text), "Hi") == 0;
    // Nothing left: a load runs off the end.
    bool ended = false;
    try {
        LoadDwordAt(&a.ar, nullptr, dword);
    } catch (::CArchiveException* thrown) {
        ended = reinterpret_cast<mfc::impl::CArchiveException*>(thrown)->cause == mfc::impl::CArchiveException::endOfFile;
        DeleteExceptionAt(thrown);
    }
    return ok && ended;
}

void ArchiveStartupCheck() {
    char temp[MAX_PATH];
    if (!::GetTempPathA(MAX_PATH, temp)) return;
    char name[64];
    std::snprintf(name, sizeof(name), "ei_mfc_archive_%lu.bin", ::GetCurrentProcessId());
    const std::string path = std::string(temp) + name;
    bool ok = false;
    try {
        StoreAll(path.c_str());
        ok = LoadAll(path.c_str());
    } catch (::CException* thrown) {
        DeleteExceptionAt(thrown);
    }
    gameapi::DeleteFileA(path.c_str());
    Check("CArchive round trip (start-up)", true, ok, "");
}
EI_STARTUP_CHECK(ArchiveStartupCheck);

struct Names {
    Names() {
        StoreByte::name = "CArchive::operator<<(BYTE)";
        StoreByte::ours = &CArchive::StoreByte;
        StoreWord::name = "CArchive::operator<<(WORD)";
        StoreWord::ours = &CArchive::StoreWord;
        StoreDword::name = "CArchive::operator<<(DWORD)";
        StoreDword::ours = &CArchive::StoreDword;
        LoadByte::name = "CArchive::operator>>(BYTE&)";
        LoadByte::ours = &CArchive::LoadByte;
        LoadWord::name = "CArchive::operator>>(WORD&)";
        LoadWord::ours = &CArchive::LoadWord;
        LoadDword::name = "CArchive::operator>>(DWORD&)";
        LoadDword::ours = &CArchive::LoadDword;
    }
} s_names;

} // namespace

EI_VERIFY(0x00708832, ReadVerify, 0, s_unused);
EI_VERIFY(0x00708941, WriteVerify, 0, s_unused);
EI_VERIFY(0x007089D1, FlushVerify, 0, s_unused);
EI_VERIFY(0x00708A4D, FillBufferVerify, 0, s_unused);
EI_VERIFY(0x00708B0F, WriteCountVerify, 0, s_unused);
EI_VERIFY(0x00708B3D, ReadCountVerify, 0, s_unused);
EI_VERIFY(0x006E6214, StoreByte::Verify, 0, s_unused);
EI_VERIFY(0x006E6237, StoreWord::Verify, 0, s_unused);
EI_VERIFY(0x006E625E, StoreDword::Verify, 0, s_unused);
EI_VERIFY(0x006E6284, LoadByte::Verify, 0, s_unused);
EI_VERIFY(0x006E62B3, LoadWord::Verify, 0, s_unused);
EI_VERIFY(0x006E62E6, LoadDword::Verify, 0, s_unused);
EI_VERIFY(0x007084C0, ReadStringLengthVerify, 0, s_unused);
EI_VERIFY(0x00708447, StoreTextVerify, 0, s_unused);
EI_VERIFY(0x00708517, LoadTextVerify, 0, s_unused);
EI_VERIFY(0x00700B20, AssignWideVerify, 5, s_assignWide);
EI_VERIFY(0x00700F63, WideToMultiByteVerify, 7, s_wideToMultiByte);
