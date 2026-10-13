// Differential checks for the world server, world client and world glue code (src/game/world*.cpp).
#include "verify.h"

#include "../../src/game/effects.h"
#include "../../src/game/map_object_figures.h"
#include "../../src/game/object_tables.h"
#include "../../src/game/script.h"
#include "../../src/game/world.h"
#include "../../src/game/world_glue.h"
#include "../../src/lib/crt.h"
#include "ei/x87.h"

#include <cstring>
#include <new>
#include <vector>

namespace {
void* s_unused;

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(object) + offset);
}

bool SameFloat(float a, float b) { return std::memcmp(&a, &b, sizeof(float)) == 0; }

// ---- Snapshots of the state the server setters touch ---------------------------------------------

std::vector<WorldPlayer*> Players(CWorldServer* server) {
    std::vector<WorldPlayer*> players;
    PlayerIterator it;
    for (WorldPlayer* p = it.First(server->players_); p; p = it.Next()) players.push_back(p);
    return players;
}

struct ServerState {
    std::uint8_t b6C;
    int dc, e0, e4;
    float e8;
    std::vector<std::uint32_t> dirty;

    static ServerState Take(CWorldServer* server, const std::vector<WorldPlayer*>& players) {
        ServerState s{server->paused_, server->unknownDC_, server->unknownE0_, server->unknownE4_, server->unknownE8_, {}};
        for (WorldPlayer* p : players) s.dirty.push_back(p->dirty);
        return s;
    }
    void Restore(CWorldServer* server, const std::vector<WorldPlayer*>& players) const {
        server->paused_ = b6C;
        server->unknownDC_ = dc;
        server->unknownE0_ = e0;
        server->unknownE4_ = e4;
        server->unknownE8_ = e8;
        for (std::size_t i = 0; i < players.size(); ++i) players[i]->dirty = dirty[i];
    }
    bool operator==(const ServerState& o) const {
        return b6C == o.b6C && dc == o.dc && e0 == o.e0 && e4 == o.e4 && SameFloat(e8, o.e8) && dirty == o.dirty;
    }
};

// Runs ours, records the state, restores the state from before, runs the original, compares.
template <typename Ours, typename Original>
void CompareSetter(const char* name, CWorldServer* server, Ours ours, Original original) {
    std::vector<WorldPlayer*> players = Players(server);
    ServerState before = ServerState::Take(server, players);
    ours();
    ServerState mine = ServerState::Take(server, players);
    before.Restore(server, players);
    original();
    ServerState theirs = ServerState::Take(server, players);
    ei::verify::Check(name, true, mine == theirs, "players %u dc %d e4 %d", static_cast<unsigned>(players.size()),
                      theirs.dc, theirs.e4);
}

// A copy of a script group holding its own references (see verify_script.cpp).
ScriptGroup CloneGroup(const ScriptGroup& g) {
    ScriptGroup c{};
    c.vtable = g.vtable;
    c.growBy = g.growBy;
    if (g.data) {
        c.data = static_cast<ScriptObjectRef*>(gamecrt::operator_new(g.maxSize * 4));
        for (int i = 0; i < g.maxSize; ++i) c.data[i].object = i < g.size ? g.data[i].object : nullptr;
        for (int i = 0; i < g.size; ++i)
            if (c.data[i].object) ++c.data[i].object->refCount_;
    }
    c.size = g.size;
    c.maxSize = g.maxSize;
    return c;
}

void DestroyGroup(ScriptGroup& g) {
    if (g.data) {
        for (int i = 0; i < g.size; ++i)
            if (g.data[i].object) g.data[i].object->Release();
        gamecrt::operator_delete(g.data);
    }
    g.data = nullptr;
    g.size = g.maxSize = 0;
}

bool SameGroup(const ScriptGroup& a, const ScriptGroup& b) {
    if (a.size != b.size || a.maxSize != b.maxSize || a.growBy != b.growBy) return false;
    for (int i = 0; i < a.size; ++i)
        if (a.data[i].object != b.data[i].object) return false;
    return true;
}
} // namespace

// ==== CWorldServer =================================================================================

// 00593AA0: PUSH ESI; MOV ESI,[ECX+0x12D74] (7 bytes). Pure.
static CEngineObject*(__fastcall* s_findObject)(CWorldServer*, void*, unsigned);
static CEngineObject* __fastcall FindObjectVerify(CWorldServer* self, void*, unsigned id) {
    CEngineObject* expected = s_findObject(self, nullptr, id);
    ei::verify::Check("CWorldServer::FindObject", expected, self->FindObject(id), "id %u", id);
    return expected;
}
EI_VERIFY(0x00593AA0, FindObjectVerify, 7, s_findObject);

// 00594180: PUSH -1; PUSH 0x726228 (7 bytes). Pure (the list's count goes up and back down).
static WorldPlayer*(__fastcall* s_getPlayer)(CWorldServer*, void*, int);
static WorldPlayer* __fastcall GetPlayerVerify(CWorldServer* self, void*, int id) {
    WorldPlayer* expected = s_getPlayer(self, nullptr, id);
    ei::verify::Check("CWorldServer::GetPlayer", expected, self->GetPlayer(id), "id %d", id);
    return expected;
}
EI_VERIFY(0x00594180, GetPlayerVerify, 7, s_getPlayer);

