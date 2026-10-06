// Magic traps: who is inside, and casting at newcomers (0052A680), the living units of the trap's
// sides in its circles (0052A920), casting the spell (0052A4B0), set-up from the zone record
// (0052A2C0), the constructor and destructor body (00529DD0 / 00529F90), Save and Load (0052B070 /
// 0052ADC0), Create / Destroy (0052A100 / 0052A200), the update (0052AC20), firing (0052AB30) and
// forgetting a removed object (0052AC90). See magic_trap.h and docs/notes/units.md ("Magic traps").
#include "magic_trap.h"

#include "engine_object.h"
#include "unit.h"
#include "world.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"

#include <cstdint>
#include <cstring>
#include "originals_ai.h"
#include "originals_core.h"
#include "originals_engine.h"
#include "originals_library.h"
#include "lib/mfc.h"
#include "originals_units.h"

namespace {

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(object) + offset);
}

constexpr std::uint32_t kArmed = 0x80000;   // flags_
constexpr int kSlotDead = 0xDC / 4;         // CUnit slot 55: non-zero when dead

// The units inside (+0x268): an MFC CList whose elements are counted references.
using VictimList = mfc::CTypedPtrList<CEngineObject>;
using VictimNode = VictimList::Node;

// MFC CPtrList (vtable 0073D454, blocks of 10) as the original keeps it on the stack.
using PtrList = mfc::CTypedPtrList<CEngineObject>;
using PtrNode = PtrList::Node;
PtrList NewPtrList() { return PtrList{mfc::PtrListVtable(), nullptr, nullptr, 0, nullptr, nullptr, 10}; }

// The trap's map object context [00455940, rewritten in object.cpp]; the living units of the sides
// `sides` in the circles of `area` (+0x240), appended to `out` after emptying it [0052A920, fastcall
// with the context in ECX, RET 8; FindTrapVictims below, called through its address so its verify
// wrapper sees the call]; casting the spell at the newcomers `units` [0052A4B0, MagicTrap::CastAtUnits
// below, called through its address for the same reason].
__declspec(naked) void __fastcall FindVictims(void* /*context*/, void* /*area*/, std::uint32_t /*sides*/, PtrList* /*out*/)
    EI_ORIGINAL(0x0052A920)
