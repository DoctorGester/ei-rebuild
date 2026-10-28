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

#include "lib/crt.h"

#include <cstdint>
#include <cstring>
#include <list>
#include <new>
#include "lib/mfc.h"
#include "lib/mfc_slots.h"
#include "game_slots.h"
#include "game_singletons.h"
#include "lib/nan_audit.h"

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

namespace {

constexpr std::uint32_t kArmed = 0x80000;   // flags_

bool IsDead(CMapObject* unit) { return unit->IsDead() != 0; }

// The units inside: an MFC CList whose elements are counted references.
using VictimList = std::list<CUnit*>;
using VictimNode = VictimList::iterator;

// MFC CPtrList (blocks of 10) as the game keeps it on the stack.
using PtrList = std::list<CUnit*>;
using PtrNode = PtrList::iterator;

// Is `unit` (its x / y) inside a circle, and alive? The first circle that holds it decides; a NaN
// distance is outside (FCOMPP; TEST AH,0x41).
bool StaysInside(MagicTrap* trap, CMapObject* unit) {
    int count = trap->area.size;
    const TrapCircle* circle = trap->area.data;
    float x = unit->localPosition_.x;
    float y = unit->localPosition_.y;
    for (int i = 0; i < count; ++i, ++circle) {
        float dx = x - circle->x;
        float dy = y - circle->y;
        if (circle->radius * circle->radius > LengthSquared2DUnrounded(dy, dx)) return !IsDead(unit);
    }
    return false;
}

void Append(PtrList* list, CUnit* unit) {
    auto node = list->emplace(list->end());
    *node = unit;
}

} // namespace

void MagicTrap::UpdateVictims() {
    VictimList* victims = &inside;
    for (auto node = victims->begin(); node != victims->end();) {
        auto next = std::next(node);
        if (!StaysInside(this, *node)) {
            // CList::RemoveAt, inlined: the reference released.
            if (*node) (*node)->Release();
            victims->erase(node);
        }
        node = next;
    }

    if (!(flags_ & kArmed)) return;
    CWorldServer* context = ServerWorld();
    PtrList found;
    PtrList newcomers;
    FindTrapVictims(context, &area, sides, &found);
    for (auto node = found.begin(); node != found.end();) {
        auto next = std::next(node);
        CUnit* unit = *node;
        auto known = victims->begin();
        while (known != victims->end() && *known != unit) ++known;
        if (known == victims->end()) {
            auto added = victims->emplace(victims->end());
            AIRefTo<CUnit>::Of(&*added)->Assign(unit);
            Append(&newcomers, unit);
        }
        node = next;
    }
    if (!newcomers.empty()) CastAtUnits(&newcomers);
    newcomers.clear();
    found.clear();
}

void FindTrapVictims(CWorldServer* context, const TrapArea* area, std::uint32_t sides,
                                std::list<CUnit*>* out) {
    using UnitList = std::list<CUnit*>;
    out->clear();
    CSectorMap* sectors = context->Sectors();
    UnitList found{};
    for (int i = 0; i < area->size; ++i) {
        const TrapCircle& circle = area->data[i];
        Vector3 point = {circle.x, circle.y, 0.0f};
        sectors->FindUnitsInRadius(&point, &found, circle.radius);
        for (auto node = found.begin(); node != found.end();) {
            CUnit* unit = *node;
            ++node;
            if (IsDead(unit)) continue;
            int side = unit->side;
            if (sides & (1u << (side & 31))) out->push_back(unit);   // SHL by CL: the count is taken mod 32
        }
    }
    found.clear();
}

namespace {
constexpr std::uint32_t kSpellAtUnits = 0x10000000;  // targetFlags_: cast at the units
constexpr float kFar = 1e30f;                        // a squared distance
} // namespace

