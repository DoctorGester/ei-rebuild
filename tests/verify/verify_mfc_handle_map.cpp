// Checks for MFC's handle maps, CMapPtrToPtr, CRuntimeClass::CreateObject, the new handler and the
// memory / resource throws (src/lib/mfc_handle_map.cpp, mfc_exception.cpp; ticket S8a).
// GetAssocAt and GetValueAt are pure: against the original through the trampoline. The rest run
// ours only: the wrappers check the map they leave (the key's association where its bucket says,
// the count). A start-up check runs 4,000 random operations on two maps side by side, one through
// the originals' trampolines and one through ours, and compares them after each (count, table size,
// every bucket's chain in order, the free list's length); it builds a handle map both ways, makes
// and finds temporary objects in one of ours, builds the image's own map (MapHGDIOBJ, image only
// otherwise) and throws the two static exceptions. Every scenario reaches the maps (the interface's
// DCs and fonts go through them).
#include "verify.h"

#include "../../src/lib/mfc_exception.h"
#include "../../src/lib/mfc_gdi.h"
#include "../../src/lib/mfc_handle_map.h"
#include "../../src/lib/mfc_string.h"

#include <cstring>
#include <new>
#include <random>
#include <vector>

// Handler types named as the game's classes (mfc_try.h declares CException).
class CMemoryException {
    CMemoryException() = delete;
};
class CResourceException {
    CResourceException() = delete;
};

#pragma warning(disable : 4740) // naked forwarders