// The sector map's units within `radius` of `point` into `list` (emptied first) [005C0BD0, rewritten
// in sector_map.cpp].
constexpr int kSectorMap = 0x294;             // the world's sector map
constexpr std::uintptr_t kPtrListVtable = 0x0073D454;
constexpr int kPtrListBlockSize = 10;
__declspec(naked) void __fastcall CastAtNewcomers(MagicTrap*, void*, PtrList* /*units*/) EI_ORIGINAL(0x0052A4B0)
// The lists' MFC template code: the victims' NewNode(prev, next) (element constructed as a null
// reference) and FreeNode (releases it) [0052CDB0 / 0052CE20]; a counted reference's assignment
// [0052A8F0]; the pointer list's NewNode(prev, next) and RemoveAll [0052CEF0 / 0052CA10].
VictimNode* __fastcall VictimNewNode(VictimList* a0, void*, VictimNode* a2, VictimNode* a3) { return (VictimNode*)ListNewNode52CDB0Original((void*)a0, nullptr, (void*)a2, (void*)a3); }
__declspec(naked) void __fastcall VictimFreeNode(VictimList*, void*, VictimNode*) EI_ORIGINAL(0x0052CE20)
void __fastcall ReferenceSet(CEngineObject** a0, void*, CEngineObject* a2) { CountedRefAssignOriginal((void*)a0, nullptr, (void*)a2); }
PtrNode* __fastcall PtrNewNode(PtrList* a0, void*, PtrNode* a2, PtrNode* a3) { return (PtrNode*)PtrListNewNode52CEF0Original((void*)a0, nullptr, (void*)a2, (void*)a3); }
inline void __fastcall PtrRemoveAll(PtrList* l) { PtrListRemoveAllOriginal(l); }

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
    VictimList* victims = reinterpret_cast<VictimList*>(&inside);
    for (VictimNode* node = victims->head; node;) {
        VictimNode* next = node->next;
        if (!StaysInside(this, node->data)) {
            // CList::RemoveAt, inlined.
            if (node == victims->head) victims->head = node->next;
            else node->prev->next = node->next;
            if (node == victims->tail) victims->tail = node->prev;
            else node->next->prev = node->prev;
            VictimFreeNode(victims, nullptr, node);
        }
        node = next;
    }

    if (!(flags_ & kArmed)) return;
    void* context = ManagerContextOf(this);
    PtrList found = NewPtrList();
    PtrList newcomers = NewPtrList();
    FindVictims(context, &area, sides, &found);
    for (PtrNode* node = found.head; node;) {
        PtrNode* next = node->next;
        CEngineObject* unit = node->data;
        VictimNode* known = victims->head;
        while (known && known->data != unit) known = known->next;
        if (!known) {
            VictimNode* added = VictimNewNode(victims, nullptr, victims->tail, nullptr);
            ReferenceSet(&added->data, nullptr, unit);
            if (victims->tail) victims->tail->next = added;
            else victims->head = added;
            victims->tail = added;
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
        SectorMapFindUnitsInRadius(sectors, nullptr, point, &found, circle.radius);
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
void __fastcall CirclesSetSize(TrapArea* a0, void*, int a2, int a3) { Array12SetSizeOriginal((void*)a0, nullptr, a2, a3); }
__declspec(naked) void __fastcall PointsSetSize(TrapPoints*, void*, int /*n*/, int /*grow*/) EI_ORIGINAL(0x0052C8E0)
constexpr int kSlotSpellManager = 0x58 / 4;   // the map object context's slot 22
constexpr int kSlotRelease = 0x10 / 4;        // a counted object's release

// The units inside (+0x268): CList::RemoveAll inlined, as SetUp, the constructor and the
// destructor all have it (each element's reference released and cleared, the list emptied, its
// blocks freed).
void ClearVictims(MagicTrap* trap) {
    MagicTrap::UnitList& inside = trap->inside;
    for (MagicTrap::UnitList::Node* node = inside.head; node; node = node->next) {
        if (node->data) node->data->Release();
        node->data = nullptr;
    }
    inside.count = 0;
    inside.free = nullptr;
    inside.tail = nullptr;
    inside.head = nullptr;
    mfc::PlexFreeDataChain(inside.blocks);
    inside.blocks = nullptr;
}
} // namespace

void MagicTrap::SetUp(const MagicTrapRecord* record) {
    sides = record->sides;
    const char* text = record->spell;
    void* manager = ei::VCall<void*>(ManagerContextOf(this), kSlotSpellManager);
    auto* made = static_cast<CEngineObject*>(CreateSpellFromText(manager, nullptr, text));
    CEngineObject* old = spell;
    spell = made;
    if (made) ++made->refCount_;
    if (old) old->Release();
    std::memcpy(&positionX, record->position, sizeof(record->position));   // copied as dwords

    // The loop bounds are read again on every pass, as in the original.
    CirclesSetSize(&area, nullptr, record->area.count, -1);
    for (int i = 0; i < record->area.count; ++i)
        std::memcpy(&area.circles[i], &record->area.circles[i], sizeof(TrapCircle));
    PointsSetSize(&points, nullptr, record->points.count, -1);
    for (int i = 0; i < record->points.count; ++i)
        std::memcpy(&points.points[2 * i], &record->points.points[2 * i], 2 * sizeof(float));

    ClearVictims(this);

    period = static_cast<int>(record->period);
    countdown = 0;
    singleTarget = record->singleTarget != 0 ? 1 : 0;
}
EI_REPLACE_METHOD(0x0052A2C0, MagicTrap::SetUp);

// --- Construction, destruction, Save / Load (ticket U8) ------------------------------------------

namespace {
// The map object's constructor and destructor body [0050E2D0 / 0050E660, rewritten in
// map_object_lifetime.cpp], its Create and Destroy (slots 2 and 3, called directly) [0050E770 /
// 0050E840, not rewritten], the map object part of Load / Save [0050F7A0 / 0050F900, not
// rewritten]: all through their game addresses.
__declspec(naked) void __fastcall ConstructMapObjectAt(void*) EI_ORIGINAL(0x0050E2D0)
__declspec(naked) void __fastcall DestructMapObjectAt(void*) EI_ORIGINAL(0x0050E660)
__declspec(naked) int __fastcall MapObjectCreate(void*) EI_ORIGINAL(0x0050E770)
__declspec(naked) void __fastcall MapObjectDestroy(void*) EI_ORIGINAL(0x0050E840)
void __fastcall MapObjectLoad(void* a0, void*, UnitStream* a2) { MapObjectLoadOriginal(a0, nullptr, (void*)a2); }
void __fastcall MapObjectSave(void* a0, void*, UnitStream* a2) { MapObjectSaveOriginal(a0, nullptr, (void*)a2); }
// A counted reference's Save (its object's id, -1 for none) [0050E290, RET 4]; the units inside
// read back and appended, each resolved in `context` [0052AF20, fastcall: ECX the list, EDX the
// stream, RET 4; MFC template code with the reference's Load 0050E220]; CBaseManager::FindObject
// [00403F40, rewritten in base_manager.cpp].
void __fastcall CountedRefSave(CEngineObject** a0, void*, UnitStream* a2) { CountedRefSaveOriginal((void*)a0, nullptr, (void*)a2); }
__declspec(naked) void __fastcall VictimListLoad(VictimList*, UnitStream*, void* /*context*/) EI_ORIGINAL(0x0052AF20)

constexpr std::uintptr_t kTrapVtable = 0x0073E63C;
constexpr std::uintptr_t kCirclesVtable = 0x0073C68C;        // while constructed
constexpr std::uintptr_t kPointsVtable = 0x0073C680;
constexpr std::uintptr_t kCirclesBaseVtable = 0x0073C698;    // set by their destructors
constexpr std::uintptr_t kPointsBaseVtable = 0x0073C6A4;
constexpr std::uintptr_t kVictimListVtable = 0x0073E730;
constexpr std::uint32_t kTrapFlags = 0x80040;
constexpr std::uint32_t kDefaultPeriod = 10;
constexpr int kVictimBlockSize = 10;
constexpr int kTailFields[] = {0x284, 0x288, 0x28C, 0x238};   // period, countdown, single, sides

void SetVtableAt(void* object, int offset, std::uintptr_t vtable) { At<std::uintptr_t>(object, offset) = vtable; }

// An empty MFC CArray's four fields after its vtable (data, grow by, capacity, count; in the
// original's order).
void EmptyArray(void* array) {
    At<void*>(array, 0x04) = nullptr;
    At<int>(array, 0x10) = 0;
    At<int>(array, 0x0C) = 0;
    At<int>(array, 0x08) = 0;
}

// The spell (+0x23C), a counted reference: the new one's count raised before the old one is
// released.
void SetSpell(MagicTrap* trap, CEngineObject* spell) {
    CEngineObject* old = trap->spell;
    trap->spell = spell;
    if (spell) ++spell->refCount_;
    if (old) old->Release();
}
} // namespace

MagicTrap* MagicTrap::Construct() {
    ConstructMapObjectAt(this);
    At<void*>(this, 0x23C) = nullptr;
    EmptyArray(&At<TrapArea>(this, 0x240));
    SetVtableAt(this, 0x240, kCirclesVtable);
    EmptyArray(&At<TrapPoints>(this, 0x254));
    SetVtableAt(this, 0x254, kPointsVtable);
    SetVtableAt(this, 0x268, kVictimListVtable);
    At<int>(this, 0x274) = 0;
    At<void*>(this, 0x278) = nullptr;
    At<void*>(this, 0x270) = nullptr;
    At<void*>(this, 0x26C) = nullptr;
    At<void*>(this, 0x27C) = nullptr;
    At<int>(this, 0x280) = kVictimBlockSize;
    SetVtableAt(this, 0, kTrapVtable);

    MapObjectCreate(this);
    At<std::uint32_t>(this, 0x238) = 0xFFFFFFFFu;
    At<std::uint32_t>(this, 0x08) |= kTrapFlags;
    SetSpell(this, nullptr);
    CirclesSetSize(&At<TrapArea>(this, 0x240), nullptr, 0, -1);
    PointsSetSize(&At<TrapPoints>(this, 0x254), nullptr, 0, -1);
    ClearVictims(this);
    At<std::uint32_t>(this, 0x288) = 0;
    At<int>(this, 0x28C) = 0;
    At<std::uint32_t>(this, 0x284) = kDefaultPeriod;
    return this;
}
EI_REPLACE_METHOD(0x00529DD0, MagicTrap::Construct);

void MagicTrap::DestructBody() {
    SetVtableAt(this, 0, kTrapVtable);
    MapObjectDestroy(this);
    SetSpell(this, nullptr);   // 0052C690(0)
    CirclesSetSize(&At<TrapArea>(this, 0x240), nullptr, 0, -1);
    PointsSetSize(&At<TrapPoints>(this, 0x254), nullptr, 0, -1);
    ClearVictims(this);        // RemoveAll (each element through 0052D030(0))
    // CList's destructor: its vtable, then RemoveAll again (elements through 0052CEC0).
    SetVtableAt(this, 0x268, kVictimListVtable);
    ClearVictims(this);
    // The arrays' destructors (no element destructors; SetSize has already freed the storage).
    SetVtableAt(this, 0x254, kPointsBaseVtable);
    if (points.points) gamecrt::operator_delete(points.points);
    SetVtableAt(this, 0x240, kCirclesBaseVtable);
    if (area.circles) gamecrt::operator_delete(area.circles);
    if (spell) spell->Release();
    DestructMapObjectAt(this);
}
EI_REPLACE_METHOD(0x00529F90, MagicTrap::DestructBody);

void MagicTrap::SaveTrap(UnitStream* stream) {
    MapObjectSave(this, nullptr, stream);
    std::int32_t value = spell ? spell->id_ : -1;
    stream->Write(&value, 4);
    value = At<int>(this, 0x248);
    stream->Write(&value, 4);
    for (int i = 0; i < At<int>(this, 0x248); ++i) stream->Write(At<TrapCircle*>(this, 0x244) + i, sizeof(TrapCircle));
    value = At<int>(this, 0x274);
    stream->Write(&value, 4);
    for (VictimNode* node = At<VictimNode*>(this, 0x26C); node;) {
        VictimNode* next = node->next;
        CountedRefSave(&node->data, nullptr, stream);
        node = next;
    }
    value = At<int>(this, 0x25C);
    stream->Write(&value, 4);
    for (int i = 0; i < At<int>(this, 0x25C); ++i) stream->Write(At<float*>(this, 0x258) + 2 * i, 2 * sizeof(float));
    for (int offset : kTailFields) {
        value = At<std::int32_t>(this, offset);
        stream->Write(&value, 4);
    }
}
EI_REPLACE_METHOD(0x0052B070, MagicTrap::SaveTrap);

void MagicTrap::LoadTrap(UnitStream* stream) {
    MapObjectLoad(this, nullptr, stream);
    void* manager = ei::VCall<void*>(ManagerContextOf(this), kSlotSpellManager);
    // The original reads the id and the two counts into its argument's stack slot: a read that
    // fails leaves the previous value there (at first the stream pointer itself).
    auto value = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(stream));
    stream->Read(&value, 4);
    SetSpell(this, value == -1 ? nullptr : ManagerFindObject(manager, nullptr, value));
    stream->Read(&value, 4);
    CirclesSetSize(&area, nullptr, value, -1);
    for (int i = 0; i < area.count; ++i) stream->Read(&area.circles[i], sizeof(TrapCircle));
    VictimListLoad(&At<VictimList>(this, 0x268), stream, ManagerContextOf(this));
    stream->Read(&value, 4);
    PointsSetSize(&points, nullptr, value, -1);
    for (int i = 0; i < points.count; ++i) stream->Read(&points.points[2 * i], 2 * sizeof(float));
    for (int offset : kTailFields) stream->Read(&At<std::uint8_t>(this, offset), 4);
}
EI_REPLACE_METHOD(0x0052ADC0, MagicTrap::LoadTrap);

