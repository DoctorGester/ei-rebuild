// The connection pump of a network session (00479E30) and its start (0047A080), with the
// constructors of the per-player driver (0047A030, vtable 0073C990: 0x54 bytes, a memory stream at
// +0x28 and the player at +0x50) and of its base record (00477E60, vtable 0073C8A4: a file stream
// at +0xC over the CFile at +0x18). The session keeps two STLport lists of the drivers (+8 owning,
// +0xC plain holders); the lists' members are library code, called at their original addresses.
// See docs/notes/unmapped_410_470.md ("Ninth round").
#include "core_refcount.h"
#include "core_streams.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "lib/mfc.h"

#include <cstdint>
#include <cstring>
#include <new>

namespace netsession {

// An STLport list node holding one RefPtr.
struct ListNode {
    ListNode* next;
    ListNode* prev;
    RefPtr value;
};

struct Session {
    std::uint8_t unknown_00[8];
    ListNode* drivers;   // +0x08 list<RefPtr> (owning)
    ListNode* holders;   // +0x0C list of plain holders (released, not detached)
};

namespace {

template <typename T>
T& At(void* base, std::size_t offset) { return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(base) + offset); }
void SetVtable(void* object, std::uintptr_t vtable) { std::memcpy(object, &vtable, sizeof(vtable)); }

constexpr std::uint32_t kStreamLimit = 2000000000;
constexpr std::uintptr_t kRecordVtable = 0x0073C8A4;
constexpr std::uintptr_t kFileStreamVtable = 0x0073C8D8;
constexpr std::uintptr_t kDriverVtable = 0x0073C990;
constexpr std::uintptr_t kMemoryStreamVtable2 = 0x0073C960;   // the second CMemoryStream vtable

bool Alive(const CRefObject* object) { return object && !(object->flags7_ & CRefObject::kDetached); }

// list::insert (0047A450 / 0047A480: RET 0xC) and list::erase (0047A380, RET 8); the nodes'
// deallocation (0047A650, 12 bytes back to the pool) and the pool's free (00404D70).
__declspec(naked) ListNode** __fastcall DriversInsert(ListNode** /*list*/, void*, ListNode** /*out*/, ListNode* /*pos*/, const RefPtr*) EI_ORIGINAL(0x0047A450)
__declspec(naked) ListNode** __fastcall HoldersInsert(ListNode** /*list*/, void*, ListNode** /*out*/, ListNode* /*pos*/, const RefPtr*) EI_ORIGINAL(0x0047A480)
__declspec(naked) ListNode** __fastcall DriversErase(ListNode** /*list*/, void*, ListNode** /*out*/, ListNode* /*pos*/) EI_ORIGINAL(0x0047A380)
__declspec(naked) void __fastcall NodeDeallocate(ListNode*, int /*count*/) EI_ORIGINAL(0x0047A650)
__declspec(naked) void __fastcall NodeFree(void*, std::uint32_t /*bytes*/) EI_ORIGINAL(0x00404D70)
// The holder's deleting destructor (0047A6D0, refptr_copies.cpp), called with 0 as the original does.
__declspec(naked) void* __fastcall HolderDelete(RefPtr*, void*, unsigned) EI_ORIGINAL(0x0047A6D0)
// The player's message queue (+0x1148): 00419CC0 raises its high-water mark.
__declspec(naked) void __fastcall QueueSync(void*) EI_ORIGINAL(0x00419CC0)
// 00417E40 (thiscall on the manager 0079B5DC, RET 4): whether the player is one of its players.
__declspec(naked) bool __fastcall ManagerHasPlayer(void*, void*, void*) EI_ORIGINAL(0x00417E40)
// 00413250 (__fastcall size): operator new with the reference count cleared.
__declspec(naked) void* __fastcall AllocRefObject(std::size_t) EI_ORIGINAL(0x00413250)
// 00468C20: the memory stream's fields reset.
__declspec(naked) void __fastcall ResetMemoryStream(void*) EI_ORIGINAL(0x00468C20)

} // namespace

// 00477E60: the base record (its reference count at +4 is left as it is).
void* __fastcall RecordConstruct(std::uint8_t* self) {
    SetVtable(self, kRefValueVtable);
    std::uint8_t* stream = self + 0xC;
    SetVtable(stream, kStreamVtable);
    std::memcpy(stream + 8, &kStreamLimit, 4);
    stream[5] = 1;
    new (stream + 0xC) mfc::CFile();
    SetVtable(stream, kFileStreamVtable);
    SetVtable(self, kRecordVtable);
    self[8] = 0;
    return self;
}
EI_REPLACE(0x00477E60, RecordConstruct);

