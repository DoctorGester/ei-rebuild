// CPlayerServer: what the server does for its player beyond the per-tick sync, the leader's death
// (slot 20) and the client's messages (handlers by type, from the table CreateServer builds), with
// the small helpers of the use order. See player_messages.h and docs/notes/objects.md ("Players").

#include "player_messages.h"

#include "ai_map.h"
#include "core_maths.h"
#include "core_streams.h"
#include "map_object.h"
#include "originals_ai.h"
#include "originals_world.h"
#include "party.h"
#include "unit.h"
#include "unit_ai.h"
#include "unit_create.h"
#include "unit_db.h"
#include "world.h"

#include "lib/mfc.h"

#include "ei/hook.h"

#include <cmath>

namespace {

constexpr std::uint8_t kMessageGameOver = 0x09;
constexpr std::uint32_t kLeaderDiedValue = 0xFF;      // message 0x0B's value for the dead leader
constexpr int kRevivedIdBase = 1001000000;            // OnLeaderDied: ids below it are moved up
constexpr int kRevivedIdStep = 1000000;
constexpr int kNoId = -1;                             // see OnLeaderDied: the original's uninitialised ids
constexpr float kUnknownZ = -1313.0f;                 // lastPosition_.z after a revival
constexpr std::uint32_t kFlagBlocked = 0x800000;      // a unit the use order leaves out
constexpr int kOrderUse = 6;                          // AIOrder::kind
constexpr float kPathRange = 1000000.0f;              // FindPath's range for the use order
constexpr float kFarAway = 1e30f;                     // the nearest-unit search's start

// The stream the received packets are decompressed into (007C83D0; ReceiveMessages fills it).
CMemoryStream& ReceivedMessages() { return *reinterpret_cast<CMemoryStream*>(std::uintptr_t(0x007C83D0)); }
// The shares a revived hero loses: of the party's money (007C5490) and of its experience above the
// NPC record's floor (007C5494). Set at startup.
float MoneyPenalty() { return *reinterpret_cast<const volatile float*>(std::uintptr_t(0x007C5490)); }
float ExperiencePenalty() { return *reinterpret_cast<const volatile float*>(std::uintptr_t(0x007C5494)); }
// The AI map's cell size factors (0074326C, 0073B86C): a half-cell coordinate times both is world units.
float CellScaleA() { return *reinterpret_cast<const volatile float*>(std::uintptr_t(0x0074326C)); }
float CellScaleB() { return *reinterpret_cast<const volatile float*>(std::uintptr_t(0x0073B86C)); }
// The use order's unit list: an MFC CArray<CUnit*> (vtable 007402AC; SetAtGrow 0060AD50, RemoveAt
// 00671200 are the template's members).
constexpr std::uintptr_t kUnitArrayVtable = std::uintptr_t(0x007402AC);
using UnitArray = mfc::CArray<CUnit*>;

CUnit* FindUnit(int id) { return static_cast<CUnit*>(WorldFindUnit(WorldServer(), nullptr, id)); }
UnitController* ControllerOf(CUnit* unit) { return reinterpret_cast<UnitController*>(unit->controller); }
const Vector3* PositionOf(const CUnit* unit) { return reinterpret_cast<const Vector3*>(&unit->positionX); }

// Appends `object` to `list` with a counted reference (the inlined AddTail of the brain's lists:
// 0052CDB0's new node holds a null reference, then the assignment counts the object).
void AddCounted(mfc::CTypedPtrList<CEngineObject>& list, CEngineObject* object) {
    list.AddTail(object);
    ++object->refCount_;
}

} // namespace

// --- 0066F4C0: the leader died ----------------------------------------------------------------------