// --- Create, Destroy, the update, firing, forgetting a removed object (ticket U10) ---------------

namespace {
// The trap's own code, called through its addresses so the verify wrappers see the calls: the units
// inside updated [0052A680, MagicTrap::UpdateVictims]; firing at what is in it [0052AB30,
// MagicTrap::FireAtOccupants].
__declspec(naked) void __fastcall UpdateVictimsAt(MagicTrap*) EI_ORIGINAL(0x0052A680)
__declspec(naked) void __fastcall FireAtOccupantsAt(MagicTrap*) EI_ORIGINAL(0x0052AB30)

// The circles' SetSize(0, -1), inlined by Create and Destroy: the storage freed, count and
// capacity 0 (in the original's order).
void FreeCircles(TrapArea& area) {
    if (area.circles) {
        gamecrt::operator_delete(area.circles);
        area.circles = nullptr;
    }
    area.maxCount = 0;
    area.count = 0;
}

// The CPtrList's destructor on the stack, RemoveAll inlined (its elements need nothing): the list
// emptied, then its blocks freed.
void DestroyPtrList(PtrList& list) {
    list.count = 0;
    list.free = nullptr;
    list.tail = nullptr;
    list.head = nullptr;
    mfc::PlexFreeDataChain(list.blocks);
}
} // namespace