namespace {

using ei::verify::Check;
using mfc::impl::CHandleMap;
using mfc::impl::CMapPtrToPtr;
using mfc::impl::CRuntimeClass;
using Assoc = CMapPtrToPtr::Assoc;
void* s_unused;

// The throws through their addresses (the wrappers below once the hooks are installed).
__declspec(naked) void __stdcall ThrowMemoryAt() EI_ORIGINAL(0x006FE389)
__declspec(naked) void __stdcall ThrowResourceAt() EI_ORIGINAL(0x0070A29E)

// CTempGdiObject's class and allocator (to give back the object the check makes).
CRuntimeClass* TempGdiObjectClass() { return reinterpret_cast<CRuntimeClass*>(0x00745F88); }
mfc::impl::CFixedAlloc& TempGdiObjectAlloc() { return *reinterpret_cast<mfc::impl::CFixedAlloc*>(0x007C8CF0); }
void* TempGdiObjectVtable() { return reinterpret_cast<void*>(0x0074645C); }

// The association of `key` found by walking its bucket (null when missing).
const Assoc* Find(const CMapPtrToPtr* map, void* key) {
    if (!map->table) return nullptr;
    for (const Assoc* assoc = map->table[(reinterpret_cast<std::uintptr_t>(key) >> 4) % map->hashSize]; assoc; assoc = assoc->next)
        if (assoc->key == key) return assoc;
    return nullptr;
}

// ---- Pure functions --------------------------------------------------------------------------------

// 00700404: PUSH ESI; MOV ESI,[ESP+8] (5 bytes).
Assoc*(__fastcall* s_getAssocAt)(const CMapPtrToPtr*, void*, void*, std::uint32_t*);
Assoc* __fastcall GetAssocAtVerify(const CMapPtrToPtr* self, void*, void* key, std::uint32_t* hash) {
    std::uint32_t expectedHash = 0;
    Assoc* expected = s_getAssocAt(self, nullptr, key, &expectedHash);
    Assoc* actual = self->GetAssocAt(key, hash);
    Check("CMapPtrToPtr::GetAssocAt", true, expected == actual && expectedHash == *hash, "key %p: %p / %p, bucket %u / %u", key, expected,
          actual, expectedHash, *hash);
    return actual;
}

// 00700436: PUSH ESI; MOV ESI,[ECX+4]; TEST ESI,ESI (6 bytes).
void*(__fastcall* s_getValueAt)(const CMapPtrToPtr*, void*, void*);
void* __fastcall GetValueAtVerify(const CMapPtrToPtr* self, void*, void* key) {
    void* expected = s_getValueAt(self, nullptr, key);
    void* actual = self->GetValueAt(key);
    Check("CMapPtrToPtr::GetValueAt", expected, actual, "key %p", key);
    return actual;
}

// ---- Ours only (the trampolines serve the start-up check) ------------------------------------------

// 007002C2: MOV EAX,ECX; XOR ECX,ECX; MOV [EAX+4],ECX (7 bytes).
CMapPtrToPtr*(__fastcall* s_construct)(CMapPtrToPtr*, void*, int);
CMapPtrToPtr* __fastcall ConstructVerify(CMapPtrToPtr* self, void*, int blockSize) {
    CMapPtrToPtr* result = self->Construct(blockSize);
    Check("CMapPtrToPtr::CMapPtrToPtr", true,
          result == self && self->vtable == CMapPtrToPtr::Vtable() && !self->table && self->hashSize == 17 && self->count == 0 && !self->freeList &&
              !self->blocks && self->blockSize == blockSize,
          "blocks of %d", blockSize);
    return result;
}

bool Empty(const CMapPtrToPtr* map) { return !map->table && map->count == 0 && !map->freeList && !map->blocks; }

// 00700375: MOV EAX,imm32 (5 bytes).
void(__fastcall* s_destruct)(CMapPtrToPtr*);
void __fastcall DestructVerify(CMapPtrToPtr* self) {
    self->Destruct();
    Check("CMapPtrToPtr::~CMapPtrToPtr", true, Empty(self) && self->vtable == CMapPtrToPtr::Vtable(), "");
}

CMapPtrToPtr* __fastcall DeletingDestructorVerify(CMapPtrToPtr* self, void*, unsigned flags) {
    Check("CMapPtrToPtr::`scalar deleting destructor'", true, self != nullptr, "flags %u", flags);
    return self->DeletingDestructor(flags);
}

// 00700305: PUSH EBX; PUSH ESI; MOV ESI,ECX; MOV EAX,[ESI+4] (7 bytes).
void(__fastcall* s_initHashTable)(CMapPtrToPtr*, void*, std::uint32_t, BOOL);
void __fastcall InitHashTableVerify(CMapPtrToPtr* self, void*, std::uint32_t size, BOOL allocNow) {
    self->InitHashTable(size, allocNow);
    bool clear = true;
    if (self->table)
        for (std::uint32_t i = 0; i < size; ++i) clear = clear && !self->table[i];
    Check("CMapPtrToPtr::InitHashTable", true, self->hashSize == size && (self->table != nullptr) == (allocNow != 0) && clear, "%u, %d", size,
          allocNow);
}

// 0070034A: PUSH ESI; MOV ESI,ECX; MOV EAX,[ESI+4] (6 bytes).
void(__fastcall* s_removeAll)(CMapPtrToPtr*);
void __fastcall RemoveAllVerify(CMapPtrToPtr* self) {
    self->RemoveAll();
    Check("CMapPtrToPtr::RemoveAll", true, Empty(self), "");
}

// 0070039E: PUSH ESI; MOV ESI,ECX; CMP dword ptr [ESI+0x10],0 (7 bytes).
Assoc*(__fastcall* s_newAssoc)(CMapPtrToPtr*);
Assoc* __fastcall NewAssocVerify(CMapPtrToPtr* self) {
    const int before = self->count;
    Assoc* assoc = self->NewAssoc();
    Check("CMapPtrToPtr::NewAssoc", true, assoc && !assoc->key && !assoc->value && self->count == before + 1 && self->blocks, "count %d", before);
    return assoc;
}

// 007003EB: MOV EAX,[ESP+4]; MOV EDX,[ECX+0x10] (7 bytes).
void(__fastcall* s_freeAssoc)(CMapPtrToPtr*, void*, Assoc*);
void __fastcall FreeAssocVerify(CMapPtrToPtr* self, void*, Assoc* assoc) {
    const int before = self->count;
    self->FreeAssoc(assoc);
    Check("CMapPtrToPtr::FreeAssoc", true, before > 1 ? self->count == before - 1 && self->freeList == assoc : Empty(self), "count %d", before);
}

// 0070048B: PUSH EBP; MOV EBP,ESP; PUSH ESI; PUSH EDI (5 bytes).
void**(__fastcall* s_at)(CMapPtrToPtr*, void*, void*);
void** __fastcall AtVerify(CMapPtrToPtr* self, void*, void* key) {
    const Assoc* existing = Find(self, key);
    const int before = self->count;
    void** value = self->At(key);
    const Assoc* assoc = Find(self, key);
    Check("CMapPtrToPtr::operator[]", true,
          assoc && value == &assoc->value && (existing ? assoc == existing && self->count == before : self->count == before + 1 && !*value), "key %p",
          key);
    return value;
}

// 007004DB: PUSH ESI; MOV ESI,[ECX+4]; TEST ESI,ESI (6 bytes).
BOOL(__fastcall* s_removeKey)(CMapPtrToPtr*, void*, void*);
BOOL __fastcall RemoveKeyVerify(CMapPtrToPtr* self, void*, void* key) {
    const bool had = Find(self, key) != nullptr;
    const int before = self->count;
    const BOOL result = self->RemoveKey(key);
    Check("CMapPtrToPtr::RemoveKey", true,
          (result != 0) == had && !Find(self, key) && (had ? (before == 1 ? Empty(self) : self->count == before - 1) : self->count == before), "key %p",
          key);
    return result;
}

// 00703617: MOV EAX,imm32 (5 bytes).
CHandleMap*(__fastcall* s_handleMapConstruct)(CHandleMap*, void*, CRuntimeClass*, unsigned, int);
CHandleMap* __fastcall HandleMapConstructVerify(CHandleMap* self, void*, CRuntimeClass* objectClass, unsigned offset, int handles) {
    CHandleMap* result = self->Construct(objectClass, offset, handles);
    Check("CHandleMap::CHandleMap", true,
          result == self && self->vtable == CHandleMap::Vtable() && self->permanentMap.blockSize == 10 && self->temporaryMap.blockSize == 4 &&
              self->temporaryMap.hashSize == 7 && self->objectClass == objectClass && self->handleOffset == offset && self->handleCount == handles,
          "class %p, offset %u, %d handles", objectClass, offset, handles);
    return result;
}

HANDLE HandleOf(const CHandleMap* map, const void* object, int index) {
    return reinterpret_cast<const HANDLE*>(static_cast<const char*>(object) + map->handleOffset)[index];
}

// 0070367C: MOV EAX,imm32 (5 bytes).
void*(__fastcall* s_fromHandle)(CHandleMap*, void*, HANDLE);
void* __fastcall FromHandleVerify(CHandleMap* self, void*, HANDLE handle) {
    void* object = self->FromHandle(handle);
    bool ok = !object;
    if (handle) {
        const bool permanent = object && self->permanentMap.GetValueAt(handle) == object;
        const bool temporary = object && !permanent && self->temporaryMap.GetValueAt(handle) == object;
        ok = permanent || (temporary && HandleOf(self, object, 0) == handle && (self->handleCount != 2 || HandleOf(self, object, 1) == handle));
    }
    Check("CHandleMap::FromHandle", true, ok, "handle %p: %p", handle, object);
    return object;
}

// 0070061B: MOV EAX,imm32 (5 bytes).
void*(__fastcall* s_createObject)(CRuntimeClass*);
void* __fastcall CreateObjectVerify(CRuntimeClass* self) {
    void* object = self->CreateObject();
    Check("CRuntimeClass::CreateObject", true, !self->create ? !object : object != nullptr, "%s", self->className);
    return object;
}

// 0070059D: MOV ECX,[ESP+4]; MOV EAX,[00796E14] (9 bytes).
gamecrt::NewHandler(__stdcall* s_setNewHandler)(gamecrt::NewHandler);
gamecrt::NewHandler __stdcall SetNewHandlerVerify(gamecrt::NewHandler handler) {
    const gamecrt::NewHandler old = mfc::impl::SetAfxNewHandler(handler);
    const gamecrt::NewHandler now = mfc::impl::SetAfxNewHandler(handler);
    Check("AfxSetNewHandler", true, now == handler, "%p", handler);
    return old;
}

void __stdcall ThrowMemoryVerify() {
    Check("AfxThrowMemoryException", true, true, "");
    mfc::impl::ThrowMemoryException();
}

void __stdcall ThrowResourceVerify() {
    Check("AfxThrowResourceException", true, true, "");
    mfc::impl::ThrowResourceException();
}

// ---- Start-up check --------------------------------------------------------------------------------

// The map as plain values: count, table size, each bucket's chain, the free list's length.
std::vector<std::uintptr_t> Shape(const CMapPtrToPtr& map) {
    std::vector<std::uintptr_t> shape{static_cast<std::uintptr_t>(map.count), map.hashSize, map.table != nullptr, map.blocks != nullptr};
    if (map.table)
        for (std::uint32_t i = 0; i < map.hashSize; ++i) {
            for (const Assoc* assoc = map.table[i]; assoc; assoc = assoc->next) {
                shape.push_back(reinterpret_cast<std::uintptr_t>(assoc->key));
                shape.push_back(reinterpret_cast<std::uintptr_t>(assoc->value));
            }
            shape.push_back(0xFFFFFFFF);
        }
    std::uintptr_t free = 0;
    for (const Assoc* assoc = map.freeList; assoc; assoc = assoc->next) ++free;
    shape.push_back(free);
    return shape;
}

void MapSideBySide() {
    alignas(CMapPtrToPtr) unsigned char originalBytes[sizeof(CMapPtrToPtr)];
    alignas(CMapPtrToPtr) unsigned char oursBytes[sizeof(CMapPtrToPtr)];
    auto* original = reinterpret_cast<CMapPtrToPtr*>(originalBytes);
    auto* ours = reinterpret_cast<CMapPtrToPtr*>(oursBytes);
    s_construct(original, nullptr, 3);
    ours->Construct(3);
    std::mt19937 random(8);
    int mismatches = 0, firstStep = -1;
    for (int step = 0; step < 4000; ++step) {
        const unsigned roll = random() % 100;
        void* key = reinterpret_cast<void*>(0x00010000 + (random() % 200) * 8);
        void* value = reinterpret_cast<void*>(static_cast<std::uintptr_t>(random()));
        bool same = true;
        if (roll < 55) {
            *s_at(original, nullptr, key) = value;
            *ours->At(key) = value;
        } else if (roll < 85) {
            same = (s_removeKey(original, nullptr, key) != 0) == (ours->RemoveKey(key) != 0);
        } else if (roll < 97) {
            std::uint32_t a = 0, b = 0;
            same = s_getValueAt(original, nullptr, key) == ours->GetValueAt(key) &&
                   (s_getAssocAt(original, nullptr, key, &a) != nullptr) == (ours->GetAssocAt(key, &b) != nullptr) && a == b;
        } else if (roll < 99) {
            s_removeAll(original);
            ours->RemoveAll();
        } else {
            // A new table size for an empty map (InitHashTable's only correct use).
            s_removeAll(original);
            ours->RemoveAll();
            const std::uint32_t size = 7 + random() % 40;
            s_initHashTable(original, nullptr, size, random() & 1);
            ours->InitHashTable(size, original->table != nullptr);
        }
        if (!same || Shape(*original) != Shape(*ours)) {
            if (firstStep < 0) firstStep = step;
            ++mismatches;
        }
    }
    Check("CMapPtrToPtr (4000 random steps against the original)", 0, mismatches, "first at step %d", firstStep);
    s_destruct(original);
    ours->Destruct();
}

void HandleMapCheck() {
    // The constructor both ways.
    alignas(CHandleMap) unsigned char originalBytes[sizeof(CHandleMap)];
    alignas(CHandleMap) unsigned char oursBytes[sizeof(CHandleMap)];
    std::memset(originalBytes, 0xCD, sizeof(originalBytes));
    std::memset(oursBytes, 0xCD, sizeof(oursBytes));
    auto* original = reinterpret_cast<CHandleMap*>(originalBytes);
    auto* ours = reinterpret_cast<CHandleMap*>(oursBytes);
    s_handleMapConstruct(original, nullptr, TempGdiObjectClass(), 4, 1);
    ours->Construct(TempGdiObjectClass(), 4, 1);
    Check("CHandleMap::CHandleMap (against the original)", true, std::memcmp(originalBytes, oursBytes, sizeof(CHandleMap)) == 0, "");

    // A temporary object for a stock pen, found again; a permanent object wins.
    const HGDIOBJ pen = ::GetStockObject(BLACK_PEN);
    auto* temporary = static_cast<mfc::impl::CGdiObject*>(ours->FromHandle(pen));
    const bool made = temporary && temporary->vtable == TempGdiObjectVtable() && temporary->handle == pen && ours->temporaryMap.count == 1;
    const bool again = ours->FromHandle(pen) == temporary && ours->temporaryMap.count == 1;
    mfc::impl::CGdiObject permanent{mfc::impl::CGdiObject::Vtable(), pen};
    *ours->permanentMap.At(pen) = &permanent;
    const bool preferred = ours->FromHandle(pen) == &permanent && !ours->FromHandle(nullptr);
    Check("CHandleMap::FromHandle (temporary and permanent objects)", true, made && again && preferred, "%d %d %d", made, again, preferred);
    if (temporary) TempGdiObjectAlloc().Free(temporary);
    original->permanentMap.Destruct();
    original->temporaryMap.Destruct();
    ours->permanentMap.Destruct();
    ours->temporaryMap.Destruct();

    // The image's own map of GDI objects (this thread's): made once, as afxMapHGDIOBJ makes it.
    CHandleMap* image = mfc::impl::MapHGDIOBJ(TRUE);
    const bool imageMap = image && mfc::impl::MapHGDIOBJ(FALSE) == image && image->objectClass == TempGdiObjectClass() &&
                          image->handleOffset == 4 && image->handleCount == 1;
    Check("afxMapHGDIOBJ (the image's)", true, imageMap, "%p", image);
}

void ThrowCheck() {
    bool memory = false, resource = false, critical = false;
    try {
        ThrowMemoryAt();
    } catch (::CMemoryException* thrown) {
        memory = reinterpret_cast<mfc::impl::CException*>(thrown)->autoDelete == 0;
    }
    try {
        ThrowResourceAt();
    } catch (::CResourceException* thrown) {
        resource = reinterpret_cast<mfc::impl::CException*>(thrown)->autoDelete == 0;
    }
    try {
        mfc::impl::CriticalNewHandler(16);
    } catch (::CMemoryException*) {
        critical = true;
    }
    Check("AfxThrowMemoryException / AfxThrowResourceException / AfxCriticalNewHandler (caught)", true, memory && resource && critical, "%d %d %d",
          memory, resource, critical);
}

void HandleMapStartupCheck() {
    MapSideBySide();
    HandleMapCheck();
    ThrowCheck();
}
EI_STARTUP_CHECK(HandleMapStartupCheck);

} // namespace

