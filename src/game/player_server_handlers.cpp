// CPlayerServer: what the server does for its player beyond the per-tick sync, the leader's death
// (slot 20) and the client's messages (handlers by type, from the table CreateServer builds), with
// the small helpers of the use order. See player_messages.h and docs/notes/objects.md ("Players").

#include "player_messages.h"
#include "lib/crt_iostream.h"

#include "ai_map.h"
#include "core_maths.h"
#include "core_streams.h"
#include "map_object.h"
#include "originals_ai.h"
#include "originals_world.h"
#include "party.h"
#include "party_records.h"
#include "registry.h"
#include "unit.h"
#include "unit_ai.h"
#include "combat.h"
#include "unit_create.h"
#include "unit_db.h"
#include "world.h"

#include "lib/crt.h"
#include "lib/mfc.h"
#include "originals_library.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "game_data.h"

#include <cmath>
#include <cstring>

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
CMemoryStream& ReceivedMessages() { return ei::GameData<CMemoryStream>(0x007C83D0); }
// The shares a revived hero loses: of the party's money (007C5490) and of its experience above the
// NPC record's floor (007C5494). Set at startup.
float MoneyPenalty() { return ei::GameData<const volatile float>(0x007C5490); }
float ExperiencePenalty() { return ei::GameData<const volatile float>(0x007C5494); }
// The AI map's cell size factors (0074326C, 0073B86C): a half-cell coordinate times both is world units.
float CellScaleA() { return 1.0f /*0074326C*/; }
float CellScaleB() { return 0.5f /*0073B86C*/; }
// The use order's unit list: an MFC CArray<CUnit*> (vtable 007402AC; SetAtGrow 0060AD50, RemoveAt
// 00671200 are the template's members).
constexpr std::uintptr_t kUnitArrayVtable = std::uintptr_t(0x007402AC);
using UnitArray = mfc::CArray<CUnit*>;