// 0047A030: the driver: the record, an empty memory stream at +0x28, no player.
void* __fastcall DriverConstruct(std::uint8_t* self) {
    RecordConstruct(self);
    At<std::uint32_t>(self, 0x30) = kStreamLimit;
    SetVtable(self + 0x28, kMemoryStreamVtable);
    At<std::uint32_t>(self, 0x40) = 0;
    At<std::uint32_t>(self, 0x44) = 0;
    At<std::uint32_t>(self, 0x48) = 0;
    At<std::uint32_t>(self, 0x4C) = 0;
    self[0x2D] = 1;
    At<std::uint32_t>(self, 0x38) = 0x80;
    At<std::int32_t>(self, 0x3C) = -1;
    At<std::uint32_t>(self, 0x50) = 0;
    SetVtable(self, kDriverVtable);
    return self;
}
EI_REPLACE(0x0047A030, DriverConstruct);

namespace {
// A new driver for `player` (allocated with 00413250 or operator new, as each caller does), put
// at the end of the owning list.
CRefObject* NewDriver(Session* self, CRefObject* player, void* raw) {
    auto* driver = raw ? static_cast<CRefObject*>(DriverConstruct(static_cast<std::uint8_t*>(raw))) : nullptr;
    reinterpret_cast<RefPtr*>(reinterpret_cast<std::uint8_t*>(driver) + 0x50)->Assign(player);
    RefPtr held{driver};   // taken without a reference; the list's copy adds one
    ListNode* out;
    DriversInsert(&self->drivers, nullptr, &out, self->drivers, &held);
    return driver;
}
} // namespace

// 00479E30 (RET 4): a driver for every player of `world` (list at +0x28) the session has none
// for, when the player has something queued and the manager 0079B5DC knows it; then each driver
// pumped (slot 10), and those whose player is gone or detached erased.
void __fastcall SessionPump(Session* self, void*, CRefObject* world) {
    if (!Alive(world)) return;
    for (ListNode* node = At<ListNode*>(world, 0x28)->next; node != At<ListNode*>(world, 0x28); node = node->next) {
        CRefObject* player = node->value.ptr;
        bool known = false;
        for (ListNode* it = self->drivers->next, *end = self->drivers; it != end; it = it->next) {
            if (At<CRefObject*>(it->value.ptr, 0x50) == player) {
                known = true;
                break;
            }
        }
        if (known) continue;
        auto* queue = reinterpret_cast<std::uint8_t*>(node->value.ptr) + 0x1148;
        QueueSync(queue);
        if (At<int>(queue, 0x14) - At<int>(queue, 4) + At<int>(queue, 0x10) <= 0) continue;
        void* manager = *reinterpret_cast<void**>(0x0079B5DC);
        if (!Alive(static_cast<CRefObject*>(manager))) continue;
        if (!ManagerHasPlayer(manager, nullptr, node->value.ptr)) continue;
        player = node->value.ptr;
        CRefObject* driver = NewDriver(self, player, AllocRefObject(0x54));
        // The original then destroys the emptied temporary holder (00413270 on null: nothing).
        RefPtr held{driver};
        ListNode* out;
        HoldersInsert(&self->holders, nullptr, &out, self->holders, &held);
    }
    for (ListNode* it = self->drivers->next; it != self->drivers;) {
        ei::VCall<void>(it->value.ptr, 10);
        if (Alive(At<CRefObject*>(it->value.ptr, 0x50))) {
            it = it->next;
        } else {
            ListNode* out;
            it = *DriversErase(&self->drivers, nullptr, &out, it);
        }
    }
}
EI_REPLACE(0x00479E30, SessionPump);

// 0047A080 (RET 4): both lists emptied, then one driver for `player`: started (slot 10), and its
// pending output drained into throwaway memory streams (slot 9) while slot 6 answers true.
// Returns the driver.
CRefObject* __fastcall SessionStart(Session* self, void*, CRefObject* player) {
    for (ListNode* node = self->drivers->next; node != self->drivers;) {
        ListNode* next = node->next;
        node->value.Destroy();
        NodeDeallocate(node, 1);
        node = next;
    }
    self->drivers->next = self->drivers;
    self->drivers->prev = self->drivers;
    for (ListNode* node = self->holders->next; node != self->holders;) {
        ListNode* next = node->next;
        HolderDelete(&node->value, nullptr, 0);
        if (node) NodeFree(node, 0xC);
        node = next;
    }
    self->holders->next = self->holders;
    self->holders->prev = self->holders;
    void* raw = gamecrt::operator_new(0x54);
    At<std::uint32_t>(raw, 4) = 0;
    CRefObject* driver = NewDriver(self, player, raw);
    ei::VCall<void>(driver, 10);
    while (ei::VCall<bool>(driver, 6)) {
        alignas(4) std::uint8_t stream[0x28];
        reinterpret_cast<CStream*>(stream)->ConstructBase();
        SetVtable(stream, kMemoryStreamVtable);
        At<std::uint32_t>(stream, 0x18) = 0;
        ResetMemoryStream(stream);
        SetVtable(stream, kMemoryStreamVtable2);
        ei::VCall<void>(driver, 9, static_cast<void*>(stream));
        SetVtable(stream, kMemoryStreamVtable);
        if (void* data = At<void*>(stream, 0x18)) gamecrt::operator_delete(data);
        SetVtable(stream, kStreamVtable);
    }
    return driver;
}
EI_REPLACE(0x0047A080, SessionStart);

} // namespace netsession