EI_VERIFY(0x00700404, GetAssocAtVerify, 5, s_getAssocAt);
EI_VERIFY(0x00700436, GetValueAtVerify, 6, s_getValueAt);
EI_VERIFY(0x007002C2, ConstructVerify, 7, s_construct);
EI_VERIFY(0x00700375, DestructVerify, 5, s_destruct);
EI_VERIFY(0x007002E9, DeletingDestructorVerify, 0, s_unused);
EI_VERIFY(0x00700305, InitHashTableVerify, 7, s_initHashTable);
EI_VERIFY(0x0070034A, RemoveAllVerify, 6, s_removeAll);
EI_VERIFY(0x0070039E, NewAssocVerify, 7, s_newAssoc);
EI_VERIFY(0x007003EB, FreeAssocVerify, 7, s_freeAssoc);
EI_VERIFY(0x0070048B, AtVerify, 5, s_at);
EI_VERIFY(0x007004DB, RemoveKeyVerify, 6, s_removeKey);
EI_VERIFY(0x00703617, HandleMapConstructVerify, 5, s_handleMapConstruct);
EI_VERIFY(0x0070367C, FromHandleVerify, 5, s_fromHandle);
EI_VERIFY(0x0070061B, CreateObjectVerify, 5, s_createObject);
EI_VERIFY(0x0070059D, SetNewHandlerVerify, 9, s_setNewHandler);
EI_VERIFY(0x006FE389, ThrowMemoryVerify, 0, s_unused);
EI_VERIFY(0x0070A29E, ThrowResourceVerify, 0, s_unused);
