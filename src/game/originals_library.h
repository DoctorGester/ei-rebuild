#pragma once

// Original functions forwarded from several files (one declaration per address; the bodies
// jump to game.exe). Types are forward declared; include the class headers where members are used.

#include "ei/hook.h"

#include <cstddef>
#include <cstdint>
#include <new>
#include "lib/crt.h"
#include "lib/mfc.h"

class CFileRegistry;
class CStream;
namespace lib { template <typename T> struct AlignedArray; }
namespace console { class CStringArray; }
namespace stlp { class string; }

__declspec(naked) inline int __cdecl GameAtExit(void(__cdecl*)()) EI_ORIGINAL(0x006E727B)
__declspec(naked) inline int __cdecl GameAtoi(const char* /*text*/) EI_ORIGINAL(0x006E80D8)
// The CRT's _getptd (006F00D1, plain RET): the calling thread's _tiddata (allocated on first use).
__declspec(naked) inline void* __cdecl CrtThreadDataOriginal() EI_ORIGINAL(0x006F00D1)
__declspec(naked) inline double __cdecl GameAtof(const char*) EI_ORIGINAL(0x006E911A)
__declspec(naked) inline const char* __cdecl MbsInc(const char*) EI_ORIGINAL(0x006E9947)
// MFC CStringArray constructor (006FE925, mfc::CStringArray::Construct). Returns `this`.
inline void* __fastcall StringArrayConstruct(void* array) {
    static_cast<mfc::CStringArray*>(array)->Construct();
    return array;
}
// 006FE958 (destructor, plain RET) and 006FE9B8 (SetSize(count, growBy), RET 8; -1 keeps the grow step):
// neither returns a result.
inline void __fastcall StringArrayDestruct(void* array) { static_cast<mfc::CStringArray*>(array)->Destruct(); }
inline void __fastcall StringArraySetSize(void* array, void*, int count, int growBy) {
    static_cast<mfc::CStringArray*>(array)->SetSize(count, growBy);
}
// The game's growing byte buffer (vtable 0073CA48 / 0073D624, 0x1C bytes): 004DA7F0 constructs it and returns
// `this` (plain RET), 004DA910 destroys it (plain RET).
__declspec(naked) inline void __fastcall GrowingBufferDestruct(void* /*buffer*/) EI_ORIGINAL(0x004DA910)
// The CRT's memmove (cdecl, returns the destination).
__declspec(naked) inline void* __cdecl GameMemmove(void* /*to*/, const void* /*from*/, std::size_t /*n*/)
    EI_ORIGINAL(0x006E79A0)
// mfc::CString::~CString (00700970) on a string given as raw memory.
inline void CStringDestroy(void* s) { static_cast<mfc::CString*>(s)->~CString(); }
// MFC CWnd::DestroyWindow 007051A1 (plain RET; nonzero when the window was destroyed): CWnd's destructor
// (mfc::impl::CWnd, src/lib/mfc_window.cpp) on a window still attached; no game window is a CWnd.
__declspec(naked) inline int __fastcall CWndDestroyWindow(void* /*wnd*/) EI_ORIGINAL(0x007051A1)

// MFC: CFileFind::FindNextFile / GetFileTitle, CFile::Write (CString::Mid(first) is a member of mfc::CString).
__declspec(naked) inline int __fastcall CFileFindNext(mfc::CFileFind* /*find*/) EI_ORIGINAL(0x006E6653)
__declspec(naked) inline mfc::CString* __fastcall CFileFindTitle(const mfc::CFileFind* /*find*/, void*,
                                                                  mfc::CString* /*out*/) EI_ORIGINAL(0x006E67BA)
// The CRT's x87 intrinsics _CIacos / _CIasin (argument and result in ST0) and _CIpow (base in ST1,
// exponent in ST0), here with ordinary double arguments: the same bodies as acos 006E7334 / asin
// 006E83D4 and libcrt::CiPow.
inline double CIacos(double c) { return gamecrt::acos(c); }
inline double CIasin(double s) { return libcrt::CiAsin(s); }
inline double CIpow(double x, double y) { return libcrt::CiPow(x, y); }

