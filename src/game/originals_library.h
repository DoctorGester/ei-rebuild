#pragma once

// Original functions forwarded from several files (one declaration per address; the bodies
// jump to game.exe). Types are forward declared; include the class headers where members are used.

#include "ei/hook.h"

#include <cstddef>
#include <cstdint>
#include <new>
#include "lib/crt.h"
#include "lib/mfc.h"

struct AnimationSteps;
class CFileRegistry;
class CStream;
struct RefPtr;
namespace lib { template <typename T> struct AlignedArray; }
namespace console { class CStringArray; }
namespace net { struct FreedBatch; struct HistoryNode; }
namespace stlp { class string; }

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
__declspec(naked) inline void __fastcall NamesSetSizeOriginal(void*, void*, int, int) EI_ORIGINAL(0x004C2510)
__declspec(naked) inline void __fastcall FloatsSetSize(void*, void*, int, int) EI_ORIGINAL(0x004C2880)
__declspec(naked) inline void __fastcall CellListDestroy(void*) EI_ORIGINAL(0x00539890)
__declspec(naked) inline void __fastcall FirstListConstruct(void*, void*, const void*) EI_ORIGINAL(0x0053F4F0)
__declspec(naked) inline void __fastcall StepsSetSize(AnimationSteps*, void*, int, int) EI_ORIGINAL(0x00557B80)
__declspec(naked) inline void __fastcall NodeFree12Fast(void*, int) EI_ORIGINAL(0x0056FB40)
__declspec(naked) inline void __fastcall ConstructModelMap(void*, void*, const void*) EI_ORIGINAL(0x005FA420)
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

// STLport std::list constructor for a list of pointers on the 12-byte node allocator (RET 4).
__declspec(naked) inline void __fastcall ListConstruct411850(void* /*list*/, void*, const void* /*alloc*/)
    EI_ORIGINAL(0x00411850)
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
__declspec(naked) inline void __fastcall PtrArraySetSize(void* /*array*/, void*, int /*count*/, int /*growBy*/)
    EI_ORIGINAL(0x004C2050)
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
// MFC CWordArray::~CWordArray 006FEECD and CByteArray::~CByteArray 006FF61D (plain RET): the storage freed.
__declspec(naked) inline void __fastcall WordArrayDestructOriginal(void* /*array*/) EI_ORIGINAL(0x006FEECD)
__declspec(naked) inline void __fastcall ByteArrayDestructOriginal(void* /*array*/) EI_ORIGINAL(0x006FF61D)
// CPlex::FreeDataChain 006FE90F (plain RET, ECX = chain head; mfc::PlexFreeDataChain is the forwarder).
inline void __fastcall PlexFreeDataChainOriginal(void* chain) { mfc::PlexFreeDataChain(static_cast<mfc::CPlex*>(chain)); }

// C3h. 00405950: __node_alloc<true,0>::allocate(bytes) (ECX = bytes, plain RET, block or null).
__declspec(naked) inline void* __fastcall NodeAllocateOriginal(std::size_t /*bytes*/) EI_ORIGINAL(0x00405950)
// 00427830: CPtrList destructor (plain RET); 00429F10: CPtrList::RemoveAll (plain RET). EAX is not a result.
__declspec(naked) inline void __fastcall PtrListDestructOriginal(void* /*list*/) EI_ORIGINAL(0x00427830)
__declspec(naked) inline void __fastcall PtrListRemoveAllNodesOriginal(void* /*list*/) EI_ORIGINAL(0x00429F10)
// 004297A0: hash_map<string, ...>::operator[](key) (RET 4): the address of the value slot (node + 4 or the new node + 0xC).
__declspec(naked) inline void* __fastcall MapRegistryFindOriginal(void* /*map*/, void*, const void* /*key*/)
    EI_ORIGINAL(0x004297A0)

// C3i. 00405720: STLport hash_map erase(node) (RET 8: node, table; EAX not a result). 00700D73: CString::operator+=(char)
// (RET 4, returns this). 006E8693: CRT `eh vector constructor iterator` (RET 0x14: array, size, count, ctor, dtor).
// 006FE296: CArray<CString> DestructElements (RET 8: first, count).
__declspec(naked) inline void __fastcall HashEraseOriginal(void* /*map*/, void*, void* /*node*/, const void* /*table*/)
    EI_ORIGINAL(0x00405720)