// 00594220: PUSH EBP; MOV EBP,ESP; PUSH ECX; FLD [0x73F904] (10 bytes). Pure.
static float(__fastcall* s_gridHeight)(CWorldServer*, void*, float, float);
static float __fastcall GridHeightVerify(CWorldServer* self, void*, float x, float y) {
    float expected = s_gridHeight(self, nullptr, x, y);
    ei::verify::Check("CWorldServer::GridHeight", expected, self->GridHeight(x, y), "x=%g y=%g", x, y);
    return expected;
}
EI_VERIFY(0x00594220, GridHeightVerify, 10, s_gridHeight);

// 00591740: MOV EAX,[ESP+4]; PUSH EBP (5 bytes). Pure.
static int(__fastcall* s_countMercs)(CWorldServer*, void*, int);
static int __fastcall CountMercsVerify(CWorldServer* self, void*, int id) {
    int expected = s_countMercs(self, nullptr, id);
    ei::verify::Check("CWorldServer::CountLivingMercs", expected, self->CountLivingMercs(id), "player %d", id);
    return expected;
}
EI_VERIFY(0x00591740, CountMercsVerify, 5, s_countMercs);

// 005949D0: PUSH ESI; MOV ESI,ECX; MOV ECX,[0x79B5DC] (9 bytes). Pure; only AL is set.
static bool(__fastcall* s_isShared)(CWorldServer*, void*, int, int);
static bool __fastcall IsSharedVerify(CWorldServer* self, void*, int objectId, int playerId) {
    bool expected = s_isShared(self, nullptr, objectId, playerId);
    ei::verify::Check("CWorldServer::IsObjectShared", expected, self->IsObjectShared(objectId, playerId), "obj %d player %d",
                      objectId, playerId);
    return expected;
}
EI_VERIFY(0x005949D0, IsSharedVerify, 9, s_isShared);

// 00594A30: PUSH EBX; PUSH EBP; MOV EBP,[ESP+0x10] (6 bytes). Writes only the group: the original
// fills a clone, ours the real group.
static void(__fastcall* s_collectShared)(CWorldServer*, void*, int, ScriptGroup*);
static void __fastcall CollectSharedVerify(CWorldServer* self, void*, int playerId, ScriptGroup* group) {
    ScriptGroup scratch = CloneGroup(*group);
    s_collectShared(self, nullptr, playerId, &scratch);
    self->CollectSharedObjects(playerId, group);
    ei::verify::Check("CWorldServer::CollectSharedObjects", true, SameGroup(scratch, *group), "player %d size %d vs %d", playerId,
                      scratch.size, group->size);
    DestroyGroup(scratch);
}
EI_VERIFY(0x00594A30, CollectSharedVerify, 6, s_collectShared);

// The setters: ours, snapshot, restore, original, compare (server fields and players' dirty flags).
// 0058B6D0: PUSH -1; PUSH 0x725A18 (7 bytes).
static void(__fastcall* s_markDirty)(CWorldServer*, void*, std::uint32_t);
static void __fastcall MarkDirtyVerify(CWorldServer* self, void*, std::uint32_t mask) {
    CompareSetter("CWorldServer::MarkPlayersDirty", self, [&] { self->MarkPlayersDirty(mask); },
                  [&] { s_markDirty(self, nullptr, mask); });
}
EI_VERIFY(0x0058B6D0, MarkDirtyVerify, 7, s_markDirty);

// 0058CC70: MOV EAX,[ESP+4]; MOV [ECX+0xE0],EAX (10 bytes).
static void(__fastcall* s_setE0)(CWorldServer*, void*, int);
static void __fastcall SetE0Verify(CWorldServer* self, void*, int value) {
    CompareSetter("CWorldServer::SetUnknownE0", self, [&] { self->SetUnknownE0(value); }, [&] { s_setE0(self, nullptr, value); });
}
EI_VERIFY(0x0058CC70, SetE0Verify, 10, s_setE0);

// 0058CC80: PUSH ESI; MOV ESI,ECX; MOV EAX,[ESI+0xE4] (9 bytes).
static void(__fastcall* s_setDC)(CWorldServer*, void*, int);
static void __fastcall SetDCVerify(CWorldServer* self, void*, int value) {
    CompareSetter("CWorldServer::SetUnknownDC", self, [&] { self->SetUnknownDC(value); }, [&] { s_setDC(self, nullptr, value); });
}
EI_VERIFY(0x0058CC80, SetDCVerify, 9, s_setDC);

// 0058EE00: MOV EAX,[ESP+4]; MOV EDX,[ESP+8] (8 bytes).
static void(__fastcall* s_setE4E8)(CWorldServer*, void*, int, float);
static void __fastcall SetE4E8Verify(CWorldServer* self, void*, int e4, float e8) {
    CompareSetter("CWorldServer::SetUnknownE4E8", self, [&] { self->SetUnknownE4E8(e4, e8); },
                  [&] { s_setE4E8(self, nullptr, e4, e8); });
}
EI_VERIFY(0x0058EE00, SetE4E8Verify, 8, s_setE4E8);

