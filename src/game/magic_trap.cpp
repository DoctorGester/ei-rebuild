// Magic traps: who is inside, casting at newcomers, set-up from the zone record, lifetime, Save and Load and the
// update. See magic_trap.h and docs/notes/units.md ("Magic traps").
#include "base_manager.h"
#include "magic_trap.h"

#include "engine_object.h"
#include "map_object.h"
#include "spell.h"
#include "unit.h"
#include "unit_ai.h"
#include "world.h"

#include "ei/hook.h"
#include "lib/crt.h"

#include <cstdint>
#include <cstring>
#include <new>
#include "lib/mfc.h"
#include "lib/mfc_slots.h"
#include "game_slots.h"
#include "game_lists.h"
#include "game_singletons.h"
#include "lib/nan_audit.h"

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

namespace {

constexpr std::uint32_t kArmed = 0x80000;   // flags_

bool IsDead(CEngineObject* unit) { return ObjectSlots(unit)->IsDead() != 0; }

// The units inside: an MFC CList whose elements are counted references.
using VictimList = mfc::CTypedPtrList<CUnit>;
using VictimNode = VictimList::Node;

// MFC CPtrList (blocks of 10) as the game keeps it on the stack.
using PtrList = mfc::CTypedPtrList<CUnit>;
using PtrNode = PtrList::Node;
PtrList NewPtrList() { return PtrList{mfc::impl::PtrListClass::Vtable(), nullptr, nullptr, 0, nullptr, nullptr, 10}; }

constexpr int kPtrListBlockSize = 10;

// Is `unit` (its x / y) inside a circle, and alive? The first circle that holds it decides; a NaN
// distance is outside (FCOMPP; TEST AH,0x41).
bool StaysInside(MagicTrap* trap, CEngineObject* unit) {
    int count = trap->area.size;
    const TrapCircle* circle = trap->area.data;
    float x = ObjectSlots(unit)->localPosition_.x;
    float y = ObjectSlots(unit)->localPosition_.y;
    for (int i = 0; i < count; ++i, ++circle) {
        float dx = x - circle->x;
        float dy = y - circle->y;
        if (circle->radius * circle->radius > LengthSquared2DUnrounded(dy, dx)) return !IsDead(unit);
    }
    return false;
}

void Append(PtrList* list, CUnit* unit) {
    PtrNode* node = list->NewNode(list->tail, nullptr);
    node->data = unit;
    if (list->tail) list->tail->next = node;
    else list->head = node;
    list->tail = node;
}

} // namespace

void MagicTrap::UpdateVictims() {
    VictimList* victims = &inside;
    for (VictimNode* node = victims->head; node;) {
        VictimNode* next = node->next;
        if (!StaysInside(this, node->data)) {
            // CList::RemoveAt, inlined.
            if (node == victims->head) victims->head = node->next;
            else node->prev->next = node->next;
            if (node == victims->tail) victims->tail = node->prev;
            else node->next->prev = node->prev;
            mfc::impl::FreeNodeAs<gamelists::ObjectRefs>(victims, node);
        }
        node = next;
    }

    if (!(flags_ & kArmed)) return;
    CWorldServer* context = ServerWorld();
    PtrList found = NewPtrList();
    PtrList newcomers = NewPtrList();
    FindTrapVictims(context, &area, sides, &found);
    for (PtrNode* node = found.head; node;) {
        PtrNode* next = node->next;
        CUnit* unit = node->data;
        VictimNode* known = victims->head;
        while (known && known->data != unit) known = known->next;
        if (!known) {
            VictimNode* added = victims->NewNode(victims->tail, nullptr);
            AIRefTo<CUnit>::Of(&added->data)->Assign(unit);
            if (victims->tail) victims->tail->next = added;
            else victims->head = added;
            victims->tail = added;
            Append(&newcomers, unit);
        }
        node = next;
    }
    if (newcomers.count) CastAtUnits(&newcomers);
    newcomers.RemoveAll();
    found.RemoveAll();
}
EI_REWRITTEN(0x0052A680, MagicTrap::UpdateVictims);