void CPlayerServer::OnLeaderDied() {
    CPlayer* player = Owner();
    CParty* party = player->Party();
    if (!IsMultiplayerClient()) {
        std::uint8_t type = kMessageGameOver;
        out_.Write(&type, 1);
        CUnit* hero = FindUnit(static_cast<int>(party->members_.data[0].unknown_01C));
        if (hero) SendObject0B(hero, kLeaderDiedValue);
        return;
    }

    // Both ids stay unset in the original (stack garbage) when the party is not a single hero;
    // here they are -1, which no object has.
    int oldId = kNoId;
    int newId = kNoId;
    if (party->members_.size == 1) {
        int lost = static_cast<int>(std::lrint(StoreFloat(static_cast<float>(party->Money()) * MoneyPenalty())));
        party->Money() -= lost;
        oldId = static_cast<int>(party->members_.data[0].unknown_01C);
        CUnit* hero = FindUnit(oldId);
        if (hero) SendObject0B(hero, kLeaderDiedValue);
        newId = oldId;
        if (newId < kRevivedIdBase) newId += kRevivedIdStep;
        while (WorldServer()->FindObject(static_cast<unsigned>(newId))) ++newId;
        WorldServer()->AddSharedObject(oldId, oldId, static_cast<std::uint32_t>(lost), &player->items_);
        player->TakeAllItems();
    }
    auto* worldPlayer = reinterpret_cast<WorldPlayer*>(player);
    if (flag70_) {
        WorldServer()->CollectParty(worldPlayer, nullptr);
        flag70_ = 0;
    }
    if (party->members_.size == 1) {
        PartyUnitRecord& record = party->members_.data[0];
        record.unknown_01C = static_cast<std::uint32_t>(newId);
        auto* stats = reinterpret_cast<UnitStats*>(record.stats);
        stats->RestoreSlots();
        float experience = StoreFloat(stats->pendingExperience + stats->experience);
        auto* npc = reinterpret_cast<UnitNpcRecord*>(unitdb::NpcTable().FindByName(record.texts[5]));
        if (npc) {
            float above = StoreFloat(experience - npc->experienceFloor);
            experience = !(above <= 0.0f) ? above : 0.0f;   // FCOMP 0 with it: a NaN is kept
        }
        stats->TakePendingExperience(ExperiencePenalty() * experience);
    }
    SendParty();
    lastPosition_.z = kUnknownZ;
    if (!flag70_) {
        WorldServer()->Redeploy(worldPlayer, nullptr, 1);
        flag70_ = 1;
    }
    CUnit* revived = FindUnit(newId);
    CUnit* dead = FindUnit(oldId);
    if (!revived || !dead) return;
    mfc::CTypedPtrList<CEngineObject>& known = ControllerOf(revived)->Brain()->unknown_12C;
    for (auto* node = ControllerOf(dead)->Brain()->unknown_12C.head; node;) {
        CEngineObject* object = node->data;
        node = node->next;
        if (object && object->GetClassId() == kClassUnit && !known.Find(object)) AddCounted(known, object);
    }
    if (!known.Find(dead)) AddCounted(known, dead);
}

// --- 00670310: message 0x36, a use order --------------------------------------------------------

double __fastcall UseOrderDistanceSquared(const Vector3* a, const Vector3* b) {
    double dx = a->x - b->x;
    double dy = a->y - b->y;
    double dz = a->z - b->z;
    return dz * dz + dy * dy + dx * dx;
}

AIPath* AIPath::Construct() {
    steps = {};
    steps.vtable = PathCellListVtable();
    steps.blockSize = 0x80;
    cells = {};
    cells.vtable = PathCellListVtable();
    cells.blockSize = 0x80;
    pointsVtable = reinterpret_cast<void*>(kPointsVtable);
    points = nullptr;
    pointCount = 0;
    pointMax = 0;
    pointGrowBy = 0;
    Clear();
    return this;
}

Vector3* AIPath::TargetPoint(Vector3* out) const {
    double scale = static_cast<double>(CellScaleA()) * CellScaleB();
    out->z = 0.0f;
    out->x = static_cast<float>(scale * targetX);
    out->y = static_cast<float>(scale * targetY);
    return out;
}