// 005942C0: MOV AL,[ESP+4]; MOV [ECX+0x6C],AL (7 bytes).
static void(__fastcall* s_set6C)(CWorldServer*, void*, std::uint8_t);
static void __fastcall Set6CVerify(CWorldServer* self, void*, std::uint8_t value) {
    CompareSetter("CWorld::SetPaused", self, [&] { self->SetPaused(value); }, [&] { s_set6C(self, nullptr, value); });
}
EI_VERIFY(0x005942C0, Set6CVerify, 7, s_set6C);

// 00566350 (A-W12): the client's byte-identical copy; ours, then the original on the same world
// with the byte put back (only +0x6C changes).
static void(__fastcall* s_setPausedClient)(CWorld*, void*, std::uint8_t);
static void __fastcall SetPausedClientVerify(CWorld* self, void*, std::uint8_t value) {
    std::uint8_t before = self->paused_;
    self->SetPaused(value);
    std::uint8_t mine = self->paused_;
    self->paused_ = before;
    s_setPausedClient(self, nullptr, value);
    ei::verify::Check("CWorld::SetPaused@00566350", self->paused_, mine, "value %u", value);
}
EI_VERIFY(0x00566350, SetPausedClientVerify, 7, s_setPausedClient);

// 0058E4C0: MOV EAX,[ESP+4]; PUSH EDI (5 bytes). Output: the original assigns a scratch copy of the
// string, ours the real one; CString assignment shares the buffer, so the pointers must match.
static BOOL(__fastcall* s_getZoneExit)(CWorldServer*, void*, int, mfc::CString*, int*);
static BOOL __fastcall GetZoneExitVerify(CWorldServer* self, void*, int index, mfc::CString* target, int* entrance) {
    mfc::CString scratch(*target);
    int scratchEntrance = *entrance;
    BOOL expected = s_getZoneExit(self, nullptr, index, &scratch, &scratchEntrance);
    BOOL actual = self->GetZoneExit(index, target, entrance);
    bool same = expected == actual && static_cast<const char*>(scratch) == static_cast<const char*>(*target) &&
                scratchEntrance == *entrance;
    ei::verify::Check("CWorldServer::GetZoneExit", true, same, "index %d -> %d/%d", index, expected, actual);
    return expected;
}
EI_VERIFY(0x0058E4C0, GetZoneExitVerify, 5, s_getZoneExit);

// The id-map functions allocate nodes and change reference counts: run ours only, then check the
// map and the object (as GetFileVerify does).
static void __fastcall AddTimedVerify(CWorldServer* self, void*, CEngineObject* object) {
    unsigned id = static_cast<unsigned>(object->id_);
    bool present = self->timedObjects_.map.Find(id) != nullptr;
    std::size_t count = self->timedObjects_.map.Size();
    int maxId = self->timedObjects_.maxId;
    int refs = object->refCount_;
    self->AddTimedObject(object);
    auto* node = self->timedObjects_.map.Find(id);
    int expectedMax = static_cast<int>(id) > maxId ? static_cast<int>(id) : maxId;
    bool ok = node && node->value == object && self->timedObjects_.map.Size() == count + (present ? 0 : 1) &&
              self->timedObjects_.maxId == expectedMax && object->refCount_ == refs + 1 && (object->flags_ & 2) &&
              At<int>(object, 0x94) == self->tick_ && At<int>(object, 0x148) == *reinterpret_cast<int*>(0x007AFFC8);
    ei::verify::Check("CWorldServer::AddTimedObject", true, ok, "id %u", id);
}
EI_VERIFY(0x0058E520, AddTimedVerify, 0, s_unused);

static void __fastcall RemoveTimedVerify(CWorldServer* self, void*, unsigned id) {
    bool present = self->timedObjects_.map.Find(id) != nullptr;
    std::size_t count = self->timedObjects_.map.Size();
    self->RemoveTimedObject(id);
    bool ok = !self->timedObjects_.map.Find(id) && self->timedObjects_.map.Size() == count - (present ? 1 : 0);
    ei::verify::Check("CWorldServer::RemoveTimedObject", true, ok, "id %u present %d", id, present);
}
EI_VERIFY(0x0058E5D0, RemoveTimedVerify, 0, s_unused);

static void __fastcall AddToUpdateMapVerify(CWorldServer* self, void*, CEngineObject* unit) {
    unsigned id = static_cast<unsigned>(unit->id_);
    bool present = self->updateMap_.map.Find(id) != nullptr;
    std::size_t count = self->updateMap_.map.Size();
    int maxId = self->updateMap_.maxId;
    self->AddToUpdateMap(unit);
    auto* node = self->updateMap_.map.Find(id);
    int expectedMax = static_cast<int>(id) > maxId ? static_cast<int>(id) : maxId;
    int slot = ((reinterpret_cast<int>(unit) >> 5) & 0xF) + 0x80;
    bool ok = node && node->value == unit && self->updateMap_.map.Size() == count + (present ? 0 : 1) &&
              self->updateMap_.maxId == expectedMax && At<int>(unit, 0x174) == slot;
    ei::verify::Check("CWorldServer::AddToUpdateMap", true, ok, "id %u", id);
}
EI_VERIFY(0x0058D5D0, AddToUpdateMapVerify, 0, s_unused);

