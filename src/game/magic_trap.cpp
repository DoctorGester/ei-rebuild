// Magic traps: who is inside, casting at newcomers, set-up from the zone record, lifetime, Save and Load and the
// update. See magic_trap.h and docs/notes/units.md ("Magic traps").
#include "magic_trap.h"

#include "base_manager.h"
#include "engine_object.h"
#include "game_singletons.h"
#include "map_object.h"
#include "spell.h"
#include "unit.h"
#include "unit_ai.h"
#include "world.h"

#include "lib/bytes.h"
#include "lib/crt.h"
#include "lib/nan_audit.h"

#include <cstdint>
#include <cstring>
#include <list>
#include <new>

namespace {


bool IsDead(CMapObject* unit) { return unit->IsDead(); }

// Is `unit` (its x / y) inside a circle, and alive? The first circle that holds it decides; a NaN
// distance is outside.
bool StaysInside(MagicTrap* trap, CMapObject* unit) {
    int count = std::ssize(trap->area);
    const TrapCircle* circle = trap->area.data();
    float x = unit->localPosition_.x;
    float y = unit->localPosition_.y;
    for (int i = 0; i < count; ++i, ++circle) {
        float dx = x - circle->x;
        float dy = y - circle->y;
        if (circle->radius * circle->radius > LengthSquared2DUnrounded(dy, dx)) return !IsDead(unit);
    }
    return false;
}

void Append(std::list<CUnit*>* list, CUnit* unit) {
    auto node = list->emplace(list->end());
    *node = unit;
}

constexpr float kFar = 1e30f;                        // a squared distance

CSpellManager* SpellManagerOf(CWorld* context) { return context->SpellManager(); }

// The units inside: the list emptied, as SetUp, the constructor and the destructor all do it (each
// element's reference released and cleared, the list emptied, its blocks freed).
void ClearVictims(MagicTrap* trap) {
    std::list<CUnit*>& inside = trap->inside;
    for (auto& node : inside) {
        if (node) node->Release();
        node = nullptr;
    }
    inside.clear();
}

constexpr MapObjectFlag kTrapFlags = MapObjectFlag::Armed | MapObjectFlag::NotDrawn;
constexpr std::uint32_t kDefaultPeriod = 10;
// The fields saved after the points: period, countdown, single target, sides.
void* TailFields(MagicTrap* trap, int i) {
    void* fields[] = {&trap->period, &trap->countdown, &trap->singleTarget, &trap->sides};
    return fields[i];
}
constexpr int kTailFieldCount = 4;


// The spell, a counted reference: the new one's count raised before the old one is released.
void SetSpell(MagicTrap* trap, CSpell* spell) {
    trap->spell.Assign(spell);
}


} // namespace

void MagicTrap::UpdateVictims() {
    std::list<CUnit*>* victims = &inside;
    for (auto node = victims->begin(); node != victims->end();) {
        auto next = std::next(node);
        if (!StaysInside(this, *node)) {
            // The reference released.
            if (*node) (*node)->Release();
            victims->erase(node);
        }
        node = next;
    }

    if (!(flags_ & MapObjectFlag::Armed)) return;
    CWorldServer* context = ServerWorld();
    std::list<CUnit*> found;
    std::list<CUnit*> newcomers;
    FindTrapVictims(context, &area, sides, &found);
    for (auto node = found.begin(); node != found.end();) {
        auto next = std::next(node);
        CUnit* unit = *node;
        auto known = victims->begin();
        while (known != victims->end() && *known != unit) ++known;
        if (known == victims->end()) {
            if (unit) unit->AddRef();
            victims->push_back(unit);
            Append(&newcomers, unit);
        }
        node = next;
    }
    if (!newcomers.empty()) CastAtUnits(&newcomers);
    newcomers.clear();
    found.clear();
}