void __fastcall FindTrapVictims(CWorldServer* context, const TrapArea* area, std::uint32_t sides,
                                mfc::CTypedPtrList<CUnit>* out) {
    using UnitList = mfc::CTypedPtrList<CUnit>;
    out->RemoveAll();
    CSectorMap* sectors = context->Sectors();
    UnitList found{mfc::impl::PtrListClass::Vtable(), nullptr, nullptr, 0, nullptr, nullptr, kPtrListBlockSize};
    for (int i = 0; i < area->size; ++i) {
        const TrapCircle& circle = area->data[i];
        Vector3 point = {circle.x, circle.y, 0.0f};
        sectors->FindUnitsInRadius(&point, &found, circle.radius);
        for (UnitList::Node* node = found.head; node;) {
            CUnit* unit = node->data;
            node = node->next;
            if (IsDead(unit)) continue;
            int side = unit->side;
            if (sides & (1u << (side & 31))) out->AddTail(unit);   // SHL by CL: the count is taken mod 32
        }
    }
    found.RemoveAll();
}
EI_REWRITTEN(0x0052A920, FindTrapVictims);

namespace {
constexpr std::uint32_t kSpellAtUnits = 0x10000000;  // targetFlags_: cast at the units
constexpr float kFar = 1e30f;                        // a squared distance
} // namespace

void MagicTrap::CastAtUnits(mfc::CTypedPtrList<CUnit>* units) {
    using UnitList = mfc::CTypedPtrList<CUnit>;
    if (units->count == 0 || !spell) return;
    const Vector3& origin = localPosition_;
    Vector3 from = {origin.x, origin.y, origin.z};
    auto* world = ObjectWorld(this);
    from.z = static_cast<float>(world->RawSurfaceHeightAt(from.x, from.y) + from.z);
    if (!(spell->targetFlags_ & kSpellAtUnits) && points.size != 0) {
        for (int i = 0; i < points.size; ++i) {
            const TrapPoint& point = points.data[i];
            Vector3 at = {point.x, point.y, 0.0f};
            TheServerWorld()->CastSpell(spell, nullptr, &from, nullptr, &at, 0);
        }
    } else {
        if (singleTarget != 0) {
            if (!units->head) return;
            CUnit* closest = nullptr;
            for (UnitList::Node* node = units->head; node; node = node->next) {
                CUnit* unit = node->data;
                float dx = ObjectSlots(unit)->localPosition_.x - origin.x;
                float dy = ObjectSlots(unit)->localPosition_.y - origin.y;
                float dz = ObjectSlots(unit)->localPosition_.z - origin.z;
                EI_NAN_AUDIT(dx, dy, dz);
                if (LengthSquaredUnrounded<Sum3::ZYX>(dx, dy, dz) < kFar) closest = unit;
            }
            if (!closest) return;
            units->RemoveAll();
            units->AddTail(closest);   // AddTail calls the list's NewNode
        }
        for (UnitList::Node* node = units->head; node;) {
            UnitList::Node* next = node->next;
            CUnit* unit = node->data;
            TheServerWorld()->CastSpell(spell, nullptr, &from, unit,
                                   &ObjectSlots(unit)->localPosition_, 0);
            node = next;
        }
    }
    countdown = period;
}
EI_REWRITTEN(0x0052A4B0, MagicTrap::CastAtUnits);

namespace {
CSpellManager* SpellManagerOf(CWorld* context) { return context->SpellManager(); }

// The units inside: CList::RemoveAll inlined, as SetUp, the constructor and the destructor all have it (each
// element's reference released and cleared, the list emptied, its blocks freed).
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
    const char* text = record->spell.c_str();
    CSpellManager* manager = SpellManagerOf(WorldContext());
    CSpell* made = manager->CreateSpellFromText(text);
    CSpell* old = spell;
    spell = made;
    if (made) ++made->refCount_;
    if (old) old->Release();
    std::memcpy(&localPosition_.x, &record->position, sizeof(record->position));   // copied as dwords

