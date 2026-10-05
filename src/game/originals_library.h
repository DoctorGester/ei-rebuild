#pragma once

// Original functions forwarded from several files (one declaration per address; the bodies
// jump to game.exe). Types are forward declared; include the class headers where members are used.

#include "ei/hook.h"

#include <cstddef>
#include <cstdint>
#include "lib/mfc.h"

struct AnimationSteps;
struct RefPtr;

// STLport std::string destructor (frees the buffer back to the node allocator unless it is the inline one).
__declspec(naked) inline void __fastcall StlStringDestroy(void* /*string*/) EI_ORIGINAL(0x0040C720)
// STLport string on the node allocator (the 12-byte begin / end / capacity rep): 0041F460 constructs an
// empty one and returns `this` (RET); 004217E0 does the same with an allocator argument (RET 4, returns
// `this`); 00428F10 assigns the range [first, last) (RET 8); 00425040 frees the buffer (RET).
__declspec(naked) inline void* __fastcall StringInitEmpty(void* /*string*/) EI_ORIGINAL(0x0041F460)
__declspec(naked) inline void* __fastcall StringInitWithAllocator(void* /*string*/, void*, const void* /*alloc*/)
    EI_ORIGINAL(0x004217E0)
__declspec(naked) inline void __fastcall StringBufferFree(void* /*string*/) EI_ORIGINAL(0x00425040)
__declspec(naked) inline void __fastcall StringAssignSpan(void* /*string*/, void*, const char* /*first*/, const char* /*last*/)
    EI_ORIGINAL(0x00428F10)
// MSVC 6 iostream ostream members on the game's streams: operator<<(manipulator function) 00434FE0,
// operator<<(char) 006FC0B0, operator<<(const char*) 006FC31D. All RET 4 and return the stream (`this`).
__declspec(naked) inline void* __fastcall OstreamManipulate(void* /*stream*/, void*, void* /*manipulator*/)
    EI_ORIGINAL(0x00434FE0)
__declspec(naked) inline void* __fastcall OstreamPutChar(void* /*stream*/, void*, char) EI_ORIGINAL(0x006FC0B0)
__declspec(naked) inline void* __fastcall OstreamPutString(void* /*stream*/, void*, const char*) EI_ORIGINAL(0x006FC31D)
__declspec(naked) inline void** __fastcall PartiesEnd(void*, void*, void**) EI_ORIGINAL(0x004208A0)
__declspec(naked) inline void __fastcall List25BCClear(void* /*list*/) EI_ORIGINAL(0x00421CD0)
__declspec(naked) inline void __fastcall List25B0FreeNode(void* /*list*/, void*, void* /*node*/, int /*n*/)
    EI_ORIGINAL(0x00422390)
__declspec(naked) inline void* __fastcall ListConstruct4231C0(void* /*list*/, void*, const void* /*alloc*/)
    EI_ORIGINAL(0x004231C0)
__declspec(naked) inline void __fastcall List2610Destruct(void* /*list*/) EI_ORIGINAL(0x00423370)
__declspec(naked) inline void __fastcall List25ACFree(void* /*list*/, void*, void* /*node*/, int /*n*/)
    EI_ORIGINAL(0x00425A40)
__declspec(naked) inline void __fastcall List25B0Clear(void* /*list*/) EI_ORIGINAL(0x00427100)
__declspec(naked) inline void __fastcall List2598Clear(void* /*list*/) EI_ORIGINAL(0x004271C0)
__declspec(naked) inline void** __fastcall PartiesErase(void*, void*, void**, void*) EI_ORIGINAL(0x004294D0)
__declspec(naked) inline RefPtr* __fastcall UninitializedCopy(const RefPtr* /*first*/, const RefPtr* /*last*/, RefPtr* /*result*/, std::uint32_t /*tag*/)
    EI_ORIGINAL(0x0042AF80)