// MFC CPtrArray::SetSize(count, growBy) (RET 8, no result): the script group / reference arrays.
// CRT _mbscmp (plain RET, EAX: <0, 0, >0); src/lib/crt.cpp includes this header for gamecrt::mbscmp.
__declspec(naked) inline int __cdecl GameMbscmp(const char*, const char*) EI_ORIGINAL(0x006E848B)
// The STLport node allocator's deallocate(p, bytes): ECX p, EDX bytes, plain RET (src/lib/stlp.cpp uses it too).
__declspec(naked) inline void __fastcall StlNodeFreeOriginal(void* /*p*/, std::size_t /*bytes*/) EI_ORIGINAL(0x00404D70)
// MFC CStringArray::SetAtGrow(index, const char*) 006FEBCE / (index, const CString&) 006FEBFA (both RET 8, EAX not a
// result). CString::operator=(const CString&) 00700AA9 (RET 4, returns this). src/lib/mfc.cpp keeps its member.
inline void __fastcall StringArraySetAtGrowTextOriginal(void* array, void*, int index, const char* text) {
    static_cast<mfc::CStringArray*>(array)->SetAtGrow(index, text);
}
inline void __fastcall StringArraySetAtGrowStringOriginal(void* array, void*, int index, const mfc::CString* value) {
    static_cast<mfc::CStringArray*>(array)->SetAtGrow(index, *value);
}
// C3n batch 5: the CString calls below are the lib members (src/lib/mfc.cpp holds the one forwarder per address).
// MFC CString::operator=(const CString&) 00700AA9 (RET 4, returns this).
inline void* __fastcall CStringAssignOriginal(void* destination, void*, const void* source) {
    *static_cast<mfc::CString*>(destination) = *static_cast<const mfc::CString*>(source);
    return destination;
}
// MFC CString::operator=(const char*) 00700AF9 (RET 4, returns this); CString::Find(char) 00700E91 (RET 4, index or
// -1; the character is the low byte of the argument).
inline void* __fastcall CStringAssignTextOriginal(void* destination, void*, const char* text) {
    *static_cast<mfc::CString*>(destination) = text;
    return destination;
}
// MFC CString::Find(LPCTSTR) 006FDBD9 (RET 4, index or -1).
inline int __fastcall CStringFindTextOriginal(const void* string, void*, const char* text) {
    return static_cast<const mfc::CString*>(string)->Find(text);
}
// CPlex::FreeDataChain 006FE90F (plain RET, ECX = chain head; mfc::PlexFreeDataChain is the forwarder).
inline void __fastcall PlexFreeDataChainOriginal(void* chain) { mfc::PlexFreeDataChain(static_cast<mfc::CPlex*>(chain)); }

// C3h. 00405950: __node_alloc<true,0>::allocate(bytes) (ECX = bytes, plain RET, block or null).
__declspec(naked) inline void* __fastcall NodeAllocateOriginal(std::size_t /*bytes*/) EI_ORIGINAL(0x00405950)


// C3i part 2. Containers and strings (STLport / MFC), all decided from the disassembly.
// 004C98F0: pointer list AddTail (RET 4, EAX not a result). 0041FCD0: list end() iterator written to `out` (RET 4,
// returns out).
// 006FEB61: CStringArray::Copy(source) (RET 4, EAX not a result).
inline void __fastcall StringArrayCopyOriginal(void* array, void*, const void* source) {
    static_cast<mfc::CStringArray*>(array)->Copy(*static_cast<const mfc::CStringArray*>(source));
}

// Container instantiations inside the exe, decided from the disassembly.
// 00426860: node allocator deallocate(p, count) of 12-byte elements (ECX p, EDX count, plain RET).

