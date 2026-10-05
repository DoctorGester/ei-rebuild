// Magic traps: who is inside, and casting at newcomers (0052A680), the living units of the trap's
// sides in its circles (0052A920), casting the spell (0052A4B0), set-up from the zone record
// (0052A2C0). See magic_trap.h and
// docs/notes/units.md ("Magic traps").
#include "magic_trap.h"

#include "engine_object.h"

#include "ei/hook.h"
#include "ei/vcall.h"

#include <cstdint>
#include <cstring>

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
// with the context in ECX, RET 8; FindTrapVictims below, called through its address so its verify
// wrapper sees the call]; casting the spell at the newcomers `units` [0052A4B0, MagicTrap::CastAtUnits
// below, called through its address for the same reason].
__declspec(naked) void* __fastcall ManagerContext(void*) EI_ORIGINAL(0x00455940)
__declspec(naked) void __fastcall FindVictims(void* /*context*/, void* /*area*/, std::uint32_t /*sides*/, PtrList* /*out*/)
    EI_ORIGINAL(0x0052A920)
// The sector map's units within `radius` of `point` into `list` (emptied first) [005C0BD0, rewritten
// in sector_map.cpp].
__declspec(naked) void __fastcall SectorFind(void* /*sectors*/, void*, const float* /*point*/, void* /*list*/, float /*radius*/)
    EI_ORIGINAL(0x005C0BD0)
constexpr int kSectorMap = 0x294;             // the world's sector map
constexpr std::uintptr_t kPtrListVtable = 0x0073D454;
constexpr int kPtrListBlockSize = 10;
__declspec(naked) void __fastcall CastAtNewcomers(MagicTrap*, void*, PtrList* /*units*/) EI_ORIGINAL(0x0052A4B0)
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
    if (newcomers.count) CastAtNewcomers(this, nullptr, &newcomers);
    PtrRemoveAll(&newcomers);
    PtrRemoveAll(&found);
}
EI_REPLACE_METHOD(0x0052A680, MagicTrap::UpdateVictims);

void __fastcall FindTrapVictims(void* context, const TrapArea* area, std::uint32_t sides,
                                mfc::CTypedPtrList<CEngineObject>* out) {
    using UnitList = mfc::CTypedPtrList<CEngineObject>;
    out->RemoveAll();
    void* sectors = static_cast<std::uint8_t*>(context) + kSectorMap;
    UnitList found{reinterpret_cast<void*>(kPtrListVtable), nullptr, nullptr, 0, nullptr, nullptr, kPtrListBlockSize};
    for (int i = 0; i < area->count; ++i) {
        const TrapCircle& circle = area->circles[i];
        float point[3] = {circle.x, circle.y, 0.0f};
        SectorFind(sectors, nullptr, point, &found, circle.radius);
        for (UnitList::Node* node = found.head; node;) {
            CEngineObject* unit = node->data;
            node = node->next;
            if (ei::VCall<int>(unit, kSlotDead) != 0) continue;
            int side = At<int>(unit, 0x250);
            if (sides & (1u << (side & 31))) out->AddTail(unit);   // SHL by CL: the count is taken mod 32
        }
    }
    found.RemoveAll();
}
EI_REPLACE(0x0052A920, FindTrapVictims);

namespace {
// The object's world [00455950]; the world server (007AFF60) casts `spell` from `caster` at
// `from` on `target` at `at` [0058E850 (spell, caster, from, target, at, 0), RET 0x18].
__declspec(naked) void* __fastcall ObjectWorld(void*) EI_ORIGINAL(0x00455950)
__declspec(naked) void __fastcall WorldCastSpell(void*, void*, void* /*spell*/, void* /*caster*/, const float* /*from*/,
                                                 CEngineObject* /*target*/, const float* /*at*/, int)
    EI_ORIGINAL(0x0058E850)
void* const kWorldServer = reinterpret_cast<void*>(0x007AFF60);
constexpr int kSlotGroundHeight = 0x70 / 4;          // world: (x, y) -> the ground height (ST0)
constexpr std::uint32_t kSpellAtUnits = 0x10000000;  // spell prototype +0x3C: cast at the units
constexpr float kFar = 1e30f;                        // 0073E73C, a squared distance
} // namespace