__declspec(naked) inline void __stdcall EhVectorConstructorIterator(void* /*array*/, unsigned /*size*/, int /*count*/,
                                                                    void* /*ctor*/, void* /*dtor*/) EI_ORIGINAL(0x006E8693)
// 004CC980: DestructElements<ScriptGroup> (RET 8: first, count; the group's destructor body 004B5EF0 each). 004C26A0: CArray<CString>::~CArray
// (vtable 0073D2F8; plain RET, EAX not a result).
__declspec(naked) inline void __stdcall ScriptGroupElementsDestruct(void* /*first*/, int /*count*/) EI_ORIGINAL(0x004CC980)
__declspec(naked) inline void __fastcall CStringArrayDestructOriginal(void* /*array*/) EI_ORIGINAL(0x004C26A0)
// 004C3B20 / 004C5AF0: CArray<ScriptGroup> / CArray<ScriptIfThen>::~CArray (plain RET).
__declspec(naked) inline void __fastcall ScriptGroupArrayDestructOriginal(void* /*array*/) EI_ORIGINAL(0x004C3B20)
__declspec(naked) inline void __fastcall ScriptIfThenArrayDestructOriginal(void* /*array*/) EI_ORIGINAL(0x004C5AF0)

// C3i part 2. Containers and strings (STLport / MFC), all decided from the disassembly.
// 004C96A0: pointer list constructor with a block size (RET 4,
// returns this). 004C98F0: pointer list AddTail (RET 4, EAX not a result). 004DCE90: string vector insert overflow
// (RET 0xC: pos, first, count). 0041FCD0: list end() iterator written to `out` (RET 4, returns out).
__declspec(naked) inline void* __fastcall PtrListConstructOriginal(void* /*list*/, void*, int /*blockSize*/)
    EI_ORIGINAL(0x004C96A0)
__declspec(naked) inline void __fastcall PtrListAddTailOriginal(void* /*list*/, void*, void* /*element*/)
    EI_ORIGINAL(0x004C98F0)
__declspec(naked) inline void __fastcall StringVectorInsertOverflowOriginal(void* /*vector*/, void*, void* /*pos*/,
                                                                            const void* /*first*/, unsigned /*count*/)
    EI_ORIGINAL(0x004DCE90)
__declspec(naked) inline void** __fastcall ListEndIteratorOriginal(void* /*list*/, void*, void** /*out*/)
    EI_ORIGINAL(0x0041FCD0)
// 006FEB61: CStringArray::Copy(source) (RET 4, EAX not a result).
inline void __fastcall StringArrayCopyOriginal(void* array, void*, const void* source) {
    static_cast<mfc::CStringArray*>(array)->Copy(*static_cast<const mfc::CStringArray*>(source));
}
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
// A-H17. hash_set<int> 007C8370 (the client's party ids, message 0x60), thiscall on the set: 0066B400
// insert_unique (RET 8: the pair<iterator, bool> written to `out`, returned; resizes first), 0066B490 find (RET 8:
// the iterator node, table to `out`), 0066BA70 clear (plain RET; nodes to the free list 0079B224).
__declspec(naked) inline void* __fastcall IntSetInsertOriginal(void* /*set*/, void*, void* /*out*/, const int* /*key*/)
    EI_ORIGINAL(0x0066B400)
__declspec(naked) inline void __fastcall IntSetFindOriginal(void* /*set*/, void*, void* /*out*/, const int* /*key*/)
    EI_ORIGINAL(0x0066B490)
__declspec(naked) inline void __fastcall IntSetClearOriginal(void* /*set*/) EI_ORIGINAL(0x0066BA70)
// A-H17. The item database's name indexes (hash_map<string, record*>), find_or_insert(pair) (thiscall, RET 4; EAX the
// stored pair): 00613B70 quick items, 006A7870 loot, 0069E2A0 quest items.
__declspec(naked) inline void* __fastcall QuickItemIndexInsertOriginal(void* /*index*/, void*, const void* /*pair*/)
    EI_ORIGINAL(0x00613B70)
