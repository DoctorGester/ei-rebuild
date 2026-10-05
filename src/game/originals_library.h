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
// STLport string members on the 12-byte rep (all __thiscall, EDX unused): assign [first, last) 00426A80
// (RET 8, returns this); erase [first, last) 0040B3C0 (RET 8, returns first); assign with an allocator
// tag 0042A4F0 (RET 0xC, returns this); the end-of-storage proxy 00425080 (RET 8, returns this).
__declspec(naked) inline void* __fastcall StringAssignRangeOriginal(void* /*string*/, void*, const char* /*first*/,
                                                                    const char* /*last*/) EI_ORIGINAL(0x00426A80)
__declspec(naked) inline char* __fastcall StringEraseOriginal(void* /*string*/, void*, char* /*first*/, char* /*last*/)
    EI_ORIGINAL(0x0040B3C0)
__declspec(naked) inline void* __fastcall StringAssignTaggedOriginal(void* /*string*/, void*, const char* /*first*/,
                                                                     const char* /*last*/, const void* /*tag*/)
    EI_ORIGINAL(0x0042A4F0)
__declspec(naked) inline void* __fastcall StringEndOfStorageOriginal(void* /*proxy*/, void*, const void* /*alloc*/,
                                                                     const void* /*value*/) EI_ORIGINAL(0x00425080)
// The STLport node allocator's deallocate(p, bytes): ECX p, EDX bytes, plain RET. src/lib/stlp.cpp keeps its own copy.
__declspec(naked) inline void __fastcall StlNodeFreeOriginal(void* /*p*/, std::size_t /*bytes*/) EI_ORIGINAL(0x00404D70)
// The CRT's `eh vector destructor iterator` (array, element size, count, destructor called with ECX = element, last
// to first): __stdcall, RET 0x10, EAX not a result.
__declspec(naked) inline void __stdcall EhVectorDestructorIterator(void* /*array*/, unsigned /*size*/, int /*count*/,
                                                                   void* /*destructor*/) EI_ORIGINAL(0x006E859F)
// MFC CStringArray::SetAtGrow(index, const char*) 006FEBCE / (index, const CString&) 006FEBFA (both RET 8, EAX not a
// result). CString::operator=(const CString&) 00700AA9 (RET 4, returns this). src/lib/mfc.cpp keeps its member.
__declspec(naked) inline void __fastcall StringArraySetAtGrowTextOriginal(void* /*array*/, void*, int /*index*/,
                                                                          const char* /*text*/) EI_ORIGINAL(0x006FEBCE)
__declspec(naked) inline void __fastcall StringArraySetAtGrowStringOriginal(void* /*array*/, void*, int /*index*/,
                                                                            const mfc::CString* /*value*/)
    EI_ORIGINAL(0x006FEBFA)
__declspec(naked) inline void* __fastcall CStringAssignOriginal(void* /*destination*/, void*, const void* /*source*/)
    EI_ORIGINAL(0x00700AA9)
// MFC CString::operator=(const char*) 00700AF9 (RET 4, returns this); CString::Find(char) 00700E91 (RET 4, index or
// -1; the character is the low byte of the argument). src/lib/mfc.cpp keeps its own member for the first.
__declspec(naked) inline void* __fastcall CStringAssignTextOriginal(void* /*destination*/, void*, const char* /*text*/)
    EI_ORIGINAL(0x00700AF9)
__declspec(naked) inline int __fastcall CStringFindCharOriginal(const void* /*string*/, void*, char /*c*/)
    EI_ORIGINAL(0x00700E91)
// MFC CPtrList::NewNode(prev, next) 004621A0 (RET 8, returns the node, data cleared); RemoveAll 0052CA10 (plain RET).
__declspec(naked) inline void* __fastcall PtrListNewNodeOriginal(void* /*list*/, void*, void* /*prev*/, void* /*next*/)
    EI_ORIGINAL(0x004621A0)
__declspec(naked) inline void __fastcall PtrListRemoveAllOriginal(void* /*list*/) EI_ORIGINAL(0x0052CA10)
// MFC CArray::SetSize(size, growBy) instantiations (all RET 8, no result): records 005719B0, dwords 0059C660,
// points 005B16D0, words 006FEEFA, bytes 006FF64A.
__declspec(naked) inline void __fastcall RecordArraySetSizeOriginal(void* /*array*/, void*, int /*size*/, int /*growBy*/)
    EI_ORIGINAL(0x005719B0)