// (The game streams' ostream members are libcrt::Ostream's, lib/crt_iostream.h.)
// MFC CString operator+(const CString&, const CString&) 00700B9F / (const CString&, const char*) 00700C05 (__stdcall, RET 0xC:
// the result slot, left, right; EAX = the result slot): src/lib/mfc.cpp's operator+. These build the result in `out` (raw
// storage), passed as the hidden result slot.
inline mfc::CString* __stdcall CStringConcatOriginal(mfc::CString* out, const mfc::CString* left, const mfc::CString* right) {
    return ::new (out) mfc::CString(*left + *right);
}
inline mfc::CString* __stdcall CStringConcatTextOriginal(mfc::CString* out, const mfc::CString* left, const char* right) {
    return ::new (out) mfc::CString(*left + right);
}
// MFC CFileFind (find handle at +0x0C): 006E6370 constructor (plain RET, returns this), 006E63AD destructor (plain RET, no
// result), 006E63E9 Close (plain RET, no result), 006E6433 FindFile(name, flags) (RET 8, EAX 1, or 0 when the search fails),
// 006E6722 GetFilePath(out) (RET 4, EAX = out). The constructor, destructor, FindFile and GetFilePath are mfc::CFileFind's
// members.
__declspec(naked) inline void __fastcall CFileFindCloseOriginal(void* /*find*/) EI_ORIGINAL(0x006E63E9)
// 006E661A: CFileFind::IsDots (plain RET, EAX 0 / 1).
__declspec(naked) inline int __fastcall CFileFindIsDotsOriginal(const void* /*find*/) EI_ORIGINAL(0x006E661A)
// GetFilePath into `out` (raw storage): the CString result is built in place, `out` passed as the hidden result slot.
inline mfc::CString* __fastcall CFileFindPathOriginal(const void* find, void*, mfc::CString* out) {
    return ::new (out) mfc::CString(static_cast<const mfc::CFileFind*>(find)->GetFilePath());
}
// 006E88FC: the CRT's qsort (cdecl, plain RET, no result).
__declspec(naked) inline void __cdecl GameQsortOriginal(void* /*base*/, std::size_t /*count*/, std::size_t /*size*/,
                                                        int(__cdecl* /*compare*/)(const void*, const void*))
    EI_ORIGINAL(0x006E88FC)
// 00700D4C: CString::operator+=(const char*) (RET 4, returns
// this). 00700E17: CString::GetBufferSetLength(length) (RET 4, EAX = the buffer). src/lib/mfc.cpp keeps its CString members.
inline void* __fastcall CStringAppendTextOriginal(void* string, void*, const char* text) {
    *static_cast<mfc::CString*>(string) += text;
    return string;
}
inline char* __fastcall CStringGetBufferSetLengthOriginal(void* string, void*, int length) {
    return static_cast<mfc::CString*>(string)->GetBufferSetLength(length);
}
// MFC CString::CString(const CString&) 007006E5 / CString::CString(const char*) 007009DE on raw memory (RET 4).
inline void __fastcall CStringCopyConstructOriginal(void* where, void*, const mfc::CString* from) {
    new (where) mfc::CString(*from);
}
inline void __fastcall CStringConstructTextOriginal(void* where, void*, const char* text) {
    new (where) mfc::CString(text);
}
// C3m. 006E9180: CRT strncmp (gamecrt::strncmp).
inline int __cdecl Strncmp(const char* a, const char* b, std::size_t n) { return gamecrt::strncmp(a, b, n); }

#include "string_arg.h"   // StringArg
// C3n. The engine's aligned arrays (lib::AlignedArray<T>, thiscall on the array): 004513A0 (rewritten as
// DwordArraySetSize2) SetSize(n, growBy); 00452FC0 / 004538D0 (rewritten as ShortArraySetAtGrow / DwordArraySetAtGrow)
// SetAtGrow(index, value) on 16-bit elements (the value pushed as a dword).
__declspec(naked) inline void __fastcall WordsSetAtGrow(lib::AlignedArray<std::uint16_t>* /*array*/, void*, int /*index*/,
                                                        int /*value*/) EI_ORIGINAL(0x00452FC0)
// C3n. 00461190 (rewritten as CStream::ReadString; thiscall on the stream, RET 4): reads a string from the stream and
// constructs it as a CString at `out` (raw storage the size of a CString), returning it.
__declspec(naked) inline mfc::CString* __fastcall StreamReadString(CStream* /*stream*/, void*, void* /*out*/)
    EI_ORIGINAL(0x00461190)
