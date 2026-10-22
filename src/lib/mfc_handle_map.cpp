// MFC 4.2's handle maps, CMapPtrToPtr, CRuntimeClass::CreateObject and the new handler, registered
// at their library addresses (mfc_handle_map.h).
#include "mfc_handle_map.h"

#include "ei/hook.h"
#include "compiled_vtable.h"
#include "mfc_exception.h"
#include "mfc_runtime_class.h"
#include "mfc_try.h"
#include "game/game_data.h"

#include <cstring>

namespace mfc::impl {

namespace {
std::uint32_t HashKey(const void* key) { return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(key)) >> 4; }
} // namespace

// ---- CRuntimeClass and the new handler ------------------------------------------------------------

void* CRuntimeClass::CreateObject() const {
    if (!create) return nullptr;
    void* object = nullptr;
    try {
        object = create();
    } catch (::CException* e) {
        DeleteException(reinterpret_cast<CException*>(e));
    }
    return object;
}

constinit gamecrt::NewHandler g_afxNewHandler = /*00796E14*/ {DefaultNewHandler};

gamecrt::NewHandler __stdcall SetAfxNewHandler(gamecrt::NewHandler handler) {
    const gamecrt::NewHandler old = g_afxNewHandler;
    g_afxNewHandler = handler;
    return old;
}

int __cdecl CriticalNewHandler(std::size_t) {
    ThrowMemoryException();
}

int __cdecl DefaultNewHandler(std::size_t) {
    ThrowMemoryException();
}

EI_REWRITTEN(0x0070061B, CRuntimeClass::CreateObject);
EI_REWRITTEN(0x0070059D, SetAfxNewHandler);
EI_REPLACE_STANDALONE(0x0070AB1A, CriticalNewHandler);
EI_REPLACE_STANDALONE(0x0070058F, DefaultNewHandler);

// ---- CMapPtrToPtr ---------------------------------------------------------------------------------

namespace {
// CMapPtrToPtr's compiled table (C19; game.exe's 0074689C): 0 GetRuntimeClass (its record), 1 the deleting destructor,
// 2 Serialize (CObject's, empty).
struct MapPtrToPtrTable final : ei::CompiledTag<MapPtrToPtrTable> {
    virtual const CRuntimeClass* GetRuntimeClass() const { return &kClassCMapPtrToPtr; }   // 0: 0070D128
    virtual void* DeleteScalar(unsigned flags) {                                            // 1: 007002E9
        return ObjectAs<CMapPtrToPtr>()->DeletingDestructor(flags);
    }
    virtual void Serialize(void*) {}                                                        // 2: 00444B00
};
} // namespace

void* CMapPtrToPtr::Vtable() { return MapPtrToPtrTable::Vtable(); }

CMapPtrToPtr* CMapPtrToPtr::Construct(int size) {
    table = nullptr;
    count = 0;
    freeList = nullptr;
    blocks = nullptr;
    vtable = Vtable();
    hashSize = 17;
    blockSize = size;
    return this;
}

void CMapPtrToPtr::Destruct() {
    vtable = Vtable();
    RemoveAll();
}

CMapPtrToPtr* CMapPtrToPtr::DeletingDestructor(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}

// The old table is deleted; a new one of `size` empty buckets only when asked for.
void CMapPtrToPtr::InitHashTable(std::uint32_t size, BOOL allocNow) {
    if (table) {
        gamecrt::operator_delete(table);
        table = nullptr;
    }
    if (allocNow) {
        table = gamecrt::operator_new_array<Assoc*>(size);
        std::memset(table, 0, size * sizeof(Assoc*));
    }
    hashSize = size;
}

void CMapPtrToPtr::RemoveAll() {
    if (table) {
        gamecrt::operator_delete(table);
        table = nullptr;
    }
    count = 0;
    freeList = nullptr;
    PlexFreeDataChain(blocks);
    blocks = nullptr;
}

// From the free list (a new block of `blockSize` when it is empty, its first association used
// first); the count raised, key and value zeroed.
CMapPtrToPtr::Assoc* CMapPtrToPtr::NewAssoc() {
    if (!freeList) {
        CPlex* block = PlexCreate(blocks, static_cast<unsigned>(blockSize), sizeof(Assoc));
        Assoc* assoc = reinterpret_cast<Assoc*>(block + 1) + (blockSize - 1);
        for (int i = blockSize - 1; i >= 0; --i, --assoc) {
            assoc->next = freeList;
            freeList = assoc;
        }
    }
    Assoc* assoc = freeList;
    freeList = assoc->next;
    ++count;
    assoc->key = nullptr;
    assoc->value = nullptr;
    return assoc;
}

// Back on the free list; the last one empties the map (RemoveAll).
void CMapPtrToPtr::FreeAssoc(Assoc* assoc) {
    assoc->next = freeList;
    freeList = assoc;
    if (--count == 0) RemoveAll();
}

CMapPtrToPtr::Assoc* CMapPtrToPtr::GetAssocAt(void* key, std::uint32_t* hash) const {
    *hash = HashKey(key) % hashSize;
    if (!table) return nullptr;
    for (Assoc* assoc = table[*hash]; assoc; assoc = assoc->next)
        if (assoc->key == key) return assoc;
    return nullptr;
}

void* CMapPtrToPtr::GetValueAt(void* key) const {
    if (!table) return nullptr;
    for (Assoc* assoc = table[HashKey(key) % hashSize]; assoc; assoc = assoc->next)
        if (assoc->key == key) return assoc->value;
    return nullptr;
}