__declspec(naked) inline void __fastcall DwordArraySetSizeOriginal(void* /*array*/, void*, int /*size*/, int /*growBy*/)
    EI_ORIGINAL(0x0059C660)
__declspec(naked) inline void __fastcall PointArraySetSizeOriginal(void* /*array*/, void*, int /*size*/, int /*growBy*/)
    EI_ORIGINAL(0x005B16D0)
__declspec(naked) inline void __fastcall WordArraySetSizeOriginal(void* /*array*/, void*, int /*size*/, int /*growBy*/)
    EI_ORIGINAL(0x006FEEFA)
__declspec(naked) inline void __fastcall ByteArraySetSizeOriginal(void* /*array*/, void*, int /*size*/, int /*growBy*/)
    EI_ORIGINAL(0x006FF64A)
// ostream::operator<<(long) 006FC9C6 (RET 4, returns the stream). CPlex::FreeDataChain 006FE90F (plain RET, ECX =
// chain head). src/lib/mfc.cpp keeps its own PlexFreeDataChain.
__declspec(naked) inline void* __fastcall OstreamPutLongOriginal(void* /*stream*/, void*, long /*value*/)
    EI_ORIGINAL(0x006FC9C6)
__declspec(naked) inline void __fastcall PlexFreeDataChainOriginal(void* /*chain*/) EI_ORIGINAL(0x006FE90F)

// C3h. 00405950: __node_alloc<true,0>::allocate(bytes) (ECX = bytes, plain RET, block or null).
__declspec(naked) inline void* __fastcall NodeAllocateOriginal(std::size_t /*bytes*/) EI_ORIGINAL(0x00405950)
// 00421800: STLport string constructor with room for n bytes (RET 8: allocator, n; returns `this`).
__declspec(naked) inline void* __fastcall StringReserveOriginal(void* /*string*/, void*, const void* /*alloc*/,
                                                                std::size_t /*n*/) EI_ORIGINAL(0x00421800)
// 00426450: the string allocator's deallocate(p, n) (RET 8; ECX and EDX are not read, so the dummies are dropped).
__declspec(naked) inline void __stdcall StringDeallocateOriginal(char* /*p*/, std::size_t /*n*/) EI_ORIGINAL(0x00426450)
// 00427830: CPtrList destructor (plain RET); 00429F10: CPtrList::RemoveAll (plain RET). EAX is not a result.
__declspec(naked) inline void __fastcall PtrListDestructOriginal(void* /*list*/) EI_ORIGINAL(0x00427830)
__declspec(naked) inline void __fastcall PtrListRemoveAllNodesOriginal(void* /*list*/) EI_ORIGINAL(0x00429F10)
// 004297A0: hash_map<string, ...>::operator[](key) (RET 4): the address of the value slot (node + 4 or the new node + 0xC).
__declspec(naked) inline void* __fastcall MapRegistryFindOriginal(void* /*map*/, void*, const void* /*key*/)
    EI_ORIGINAL(0x004297A0)
// 0042A3D0: STLport string constructor from [first, last) (RET 0xC; the third argument is not read). EAX is the end pointer.
__declspec(naked) inline char* __fastcall StringInitRangeOriginal(void* /*string*/, void*, const char* /*first*/,
                                                                  const char* /*last*/, const void* /*tag*/)
    EI_ORIGINAL(0x0042A3D0)
// 004DA5C0: the game buffer grows its storage to `size` bytes (RET 4, EAX not a result).
__declspec(naked) inline void __fastcall GameBufferGrowOriginal(void* /*buffer*/, void*, std::uint32_t /*size*/)
    EI_ORIGINAL(0x004DA5C0)

// C3i. 00405720: STLport hash_map erase(node) (RET 8: node, table; EAX not a result). 0043DBE0: red-black tree
// iterator increment (ECX = the iterator, plain RET, EAX not a result). 0040C280: string append(first, last, tag)
// (RET 0xC, returns this). 0041F550: string assign(const char*) (RET 4, returns this). 00700D73: CString::operator+=(char)
// (RET 4, returns this). 006E8693: CRT `eh vector constructor iterator` (RET 0x14: array, size, count, ctor, dtor).
// 006FE296: CArray<CString> DestructElements (RET 8: first, count).
__declspec(naked) inline void __fastcall HashEraseOriginal(void* /*map*/, void*, void* /*node*/, const void* /*table*/)
    EI_ORIGINAL(0x00405720)