__declspec(naked) inline void* __fastcall LootIndexInsertOriginal(void* /*index*/, void*, const void* /*pair*/)
    EI_ORIGINAL(0x006A7870)
__declspec(naked) inline void* __fastcall QuestItemIndexInsertOriginal(void* /*index*/, void*, const void* /*pair*/)
    EI_ORIGINAL(0x0069E2A0)

// C3k. 004C96F0: destructor of the pointer list built by 004C96A0 (vtable 0073D454; plain RET, EAX not a result).
// 004C98C0: constructor of the item pointer list (vtable 0073BE80) with a block size (RET 4, returns `this`).
__declspec(naked) inline void __fastcall PtrListDestruct4C96F0Original(void* /*list*/) EI_ORIGINAL(0x004C96F0)
__declspec(naked) inline void* __fastcall ItemListConstructOriginal(void* /*list*/, void*, int /*blockSize*/)
    EI_ORIGINAL(0x004C98C0)
// Container instantiations inside the exe, decided from the disassembly. CList / CPtrList NewNode(prev, next)
// (all RET 8, EAX the new node): 0052CEF0 is CPtrList's (the node's data is cleared), 0052CDB0 / 00429EA0 / 004CD6D0
// (0x20-byte nodes) / 004CDA30 (0xBC-byte nodes) construct the element through the template's ConstructElements.
__declspec(naked) inline void* __fastcall PtrListNewNode52CEF0Original(void* /*list*/, void*, void* /*prev*/, void* /*next*/)
    EI_ORIGINAL(0x0052CEF0)
__declspec(naked) inline void* __fastcall ListNewNode52CDB0Original(void* /*list*/, void*, void* /*prev*/, void* /*next*/)
    EI_ORIGINAL(0x0052CDB0)
__declspec(naked) inline void* __fastcall ListNewNode429EA0Original(void* /*list*/, void*, void* /*prev*/, void* /*next*/)
    EI_ORIGINAL(0x00429EA0)
__declspec(naked) inline void* __fastcall ScriptBlockListNewNodeOriginal(void* /*list*/, void*, void* /*prev*/, void* /*next*/)
    EI_ORIGINAL(0x004CD6D0)
__declspec(naked) inline void* __fastcall ScriptRunListNewNodeOriginal(void* /*list*/, void*, void* /*prev*/, void* /*next*/)
    EI_ORIGINAL(0x004CDA30)
// CList::NewNode(prev, next) of the script's particle source (004CD540), FX source (004CD5B0) and lightning
// (004CD620) lists (RET 8, the node in EAX; the element zeroed, an FX source's name constructed empty).
__declspec(naked) inline void* __fastcall ScriptParticleListNewNodeOriginal(void* /*list*/, void*, void* /*prev*/,
                                                                            void* /*next*/) EI_ORIGINAL(0x004CD540)
__declspec(naked) inline void* __fastcall ScriptFxListNewNodeOriginal(void* /*list*/, void*, void* /*prev*/,
                                                                      void* /*next*/) EI_ORIGINAL(0x004CD5B0)
__declspec(naked) inline void* __fastcall ScriptLightningListNewNodeOriginal(void* /*list*/, void*, void* /*prev*/,
                                                                             void* /*next*/) EI_ORIGINAL(0x004CD620)
// 005588A0: CPtrList::FreeNode(node) (RET 4, EAX not a result). 00558C50: CPtrList::RemoveAll (plain RET).
__declspec(naked) inline void __fastcall UnitListFreeNodeOriginal(void* /*list*/, void*, void* /*node*/)
    EI_ORIGINAL(0x005588A0)
__declspec(naked) inline void __fastcall ItemListRemoveAllOriginal(void* /*list*/) EI_ORIGINAL(0x00558C50)
// STLport list insert(position, value) writing the new iterator to `out` (RET 0xC: out, position, value; EAX = out):
// 00421AF0 and 00423190 on the 12-byte node pool (NetAddress / uint list, RefPtr list), 0056E8A0 on the cell lists.
__declspec(naked) inline void** __fastcall ListInsert56E8A0Original(void* /*list*/, void*, void** /*out*/, void* /*pos*/,
                                                                    const void* /*value*/) EI_ORIGINAL(0x0056E8A0)
