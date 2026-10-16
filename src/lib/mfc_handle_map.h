#pragma once

// MFC 4.2's handle maps (winhand.cpp) and what they are built from: CMapPtrToPtr (map_pp.cpp),
// CRuntimeClass::CreateObject and the new handler MFC swaps around its allocations (ticket S8a,
// docs/notes/standalone.md section 2). A handle map finds the C++ object of a Windows handle: the
// permanent map holds the objects that attached their handle (a CPen, a CFont), the temporary map
// the objects FromHandle made for the others (a CTempDC for a surface's DC, a CTempGdiObject for a
// stock pen). The game has no CWinApp, so nothing ever empties the temporary maps (no OnIdle).
//
// Every body is registered at its library address in both builds (mfc_handle_map.cpp); the maps
// themselves live in MFC's module thread state, which the hook build keeps (the original's
// afxMapHDC / afxMapHGDIOBJ) and the image replaces with its own (mfc_gdi.cpp). Checked in game by
// verify_mfc_handle_map.cpp until S21d.

#include "crt.h"
#include "mfc.h"

#include <cstdint>

#include <windows.h>

namespace mfc::impl {

// CRuntimeClass (the members used here).
struct CRuntimeClass {
    const char* className;          // +0x00 m_lpszClassName
    int objectSize;                 // +0x04 m_nObjectSize
    unsigned schema;                // +0x08 m_wSchema
    void*(__stdcall* create)();     // +0x0C m_pfnCreateObject (null: not creatable)
    CRuntimeClass* baseClass;       // +0x10 m_pBaseClass

    // 0070061B: a new object through `create`, or null (no create function, or a CException thrown
    // on the way, which is deleted).
    void* CreateObject();
};

// The new handler operator new calls when an allocation fails (_afxNewHandler).
gamecrt::NewHandler __stdcall SetAfxNewHandler(gamecrt::NewHandler handler);   // 0070059D AfxSetNewHandler
// AfxCriticalNewHandler (0070AB1A): what a handle map's allocations run under. Image only: with no
// CWinThread there is no safety pool to release, so it throws a CMemoryException.
int __cdecl CriticalNewHandler(std::size_t size);
// AfxNewHandler (0070058F): _afxNewHandler's initial value (00796E14), which MFC's operator new calls
// when an allocation fails: throws a CMemoryException. Image only (the hook build keeps the exe's).
int __cdecl DefaultNewHandler(std::size_t size);
// The handler's address as the exe has it (our CriticalNewHandler in the image).
inline gamecrt::NewHandler CriticalNewHandlerAt() { return reinterpret_cast<gamecrt::NewHandler>(0x0070AB1A); }

// CMapPtrToPtr (0x1C bytes, vtable 0074689C): void* to void*, hashed by `key >> 4`.
struct CMapPtrToPtr {
    struct Assoc {
        Assoc* next;    // +0x00
        void* key;      // +0x04
        void* value;    // +0x08
    };

    void* vtable;               // +0x00
    Assoc** table;              // +0x04 m_pHashTable
    std::uint32_t hashSize;     // +0x08 m_nHashTableSize (17 until InitHashTable)
    int count;                  // +0x0C m_nCount
    Assoc* freeList;            // +0x10 m_pFreeList
    CPlex* blocks;              // +0x14 m_pBlocks
    int blockSize;              // +0x18 m_nBlockSize

    static void* Vtable() { return reinterpret_cast<void*>(0x0074689C); }

    CMapPtrToPtr* Construct(int blockSize);                             // 007002C2 (returns this)
    void Destruct();                                                    // 00700375
    CMapPtrToPtr* DeletingDestructor(unsigned flags);                   // 007002E9 (vtable slot 1)
    void InitHashTable(std::uint32_t size, BOOL allocNow);              // 00700305
    void RemoveAll();                                                   // 0070034A
    Assoc* NewAssoc();                                                  // 0070039E
    void FreeAssoc(Assoc* assoc);                                       // 007003EB
    Assoc* GetAssocAt(void* key, std::uint32_t* hash) const;            // 00700404 (hash: the key's bucket)
    void* GetValueAt(void* key) const;                                  // 00700436 (null when missing)
    void** At(void* key);                                               // 0070048B operator[]
    BOOL RemoveKey(void* key);                                          // 007004DB
};
static_assert(sizeof(CMapPtrToPtr) == 0x1C);
static_assert(sizeof(CMapPtrToPtr::Assoc) == 12);

// CHandleMap (0x48 bytes, vtable 0074685C).
struct CHandleMap {
    void* vtable;
    CMapPtrToPtr permanentMap;      // +0x04 (blocks of 10)
    CMapPtrToPtr temporaryMap;      // +0x20 (blocks of 4, 7 buckets)
    CRuntimeClass* objectClass;     // +0x3C what FromHandle makes for an unknown handle
    unsigned handleOffset;          // +0x40 where the object keeps its handle
    int handleCount;                // +0x44 1, or 2 (a DC's output and attribute handles)

    static void* Vtable() { return reinterpret_cast<void*>(0x0074685C); }

    CHandleMap* Construct(CRuntimeClass* objectClass, unsigned handleOffset, int handleCount);   // 00703617
    // 0070367C: the object of `handle` (null for null): the permanent one, else the temporary one,
    // else a new temporary object of objectClass (under the critical new handler; a CMemoryException
    // when none could be made). A temporary object's handles are (re)written.
    void* FromHandle(HANDLE handle);

private:
    void SetHandles(void* object, HANDLE handle) const;
};
static_assert(sizeof(CHandleMap) == 0x48);

} // namespace mfc::impl