__declspec(naked) inline void __fastcall TreeIncrementOriginal(void* /*iterator*/) EI_ORIGINAL(0x0043DBE0)
__declspec(naked) inline void* __fastcall StringAppendRangeOriginal(void* /*string*/, void*, const char* /*first*/,
                                                                    const char* /*last*/, const void* /*tag*/)
    EI_ORIGINAL(0x0040C280)
__declspec(naked) inline void* __fastcall StringAssignCStrOriginal(void* /*string*/, void*, const char* /*text*/)
    EI_ORIGINAL(0x0041F550)
__declspec(naked) inline void* __fastcall CStringAppendCharOriginal(void* /*string*/, void*, char /*c*/)
    EI_ORIGINAL(0x00700D73)
__declspec(naked) inline void __stdcall EhVectorConstructorIterator(void* /*array*/, unsigned /*size*/, int /*count*/,
                                                                    void* /*ctor*/, void* /*dtor*/) EI_ORIGINAL(0x006E8693)
__declspec(naked) inline void __stdcall CStringElementsDestruct(void* /*first*/, int /*count*/) EI_ORIGINAL(0x006FE296)

// C3i part 2. Containers and strings (STLport / MFC), all decided from the disassembly.
// 004A8930: string copy constructor (RET 4, returns this). 004C96A0: pointer list constructor with a block size (RET 4,
// returns this). 004C98F0: pointer list AddTail (RET 4, EAX not a result). 004DCE90: string vector insert overflow
// (RET 0xC: pos, first, count). 0041FCD0: list end() iterator written to `out` (RET 4, returns out).
__declspec(naked) inline void* __fastcall StringCopyConstructOriginal(void* /*string*/, void*, const void* /*source*/)
    EI_ORIGINAL(0x004A8930)
__declspec(naked) inline void* __fastcall PtrListConstructOriginal(void* /*list*/, void*, int /*blockSize*/)
    EI_ORIGINAL(0x004C96A0)
__declspec(naked) inline void __fastcall PtrListAddTailOriginal(void* /*list*/, void*, void* /*element*/)
    EI_ORIGINAL(0x004C98F0)
__declspec(naked) inline void __fastcall StringVectorInsertOverflowOriginal(void* /*vector*/, void*, void* /*pos*/,
                                                                            const void* /*first*/, unsigned /*count*/)
    EI_ORIGINAL(0x004DCE90)
__declspec(naked) inline void** __fastcall ListEndIteratorOriginal(void* /*list*/, void*, void** /*out*/)
    EI_ORIGINAL(0x0041FCD0)
// 006FE0DD: CString::TrimRight(char) (RET 4). 006FE268: CArray<CString> ConstructElements (RET 8: first, count).
// 006FEB61: CStringArray::Copy(source) (RET 4, EAX not a result).
__declspec(naked) inline void __fastcall CStringTrimRightCharOriginal(void* /*string*/, void*, char /*c*/)
    EI_ORIGINAL(0x006FE0DD)
__declspec(naked) inline void __stdcall CStringElementsConstruct(void* /*first*/, int /*count*/) EI_ORIGINAL(0x006FE268)
__declspec(naked) inline void __fastcall StringArrayCopyOriginal(void* /*array*/, void*, const void* /*source*/)
    EI_ORIGINAL(0x006FEB61)
// The 0x4xxxxx hash tables (STLport hash_map instantiations): 00405620 erase(node, table) (RET 8), 00406640 / 00406D10 /
// 004070B0 resize(hint) (RET 4), 00406E80 clear (plain RET), 00406590 deallocate(pointers, count) (ECX, EDX, plain RET).
__declspec(naked) inline void __fastcall ObjectsEraseOriginal(void* /*map*/, void*, void* /*node*/, const void* /*table*/)
    EI_ORIGINAL(0x00405620)
__declspec(naked) inline void __fastcall ObjectsResizeOriginal(void* /*map*/, void*, std::uint32_t /*hint*/)
    EI_ORIGINAL(0x00406640)
__declspec(naked) inline void __fastcall PositionsResizeOriginal(void* /*map*/, void*, std::uint32_t /*hint*/)
    EI_ORIGINAL(0x00406D10)
__declspec(naked) inline void __fastcall SpareCountsResizeOriginal(void* /*map*/, void*, std::uint32_t /*hint*/)
    EI_ORIGINAL(0x004070B0)
__declspec(naked) inline void __fastcall HashClearOriginal(void* /*map*/) EI_ORIGINAL(0x00406E80)
__declspec(naked) inline void __fastcall BucketsDeallocateOriginal(void* /*pointers*/, std::size_t /*count*/)
    EI_ORIGINAL(0x00406590)