__declspec(naked) inline void** __fastcall ListInsert421AF0Original(void* /*list*/, void*, void** /*out*/, void* /*pos*/,
                                                                    const void* /*value*/) EI_ORIGINAL(0x00421AF0)
__declspec(naked) inline void** __fastcall ListInsert423190Original(void* /*list*/, void*, void** /*out*/, void* /*pos*/,
                                                                    const void* /*value*/) EI_ORIGINAL(0x00423190)
// 00421EB0: vector<RefPtr>::get_allocator (RET 4: out; copies the allocator tag byte at +8 to *out, EAX = out).
// 00426860: node allocator deallocate(p, count) of 12-byte elements (ECX p, EDX count, plain RET).
__declspec(naked) inline void* __fastcall VectorGetAllocatorOriginal(const void* /*vector*/, void*, void* /*out*/)
    EI_ORIGINAL(0x00421EB0)
__declspec(naked) inline void __fastcall NodeFree12Original(void* /*p*/, std::size_t /*count*/) EI_ORIGINAL(0x00426860)
// The type registry's by-vtable hash_map: 0042B8A0 _M_new_node(pair) (RET 4, EAX the node; ECX is not read), 0042B9C0
// resize(hint) (RET 4, no result).
__declspec(naked) inline void* __fastcall ByVtableNewNodeOriginal(void* /*table*/, void*, const void* /*pair*/)
    EI_ORIGINAL(0x0042B8A0)
__declspec(naked) inline void __fastcall ByVtableResizeOriginal(void* /*table*/, void*, std::uint32_t /*hint*/)
    EI_ORIGINAL(0x0042B9C0)
// CArray::SetSize(size, growBy) instantiations (RET 8, no result): 0044FCD0 the figure array (0x220-byte elements with a
// virtual destructor), 0046EAD0 plain 12-byte elements, 0050FE70 the map object's effect array (20-byte elements).
// 00468D60 / 00557230: sprite / point array constructors (vtables 0073C584 / 0073F07C; plain RET, return `this`).
__declspec(naked) inline void __fastcall FigureArraySetSizeOriginal(void* /*array*/, void*, int /*size*/, int /*growBy*/)
    EI_ORIGINAL(0x0044FCD0)
__declspec(naked) inline void __fastcall Array12SetSizeOriginal(void* /*array*/, void*, int /*size*/, int /*growBy*/)
    EI_ORIGINAL(0x0046EAD0)
__declspec(naked) inline void __fastcall EffectArraySetSizeOriginal(void* /*array*/, void*, int /*size*/, int /*growBy*/)
    EI_ORIGINAL(0x0050FE70)
// 0050FA30: CArray<DeltaField>::SetSize(size, growBy) on a DeltaState (net_delta.h; growBy -1 keeps the step).
__declspec(naked) inline void __fastcall NetFieldsSetSizeOriginal(void* /*state*/, void*, int /*size*/, int /*growBy*/)
    EI_ORIGINAL(0x0050FA30)
__declspec(naked) inline void* __fastcall SpriteArrayConstructOriginal(void* /*array*/) EI_ORIGINAL(0x00468D60)
__declspec(naked) inline void* __fastcall PointArrayConstructOriginal(void* /*array*/) EI_ORIGINAL(0x00557230)
// 00514880: hash_map<string, ...>::insert(pair) (RET 4: pair; EAX = node + 4, the stored pair). 005197B0: map find(key) writing
// the iterator to `out` (RET 8: out, key; EAX = out).
__declspec(naked) inline void* __fastcall NameMapInsertOriginal(void* /*map*/, void*, const void* /*pair*/)
    EI_ORIGINAL(0x00514880)
// 0053C780: operator[] of the figure prototypes' hash_map<string, record*> (007AF3A4; RET 4: the key string; EAX the
// value's address; an unknown name gets a null entry).
__declspec(naked) inline char** __fastcall FigureMapAtOriginal(void* /*map*/, void*, const void* /*key*/)
    EI_ORIGINAL(0x0053C780)