__declspec(naked) inline void __stdcall ZeroPoints(void*, int) EI_ORIGINAL(0x00452DC0)
__declspec(naked) inline void __fastcall StringsCopyOriginal(void*, void*, const void*) EI_ORIGINAL(0x004C14A0)
__declspec(naked) inline void __fastcall GroupsCopy(void*, void*, const void*) EI_ORIGINAL(0x004C1BE0)
__declspec(naked) inline void __fastcall NamesSetSizeOriginal(void*, void*, int, int) EI_ORIGINAL(0x004C2510)
__declspec(naked) inline void __fastcall FloatsSetSize(void*, void*, int, int) EI_ORIGINAL(0x004C2880)
__declspec(naked) inline void __fastcall CommandsCopy(void*, void*, const void*) EI_ORIGINAL(0x004CC0C0)
__declspec(naked) inline void __fastcall CellListDestroy(void*) EI_ORIGINAL(0x00539890)
__declspec(naked) inline void __fastcall FirstListConstruct(void*, void*, const void*) EI_ORIGINAL(0x0053F4F0)
__declspec(naked) inline void __fastcall StepsSetSize(AnimationSteps*, void*, int, int) EI_ORIGINAL(0x00557B80)
__declspec(naked) inline void __fastcall NodeFree12Fast(void*, int) EI_ORIGINAL(0x0056FB40)
__declspec(naked) inline void __fastcall ConstructModelMap(void*, void*, const void*) EI_ORIGINAL(0x005FA420)
__declspec(naked) inline int __stdcall WsGetHostName(char*, int) EI_ORIGINAL(0x006E6100)
__declspec(naked) inline char* __cdecl GameStrncpy(char* /*to*/, const char* /*from*/, std::size_t /*n*/)
    EI_ORIGINAL(0x006E7E80)
__declspec(naked) inline int __cdecl GameAtExit(void(__cdecl*)()) EI_ORIGINAL(0x006E727B)
__declspec(naked) inline int __cdecl GameAtoi(const char* /*text*/) EI_ORIGINAL(0x006E80D8)
__declspec(naked) inline void __cdecl ExitGame(int) EI_ORIGINAL(0x006E8DA7)
__declspec(naked) inline double __cdecl GameAtof(const char*) EI_ORIGINAL(0x006E911A)
__declspec(naked) inline const char* __cdecl MbsInc(const char*) EI_ORIGINAL(0x006E9947)
__declspec(naked) inline int __cdecl IsMbbLead(unsigned) EI_ORIGINAL(0x006E99F2)
__declspec(naked) inline int __cdecl IsLeadByte(int) EI_ORIGINAL(0x006E9A6C)
__declspec(naked) inline double __cdecl Hypot(double /*x*/, double /*y*/) EI_ORIGINAL(0x006FD026)
__declspec(naked) inline mfc::CString __stdcall PlusChar(const mfc::CString&, char) EI_ORIGINAL(0x006FD56E)
__declspec(naked) inline void __cdecl GameDelete(void*) EI_ORIGINAL(0x007005D8)
// MFC CStringArray constructor (006FE925; also console::CStringArray). Returns `this`.
__declspec(naked) inline void* __fastcall StringArrayConstruct(void* /*array*/) EI_ORIGINAL(0x006FE925)
// 006FE958 (destructor, plain RET) and 006FE9B8 (SetSize(count, growBy), RET 8; -1 keeps the grow step):
// neither returns a result.
__declspec(naked) inline void __fastcall StringArrayDestruct(void* /*array*/) EI_ORIGINAL(0x006FE958)
__declspec(naked) inline void __fastcall StringArraySetSize(void* /*array*/, void*, int /*count*/, int /*growBy*/)
    EI_ORIGINAL(0x006FE9B8)
// 004DBE40: splits the text at a separator into a CStringArray (ECX text, EDX array, RET 4 for the character).
__declspec(naked) inline void __fastcall StringSplit(const char* /*text*/, void* /*array*/, char /*separator*/)
    EI_ORIGINAL(0x004DBE40)