    // The loop bounds are read again on every pass, as in the game.
    area.SetSize(record->area.size, -1);
    for (int i = 0; i < record->area.size; ++i)
        std::memcpy(&area.data[i], &record->area.data[i], sizeof(TrapCircle));
    points.SetSize(record->points.size, -1);
    for (int i = 0; i < record->points.size; ++i)
        std::memcpy(&points.data[i], &record->points.data[i], sizeof(TrapPoint));

    ClearVictims(this);

    period = record->period;
    countdown = 0;
    singleTarget = record->singleTarget != 0 ? 1 : 0;
}
EI_REWRITTEN(0x0052A2C0, MagicTrap::SetUp);

// --- Construction, destruction, Save / Load ------------------------------------------

namespace {

constexpr std::uint32_t kTrapFlags = 0x80040;
constexpr std::uint32_t kDefaultPeriod = 10;
constexpr int kVictimBlockSize = 10;
// The fields saved after the points: period, countdown, single target, sides.
void* TailFields(MagicTrap* trap, int i) {
    void* fields[] = {&trap->period, &trap->countdown, &trap->singleTarget, &trap->sides};
    return fields[i];
}
constexpr int kTailFieldCount = 4;

// An empty MFC CArray's four fields after its vtable (data, grow by, capacity, count; in the game's order).
template <typename T>
void EmptyArray(mfc::CArray<T>& array) {
    array.data = nullptr;
    array.growBy = 0;
    array.maxSize = 0;
    array.size = 0;
}

// The spell, a counted reference: the new one's count raised before the old one is released.
void SetSpell(MagicTrap* trap, CSpell* spell) {
    CSpell* old = trap->spell;
    trap->spell = spell;
    if (spell) ++spell->refCount_;
    if (old) old->Release();
}
} // namespace

MagicTrap::MagicTrap() {
    spell = nullptr;
    EmptyArray(area);
    area.vtable = mfc::impl::TrapAreasArrayClass::Vtable();   // while constructed
    EmptyArray(points);
    points.vtable = mfc::impl::TrapTargetsArrayClass::Vtable();
    inside.vtable = gameslots::VictimListClass::Vtable();
    inside.count = 0;
    inside.free = nullptr;
    inside.tail = nullptr;
    inside.head = nullptr;
    inside.blocks = nullptr;
    inside.blockSize = kVictimBlockSize;

    CreateMapObject();
    sides = 0xFFFFFFFFu;
    flags_ |= kTrapFlags;
    SetSpell(this, nullptr);
    area.SetSize(0, -1);
    points.SetSize(0, -1);
    ClearVictims(this);
    countdown = 0;
    singleTarget = 0;
    period = static_cast<int>(kDefaultPeriod);
}
EI_REWRITTEN_SPECIAL(0x00529DD0, MagicTrap::MagicTrap);

MagicTrap::~MagicTrap() {
    DestroyMapObject();
    SetSpell(this, nullptr);
    area.SetSize(0, -1);
    points.SetSize(0, -1);
    ClearVictims(this);        // RemoveAll (each element released)
    // CList's destructor: its vtable, then RemoveAll again.
    inside.vtable = gameslots::VictimListClass::Vtable();
    ClearVictims(this);
    // The arrays' destructors (no element destructors; SetSize has already freed the storage).
    points.vtable = mfc::impl::TrapPointArrayClass::Vtable();
    if (points.data) gamecrt::operator_delete(points.data);
    area.vtable = mfc::impl::TrapCircleArrayClass::Vtable();
    if (area.data) gamecrt::operator_delete(area.data);
    if (spell) spell->Release();
}
EI_REWRITTEN_SPECIAL(0x00529F90, MagicTrap::~MagicTrap);

void MagicTrap::Load(CStream* stream) { LoadTrap(stream); }
void MagicTrap::Save(CStream* stream) { SaveTrap(stream); }