static void __fastcall IdMapSetVerify(ObjectIdMap* self, void*, const unsigned* key, CEngineObject* const* value) {
    unsigned id = *key;
    CEngineObject* object = *value;
    bool present = self->map.Find(id) != nullptr;
    std::size_t count = self->map.Size();
    int maxId = self->maxId;
    self->Set(id, object);
    auto* node = self->map.Find(id);
    int expectedMax = static_cast<int>(id) > maxId ? static_cast<int>(id) : maxId;
    bool ok = node && node->value == object && self->map.Size() == count + (present ? 0 : 1) && self->maxId == expectedMax;
    ei::verify::Check("ObjectIdMap::Set", true, ok, "id %u", id);
}
EI_VERIFY(0x005999E0, IdMapSetVerify, 0, s_unused);
EI_VERIFY(0x00599BD0, IdMapSetVerify, 0, s_unused);

// ==== CWorldClient =================================================================================

// 00563F70: PUSH EBP; MOV EBP,ESP; SUB ESP,0xC (6 bytes). Pure.
static float(__fastcall* s_clampedHeight)(CWorldClient*, void*, float, float);
static float __fastcall ClampedHeightVerify(CWorldClient* self, void*, float x, float y) {
    float expected = s_clampedHeight(self, nullptr, x, y);
    ei::verify::Check("CWorldClient::ClampedGroundHeight", expected, self->ClampedGroundHeight(x, y), "x=%g y=%g", x, y);
    return expected;
}
EI_VERIFY(0x00563F70, ClampedHeightVerify, 6, s_clampedHeight);

// 00560F00: PUSH EBP; MOV EBP,ESP; SUB ESP,0x50 (6 bytes). Pure.
static float(__fastcall* s_platformHeight)(PlatformSet*, void*, const Vector3*, float);
static float __fastcall PlatformHeightVerify(PlatformSet* self, void*, const Vector3* point, float height) {
    float expected = s_platformHeight(self, nullptr, point, height);
    ei::verify::Check("PlatformSet::SurfaceHeight", expected, self->SurfaceHeight(point, height), "(%g %g %g) h=%g", point->x,
                      point->y, point->z, height);
    return expected;
}
EI_VERIFY(0x00560F00, PlatformHeightVerify, 6, s_platformHeight);

// 00561050: MOV EDX,[ECX+0x20]; SUB ESP,8 (6 bytes). Pure; only AL is set.
static bool(__fastcall* s_onPlatform)(PlatformSet*, void*, float, float);
static bool __fastcall OnPlatformVerify(PlatformSet* self, void*, float x, float y) {
    bool expected = s_onPlatform(self, nullptr, x, y);
    ei::verify::Check("PlatformSet::Contains", expected, self->Contains(x, y), "x=%g y=%g", x, y);
    return expected;
}
EI_VERIFY(0x00561050, OnPlatformVerify, 6, s_onPlatform);

// 00561150: SUB ESP,0xC; PUSH EBX; MOV EBX,[ESP+0x1C] (8 bytes). Writes only its outputs: the
// original into scratch, ours into the caller's; height and normal compared within a few ULPs (a
// surface whose z ties with the best so far may rarely pick the other normal, rule 7). Reached by
// the ground dust particles (0x203A / 0x203B, CParticleSource::TickGroundPlane calls through the
// address): footsteps and landings on zone1.
static void(__fastcall* s_surfacePlane)(PlatformSet*, void*, float, float, Vector3*, float*);
static void __fastcall SurfacePlaneVerify(PlatformSet* self, void*, float x, float y, Vector3* normal, float* height) {
    Vector3 expectedNormal;
    float expectedHeight;
    s_surfacePlane(self, nullptr, x, y, &expectedNormal, &expectedHeight);
    self->SurfacePlane(x, y, normal, height);
    const bool same = ei::verify::NearlyEqual(expectedHeight, *height, 4, 1e-6f) &&
                      ei::verify::NearlyEqual(expectedNormal.x, normal->x, 4, 1e-6f) &&
                      ei::verify::NearlyEqual(expectedNormal.y, normal->y, 4, 1e-6f) &&
                      ei::verify::NearlyEqual(expectedNormal.z, normal->z, 4, 1e-6f);
    ei::verify::Check("PlatformSet::SurfacePlane", true, same, "(%g %g): %g (%g %g %g) vs %g (%g %g %g)", x, y,
                      expectedHeight, expectedNormal.x, expectedNormal.y, expectedNormal.z, *height, normal->x, normal->y,
                      normal->z);
}
EI_VERIFY(0x00561150, SurfacePlaneVerify, 8, s_surfacePlane);