// The game's growing byte buffer (vtable 0073CA48 / 0073D624, 0x1C bytes): 004DA7F0 constructs it and returns
// `this` (plain RET), 004DA910 destroys it (plain RET).
__declspec(naked) inline void* __fastcall GrowingBufferConstruct(void* /*buffer*/) EI_ORIGINAL(0x004DA7F0)
__declspec(naked) inline void __fastcall GrowingBufferDestruct(void* /*buffer*/) EI_ORIGINAL(0x004DA910)
// STLport string (begin / end / capacity rep): 0040C5A0 constructs it from text and an allocator and returns
// `this` (RET 8); 0040C770 frees the buffer back to the node allocator (plain RET).
__declspec(naked) inline void* __fastcall StringInitFromChars(void* /*string*/, void*, const char* /*text*/,
    const void* /*alloc*/) EI_ORIGINAL(0x0040C5A0)
__declspec(naked) inline void __fastcall StringRepDestroy(void* /*string*/) EI_ORIGINAL(0x0040C770)
// The CRT's memmove (cdecl, returns the destination).
__declspec(naked) inline void* __cdecl GameMemmove(void* /*to*/, const void* /*from*/, std::size_t /*n*/)
    EI_ORIGINAL(0x006E79A0)
// mfc::CString::~CString (00700970) on a string given as raw memory.
inline void CStringDestroy(void* s) { static_cast<mfc::CString*>(s)->~CString(); }
__declspec(naked) inline int __stdcall AfxMessageBox(const char*, unsigned, unsigned) EI_ORIGINAL(0x0070AC94)

// STLport std::list constructor for a list of pointers on the 12-byte node allocator (RET 4).
__declspec(naked) inline void __fastcall ListConstruct411850(void* /*list*/, void*, const void* /*alloc*/)
    EI_ORIGINAL(0x00411850)
// MFC: CFileFind::FindNextFile / GetFileTitle, CFile::Write (CString::Mid(first) is a member of mfc::CString).
__declspec(naked) inline int __fastcall CFileFindNext(mfc::CFileFind* /*find*/) EI_ORIGINAL(0x006E6653)
__declspec(naked) inline mfc::CString* __fastcall CFileFindTitle(const mfc::CFileFind* /*find*/, void*,
                                                                  mfc::CString* /*out*/) EI_ORIGINAL(0x006E67BA)
__declspec(naked) inline void __fastcall CFileWrite(mfc::CFile* /*file*/, void*, const void* /*buffer*/,
                                                    unsigned /*count*/) EI_ORIGINAL(0x0070133F)
// The CRT's x87 intrinsics: _CIacos / _CIasin (argument and result in ST0) and _CIpow (base in ST1,
// exponent in ST0), called here with ordinary double arguments.
__declspec(naked) inline double __cdecl CIacos(double) {
    __asm fld qword ptr [esp + 4]
    __asm mov eax, 0x006E7320
    __asm call eax
    __asm ret
}
__declspec(naked) inline double __cdecl CIasin(double) {
    __asm fld qword ptr [esp + 4]
    __asm mov eax, 0x006E83C0
    __asm call eax
    __asm ret
}
__declspec(naked) inline double __cdecl CIpow(double, double) {
    __asm fld qword ptr [esp + 4]
    __asm fld qword ptr [esp + 12]
    __asm mov eax, 0x006E81A0
    __asm call eax
    __asm ret
}

// MFC CPtrArray::SetSize(count, growBy) (RET 8, no result): the script group / reference arrays.
__declspec(naked) inline void __fastcall PtrArraySetSize(void* /*array*/, void*, int /*count*/, int /*growBy*/)
    EI_ORIGINAL(0x004C2050)
// CRT _mbscmp (plain RET, EAX: <0, 0, >0). src/lib/crt.cpp keeps its own copy (lib cannot include game headers).
__declspec(naked) inline int __cdecl GameMbscmp(const char*, const char*) EI_ORIGINAL(0x006E848B)