// 0053D340: the same find_or_insert on another hash_map<string, T*> instantiation (the figure prototypes, 007AF3A4).
__declspec(naked) inline void* __fastcall FigureMapInsertOriginal(void* /*map*/, void*, const void* /*pair*/)
    EI_ORIGINAL(0x0053D340)
__declspec(naked) inline void** __fastcall StringMapFindOriginal(void* /*map*/, void*, void** /*out*/, const void* /*key*/)
    EI_ORIGINAL(0x005197B0)
// vector<CString>: 0051F5B0 resize(count) (RET 4), 0052E010 clear (destroys the elements, plain RET); no result.
__declspec(naked) inline void __fastcall CStringVectorResizeOriginal(void* /*vector*/, void*, std::uint32_t /*count*/)
    EI_ORIGINAL(0x0051F5B0)
__declspec(naked) inline void __fastcall CStringVectorClearOriginal(void* /*vector*/) EI_ORIGINAL(0x0052E010)

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
// Instances of std::list<int> on the node pool (the list's head node is at +0): 004049D0 end() (RET 4: out; *out = the head
// node; EAX = out), 00404A40 insert(out, pos, value) (RET 0xC; EAX = out), 00404A70 constructor (RET 4: the allocator tag;
// returns this). 0068C560 is insert(out, pos, value) of a list of pointers (RET 0xC; EAX = out).
__declspec(naked) inline void** __fastcall ListEnd4049D0Original(const void* /*list*/, void*, void** /*out*/)
    EI_ORIGINAL(0x004049D0)
__declspec(naked) inline void** __fastcall ListInsert404A40Original(void* /*list*/, void*, void** /*out*/, void* /*pos*/,
                                                                    const void* /*value*/) EI_ORIGINAL(0x00404A40)
__declspec(naked) inline void* __fastcall ListConstruct404A70Original(void* /*list*/, void*, const void* /*alloc*/)
    EI_ORIGINAL(0x00404A70)
__declspec(naked) inline void** __fastcall ListInsert68C560Original(void* /*list*/, void*, void** /*out*/, void* /*pos*/,
                                                                    const void* /*value*/) EI_ORIGINAL(0x0068C560)
// SetAtGrow(index, value) of two 4-byte element arrays (the same code, one copy per instantiation): 00460500 (dwords) and
// 0059D2E0 (pointers to party member records); both RET 8, EAX is not a result.
__declspec(naked) inline void __fastcall DwordArraySetAtGrowOriginal(void* /*array*/, void*, int /*index*/,
                                                                     std::uint32_t /*value*/) EI_ORIGINAL(0x00460500)
__declspec(naked) inline void __fastcall RecordArraySetAtGrowOriginal(void* /*array*/, void*, int /*index*/, void* /*record*/)
    EI_ORIGINAL(0x0059D2E0)
// 004E2FA0: vector<dword>::_M_insert_overflow(position, value, count) on the node allocator (ECX = vector; RET 0xC; EAX is not a
// result).
__declspec(naked) inline void __fastcall DwordVectorInsertOverflowOriginal(void* /*vector*/, void*, void* /*pos*/,
                                                                           const void* /*value*/, std::uint32_t /*count*/)
    EI_ORIGINAL(0x004E2FA0)
// Counted objects (virtual slot 4 releases): 005703C0 clear() of their list (ECX = list; each node's object is released and the
// node goes back to the node pool; plain RET, no result); 00573290 the same list's NewNode(prev, next) (RET 8, EAX = the node,
// its data cleared; a CPtrList instance); 0059A100 the pointer array's destructor (releases every element, frees the buffer;
// plain RET, no result); 0059C890 releases `count` elements from `first` (stdcall, RET 8, no result).
__declspec(naked) inline void __fastcall CountedListClearOriginal(void* /*list*/) EI_ORIGINAL(0x005703C0)
__declspec(naked) inline void* __fastcall PtrListNewNode573290Original(void* /*list*/, void*, void* /*prev*/, void* /*next*/)
    EI_ORIGINAL(0x00573290)