// 00560E10: MOV EAX,[ESP+8]; FLD [ECX+0x28] (7 bytes). Output only: ours into a scratch vector first.
static Vector3*(__fastcall* s_transform)(const MatrixRows*, void*, Vector3*, const Vector3*);
static Vector3* __fastcall TransformVerify(const MatrixRows* self, void*, Vector3* out, const Vector3* v) {
    Vector3 mine{};
    Vector3* mineResult = self->Transform(&mine, v);
    Vector3* expected = s_transform(self, nullptr, out, v);
    bool same = mineResult == &mine && expected == out && std::memcmp(&mine, out, sizeof(Vector3)) == 0;
    ei::verify::Check("MatrixRows::Transform", true, same, "v=(%g %g %g)", out->x, out->y, out->z);
    return expected;
}
EI_VERIFY(0x00560E10, TransformVerify, 7, s_transform);

// 005640A0: SUB ESP,8; MOV EAX,[ECX+0x20C] (9 bytes). Pure.
static CEngineObject*(__fastcall* s_nearestUnit)(CWorldClient*, void*, CEngineObject*);
static CEngineObject* __fastcall NearestUnitVerify(CWorldClient* self, void*, CEngineObject* unit) {
    CEngineObject* expected = s_nearestUnit(self, nullptr, unit);
    ei::verify::Check("CWorldClient::NearestVisibleUnit", expected, self->NearestVisibleUnit(unit), "unit %p", unit);
    return expected;
}
EI_VERIFY(0x005640A0, NearestUnitVerify, 9, s_nearestUnit);

// 00564AF0: MOV EDX,[ECX+0xD4] (6 bytes). Pure.
static FootprintPart*(__fastcall* s_listItem)(CObject3D*, void*, int);
static FootprintPart* __fastcall ListItemVerify(CObject3D* self, void*, int index) {
    FootprintPart* expected = s_listItem(self, nullptr, index);
    ei::verify::Check("CObject3D::PartBox", static_cast<void*>(expected), static_cast<void*>(self->PartBox(index)),
                      "index %d", index);
    return expected;
}
EI_VERIFY(0x00564AF0, ListItemVerify, 6, s_listItem);

// 005699F0 / 00566EC0 / 00566EF0: MOV EAX,[ECX+0x20C] (6 bytes). Pure.
static std::uint8_t*(__fastcall* s_localView)(CWorldClient*);
static std::uint8_t* __fastcall LocalViewVerify(CWorldClient* self) {
    std::uint8_t* expected = s_localView(self);
    ei::verify::Check("CWorldClient::LocalView", expected, self->LocalView(), "%p", self);
    return expected;
}
EI_VERIFY(0x005699F0, LocalViewVerify, 6, s_localView);

static bool(__fastcall* s_viewMode2)(CWorldClient*);
static bool __fastcall ViewMode2Verify(CWorldClient* self) {
    bool expected = s_viewMode2(self);
    ei::verify::Check("CWorldClient::IsLocalViewMode2", expected, self->IsLocalViewMode2(), "%p", self);
    return expected;
}
EI_VERIFY(0x00566EC0, ViewMode2Verify, 6, s_viewMode2);

static bool(__fastcall* s_playerIdle)(CWorldClient*);
static bool __fastcall PlayerIdleVerify(CWorldClient* self) {
    bool expected = s_playerIdle(self);
    ei::verify::Check("CWorldClient::IsLocalPlayerIdle", expected, self->IsLocalPlayerIdle(), "%p", self);
    return expected;
}
EI_VERIFY(0x00566EF0, PlayerIdleVerify, 6, s_playerIdle);

// 00567440: PUSH EBX; PUSH EBP; PUSH ESI; MOV ESI,ECX (5 bytes). Output: the index into scratch.
static BOOL(__fastcall* s_findExit)(CWorldClient*, void*, const float*, int*);
static BOOL __fastcall FindExitVerify(CWorldClient* self, void*, const float* point, int* index) {
    int expectedIndex = index ? *index : -12345, actualIndex = expectedIndex;
    BOOL expected = s_findExit(self, nullptr, point, index ? &expectedIndex : nullptr);
    BOOL actual = self->FindExitAt(point, index ? &actualIndex : nullptr);
    ei::verify::Check("CWorldClient::FindExitAt", true, expected == actual && expectedIndex == actualIndex, "(%g %g) %d/%d",
                      point[0], point[1], expected, actual);
    if (index) *index = expectedIndex;
    return expected;
}
EI_VERIFY(0x00567440, FindExitVerify, 5, s_findExit);

