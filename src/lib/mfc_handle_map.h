#pragma once

// MFC 4.2's handle maps (winhand.cpp) and what they are built from: CMapPtrToPtr (map_pp.cpp),
// CRuntimeClass::CreateObject and the new handler MFC swaps around its allocations (ticket,
// docs/notes/standalone.md section 2). A handle map finds the C++ object of a Windows handle: the
// permanent map holds the objects that attached their handle (a CPen, a CFont), the temporary map
// the objects FromHandle made for the others (a CTempDC for a surface's DC, a CTempGdiObject for a
// stock pen). The game has no CWinApp, so nothing ever empties the temporary maps (no OnIdle).
// Every body is registered at its library address in both builds (mfc_handle_map.cpp); the maps
// themselves live in MFC's module thread state, which the hook build keeps (the original's
// afxMapHDC / afxMapHGDIOBJ) and the image replaces with its own (mfc_gdi.cpp). Checked in game by
// verify_mfc_handle_map.cpp until.

#include "crt.h"
#include "mfc.h"
#include "mfc_object.h"
#include "ei/hook.h"
#include "game/game_data.h"

#include <cstdint>

#include <windows.h>

namespace mfc::impl {

// CRuntimeClass: a class's record (mfc_runtime_class.h has the game's 41).
struct CRuntimeClass {
    const char* className;          // +0x00 m_lpszClassName
    int objectSize;                 // +0x04 m_nObjectSize
    unsigned schema;                // +0x08 m_wSchema (0xFFFF: not serializable)
    void*(__stdcall* create)();     // +0x0C m_pfnCreateObject (null: not creatable)
    const CRuntimeClass* baseClass; // +0x10 m_pBaseClass
    CRuntimeClass* nextClass;       // +0x14 m_pNextClass (the module state's class list; AfxClassInit links it)

    // A new object through `create`, or null (no create function, or a CException thrown
    // on the way, which is deleted).
    void* CreateObject() const;
};

// The new handler operator new calls when an allocation fails (_afxNewHandler).
gamecrt::NewHandler __stdcall SetAfxNewHandler(gamecrt::NewHandler handler);   // AfxSetNewHandler
// AfxCriticalNewHandler: what a handle map's allocations run under. Image only: with no
// CWinThread there is no safety pool to release, so it throws a CMemoryException.
int __cdecl CriticalNewHandler(std::size_t size);
// AfxNewHandler: _afxNewHandler's initial value, which MFC's operator new calls
// when an allocation fails: throws a CMemoryException. Image only (the hook build keeps the exe's).
int __cdecl DefaultNewHandler(std::size_t size);
// _afxNewHandler: the handler MFC's operator new (libcrt::OperatorNew) calls when an allocation fails.
extern gamecrt::NewHandler g_afxNewHandler;
// The handler's address as the exe has it (our CriticalNewHandler in the image).
inline gamecrt::NewHandler CriticalNewHandlerAt() { return ei::Code<gamecrt::NewHandler>(0x0070AB1A); }

// CMapPtrToPtr (0x1C bytes): void* to void*, hashed by `key >> 4`.
class CMapPtrToPtr final : public CObject {
public:
    struct Assoc {
        Assoc* next;    // +0x00
        void* key;      // +0x04
        void* value;    // +0x08
    };

    // Empty, 17 buckets (no table yet), associations in blocks of `blockSize`.
    explicit CMapPtrToPtr(int blockSize);
    // RemoveAll.
    ~CMapPtrToPtr() override;
    // (0).
    const CRuntimeClass* GetRuntimeClass() const override;

    void InitHashTable(std::uint32_t size, BOOL allocNow);
    void RemoveAll();
    Assoc* NewAssoc();
    void FreeAssoc(Assoc* assoc);
    Assoc* GetAssocAt(void* key, std::uint32_t* hash) const;            // (hash: the key's bucket)
    void* GetValueAt(void* key) const;                                  // (null when missing)
    void** At(void* key);                                               // operator[]
    BOOL RemoveKey(void* key);
    // The entry at `position` (StartPosition(): the first one) and the position after it (null after the
    // last). The map must not be empty.
    void GetNextAssoc(Assoc*& position, void*& key, void*& value) const;

    // BEFORE_START_POSITION.
    static Assoc* StartPosition() { return reinterpret_cast<Assoc*>(-1); }

    Assoc** table = nullptr;        // +0x04 m_pHashTable
    std::uint32_t hashSize = 17;    // +0x08 m_nHashTableSize (17 until InitHashTable)
    int count = 0;                  // +0x0C m_nCount
    Assoc* freeList = nullptr;      // +0x10 m_pFreeList
    CPlex* blocks = nullptr;        // +0x14 m_pBlocks
    int blockSize;                  // +0x18 m_nBlockSize
};
static_assert(sizeof(CMapPtrToPtr) == 0x1C);
static_assert(sizeof(CMapPtrToPtr::Assoc) == 12);

// CHandleMap (0x48 bytes; one slot: the deleting destructor).
class CHandleMap {
public:
    // The two maps (the temporary one 7 buckets), the objects' class and where they keep their handles.
    CHandleMap(const CRuntimeClass* objectClass, unsigned handleOffset, int handleCount);
    // DeleteTemp, then the two maps destroyed (members). The image's maps are never
    // destroyed (mfc_gdi.cpp MapHDC / MapHGDIOBJ live until the process ends).
    virtual ~CHandleMap();

    static void* operator new(std::size_t size) noexcept { return gamecrt::operator_new(size); }
    static void operator delete(void* block) { gamecrt::operator_delete(block); }

    // Every temporary object's handles cleared and the object deleted, then the temporary map emptied. The original also returns at once for a null map (MFC's `this == NULL` test); every
    // caller here has one.
    void DeleteTemp();
    // The object of `handle` (null for null): the permanent one, else the temporary one,
    // else a new temporary object of objectClass (under the critical new handler; a CMemoryException
    // when none could be made). A temporary object's handles are (re)written.
    void* FromHandle(HANDLE handle);

    CMapPtrToPtr permanentMap{10};      // +0x04 (blocks of 10)
    CMapPtrToPtr temporaryMap{4};       // +0x20 (blocks of 4, 7 buckets)
    const CRuntimeClass* objectClass;   // +0x3C what FromHandle makes for an unknown handle
    unsigned handleOffset;              // +0x40 where the object keeps its handle
    int handleCount;                    // +0x44 1, or 2 (a DC's output and attribute handles)

private:
    void SetHandles(void* object, HANDLE handle) const;
};
static_assert(sizeof(CHandleMap) == 0x48);

} // namespace mfc::impl