void MagicTrap::CastAtUnits(std::list<CUnit*>* units) {
    using UnitList = std::list<CUnit*>;
    if (units->empty() || !spell) return;
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
            if (units->empty()) return;
            CUnit* closest = nullptr;
            for (auto node = units->begin(); node != units->end(); ++node) {
                CUnit* unit = *node;
                float dx = unit->localPosition_.x - origin.x;
                float dy = unit->localPosition_.y - origin.y;
                float dz = unit->localPosition_.z - origin.z;
                EI_NAN_AUDIT(dx, dy, dz);
                if (LengthSquaredUnrounded<Sum3::ZYX>(dx, dy, dz) < kFar) closest = unit;
            }
            if (!closest) return;
            units->clear();
            units->push_back(closest);   // AddTail calls the list's NewNode
        }
        for (auto node = units->begin(); node != units->end();) {
            auto next = std::next(node);
            CUnit* unit = *node;
            TheServerWorld()->CastSpell(spell, nullptr, &from, unit,
                                   &unit->localPosition_, 0);
            node = next;
        }
    }
    countdown = period;
}

namespace {
CSpellManager* SpellManagerOf(CWorld* context) { return context->SpellManager(); }

// The units inside: CList::RemoveAll inlined, as SetUp, the constructor and the destructor all have it (each
// element's reference released and cleared, the list emptied, its blocks freed).
void ClearVictims(MagicTrap* trap) {
    MagicTrap::UnitList& inside = trap->inside;
    for (auto node = inside.begin(); node != inside.end(); ++node) {
        if (*node) (*node)->Release();
        *node = nullptr;
    }
    inside.clear();
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

// --- Construction, destruction, Save / Load ------------------------------------------

namespace {

constexpr std::uint32_t kTrapFlags = 0x80040;
constexpr std::uint32_t kDefaultPeriod = 10;
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

MagicTrap::~MagicTrap() {
    DestroyMapObject();
    SetSpell(this, nullptr);
    area.SetSize(0, -1);
    points.SetSize(0, -1);
    ClearVictims(this);        // RemoveAll (each element released)
    // CList's destructor: its vtable, then RemoveAll again.
    ClearVictims(this);
    // The arrays' destructors (no element destructors; SetSize has already freed the storage).
    points.vtable = mfc::impl::TrapPointArrayClass::Vtable();
    if (points.data) gamecrt::operator_delete(points.data);
    area.vtable = mfc::impl::TrapCircleArrayClass::Vtable();
    if (area.data) gamecrt::operator_delete(area.data);
    if (spell) spell->Release();
}

void MagicTrap::Load(CStream* stream) { LoadTrap(stream); }
void MagicTrap::Save(CStream* stream) { SaveTrap(stream); }

void MagicTrap::SaveTrap(CStream* stream) {
    SaveMapObject(stream);
    std::int32_t value = spell ? spell->id_ : -1;
    stream->Write(&value, 4);
    value = area.size;
    stream->Write(&value, 4);
    for (int i = 0; i < area.size; ++i) stream->Write(&area.data[i], sizeof(TrapCircle));
    value = std::ssize(inside);
    stream->Write(&value, 4);
    for (auto node = inside.begin(); node != inside.end();) {
        auto next = std::next(node);
        AIRefTo<CUnit>::Of(&*node)->Save(stream);
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

void MagicTrap::DestroyTrap() {
    DestroyMapObject();
    SetSpell(this, nullptr);
    FreeCircles(area);
    points.SetSize(0, -1);
    ClearVictims(this);
}

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

void MagicTrap::FireAtOccupants() {
    CWorldServer* context = ServerWorld();
    PtrList found;
    if (flags_ & kArmed) {
        FindTrapVictims(context, &area, sides, &found);
        if (!found.empty()) CastAtUnits(&found);
    }
}

int MagicTrap::ForgetObject(CEngineObject* object) {
    for (auto node = inside.begin(); node != inside.end();) {
        auto next = std::next(node);
        if (*node == object) {
            // CList::RemoveAt, inlined: the reference released, the node removed.
            if (*node) (*node)->Release();
            inside.erase(node);
        }
        node = next;
    }
    return 0;
}