// 00567500 CreateExitMarkers: ours only (it makes particle sources; running the original too would
// leave a second set in the particle manager). Reached by every zone load (the client world load
// 00567AB0: `newgame`, `zone_gz2g_traps`). Checks: one entry per exit, null exactly for the exits
// whose target is "none" (any case), the others ZoneExit sources (0x2038) at the exit's centre with
// its half extents.
static void __fastcall CreateExitMarkersVerify(CWorldClient* self) {
    self->CreateExitMarkers();
    bool ok = self->countedA0_.GetSize() == self->exitCount_;
    for (int i = 0; ok && i < self->exitCount_; ++i) {
        const ZoneExit& exit = self->exits_[i];
        const bool none = gamecrt::stricmp(exit.target, "none") == 0;
        auto* source = reinterpret_cast<CParticleSource*>(self->countedA0_[i]);
        if (none || !source) {
            ok = none && !source;
            continue;
        }
        ok = source->type_ == 0x2038 && source->refCount_ >= 2 &&
             ei::verify::NearlyEqual(source->position.x, (exit.x2 + exit.x1) * 0.5f, 4, 1e-6f) &&
             ei::verify::NearlyEqual(source->position.y, (exit.y2 + exit.y1) * 0.5f, 4, 1e-6f) &&
             ei::verify::NearlyEqual(source->alpha, (exit.x2 - exit.x1) * 0.5f, 4, 1e-6f) &&
             ei::verify::NearlyEqual(source->unknown_11C, (exit.y2 - exit.y1) * 0.5f, 4, 1e-6f);
    }
    ei::verify::Check("CWorldClient::CreateExitMarkers", true, ok, "%d exits, %d markers", self->exitCount_,
                      self->countedA0_.GetSize());
}
EI_VERIFY(0x00567500, CreateExitMarkersVerify, 0, s_unused);

// 005677F0 PartyInExit: SUB ESP,0x14; PUSH EBP; PUSH ESI; MOV ESI,[ESP+0x28] (9 bytes).
// Differential: the original fills a scratch array (same vtable, empty), ours the caller's; the
// result and the listed records are compared. Reached when the party walks into an exit
// (00578A20 from the battle screen's update and the base dialogue's exit; `zone_base_bz1g`).
static BOOL(__fastcall* s_partyInExit)(CWorldClient*, void*, const RecordPtrArray*, int, RecordPtrArray*);
static BOOL __fastcall PartyInExitVerify(CWorldClient* self, void*, const RecordPtrArray* records, int exit,
                                         RecordPtrArray* inExit) {
    RecordPtrArray scratch{inExit->vtable, nullptr, 0, 0, 0};
    BOOL expected = s_partyInExit(self, nullptr, records, exit, &scratch);
    BOOL actual = self->PartyInExit(records, exit, inExit);
    bool same = expected == actual && scratch.size == inExit->size;
    for (int i = 0; same && i < scratch.size; ++i) same = scratch[i] == (*inExit)[i];
    ei::verify::Check("CWorldClient::PartyInExit", true, same, "exit %d: %d/%d, %d/%d listed", exit, expected, actual,
                      scratch.size, inExit->size);
    scratch.SetSize(0);
    return actual;
}
EI_VERIFY(0x005677F0, PartyInExitVerify, 9, s_partyInExit);

// 00565380 OnQuestVariable / 00566C20 SummonBird: ours only (they make the bird and tell objects
// their visibility). Reached by `ui_quest_notice` ("q.zxzone.zxq" set to 1 and 2: the bird is
// summoned at 2; "q.zxzone.zxs.1" keeps a dot). Checked: the bird exists afterwards when it had to
// be summoned.
static void __fastcall OnQuestVariableVerify(CWorldClient* self, void*, const char* variable, float value) {
    self->OnQuestVariable(variable, value);
    const char* second = std::strchr(variable, '.');
    second = second ? std::strchr(second + 1, '.') : nullptr;
    const char* name = second ? second + 1 : variable;
    const bool summoned = (value == 2.0f || x87::IsNaN(value)) && !std::strchr(name, '.');
    ei::verify::Check("CWorldClient::OnQuestVariable", true, !summoned || self->owned1B0_ != nullptr, "%s = %g",
                      variable, value);
}
EI_VERIFY(0x00565380, OnQuestVariableVerify, 0, s_unused);

// 005654F0 ObjectVariableName: PUSH -1; PUSH 0x722677 (7 bytes). The original builds its string in
// scratch storage, ours in the caller's; the texts are compared, the scratch destroyed. Reached by
// the client's new objects with a name (CPlayerClient::OnNewObject through its address, every zone
// load).
static mfc::CString*(__fastcall* s_objectVariableName)(CWorldClient*, void*, mfc::CString*, const char*);
static mfc::CString* __fastcall ObjectVariableNameVerify(CWorldClient* self, void*, mfc::CString* out, const char* name) {
    alignas(mfc::CString) unsigned char storage[sizeof(mfc::CString)];
    auto* theirs = s_objectVariableName(self, nullptr, reinterpret_cast<mfc::CString*>(storage), name);
    mfc::CString* ours = self->ObjectVariableName(out, name);
    ei::verify::Check("CWorldClient::ObjectVariableName", true, ours == out && std::strcmp(*theirs, *ours) == 0,
                      "%s: %s vs %s", name, static_cast<const char*>(*theirs), static_cast<const char*>(*ours));
    theirs->~CString();
    return ours;
}
EI_VERIFY(0x005654F0, ObjectVariableNameVerify, 7, s_objectVariableName);