void AIOrder::MakeUse(CEngineObject* target, int use) {
    Clear();
    kind = kOrderUse;
    if (target != object) {
        if (object) object->Release();
        object = nullptr;
        if (target) ++target->refCount_;
        object = target;
    }
    what = use;
}

Vector3* AIOrder::AimPoint(Vector3* out) const {
    if (object == nullptr) {
        out->x = point[0];
        out->y = point[1];
        out->z = point[2];
        return out;
    }
    static_cast<CMapObjectVirtuals*>(object)->AimPoint(out);
    return out;
}

int CPlayerServer::OnUseOrder() {
    CMemoryStream& in = ReceivedMessages();
    UnitArray units{reinterpret_cast<void*>(kUnitArrayVtable), nullptr, 0, 0, 0};
    int count = 0;
    in.Read(&count, 4);
    for (int i = 0; i < count; ++i) {
        int id = 0;
        in.Read(&id, 4);
        CUnit* unit = FindUnit(id);
        if (unit && !(unit->flags_ & kFlagBlocked)) units.SetAtGrow(units.size, unit);
    }
    int flag = 0, kind = 0, value = 0, objectId = 0;
    in.Read(&flag, 4);
    in.Read(&kind, 4);
    in.Read(&value, 4);
    in.Read(&objectId, 4);
    CEngineObject* object = WorldServer()->FindObject(static_cast<unsigned>(objectId));

    CUnit* chosen = nullptr;
    if (units.size >= 2) {
        AIPath path;
        path.Construct();
        AIOrder order;
        order.object = nullptr;
        order.Clear();
        order.MakeUse(object, kind);
        Vector3 aim;
        order.AimPoint(&aim);
        CUnit* first = nullptr;
        bool accepted = false;
        while (units.size != 0) {
            float best = kFarAway;
            CUnit* nearest = nullptr;
            int nearestIndex = 0;
            for (int i = 0; i < units.size; ++i) {
                double distance = UseOrderDistanceSquared(&aim, PositionOf(units[i]));
                if (!(distance >= best)) {
                    best = static_cast<float>(distance);
                    nearest = units[i];
                    nearestIndex = i;
                }
            }
            if (!nearest) break;
            if (!first) first = nearest;
            UnitController* controller = ControllerOf(nearest);
            if (controller->CheckOrder(&order)) {
                chosen = nearest;
                accepted = true;
                break;
            }
            Vector3 from;
            if (WorldServer()->AIMap()->FindPath(nearest, &aim, &path, kPathRange, 0.0f)) {
                Vector3 end;
                from = *path.TargetPoint(&end);
            } else {
                from = *PositionOf(nearest);
            }
            if (controller->CheckOrderAt(&order, &from)) {
                chosen = nearest;
                accepted = true;
                break;
            }
            units.RemoveAt(nearestIndex, 1);
        }
        if (!accepted) chosen = first;
        if (order.object) order.object->Release();   // the counted reference's reset (0052B030)
        order.object = nullptr;
        AIPathDestructOriginal(&path);
    } else if (units.size == 1) {
        chosen = units[0];
    }

    if (chosen) {
        AIBrain* brain = ControllerOf(chosen)->Brain();
        if (AIMode* mode = brain->FindPlayerMode())
            static_cast<PlayerMode*>(mode)->CommandUse(brain, object, kind, value, flag != 0);
    }
    units.vtable = reinterpret_cast<void*>(kUnitArrayVtable);
    if (units.data) gamecrt::operator_delete(units.data);
    return 1;
}

EI_REPLACE_METHOD(0x0066F4C0, CPlayerServer::OnLeaderDied);
EI_REPLACE(0x00670670, UseOrderDistanceSquared);
EI_REPLACE_METHOD(0x006706A0, AIPath::Construct);
EI_REPLACE_METHOD(0x00670730, AIPath::TargetPoint);
EI_REPLACE_METHOD(0x00670760, AIOrder::MakeUse);
EI_REPLACE_METHOD(0x006707D0, AIOrder::AimPoint);
EI_REPLACE_METHOD(0x00670310, CPlayerServer::OnUseOrder);