// A missing key gets a new association (value null) at the head of its bucket, the table allocated
// first if there is none.
void** CMapPtrToPtr::At(void* key) {
    std::uint32_t hash;
    Assoc* assoc = GetAssocAt(key, &hash);
    if (!assoc) {
        if (!table) InitHashTable(hashSize, TRUE);
        assoc = NewAssoc();
        assoc->key = key;
        assoc->next = table[hash];
        table[hash] = assoc;
    }
    return &assoc->value;
}

BOOL CMapPtrToPtr::RemoveKey(void* key) {
    if (!table) return FALSE;
    Assoc** link = &table[HashKey(key) % hashSize];
    for (Assoc* assoc = *link; assoc; assoc = assoc->next) {
        if (assoc->key == key) {
            *link = assoc->next;
            FreeAssoc(assoc);
            return TRUE;
        }
        link = &assoc->next;
    }
    return FALSE;
}

// The next entry is the next in the chain, else the head of a later bucket.
void CMapPtrToPtr::GetNextAssoc(Assoc*& position, void*& key, void*& value) const {
    Assoc* assoc = position;
    if (assoc == StartPosition()) {
        for (std::uint32_t bucket = 0; bucket < hashSize; ++bucket)
            if ((assoc = table[bucket]) != nullptr) break;
    }
    Assoc* next = assoc->next;
    if (!next) {
        for (std::uint32_t bucket = HashKey(assoc->key) % hashSize + 1; bucket < hashSize; ++bucket)
            if ((next = table[bucket]) != nullptr) break;
    }
    position = next;
    key = assoc->key;
    value = assoc->value;
}

EI_REWRITTEN(0x007002C2, CMapPtrToPtr::Construct);
EI_REWRITTEN(0x00700375, CMapPtrToPtr::Destruct);
EI_REPLACE_METHOD(0x007002E9, CMapPtrToPtr::DeletingDestructor);
EI_REWRITTEN(0x00700305, CMapPtrToPtr::InitHashTable);
EI_REWRITTEN(0x0070034A, CMapPtrToPtr::RemoveAll);
EI_REWRITTEN(0x0070039E, CMapPtrToPtr::NewAssoc);
EI_REWRITTEN(0x007003EB, CMapPtrToPtr::FreeAssoc);
EI_REWRITTEN(0x00700404, CMapPtrToPtr::GetAssocAt);
EI_REWRITTEN(0x00700436, CMapPtrToPtr::GetValueAt);
EI_REWRITTEN(0x0070048B, CMapPtrToPtr::At);
EI_REWRITTEN(0x007004DB, CMapPtrToPtr::RemoveKey);
EI_REWRITTEN(0x0070051D, CMapPtrToPtr::GetNextAssoc);

// ---- CHandleMap -----------------------------------------------------------------------------------

namespace {
// CHandleMap's compiled table (C19; game.exe's 0074685C): 0 the deleting destructor (006E6CEC).
struct HandleMapTable final : ei::CompiledDeleter<HandleMapTable, &CHandleMap::DeletingDestructor> {};
} // namespace

void* CHandleMap::Vtable() { return HandleMapTable::Vtable(); }

void CHandleMap::Destruct() {
    vtable = Vtable();
    DeleteTemp();
    temporaryMap.Destruct();
    permanentMap.Destruct();
}

CHandleMap* CHandleMap::DeletingDestructor(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}

void CHandleMap::DeleteTemp() {
    CMapPtrToPtr::Assoc* position = temporaryMap.count ? CMapPtrToPtr::StartPosition() : nullptr;
    while (position) {
        void* handle;
        void* object;
        temporaryMap.GetNextAssoc(position, handle, object);
        SetHandles(object, nullptr);
        if (object) static_cast<CObjectVirtuals*>(object)->DeletingDestructor(1);
    }
    temporaryMap.RemoveAll();
}

CHandleMap* CHandleMap::Construct(const CRuntimeClass* cls, unsigned offset, int handles) {
    permanentMap.Construct(10);
    temporaryMap.Construct(4);
    vtable = Vtable();
    temporaryMap.InitHashTable(7, FALSE);
    objectClass = cls;
    handleOffset = offset;
    handleCount = handles;
    return this;
}

void CHandleMap::SetHandles(void* object, HANDLE handle) const {
    HANDLE* handles = reinterpret_cast<HANDLE*>(static_cast<char*>(object) + handleOffset);
    handles[0] = handle;
    if (handleCount == 2) handles[1] = handle;
}

void* CHandleMap::FromHandle(HANDLE handle) {
    if (!handle) return nullptr;
    if (void* object = permanentMap.GetValueAt(handle)) return object;
    if (void* object = temporaryMap.GetValueAt(handle)) {
        SetHandles(object, handle);
        return object;
    }
    const gamecrt::NewHandler old = SetAfxNewHandler(CriticalNewHandlerAt());
    void* object;
    try {
        object = objectClass->CreateObject();
        if (!object) ThrowMemoryException();
        *temporaryMap.At(handle) = object;
    } catch (::CException*) {
        SetAfxNewHandler(old);
        throw;
    }
    SetAfxNewHandler(old);
    SetHandles(object, handle);
    return object;
}

EI_REWRITTEN(0x00703617, CHandleMap::Construct);
EI_REWRITTEN(0x006E6D08, CHandleMap::Destruct);
EI_REWRITTEN(0x006E6CEC, CHandleMap::DeletingDestructor);
EI_REWRITTEN(0x0070373B, CHandleMap::DeleteTemp);
EI_REPLACE_METHOD(0x0070367C, CHandleMap::FromHandle);

} // namespace mfc::impl