__declspec(naked) inline void __fastcall CountedPtrArrayDestructOriginal(void* /*array*/) EI_ORIGINAL(0x0059A100)
__declspec(naked) inline void __stdcall CountedRangeReleaseOriginal(void* /*first*/, int /*count*/) EI_ORIGINAL(0x0059C890)
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
// 006FF29A: SetSize(count, growBy) of a dword array (RET 8, no result). 00700D4C: CString::operator+=(const char*) (RET 4, returns
// this). 00700E17: CString::GetBufferSetLength(length) (RET 4, EAX = the buffer). src/lib/mfc.cpp keeps its CString members.
__declspec(naked) inline void __fastcall UIntArraySetSizeOriginal(void* /*array*/, void*, int /*count*/, int /*growBy*/)
    EI_ORIGINAL(0x006FF29A)
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
// STLport hash_map<CString, float> (NamedFloatTable, object_tables.h; the party's variables), thiscall on the table:
// 004A8A50 copy constructor from `source` (RET 4, returns this), 004AA950 copy_from(`source`) (RET 4), 00461540
// clear (plain RET), 004616A0 deletes one node (RET 4), 004614C0 begin() (RET 4: the iterator it fills; no result).
// 00609F50: the bucket vector's destructor (plain RET).
__declspec(naked) inline void* __fastcall FloatTableCopyConstructOriginal(void* /*table*/, void*, const void* /*source*/)
    EI_ORIGINAL(0x004A8A50)
__declspec(naked) inline void __fastcall FloatTableCopyFromOriginal(void* /*table*/, void*, const void* /*source*/)
    EI_ORIGINAL(0x004AA950)
__declspec(naked) inline void __fastcall FloatTableClearOriginal(void* /*table*/) EI_ORIGINAL(0x00461540)
__declspec(naked) inline void __fastcall FloatTableDeleteNodeOriginal(void* /*table*/, void*, void* /*node*/)
    EI_ORIGINAL(0x004616A0)
__declspec(naked) inline void __fastcall FloatTableBeginOriginal(void* /*table*/, void*, void* /*iterator*/)
    EI_ORIGINAL(0x004614C0)
__declspec(naked) inline void __fastcall BucketVectorDestroyOriginal(void* /*vector*/) EI_ORIGINAL(0x00609F50)

// C3m. 006E9180: CRT strncmp (gamecrt::strncmp).
inline int __cdecl Strncmp(const char* a, const char* b, std::size_t n) { return gamecrt::strncmp(a, b, n); }
// C3m. 006FEE9A / 006FF5EA: the static CArray<uint16_t> / CArray<uint8_t> constructors (ECX this, plain RET).
__declspec(naked) inline void __fastcall WordArrayConstruct(mfc::CArray<std::uint16_t>*) EI_ORIGINAL(0x006FEE9A)
__declspec(naked) inline void __fastcall ByteArrayConstruct(mfc::CArray<std::uint8_t>*) EI_ORIGINAL(0x006FF5EA)
// C3m. 00421F60: the counted-pointer vector's base constructor (n elements, allocator; plain RET).
__declspec(naked) inline void __fastcall VectorBaseConstruct(void* /*vector*/, void*, std::size_t /*n*/,
                                                           const void* /*allocator*/) EI_ORIGINAL(0x00421F60)
// C3m. The net connection's history list (STLport list of HistoryNode): 00437F30 erase(pos).
__declspec(naked) inline net::HistoryNode** __fastcall HistoryErase(net::HistoryNode** /*list*/, void*, net::HistoryNode** /*out*/,
                                                             net::HistoryNode* /*pos*/) EI_ORIGINAL(0x00437F30)
// C3m. The freed-batch list (STLport list of FreedBatch): 00439000 insert(pos) of a default batch.
__declspec(naked) inline net::FreedBatch** __fastcall FreedInsert(net::FreedBatch** /*list*/, void*, net::FreedBatch** /*out*/,
                                                           net::FreedBatch* /*pos*/) EI_ORIGINAL(0x00439000)

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