void MagicTrap::SaveTrap(CStream* stream) {
    SaveMapObject(stream);
    std::int32_t value = spell ? spell->id_ : -1;
    stream->Write(&value, 4);
    value = area.size;
    stream->Write(&value, 4);
    for (int i = 0; i < area.size; ++i) stream->Write(&area.data[i], sizeof(TrapCircle));
    value = inside.count;
    stream->Write(&value, 4);
    for (VictimNode* node = inside.head; node;) {
        VictimNode* next = node->next;
        AIRefTo<CUnit>::Of(&node->data)->Save(stream);
        node = next;
    }
    value = points.size;
    stream->Write(&value, 4);
    for (int i = 0; i < points.size; ++i) stream->Write(&points.data[i], sizeof(TrapPoint));
    for (int i = 0; i < kTailFieldCount; ++i) {
        std::memcpy(&value, TailFields(this, i), 4);
        stream->Write(&value, 4);
    }
}
EI_REWRITTEN(0x0052B070, MagicTrap::SaveTrap);

void MagicTrap::LoadTrap(CStream* stream) {
    LoadMapObject(stream);
    CSpellManager* manager = SpellManagerOf(WorldContext());
    // The id and the two counts are read into one local: a read that fails leaves the previous value.
    std::int32_t value = 0;
    stream->Read(&value, 4);
    SetSpell(this, value == -1 ? nullptr : manager->FindObjectAs<CSpell>(value));
    stream->Read(&value, 4);
    area.SetSize(value, -1);
    for (int i = 0; i < area.size; ++i) stream->Read(&area.data[i], sizeof(TrapCircle));
    CountedListLoad(&inside, stream, WorldContext());
    stream->Read(&value, 4);
    points.SetSize(value, -1);
    for (int i = 0; i < points.size; ++i) stream->Read(&points.data[i], sizeof(TrapPoint));
    for (int i = 0; i < kTailFieldCount; ++i) stream->Read(TailFields(this, i), 4);
}
EI_REWRITTEN(0x0052ADC0, MagicTrap::LoadTrap);

// --- Create, Destroy, the update, firing, forgetting a removed object -------------

namespace {

// The circles' SetSize(0, -1), inlined by Create and Destroy: the storage freed, count and
// capacity 0 (in the game's order).
void FreeCircles(TrapArea& area) {
    if (area.data) {
        gamecrt::operator_delete(area.data);
        area.data = nullptr;
    }
    area.maxSize = 0;
    area.size = 0;
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
    int result = CreateMapObject();
    sides = 0xFFFFFFFFu;
    flags_ |= kTrapFlags;
    SetSpell(this, nullptr);
    FreeCircles(area);
    points.SetSize(0, -1);
    ClearVictims(this);
    period = kDefaultPeriod;
    countdown = 0;
    singleTarget = 0;
    return result;
}
EI_REWRITTEN(0x0052A100, MagicTrap::CreateTrap);

void MagicTrap::DestroyTrap() {
    DestroyMapObject();
    SetSpell(this, nullptr);
    FreeCircles(area);
    points.SetSize(0, -1);
    ClearVictims(this);
}
EI_REWRITTEN(0x0052A200, MagicTrap::DestroyTrap);

int MagicTrap::UpdateTrap() {
    int result = CMapObject::Update();
    auto* world = WorldContext();
    if (!world->IsServer() || !spell) return result;
    if (period <= 0) {
        UpdateVictims();
        return result;
    }
    if (countdown > 0) --countdown;
    if (countdown <= 0) FireAtOccupants();
    return result;
}
EI_REWRITTEN(0x0052AC20, MagicTrap::UpdateTrap);

void MagicTrap::FireAtOccupants() {
    CWorldServer* context = ServerWorld();
    PtrList found = NewPtrList();
    if (flags_ & kArmed) {
        FindTrapVictims(context, &area, sides, &found);
        if (found.count) CastAtUnits(&found);
    }
    DestroyPtrList(found);
}
EI_REWRITTEN(0x0052AB30, MagicTrap::FireAtOccupants);

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
EI_REWRITTEN(0x0052AC90, MagicTrap::ForgetObject);