// 005652E0 ApplyObjectVariables: ours only (it sets the objects' visibility; reached when the
// party's variables arrive, CPlayerClient's update through its address). Nothing is compared.
static void __fastcall ApplyObjectVariablesVerify(CWorldClient* self) {
    self->ApplyObjectVariables();
    ei::verify::Check("CWorldClient::ApplyObjectVariables", true, true, "");
}
EI_VERIFY(0x005652E0, ApplyObjectVariablesVerify, 0, s_unused);

static void __fastcall SummonBirdVerify(CWorldClient* self) {
    self->SummonBird();
    ei::verify::Check("CWorldClient::SummonBird", true, self->owned1B0_ != nullptr, "");
}
EI_VERIFY(0x00566C20, SummonBirdVerify, 0, s_unused);

// 005674C0: MOV EAX,[ESP+8]; MOV ECX,[ECX+0x90] (10 bytes). Output only.
static float*(__fastcall* s_exitCentre)(CWorldClient*, void*, float*, int);
static float* __fastcall ExitCentreVerify(CWorldClient* self, void*, float* out, int index) {
    float mine[2] = {};
    float* mineResult = self->ExitCentre(mine, index);
    float* expected = s_exitCentre(self, nullptr, out, index);
    bool same = mineResult == mine && expected == out && std::memcmp(mine, out, sizeof(mine)) == 0;
    ei::verify::Check("CWorldClient::ExitCentre", true, same, "index %d", index);
    return expected;
}
EI_VERIFY(0x005674C0, ExitCentreVerify, 10, s_exitCentre);

// 0056D9F0: MOV EAX,[ESP+4]; PUSH ESI (5 bytes). Pure.
static std::uint32_t(__fastcall* s_envFlags)(CWorldClient*, void*, CEngineObject*);
static std::uint32_t __fastcall EnvFlagsVerify(CWorldClient* self, void*, CEngineObject* unit) {
    std::uint32_t expected = s_envFlags(self, nullptr, unit);
    ei::verify::Check("CWorldClient::EnvironmentFlags", expected, self->EnvironmentFlags(unit), "unit %p", unit);
    return expected;
}
EI_VERIFY(0x0056D9F0, EnvFlagsVerify, 5, s_envFlags);

// ==== Predicates and glue =========================================================================

// The predicates return their answer in AL (some paths leave other bytes of EAX set): compare AL.
// 00573A60: MOV EAX,[ESP+4]; PUSH EBX (5 bytes).
static std::uint32_t(__fastcall* s_depthLess)(DepthLess*, void*, const int*, const int*);
static std::uint32_t __fastcall DepthLessVerify(DepthLess* self, void*, const int* a, const int* b) {
    std::uint32_t expected = s_depthLess(self, nullptr, a, b);
    bool actual = self->Less(a, b);
    ei::verify::Check("DepthLess::Less", static_cast<std::uint8_t>(expected), static_cast<std::uint8_t>(actual), "%d %d", a[1], b[1]);
    return expected;
}
EI_VERIFY(0x00573A60, DepthLessVerify, 5, s_depthLess);

// 00574300: PUSH ESI; PUSH EDI; MOV EDI,[ESP+0xC] (6 bytes).
static std::uint32_t(__fastcall* s_unitOrder)(UnitOrderLess*, void*, CEngineObject*, CEngineObject*);
static std::uint32_t __fastcall UnitOrderVerify(UnitOrderLess* self, void*, CEngineObject* a, CEngineObject* b) {
    std::uint32_t expected = s_unitOrder(self, nullptr, a, b);
    bool actual = self->Less(a, b);
    ei::verify::Check("UnitOrderLess::Less", static_cast<std::uint8_t>(expected), static_cast<std::uint8_t>(actual), "%p %p", a, b);
    return expected;
}
EI_VERIFY(0x00574300, UnitOrderVerify, 6, s_unitOrder);

// 00589580: MOV EAX,[ESP+4]; MOV EDX,[ESP+8] (8 bytes). Returns 0/1 in all of EAX.
static BOOL(__fastcall* s_entryLess)(EntryIdLess*, void*, const std::uint8_t*, const std::uint8_t*);
static BOOL __fastcall EntryLessVerify(EntryIdLess* self, void*, const std::uint8_t* a, const std::uint8_t* b) {
    BOOL expected = s_entryLess(self, nullptr, a, b);
    ei::verify::Check("EntryIdLess::Less", expected, self->Less(a, b), "%p %p", a, b);
    return expected;
}
EI_VERIFY(0x00589580, EntryLessVerify, 8, s_entryLess);

// 0057F440: MOV EAX,[0x79B5D8] (5 bytes). Pure.
static BOOL(__cdecl* s_isMpClient)();
static BOOL __cdecl IsMpClientVerify() {
    BOOL expected = s_isMpClient();
    ei::verify::Check("IsMultiplayerClient", expected, IsMultiplayerClient(), "");
    return expected;
}
EI_VERIFY(0x0057F440, IsMpClientVerify, 5, s_isMpClient);

// 00578B40: FILD [0x7AF61C] (6 bytes). Pure.
static float(__cdecl* s_gameTime)();
static float __cdecl GameTimeVerify() {
    float expected = s_gameTime();
    ei::verify::Check("GameTime", expected, GameTime(), "");
    return expected;
}
EI_VERIFY(0x00578B40, GameTimeVerify, 6, s_gameTime);