CUnit* FindUnit(int id) { return static_cast<CUnit*>(WorldFindUnit(WorldServer(), nullptr, id)); }
CUnitServer* ControllerOf(CUnit* unit) { return reinterpret_cast<CUnitServer*>(unit->controller); }
const Vector3* PositionOf(const CUnit* unit) { return &unit->position; }

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
        CUnit* hero = FindUnit(static_cast<int>(party->members_.data[0].unitId));
        if (hero) SendObject0B(hero, kLeaderDiedValue);
        return;
    }

    // Both ids stay unset in the original (stack garbage) when the party is not a single hero;
    // here they are -1, which no object has.
    int oldId = kNoId;
    int newId = kNoId;
    if (party->members_.size == 1) {
        int lost = x87::RoundToInt(static_cast<float>(static_cast<float>(party->Money()) * MoneyPenalty()));
        party->Money() -= lost;
        oldId = static_cast<int>(party->members_.data[0].unitId);
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
        record.unitId = static_cast<std::uint32_t>(newId);
        auto* stats = reinterpret_cast<UnitStats*>(record.stats);
        stats->RestoreSlots();
        float experience = static_cast<float>(stats->pendingExperience + stats->experience);
        auto* npc = reinterpret_cast<UnitNpcRecord*>(unitdb::NpcTable().FindByName(record.texts[5]));
        if (npc) {
            float above = static_cast<float>(experience - npc->experienceFloor);
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
    mfc::CTypedPtrList<CEngineObject>& known = ControllerOf(revived)->Brain()->seenCorpses;
    for (auto* node = ControllerOf(dead)->Brain()->seenCorpses.head; node;) {
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
    return LengthSquaredUnrounded<Sum3::ZYX, double>(dx, dy, dz);
}

AIPath* AIPath::Construct() {
    steps = {};
    steps.vtable = PathCellListVtable();
    steps.blockSize = 0x80;
    cells = {};
    cells.vtable = PathCellListVtable();
    cells.blockSize = 0x80;
    excludedBlocks.vtable = ei::GamePointer<void>(kExcludedBlocksVtable);
    excludedBlocks.data = nullptr;
    excludedBlocks.size = 0;
    excludedBlocks.maxSize = 0;
    excludedBlocks.growBy = 0;
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
        out->x = point.x;
        out->y = point.y;
        out->z = point.z;
        return out;
    }
    static_cast<CMapObjectVirtuals*>(object)->AimPoint(out);
    return out;
}

int CPlayerServer::OnUseOrder() {
    CMemoryStream& in = ReceivedMessages();
    UnitArray units{ei::GamePointer<void>(kUnitArrayVtable), nullptr, 0, 0, 0};
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
            CUnitServer* controller = ControllerOf(nearest);
            if (controller->CanReachAndPlan(AsUnitAction(&order))) {
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
            if (controller->CanReachAndPlanFrom(AsUnitAction(&order), &from)) {
                chosen = nearest;
                accepted = true;
                break;
            }
            units.RemoveAt(nearestIndex, 1);
        }
        if (!accepted) chosen = first;
        if (order.object) order.object->Release();   // the counted reference's reset (0052B030)
        order.object = nullptr;
        path.Destruct();
    } else if (units.size == 1) {
        chosen = units[0];
    }

    if (chosen) {
        AIBrain* brain = ControllerOf(chosen)->Brain();
        if (AIMode* mode = brain->FindPlayerMode())
            static_cast<PlayerMode*>(mode)->CommandUse(brain, object, kind, value, flag != 0);
    }
    units.vtable = ei::GamePointer<void>(kUnitArrayVtable);
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

// --- A-H14: zone changes, joining, party blocks, the walk / attack orders -----------------------------

namespace {

// The campaign mode (007C2CC4; 1 and 3 end the game when the leader is gone) and the game mode
// (007B0098; 1 single player, 2 multiplayer).
int CampaignMode() { return ei::GameData<const volatile int>(0x007C2CC4); }
int GameMode() { return ei::GameData<const volatile int>(0x007B0098); }
// The party manager (007AA5A0) and its next object id for a new party's record (00794E3C).
void* PartyManager() { return ei::GamePointer<void>(0x007AA5A0); }
int& NextRecordObjectId() { return ei::GameData<int>(0x00794E3C); }

constexpr std::uint8_t kMessageZone = 0x08;
constexpr std::uint32_t kLoneHeroRecordId = 0x3B9AF5CE;   // 999999950
constexpr std::uint32_t kJoinedFlag = 0x10;               // CPlayer +0x130
constexpr std::uintptr_t kPointArrayVtable = std::uintptr_t(0x0073C120);
using PointArray = mfc::CArray<Vector3>;

// The unit list of the walk / attack orders: a count, then that many ids (missing units and units
// flagged 0x800000 are left out).
void ReadUnits(CMemoryStream& in, UnitArray* units) {
    int count = 0;
    in.Read(&count, 4);
    for (int i = 0; i < count; ++i) {
        int id = 0;
        in.Read(&id, 4);
        CUnit* unit = FindUnit(id);
        if (unit && !(unit->flags_ & kFlagBlocked)) units->SetAtGrow(units->size, unit);
    }
}
// The arrays' inlined destructors: the vtable back, the storage freed.
template <typename T>
void FreeArray(mfc::CArray<T>* array, std::uintptr_t vtable) {
    array->vtable = reinterpret_cast<void*>(vtable);
    if (array->data) gamecrt::operator_delete(array->data);
}

using MoveCommand = void (PlayerMode::*)(AIBrain*, const Vector3*, int, int, const Vector3*);
int MoveUnits(MoveCommand command) {
    CMemoryStream& in = ReceivedMessages();
    UnitArray units{ei::GamePointer<void>(kUnitArrayVtable), nullptr, 0, 0, 0};
    PointArray offsets{ei::GamePointer<void>(kPointArrayVtable), nullptr, 0, 0, 0};
    ReadUnits(in, &units);
    int run = 0;
    Vector3 to{};
    in.Read(&run, 4);
    in.Read(&to, sizeof(to));
    FormationOffsets(&units, &offsets);
    for (int i = 0; i < units.size; ++i) {
        AIBrain* brain = ControllerOf(units[i])->Brain();
        if (AIMode* mode = brain->FindPlayerMode())
            (static_cast<PlayerMode*>(mode)->*command)(brain, &to, units.size > 1, run != 0, &offsets.data[i]);
    }
    FreeArray(&offsets, kPointArrayVtable);
    FreeArray(&units, kUnitArrayVtable);
    return 1;
}

// A memory stream on the stack, as the original builds and destroys it inline.
struct LocalStream {
    alignas(4) unsigned char bytes[sizeof(CMemoryStream)];
    LocalStream() { Get()->Construct(); }
    ~LocalStream() { Get()->Destruct(); }
    CMemoryStream* Get() { return reinterpret_cast<CMemoryStream*>(bytes); }
};
// A party on the stack (constructor 00661050, destructor body 006611D0).
struct LocalParty {
    alignas(4) unsigned char bytes[sizeof(CParty)];
    LocalParty() { Get()->Construct(); }
    ~LocalParty() { Get()->DestructBody(); }
    CParty* Get() { return reinterpret_cast<CParty*>(bytes); }
};

} // namespace

void __fastcall FormationOffsets(const mfc::CArray<CUnit*>* units, mfc::CArray<Vector3>* offsets) {
    int count = units->size;
    if (count < 1) return;
    if (count == 1) {
        offsets->SetSize(1, -1);
        offsets->data[0] = Vector3{0.0f, 0.0f, 0.0f};
        return;
    }
    float sumX = 0.0f;
    float sumY = 0.0f;
    for (int i = 0; i < count; ++i) {
        const Vector3* at = PositionOf(units->data[i]);
        sumX = static_cast<float>(static_cast<double>(sumX) + at->x);
        sumY = static_cast<float>(static_cast<double>(sumY) + at->y);
    }
    float n = static_cast<float>(count);
    float centreX = static_cast<float>(static_cast<double>(sumX) / n);
    float centreY = static_cast<float>(static_cast<double>(sumY) / n);
    offsets->SetSize(count, -1);
    for (int i = 0; i < units->size; ++i) {
        const Vector3* at = PositionOf(units->data[i]);
        float dx = static_cast<float>(static_cast<double>(at->x) - centreX);
        float dy = static_cast<float>(static_cast<double>(at->y) - centreY);
        double length = x87::fsqrt(LengthSquared2DUnrounded<double>(dx, dy));
        if (length > 0.0) {   // the original skips zero and NaN (FCOM; TEST AH,0x40)
            double inverse = 1.0 / length;
            dx = static_cast<float>(dx * inverse);
            dy = static_cast<float>(dy * inverse);
        }
        offsets->data[i] = Vector3{dx, dy, 0.0f};
    }
}
EI_REPLACE(0x0066FD70, FormationOffsets);

void CPlayerServer::LeaveToZone(const char* zone, int entrance) {
    if (state_ != 1) return;
    CPlayer* player = Owner();
    CParty* party = player->Party();
    int campaign = CampaignMode();
    if (campaign == 1 || campaign == 3) {
        CUnit* leader = FindUnit(static_cast<int>(party->members_.data[0].unitId));
        if (leader == nullptr || ObjectSlots(leader)->IsDead() != 0) {
            reinterpret_cast<CPlayerConnection*>(this)->LeaderDied();
            if (!IsMultiplayerClient()) return;
        }
    }
    auto* worldPlayer = reinterpret_cast<WorldPlayer*>(player);
    if (flag70_) {
        WorldServer()->CollectParty(worldPlayer, nullptr);
        flag70_ = 0;
    }
    state_ = 3;
    party->entrance_ = static_cast<std::uint32_t>(entrance);
    party->zone_ = zone;
    int mode = GameMode();
    if (mode == 1) {
        PartyManagerBeginRedeploy(PartyManager(), nullptr, party);
    } else if (mode == 2) {
        if (party->members_.size == 1) party->members_.data[0].unitId = kLoneHeroRecordId;
        SendParty();
    }
    std::uint8_t type = kMessageZone;
    out_.Write(&type, 1);
    out_.Write(zone, static_cast<std::uint32_t>(std::strlen(zone) + 1));
    out_.Write(&entrance, 4);
    Flush();
}
EI_REPLACE_METHOD(0x0066F310, CPlayerServer::LeaveToZone);

// 0066D020 (slot 16; A-H15).
void CPlayerServer::Leave() {
    if (state_ == 1) {
        if (flag70_) {
            WorldServer()->CollectParty(reinterpret_cast<WorldPlayer*>(Owner()), nullptr);
            flag70_ = 0;
        }
        if (GameMode() == 2) SendParty();
    }
    if (state_ != 3) {
        state_ = 3;
        std::uint8_t type = 0x01;
        out_.Write(&type, 1);
    }
    Flush();
}
EI_REPLACE_METHOD(0x0066D020, CPlayerServer::Leave);

int CPlayerServer::OnRefreshParty() {
    Owner()->ForgetKnownObjects();
    SendParty();
    return 1;
}

int CPlayerServer::OnTakeExit() {
    mfc::CString zone;
    int exit = 0;
    int entrance = 0;
    ReceivedMessages().Read(&exit, 4);
    if (!WorldServer()->GetZoneExit(exit, &zone, &entrance)) return 0;
    LeaveToZone(zone, entrance);
    return 1;
}

int CPlayerServer::OnJoined() {
    if (state_ != 2) return 0;
    state_ = 1;
    CPlayer* player = Owner();
    player->sessionKind100_ = static_cast<CWorldServer*>(player->ManagerContext())->sessionKind_;
    WorldServer()->Redeploy(reinterpret_cast<WorldPlayer*>(player), &placements_, 1);
    player->dirty_ |= kJoinedFlag;
    return 1;
}

int CPlayerServer::OnLeaveToZone() {
    mfc::CString zone;
    {
        alignas(mfc::CString) unsigned char text[sizeof(mfc::CString)];
        mfc::CString* read = ReceivedMessages().ReadString(text);
        zone = *read;
        read->~CString();
    }
    int entrance = 0;
    ReceivedMessages().Read(&entrance, 4);
    LeaveToZone(zone, entrance);
    return 1;
}

int CPlayerServer::OnBriefingDone() {
    mfc::CString name;
    {
        alignas(mfc::CString) unsigned char text[sizeof(mfc::CString)];
        mfc::CString* read = ReceivedMessages().ReadString(text);
        name = *read;
        read->~CString();
    }
    WorldServer()->CompleteBriefing(Owner()->playerId_, name);
    return 1;
}

int CPlayerServer::OnNewParty() {
    LocalParty local;
    CParty* party = local.Get();
    party->LoadParty(&ReceivedMessages(), 1);
    if (party->members_.size != 1) return 0;
    int* key = reinterpret_cast<int*>(&Owner()->partyId_);
    PartyNextId(key);
    int& next = NextRecordObjectId();
    while (WorldServer()->FindObject(static_cast<unsigned>(next)) != nullptr) ++next;
    party->members_.data[0].unitId = static_cast<std::uint32_t>(next);
    ++next;
    PartyMapStore(PartyManager(), nullptr, key, party);
    return 1;
}

int CPlayerServer::OnResume() {
    if (state_ != 4) return 0;
    state_ = 1;
    if (!flag70_) {
        WorldServer()->Redeploy(reinterpret_cast<WorldPlayer*>(Owner()), nullptr, 1);
        flag70_ = 1;
    }
    return 1;
}

int CPlayerServer::OnWalk() { return MoveUnits(&PlayerMode::CommandWalk); }
int CPlayerServer::OnGoTo() { return MoveUnits(&PlayerMode::CommandGoTo); }

int CPlayerServer::OnAttack() {
    CMemoryStream& in = ReceivedMessages();
    UnitArray units{ei::GamePointer<void>(kUnitArrayVtable), nullptr, 0, 0, 0};
    ReadUnits(in, &units);
    int ctrl = 0, how = 0, objectId = 0;
    in.Read(&ctrl, 4);
    in.Read(&how, 4);
    in.Read(&objectId, 4);
    if (CEngineObject* object = WorldServer()->FindObject(static_cast<unsigned>(objectId))) {
        for (int i = 0; i < units.size; ++i) {
            AIBrain* brain = ControllerOf(units[i])->Brain();
            if (AIMode* mode = brain->FindPlayerMode())
                static_cast<PlayerMode*>(mode)->CommandAttack(brain, object, how, ctrl != 0);
        }
    }
    FreeArray(&units, kUnitArrayVtable);
    return 1;
}

int CPlayerServer::OnPartyUpdate() {
    LocalStream block;
    CMemoryStream& in = ReceivedMessages();
    in.ReadSized(block.Get());
    block.Get()->CMemoryStream::SeekImpl(0);
    if (in.ok_) Owner()->Party()->LoadParty(block.Get(), 0);
    return 1;
}

// --- A-H19: the login ------------------------------------------------------------------------------

namespace {
constexpr int kClientVersion = 0x64;
constexpr int kLoginTextLength = 0x3F;
constexpr std::uint8_t kMessageLogin = 0x00;
constexpr std::uint8_t kMessageError = 0x11;
} // namespace

BOOL __fastcall IsPrintableText(const char* text, int maxLength, int minLength) {
    for (int i = 0; i < maxLength; ++i) {
        char c = text[i];
        if (c == 0) return i >= minLength;
        if (libcrt::Isprint(c) == 0) return 0;
    }
    return 1;
}
EI_REPLACE(0x0047ADA0, IsPrintableText);

int CPlayerServer::OnLogin() {
    LoginMessage login;
    login.partyId = 0;
    ReceivedMessages().Read(&login, sizeof(login));
    if (state_ != 0) return 0;
    CPlayer* player = Owner();
    if (login.version != kClientVersion) {
        int length = 0x100;
        char format[0x100];
        char text[0x140];
        auto* world = static_cast<CWorldServer*>(player->ManagerContext());
        world->Messages()->GetStringBuffer("SrvErrMsg", "WrongClientVersion", format, &length, "");
        gamecrt::sprintf(text, format, 1, 0);
        std::uint8_t type = kMessageError;
        out_.Write(&type, 1);
        out_.Write(text, static_cast<std::uint32_t>(std::strlen(text) + 1));
        state_ = 3;
        return 0;
    }
    if (!IsPrintableText(login.name, kLoginTextLength, 1)) return 0;
    if (!IsPrintableText(login.password, kLoginTextLength, 1)) return 0;
    login.name[kLoginTextLength] = 0;
    login.password[kLoginTextLength] = 0;
    CParty* party = player->Party();
    switch (GameMode()) {
    case 1:
        PartyManagerEndRedeploy(PartyManager(), nullptr, party);
        party->OpenEdgeExits();
        break;
    case 2:
    case 3:
        // The two strings are passed but not read.
        if (!PartyMapLoad(PartyManager(), nullptr, static_cast<int>(reinterpret_cast<std::intptr_t>(login.name)),
                          static_cast<int>(reinterpret_cast<std::intptr_t>(login.password)),
                          reinterpret_cast<const int*>(&player->partyId_), party))
            return 0;
        break;
    case 4:
        party->Clear(1, 0);
        break;
    default:
        break;
    }
    std::uint8_t type = kMessageLogin;
    int id = player->id_;
    out_.Write(&type, 1);
    out_.Write(&id, 4);
    SendParty();
    state_ = 1;
    return 1;
}
EI_REPLACE_METHOD(0x0066F890, CPlayerServer::OnLogin);

EI_REPLACE_METHOD(0x0066FAA0, CPlayerServer::OnRefreshParty);
EI_REPLACE_METHOD(0x00670C20, CPlayerServer::OnTakeExit);
EI_REPLACE_METHOD(0x00670DA0, CPlayerServer::OnJoined);
EI_REPLACE_METHOD(0x00670E00, CPlayerServer::OnLeaveToZone);
EI_REPLACE_METHOD(0x00670EB0, CPlayerServer::OnBriefingDone);
EI_REPLACE_METHOD(0x00670F50, CPlayerServer::OnNewParty);
EI_REPLACE_METHOD(0x00671060, CPlayerServer::OnResume);
EI_REPLACE_METHOD(0x0066FFF0, CPlayerServer::OnWalk);
EI_REPLACE_METHOD(0x0066FBD0, CPlayerServer::OnGoTo);
EI_REPLACE_METHOD(0x00670190, CPlayerServer::OnAttack);
EI_REPLACE_METHOD(0x00670B60, CPlayerServer::OnPartyUpdate);

// --- A-H14: party resends, message 0x19 ------------------------------------------------------------

namespace {
constexpr std::uint8_t kMessagePartyUpdate = 0x83;
constexpr std::uint8_t kMessage19 = 0x19;
const char* const kResendingParty = "Resending party of player ";   // 00794F34
libcrt::Ostream* GameCout() { return ei::GamePointer<libcrt::Ostream>(0x007C56B0); }
} // namespace

void CPlayerServer::SendPartyUpdate() {
    LocalStream block;
    Owner()->Party()->SaveParty(block.Get(), 0);
    std::uint8_t type = kMessagePartyUpdate;
    out_.Write(&type, 1);
    out_.WriteSized(block.Get());
}
EI_REPLACE_METHOD(0x0066F0C0, CPlayerServer::SendPartyUpdate);

void CPlayerServer::ResendParty() {
    if (state_ != 1 || !flag70_) return;
    CPlayer* player = Owner();
    GameCout()->PutText(kResendingParty)->PutInt(player->playerId_)->PutChar('\n')->Apply(libcrt::FlushManipulator);
    auto* worldPlayer = reinterpret_cast<WorldPlayer*>(player);
    WorldServer()->SyncPartyRecords(worldPlayer);
    SendParty();
    WorldServer()->ClearPlayerItems(worldPlayer);
}
EI_REPLACE_METHOD(0x0066F450, CPlayerServer::ResendParty);

void CPlayerServer::SendPair19(std::uint8_t a, std::uint32_t b) {
    std::uint8_t type = kMessage19;
    out_.Write(&type, 1);
    out_.Write(&a, 1);
    out_.Write(&b, 4);
}
EI_REPLACE_METHOD(0x0066F840, CPlayerServer::SendPair19);