void FindTrapVictims(CWorldServer* context, const std::vector<TrapCircle>* area, std::uint32_t sides,
                                std::list<CUnit*>* out) {
    out->clear();
    CSectorMap* sectors = &context->aiMap_.sectorMap;
    std::list<CUnit*> found{};
    for (int i = 0; i < std::ssize(*area); ++i) {
        const TrapCircle& circle = (*area)[i];
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

void MagicTrap::CastAtUnits(std::list<CUnit*>* units) {
    if (units->empty() || !spell) return;
    const Vector3& origin = localPosition_;
    Vector3 from = {origin.x, origin.y, origin.z};
    auto* world = ObjectWorld(this);
    from.z = static_cast<float>(world->RawSurfaceHeightAt(from.x, from.y) + from.z);
    if (!(spell->targetFlags_ & SpellTargetFlag::AtUnits) && std::ssize(points) != 0) {
        for (int i = 0; i < std::ssize(points); ++i) {
            const TrapPoint& point = points[i];
            Vector3 at = {point.x, point.y, 0.0f};
            g_serverWorld->CastSpell(spell, nullptr, &from, nullptr, &at, 0);
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
            g_serverWorld->CastSpell(spell, nullptr, &from, unit,
                                   &unit->localPosition_, 0);
            node = next;
        }
    }
    countdown = period;
}

void MagicTrap::SetUp(const MagicTrapRecord* record) {
    sides = record->sides;
    CSpellManager* manager = SpellManagerOf(WorldContext());
    CSpell* made = manager->CreateSpellFromText(record->spell);
    spell.Assign(made);
    std::memcpy(&localPosition_.x, &record->position, sizeof(record->position));   // bit for bit

    area = record->area;
    points = record->points;

    ClearVictims(this);

    period = record->period;
    countdown = 0;
    singleTarget = record->singleTarget != 0 ? 1 : 0;
}

// --- Construction, destruction, Save / Load ------------------------------------------

MagicTrap::MagicTrap() {
    CreateMapObject();
    sides = 0xFFFFFFFFu;
    flags_ |= kTrapFlags;
    SetSpell(this, nullptr);
    area.clear();
    points.clear();
    ClearVictims(this);
    countdown = 0;
    singleTarget = 0;
    period = static_cast<int>(kDefaultPeriod);
}

MagicTrap::~MagicTrap() {
    DestroyMapObject();
    SetSpell(this, nullptr);
    area.clear();
    points.clear();
    ClearVictims(this);        // the list emptied (each element released)
    // The list's destructor: its vtable, then emptied again.
    ClearVictims(this);
}

void MagicTrap::Load(CStream* stream) { LoadTrap(stream); }
void MagicTrap::Save(CStream* stream) { SaveTrap(stream); }

void MagicTrap::SaveTrap(CStream* stream) {
    SaveMapObject(stream);
    std::int32_t value = spell ? spell->id_ : -1;
    stream->WriteValue(value);
    value = std::ssize(area);
    stream->WriteValue(value);
    for (int i = 0; i < std::ssize(area); ++i) stream->WriteValue(area[i]);
    value = std::ssize(inside);
    stream->WriteValue(value);
    for (auto node = inside.begin(); node != inside.end();) {
        auto next = std::next(node);
        SaveRef(*node, stream);
        node = next;
    }
    value = std::ssize(points);
    stream->WriteValue(value);
    for (int i = 0; i < std::ssize(points); ++i) stream->WriteValue(points[i]);
    for (int i = 0; i < kTailFieldCount; ++i) {
        lib::LoadBytes(value, TailFields(this, i));
        stream->WriteValue(value);
    }
}

void MagicTrap::LoadTrap(CStream* stream) {
    LoadMapObject(stream);
    CSpellManager* manager = SpellManagerOf(WorldContext());
    // The id and the two counts are read into one local: a read that fails leaves the previous value.
    std::int32_t value = 0;
    stream->ReadValue(value);
    SetSpell(this, value == -1 ? nullptr : manager->FindObjectAs<CSpell>(value));
    stream->ReadValue(value);
    area.resize(value);
    for (int i = 0; i < std::ssize(area); ++i) stream->ReadValue(area[i]);
    CountedListLoad(&inside, stream, WorldContext());
    stream->ReadValue(value);
    points.resize(value);
    for (int i = 0; i < std::ssize(points); ++i) stream->ReadValue(points[i]);
    for (int i = 0; i < kTailFieldCount; ++i) stream->Read(TailFields(this, i), 4);
}

// --- Create, Destroy, the update, firing, forgetting a removed object -------------

void MagicTrap::Create() {
    CreateMapObject();
    sides = 0xFFFFFFFFu;
    flags_ |= kTrapFlags;
    SetSpell(this, nullptr);
    area = {};
    points.clear();
    ClearVictims(this);
    period = kDefaultPeriod;
    countdown = 0;
    singleTarget = 0;
}

void MagicTrap::DestroyTrap() {
    DestroyMapObject();
    SetSpell(this, nullptr);
    area = {};
    points.clear();
    ClearVictims(this);
}

bool MagicTrap::RunTick() {
    CMapObject::Update();
    auto* world = WorldContext();
    if (!world->IsServer() || !spell) return true;
    if (period <= 0) {
        UpdateVictims();
        return true;
    }
    if (countdown > 0) --countdown;
    if (countdown <= 0) FireAtOccupants();
    return true;
}

void MagicTrap::FireAtOccupants() {
    CWorldServer* context = ServerWorld();
    std::list<CUnit*> found;
    if (flags_ & MapObjectFlag::Armed) {
        FindTrapVictims(context, &area, sides, &found);
        if (!found.empty()) CastAtUnits(&found);
    }
}

int MagicTrap::ForgetObject(CEngineObject* object) {
    for (auto node = inside.begin(); node != inside.end();) {
        auto next = std::next(node);
        if (*node == object) {
            // The reference released, the node removed.
            if (*node) (*node)->Release();
            inside.erase(node);
        }
        node = next;
    }
    return 0;
}
