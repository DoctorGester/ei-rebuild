// Magic traps: who is inside, and casting at newcomers (0052A680). See magic_trap.h and
// docs/notes/units.md ("Magic traps").
#include "magic_trap.h"

#include "engine_object.h"

#include "ei/hook.h"
#include "ei/vcall.h"

#include <cstdint>

namespace {

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(object) + offset);
}

constexpr std::uint32_t kArmed = 0x80000;   // flags_
constexpr int kSlotDead = 0xDC / 4;         // CUnit slot 55: non-zero when dead

// The units inside (+0x268): an MFC CList whose elements are counted references.
struct VictimNode {
    VictimNode* next;
    VictimNode* prev;
    CEngineObject* unit;
};
struct VictimList {
    void* vtable;
    VictimNode* head;
    VictimNode* tail;
    int count;
};

// MFC CPtrList (vtable 0073D454, blocks of 10) as the original keeps it on the stack.
struct PtrNode {
    PtrNode* next;
    PtrNode* prev;
    CEngineObject* data;
};
struct PtrList {
    std::uintptr_t vtable = 0x0073D454;
    PtrNode* head = nullptr;
    PtrNode* tail = nullptr;
    int count = 0;
    void* free = nullptr;
    void* blocks = nullptr;
    int blockSize = 10;
};

// The trap's map object context [00455940, rewritten in object.cpp]; the living units of the sides
// `sides` in the circles of `area` (+0x240), appended to `out` after emptying it [0052A920, fastcall
// with the context in ECX, RET 8]; casting the spell at the newcomers `units` [0052A4B0].
__declspec(naked) void* __fastcall ManagerContext(void*) EI_ORIGINAL(0x00455940)
__declspec(naked) void __fastcall FindVictims(void* /*context*/, void* /*area*/, std::uint32_t /*sides*/, PtrList* /*out*/)
    EI_ORIGINAL(0x0052A920)
__declspec(naked) void __fastcall CastAtUnits(MagicTrap*, void*, PtrList* /*units*/) EI_ORIGINAL(0x0052A4B0)
// The lists' MFC template code: the victims' NewNode(prev, next) (element constructed as a null
// reference) and FreeNode (releases it) [0052CDB0 / 0052CE20]; a counted reference's assignment
// [0052A8F0]; the pointer list's NewNode(prev, next) and RemoveAll [0052CEF0 / 0052CA10].
__declspec(naked) VictimNode* __fastcall VictimNewNode(VictimList*, void*, VictimNode* /*prev*/, VictimNode* /*next*/)
    EI_ORIGINAL(0x0052CDB0)
__declspec(naked) void __fastcall VictimFreeNode(VictimList*, void*, VictimNode*) EI_ORIGINAL(0x0052CE20)
__declspec(naked) void __fastcall ReferenceSet(CEngineObject**, void*, CEngineObject*) EI_ORIGINAL(0x0052A8F0)
__declspec(naked) PtrNode* __fastcall PtrNewNode(PtrList*, void*, PtrNode* /*prev*/, PtrNode* /*next*/) EI_ORIGINAL(0x0052CEF0)
__declspec(naked) void __fastcall PtrRemoveAll(PtrList*) EI_ORIGINAL(0x0052CA10)

// Is `unit` (its x / y) inside a circle, and alive? The first circle that holds it decides; a NaN
// distance is outside (FCOMPP; TEST AH,0x41).
bool StaysInside(MagicTrap* trap, CEngineObject* unit) {
    int count = At<int>(trap, 0x248);
    const TrapCircle* circle = At<const TrapCircle*>(trap, 0x244);
    float x = At<float>(unit, 0x1C);
    float y = At<float>(unit, 0x20);
    for (int i = 0; i < count; ++i, ++circle) {
        float dx = x - circle->x;
        float dy = y - circle->y;
        if (circle->radius * circle->radius > dy * dy + dx * dx) return ei::VCall<int>(unit, kSlotDead) == 0;
    }
    return false;
}

void Append(PtrList* list, CEngineObject* unit) {
    PtrNode* node = PtrNewNode(list, nullptr, list->tail, nullptr);
    node->data = unit;
    if (list->tail) list->tail->next = node;
    else list->head = node;
    list->tail = node;
}

} // namespace

void MagicTrap::UpdateVictims() {
    VictimList* inside = &At<VictimList>(this, 0x268);
    for (VictimNode* node = inside->head; node;) {
        VictimNode* next = node->next;
        if (!StaysInside(this, node->unit)) {
            // CList::RemoveAt, inlined.
            if (node == inside->head) inside->head = node->next;
            else node->prev->next = node->next;
            if (node == inside->tail) inside->tail = node->prev;
            else node->next->prev = node->prev;
            VictimFreeNode(inside, nullptr, node);
        }
        node = next;
    }

    if (!(At<std::uint32_t>(this, 0x08) & kArmed)) return;
    void* context = ManagerContext(this);
    PtrList found;
    PtrList newcomers;
    FindVictims(context, &At<std::uint8_t>(this, 0x240), At<std::uint32_t>(this, 0x238), &found);
    for (PtrNode* node = found.head; node;) {
        PtrNode* next = node->next;
        CEngineObject* unit = node->data;
        VictimNode* known = inside->head;
        while (known && known->unit != unit) known = known->next;
        if (!known) {
            VictimNode* added = VictimNewNode(inside, nullptr, inside->tail, nullptr);
            ReferenceSet(&added->unit, nullptr, unit);
            if (inside->tail) inside->tail->next = added;
            else inside->head = added;
            inside->tail = added;
            Append(&newcomers, unit);
        }
        node = next;
    }
    if (newcomers.count) CastAtUnits(this, nullptr, &newcomers);
    PtrRemoveAll(&newcomers);
    PtrRemoveAll(&found);
}
EI_REPLACE_METHOD(0x0052A680, MagicTrap::UpdateVictims);
