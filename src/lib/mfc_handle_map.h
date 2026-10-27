#pragma once

// MFC 4.2's handle maps (winhand.cpp): the C++ object of a Windows handle. Nothing ever empties the temporary
// maps (no CWinApp, no OnIdle). See docs/notes/standalone.md, "The image's source files".

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
    const char* className;
    int objectSize;
    unsigned schema;                // 0xFFFF: not serializable
    void*(__stdcall* create)();     // null: not creatable
    const CRuntimeClass* baseClass;
    CRuntimeClass* nextClass;       // the module state's class list; AfxClassInit links it

    // A new object through `create`, or null (no create function, or a CException thrown
    // on the way, which is deleted).
    void* CreateObject() const;
};

// The new handler operator new calls when an allocation fails (_afxNewHandler).
gamecrt::NewHandler __stdcall SetAfxNewHandler(gamecrt::NewHandler handler);
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
        Assoc* next;
        void* key;
        void* value;
    };

    // Empty, 17 buckets (no table yet), associations in blocks of `blockSize`.
    explicit CMapPtrToPtr(int blockSize);
    ~CMapPtrToPtr() override;
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

    static Assoc* StartPosition() { return reinterpret_cast<Assoc*>(-1); }

    Assoc** table = nullptr;
    std::uint32_t hashSize = 17;    // 17 until InitHashTable
    int count = 0;
    Assoc* freeList = nullptr;
    CPlex* blocks = nullptr;
    int blockSize;
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

    // Every temporary object's handles cleared and the object deleted, then the temporary map emptied. A null
    // map also returns at once (MFC's `this == NULL` test); every caller here has one.
    void DeleteTemp();
    // The object of `handle` (null for null): the permanent one, else the temporary one,
    // else a new temporary object of objectClass (under the critical new handler; a CMemoryException
    // when none could be made). A temporary object's handles are (re)written.
    void* FromHandle(HANDLE handle);

    CMapPtrToPtr permanentMap{10};      // (blocks of 10)
    CMapPtrToPtr temporaryMap{4};       // (blocks of 4, 7 buckets)
    const CRuntimeClass* objectClass;   // what FromHandle makes for an unknown handle
    unsigned handleOffset;              // where the object keeps its handle
    int handleCount;                    // 1, or 2 (a DC's output and attribute handles)

private:
    void SetHandles(void* object, HANDLE handle) const;
};
static_assert(sizeof(CHandleMap) == 0x48);

} // namespace mfc::impl
