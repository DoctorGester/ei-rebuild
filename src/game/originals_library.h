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
// The CRT's _getptd (006F00D1, plain RET): the calling thread's _tiddata (allocated on first use).
__declspec(naked) inline void* __cdecl CrtThreadDataOriginal() EI_ORIGINAL(0x006F00D1)
__declspec(naked) inline double __cdecl GameAtof(const char*) EI_ORIGINAL(0x006E911A)
// The CRT's _mbsinc (006E9947) and _mbscmp (006E848B) on char strings.
inline const char* __cdecl MbsInc(const char* p) {
    return reinterpret_cast<const char*>(libcrt::Mbsinc(reinterpret_cast<const unsigned char*>(p)));
}
inline int __cdecl GameMbscmp(const char* a, const char* b) {
    return libcrt::Mbscmp(reinterpret_cast<const unsigned char*>(a), reinterpret_cast<const unsigned char*>(b));
}
// The game's growing byte buffer (vtable 0073CA48 / 0073D624, 0x1C bytes): 004DA7F0 constructs it and returns
// `this` (plain RET), 004DA910 destroys it (plain RET).
__declspec(naked) inline void __fastcall GrowingBufferDestruct(void* /*buffer*/) EI_ORIGINAL(0x004DA910)
// mfc::CString::~CString (00700970) on a string given as raw memory.
inline void CStringDestroy(void* s) { static_cast<mfc::CString*>(s)->~CString(); }
// MFC CWnd::DestroyWindow 007051A1 (plain RET; nonzero when the window was destroyed): CWnd's destructor
// (mfc::impl::CWnd, src/lib/mfc_window.cpp) on a window still attached; no game window is a CWnd.
__declspec(naked) inline int __fastcall CWndDestroyWindow(void* /*wnd*/) EI_ORIGINAL(0x007051A1)

// The CRT's x87 intrinsics _CIacos / _CIasin (argument and result in ST0) and _CIpow (base in ST1,
// exponent in ST0), here with ordinary double arguments: the same bodies as acos 006E7334 / asin
// 006E83D4 and libcrt::CiPow.
inline double CIacos(double c) { return gamecrt::acos(c); }
inline double CIasin(double s) { return libcrt::CiAsin(s); }
inline double CIpow(double x, double y) { return libcrt::CiPow(x, y); }

// MFC CPtrArray::SetSize(count, growBy) (RET 8, no result): the script group / reference arrays.
// MFC CStringArray::SetAtGrow(index, const char*) 006FEBCE / (index, const CString&) 006FEBFA (both RET 8, EAX not a
// result). CString::operator=(const CString&) 00700AA9 (RET 4, returns this). src/lib/mfc.cpp keeps its member.
// C3n batch 5: the CString calls below are the lib members (src/lib/mfc.cpp holds the one forwarder per address).
// MFC CString::operator=(const CString&) 00700AA9 (RET 4, returns this).
// MFC CString::operator=(const char*) 00700AF9 (RET 4, returns this); CString::Find(char) 00700E91 (RET 4, index or
// -1; the character is the low byte of the argument).
// MFC CString::Find(LPCTSTR) 006FDBD9 (RET 4, index or -1).
// CPlex::FreeDataChain 006FE90F (plain RET, ECX = chain head; mfc::PlexFreeDataChain is the forwarder).
inline void __fastcall PlexFreeDataChainOriginal(void* chain) { mfc::PlexFreeDataChain(static_cast<mfc::CPlex*>(chain)); }


// C3i part 2. Containers and strings (STLport / MFC), all decided from the disassembly.
// 004C98F0: pointer list AddTail (RET 4, EAX not a result). 0041FCD0: list end() iterator written to `out` (RET 4,
// returns out).
// 006FEB61: CStringArray::Copy(source) (RET 4, EAX not a result).

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
// MFC CFileFind: mfc::CFileFind's members (lib/mfc.h). GetFilePath (006E6722) into `out` (raw storage): the CString result is built in place, `out` passed as the hidden result slot.
// 00700D4C: CString::operator+=(const char*) (RET 4, returns
// this). 00700E17: CString::GetBufferSetLength(length) (RET 4, EAX = the buffer). src/lib/mfc.cpp keeps its CString members.
// MFC CString::CString(const CString&) 007006E5 / CString::CString(const char*) 007009DE on raw memory (RET 4).
// C3m. 006E9180: CRT strncmp (gamecrt::strncmp).
inline int __cdecl Strncmp(const char* a, const char* b, std::size_t n) { return gamecrt::strncmp(a, b, n); }

#include "string_arg.h"   // StringArg
// C3n. The engine's aligned arrays (lib::AlignedArray<T>, thiscall on the array): 004513A0 (rewritten as
// DwordArraySetSize2) SetSize(n, growBy); 00452FC0 / 004538D0 (rewritten as ShortArraySetAtGrow / DwordArraySetAtGrow)
// SetAtGrow(index, value) on 16-bit elements (the value pushed as a dword).
__declspec(naked) inline void __fastcall WordsSetAtGrow(lib::AlignedArray<std::uint16_t>* /*array*/, void*, int /*index*/,
                                                        int /*value*/) EI_ORIGINAL(0x00452FC0)