// 00578DB0 / 00578DC0: MOV EAX,imm32 (5 bytes). Pure. Every start (the key bindings file) and the
// main menu's exit movie.
static const mfc::CString*(__cdecl* s_dataPath)();
static const mfc::CString* __cdecl DataPathVerify() {
    const mfc::CString* expected = s_dataPath();
    ei::verify::Check("GameDataPathString", expected, GameDataPathString(), "");
    return expected;
}
EI_VERIFY(0x00578DB0, DataPathVerify, 5, s_dataPath);

static const mfc::CString*(__cdecl* s_basePath)();
static const mfc::CString* __cdecl BasePathVerify() {
    const mfc::CString* expected = s_basePath();
    ei::verify::Check("GameBasePathString", expected, GameBasePathString(), "");
    return expected;
}
EI_VERIFY(0x00578DC0, BasePathVerify, 5, s_basePath);

// 00578BA0 / 00578B50: MOV ECX,0x7AF5B8 (5 bytes). Pure.
static std::uint8_t*(__cdecl* s_playerData)();
static std::uint8_t* __cdecl PlayerDataVerify() {
    std::uint8_t* expected = s_playerData();
    ei::verify::Check("LocalPlayerData", expected, LocalPlayerData(), "");
    return expected;
}
EI_VERIFY(0x00578BA0, PlayerDataVerify, 5, s_playerData);

static float(__stdcall* s_localVar)(const char*);
static float __stdcall LocalVarVerify(const char* name) {
    float expected = s_localVar(name);
    ei::verify::Check("LocalPlayerVariable", expected, LocalPlayerVariable(name), "\"%s\"", name);
    return expected;
}
EI_VERIFY(0x00578B50, LocalVarVerify, 5, s_localVar);

// 00578BE0: MOV EAX,[0x79B5D8] (5 bytes). Pure.
static float(__stdcall* s_playerZeroVar)(const char*);
static float __stdcall PlayerZeroVarVerify(const char* name) {
    float expected = s_playerZeroVar(name);
    ei::verify::Check("PlayerZeroVariable", expected, PlayerZeroVariable(name), "\"%s\"", name);
    return expected;
}
EI_VERIFY(0x00578BE0, PlayerZeroVarVerify, 5, s_playerZeroVar);

// 00578A90: PUSH ECX; PUSH ESI; MOV ECX,0x7AF5B8 (7 bytes). Output: the original constructs a
// scratch string; the copies share the player's buffer, so the pointers must match.
static mfc::CString*(__stdcall* s_playerName)(mfc::CString*);
static mfc::CString* __stdcall PlayerNameVerify(mfc::CString* out) {
    alignas(mfc::CString) unsigned char raw[sizeof(mfc::CString)];
    auto* scratch = reinterpret_cast<mfc::CString*>(raw);
    mfc::CString* expected = s_playerName(scratch);
    mfc::CString* actual = LocalPlayerName(out);
    bool same = expected == scratch && actual == out && static_cast<const char*>(*scratch) == static_cast<const char*>(*out);
    ei::verify::Check("LocalPlayerName", true, same, "\"%s\"", static_cast<const char*>(*out));
    scratch->~CString();
    return out;
}
EI_VERIFY(0x00578A90, PlayerNameVerify, 7, s_playerName);

// A-W13. 005785D0 loads and may run a script: ours only. Checked: the result is Load's 0 / 1 and the
// error copied out is the command script's (code, line, text by content). The console's script
// command.
static int __stdcall RunScriptTextVerify(mfc::CString* text, char run, ScriptErrorInfo* error) {
    int result = RunScriptText(text, run, error);
    auto* script = reinterpret_cast<CScript*>(0x007C29CC);
    bool ok = (result == 0 || result == 1) &&
              (!error || (error->code == script->ErrorCode() && error->line == script->ErrorLine() &&
                          std::strcmp(error->text, script->ErrorText()) == 0));
    ei::verify::Check("RunScriptText", true, ok, "result %d", result);
    return result;
}
EI_VERIFY(0x005785D0, RunScriptTextVerify, 0, s_unused);

// 00578C40 fills `out` (emptied first; its nodes are allocated): ours only. Checked: every name
// copied starts with the prefix. The quest and briefing screens (every game with quests).
static void __stdcall PlayerZeroVariablesVerify(const char* prefix, NamedFloatTable* out) {
    PlayerZeroVariables(prefix, out);
    std::size_t length = std::strlen(prefix);
    bool ok = true;
    for (NamedFloatTable::Node** bucket = out->buckets; bucket != out->finish; ++bucket)
        for (NamedFloatTable::Node* node = *bucket; node; node = node->next)
            if (std::strncmp(node->key, prefix, length) != 0) ok = false;
    ei::verify::Check("PlayerZeroVariables", true, ok, "\"%s\": %u entries", prefix, out->count);
}
EI_VERIFY(0x00578C40, PlayerZeroVariablesVerify, 0, s_unused);