int MagicTrap::CreateTrap() {
    int result = MapObjectCreate(this);
    sides = 0xFFFFFFFFu;
    flags_ |= kTrapFlags;
    SetSpell(this, nullptr);
    FreeCircles(area);
    PointsSetSize(&points, nullptr, 0, -1);
    ClearVictims(this);
    period = kDefaultPeriod;
    countdown = 0;
    singleTarget = 0;
    return result;
}
EI_REPLACE_METHOD(0x0052A100, MagicTrap::CreateTrap);

void MagicTrap::DestroyTrap() {
    MapObjectDestroy(this);
    SetSpell(this, nullptr);
    FreeCircles(area);
    PointsSetSize(&points, nullptr, 0, -1);
    ClearVictims(this);
}
EI_REPLACE_METHOD(0x0052A200, MagicTrap::DestroyTrap);

int MagicTrap::UpdateTrap() {
    int result = CMapObject::Update();
    auto* world = static_cast<CWorld*>(ManagerContextOf(this));
    if (!world->IsServer() || !spell) return result;
    if (period <= 0) {
        UpdateVictimsAt(this);
        return result;
    }
    if (countdown > 0) --countdown;
    if (countdown <= 0) FireAtOccupantsAt(this);
    return result;
}
EI_REPLACE_METHOD(0x0052AC20, MagicTrap::UpdateTrap);

void MagicTrap::FireAtOccupants() {
    void* context = ManagerContextOf(this);
    PtrList found = NewPtrList();
    if (flags_ & kArmed) {
        FindVictims(context, &area, sides, &found);
        if (found.count) CastAtNewcomers(this, nullptr, &found);
    }
    DestroyPtrList(found);
}
EI_REPLACE_METHOD(0x0052AB30, MagicTrap::FireAtOccupants);

int MagicTrap::ForgetObject(CEngineObject* object) {
    for (UnitList::Node* node = inside.head; node;) {
        UnitList::Node* next = node->next;
        if (node->data == object) {
            // CList::RemoveAt, inlined: unlinked, the reference released and cleared, the node
            // freed; the last node empties the list as RemoveAll does.
            if (node == inside.head) inside.head = node->next;
            else node->prev->next = node->next;
            if (node == inside.tail) inside.tail = node->prev;
            else node->next->prev = node->prev;
            if (node->data) node->data->Release();
            node->data = nullptr;
            node->next = inside.free;
            inside.free = node;
            if (--inside.count == 0) ClearVictims(this);
        }
        node = next;
    }
    return 0;
}
EI_REPLACE_METHOD(0x0052AC90, MagicTrap::ForgetObject);