void MagicTrap::CastAtUnits(mfc::CTypedPtrList<CEngineObject>* units) {
    using UnitList = mfc::CTypedPtrList<CEngineObject>;
    if (units->count == 0 || !At<void*>(this, 0x23C)) return;
    const float* position = &At<float>(this, 0x1C);
    float from[3] = {position[0], position[1], position[2]};
    void* world = ObjectWorld(this);
    from[2] = static_cast<float>(ei::VCall<double>(world, kSlotGroundHeight, from[0], from[1]) + from[2]);
    if (!(At<std::uint32_t>(At<void*>(this, 0x23C), 0x3C) & kSpellAtUnits) && At<int>(this, 0x25C) != 0) {
        for (int i = 0; i < At<int>(this, 0x25C); ++i) {
            const float* point = At<const float*>(this, 0x258) + 2 * i;
            float at[3] = {point[0], point[1], 0.0f};
            WorldCastSpell(kWorldServer, nullptr, At<void*>(this, 0x23C), nullptr, from, nullptr, at, 0);
        }
    } else {
        if (At<int>(this, 0x28C) != 0) {
            if (!units->head) return;
            CEngineObject* closest = nullptr;
            for (UnitList::Node* node = units->head; node; node = node->next) {
                CEngineObject* unit = node->data;
                float dx = At<float>(unit, 0x1C) - position[0];
                float dy = At<float>(unit, 0x20) - position[1];
                float dz = At<float>(unit, 0x24) - position[2];
                // FCOMP; TEST AH,1: taken when below, or NaN.
                if (!(dz * dz + dy * dy + dx * dx >= kFar)) closest = unit;
            }
            if (!closest) return;
            units->RemoveAll();
            units->AddTail(closest);   // the original's AddTail calls the list's NewNode (0052CEF0)
        }
        for (UnitList::Node* node = units->head; node;) {
            UnitList::Node* next = node->next;
            CEngineObject* unit = node->data;
            WorldCastSpell(kWorldServer, nullptr, At<void*>(this, 0x23C), nullptr, from, unit, &At<float>(unit, 0x1C), 0);
            node = next;
        }
    }
    At<std::uint32_t>(this, 0x288) = At<std::uint32_t>(this, 0x284);
}
EI_REPLACE_METHOD(0x0052A4B0, MagicTrap::CastAtUnits);

namespace {
// The spell manager's CreateSpellFromText (00689030, RET 4; not rewritten); the two arrays'
// SetSize(n, -1) (0046EAD0 for the circles, 0052C8E0 for the points; MFC template code, RET 8).
__declspec(naked) void* __fastcall CreateSpellFromText(void* /*manager*/, void*, const char*) EI_ORIGINAL(0x00689030)
__declspec(naked) void __fastcall CirclesSetSize(TrapArea*, void*, int /*n*/, int /*grow*/) EI_ORIGINAL(0x0046EAD0)
__declspec(naked) void __fastcall PointsSetSize(TrapPoints*, void*, int /*n*/, int /*grow*/) EI_ORIGINAL(0x0052C8E0)
constexpr int kSlotSpellManager = 0x58 / 4;   // the map object context's slot 22
constexpr int kSlotRelease = 0x10 / 4;        // a counted object's release
} // namespace

void MagicTrap::SetUp(const MagicTrapRecord* record) {
    At<std::uint32_t>(this, 0x238) = record->sides;
    const char* text = record->spell;
    void* manager = ei::VCall<void*>(ManagerContext(this), kSlotSpellManager);
    void* spell = CreateSpellFromText(manager, nullptr, text);
    void* old = At<void*>(this, 0x23C);
    At<void*>(this, 0x23C) = spell;
    if (spell) ++At<int>(spell, 4);
    if (old) ei::VCall<void>(old, kSlotRelease);
    std::memcpy(&At<float>(this, 0x1C), record->position, sizeof(record->position));   // copied as dwords

    // The loop bounds are read again on every pass, as in the original.
    auto* area = &At<TrapArea>(this, 0x240);
    CirclesSetSize(area, nullptr, record->area.count, -1);
    for (int i = 0; i < record->area.count; ++i)
        std::memcpy(&area->circles[i], &record->area.circles[i], sizeof(TrapCircle));
    auto* points = &At<TrapPoints>(this, 0x254);
    PointsSetSize(points, nullptr, record->points.count, -1);
    for (int i = 0; i < record->points.count; ++i)
        std::memcpy(&points->points[2 * i], &record->points.points[2 * i], 2 * sizeof(float));

    // The units inside: CList::RemoveAll inlined (each element's reference released and cleared,
    // the list emptied, its blocks freed).
    for (VictimNode* node = At<VictimNode*>(this, 0x26C); node; node = node->next) {
        if (node->unit) ei::VCall<void>(node->unit, kSlotRelease);
        node->unit = nullptr;
    }
    At<int>(this, 0x274) = 0;           // count
    At<void*>(this, 0x278) = nullptr;   // free nodes
    At<void*>(this, 0x270) = nullptr;   // tail
    At<void*>(this, 0x26C) = nullptr;   // head
    mfc::PlexFreeDataChain(At<mfc::CPlex*>(this, 0x27C));
    At<void*>(this, 0x27C) = nullptr;

    At<std::uint32_t>(this, 0x284) = record->period;
    At<std::uint32_t>(this, 0x288) = 0;
    At<int>(this, 0x28C) = record->singleTarget != 0 ? 1 : 0;
}
EI_REPLACE_METHOD(0x0052A2C0, MagicTrap::SetUp);
