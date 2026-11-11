// WorldClient: terrain and platform height queries, unit and exit lookups. See world.h.
#include "game/app/app.h"
#include "engine/scene/camera.h"
#include "engine/core/core_maths.h"
#include "game/effects/effects.h"
#include "game/app/singletons.h"
#include "engine/core/id_tables.h"
#include "game/objects/map_object.h"
#include "game/objects/object3d.h"
#include "engine/models/material.h"
#include "engine/scene/pipeline.h"
#include "engine/models/texture.h"
#include "game/net/net_session.h"
#include "game/net/net_link.h"
#include "engine/core/named_floats.h"
#include "game/map/map_database.h"
#include "engine/data/options.h"
#include "game/players/party.h"
#include "game/players/player_client.h"
#include "game/players/player_messages.h"
#include "game/players/player.h"
#include "game/sound/sound.h"
#include "game/spells/spell.h"
#include "game/terrain/terrain.h"
#include "game/ui/ui.h"
#include "game/units/unit.h"
#include "game/world/world.h"
#include "game/effects/world_effect.h"

#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/maths.h"
#include "lib/str.h"
#include "lib/text_case.h"

#include <cmath>
#include <cstring>
#include <iterator>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <vector>

// ---- Heights -------------------------------------------------------------------------------------

float WorldClient::ClampedGroundHeight(float x, float y) {
    // Branch-free (the comparison turned into a select mask); NaN picks the first operand of each comparison.
    float cx = MaxKeepB(0.0f, x);
    float maxX = terrain->width - 1.0f;
    cx = MinKeepA(maxX, cx);
    float cy = MaxKeepB(0.0f, y);
    float maxY = terrain->height - 1.0f;
    cy = MinKeepA(maxY, cy);
    return terrain->HeightAt(cx, cy, terrain::Layer::Ground);
}

float WorldClient::RawSurfaceHeightAt(float x, float y) { return terrain->SurfaceHeight(x, y); }

const char* WorldClient::AmbienceTimeOfDay() const {
    const float hour = TimeOfDay();
    if (!(hour < 6.0f || hour > 18.0f)) return "day";   // also NaN
    if (hour >= 22.0f || hour <= 2.0f) return "night";
    return hour < 6.0f ? "morning" : "evening";
}

// The (u, v) of `d` (a point relative to the platform's origin) moved along z onto the platform's plane.
Vector2 PlaneUV(const Platform& p, Vector3 d) {
    d.z -= Dot(p.normal, d) / p.normal.z;
    return {Dot(p.axisU, d), Dot(p.axisV, d)};
}

float PlatformSet::SurfaceHeight(const Vector3& point, float height) {
    for (auto& node : list) {
        const Platform& p = node;
        float dx = point.x - p.centre.x;
        float dy = point.y - p.centre.y;
        if (p.radius * p.radius < LengthSquared2D(dy, dx)) continue;
        const auto [u, v] = PlaneUV(p, point - p.origin);
        if (u < -0.5f || v < -0.5f) continue;
        if (p.sizeU + 0.5f < u || p.sizeV + 0.5f < v) continue;
        float z = p.axisV.z * v + p.axisU.z * u + p.origin.z;
        height = MaxKeepB(height, z);
    }
    return height;
}

bool PlatformSet::Contains(float x, float y) {
    for (auto& node : list) {
        const Platform& p = node;
        float dx = x - p.centre.x;
        float dy = y - p.centre.y;
        if (p.radius * p.radius < LengthSquared2D(dx, dy)) continue;
        const auto [u, v] = PlaneUV(p, {x - p.origin.x, y - p.origin.y, -p.origin.z});
        if (u < -0.1f || v < -0.1f) continue;
        if (p.sizeU + 0.1f < u || p.sizeV + 0.1f < v) continue;
        return true;
    }
    return false;
}

PlatformSet::SurfacePlaneResult PlatformSet::SurfacePlane(float x, float y) {
    SurfacePlaneResult result{{0.0f, 0.0f, 1.0f}, 0.0f};
    for (auto& node : list) {
        const Platform& p = node;
        if (p.radius * p.radius < DistanceSquared2D(Vector2{x, y}, Vector2{p.centre.x, p.centre.y})) continue;
        const auto [u, v] = PlaneUV(p, {x - p.origin.x, y - p.origin.y, -p.origin.z});
        if (u < -0.5f || v < -0.5f) continue;
        if (p.sizeU + 0.5f < u || p.sizeV + 0.5f < v) continue;
        float z = p.axisV.z * v + p.axisU.z * u + p.origin.z;
        if (z > result.height) {   // lower, equal or NaN keeps the old one
            result.height = z;
            result.normal = p.normal;
        }
    }
    return result;
}

// ---- Units and players ---------------------------------------------------------------------------

Unit* WorldClient::NearestVisibleUnit(Unit* unit) {
    PlayerClient* view = LocalView();
    if (!view || !unit) return nullptr;
    float best = 1e38f;
    Unit* nearest = nullptr;
    for (int i = 0; i < std::ssize(view->partyUnitIds); ++i) {
        Unit* found = FindUnit(view->partyUnitIds[i]);
        if (!found || found->GetClassId() != ClassId::Unit) continue;
        if (found->IsDead() != 0) continue;
        std::uint32_t side = 1u << (unit->side & 31);
        if ((found->relationsA & (side & found->relationsB)) == 0) continue;
        float distance = DistanceSquaredXY(found->localPosition, unit->localPosition);
        if (distance < best) {
            best = static_cast<float>(distance);
            nearest = found;
        }
    }
    return nearest;
}

FootprintPart* Object3D::PartBox(int index) {
    return &parts[index]->box;
}

PlayerClient* WorldClient::LocalView() {
    return session ? dynamic_cast<PlayerClient*>(session->connection) : nullptr;
}

bool WorldClient::IsLocalViewSendingCamera() {
    PlayerClient* view = LocalView();
    if (!view) return false;
    return view->state == PlayerClient::State::SendingCamera;
}

bool WorldClient::IsLocalPlayerIdle() {
    if (!session) return false;
    PlayerConnection* view = session->connection;
    if (!view) return false;
    netlink::NetLink* link = view->player->link;
    if (IsLive(link) && link->Alive()) return false;
    return true;
}

// ---- Exits ---------------------------------------------------------------------------------------

std::optional<int> WorldClient::FindExitAt(const Vector3& point) {
    for (int i = 0; i < std::ssize(exits); ++i) {
        if (!exits[i].Contains(point.x, point.y)) continue;
        if (str::Compare(exits[i].target, "none") == 0) continue;
        return i;
    }
    return std::nullopt;
}

namespace {

constexpr int kExitParticles = static_cast<int>(ParticleType::ZoneExit);   // "ZoneExit" (particle_world.cpp EmitZoneExit)
constexpr int kExitWarmUpTicks = 20;
constexpr float kOneHalf = 0.5f;
constexpr const char* kDotText = ".";
constexpr const char* kQuestPrefixText = "q.";

void SessionDisconnect(Player* session) { session->Disconnect(); }

// One print vector ticked in order: the prints whose tick returns 0 are erased.
template <typename T>
void RunEffects(std::vector<T>& prints) {
    std::erase_if(prints, [](T& print) { return print.Tick() == 0; });
}

} // namespace

void WorldClient::CreateExitMarkers() {
    exitMarkers.clear();
    exitMarkers.resize(exits.size());
    for (int i = 0; i < std::ssize(exits); ++i) {
        if (text::CompareIgnoringCase(exits[i].target,
                            "none") == 0) {
            continue;
        }
        auto* source = particleManager->CreateSource(kExitParticles);
        exitMarkers[i] = source;
        const ZoneExit& exit = exits[i];
        source->localPosition = {(exit.x2 + exit.x1) * kOneHalf, (exit.y2 + exit.y1) * kOneHalf, 0.0f};
        source->alpha = (exit.x2 - exit.x1) * kOneHalf;   // half extents, read by EmitZoneExit
        source->typeParam = (exit.y2 - exit.y1) * kOneHalf;
        source->turnCos = lib::Cos(exit.angle);
        source->turnSin = lib::Sin(exit.angle);
        for (int warmUpTick = 0; warmUpTick < kExitWarmUpTicks; ++warmUpTick) source->RunTick();
    }
    UpdateExitMarkers();
}

bool WorldClient::PartyInExit(const std::vector<PartyUnitRecord*>& records, int exit, std::vector<PartyUnitRecord*>* inExit) {
    bool result = 1;
    int living = 0;
    inExit->clear();
    for (int i = 0; i < std::ssize(records); ++i) {
        PartyUnitRecord* record = records[i];
        Unit* found =
            FindUnit(static_cast<int>(record->unitId));
        if (!found || found->IsDead() != 0) continue;
        ++living;
        if (FindExitAt(found->localPosition) != exit) {
            result = 0;
            continue;
        }
        inExit->push_back(record);
    }
    return living != 0 ? result : 0;
}

// ---- The bird and quest variables ----------------------------------------------------------------

void WorldClient::SummonBird() {
    if (!bird) bird = std::make_unique<EffectMoshka>();
    bird->Reset();
    auto* leader = session->GetParty()->members.data();
    bird->Follow(static_cast<int>(leader->unitId));
}

void WorldClient::OnQuestVariable(std::string_view variable, float value) {
    const char* dot = kDotText;
    std::string name(variable);
    const int length = lib::Count(name);
    const int second = str::Find(name, dot, str::Find(name, dot) + 1);
    name = str::Right(name, length - second - 1);
    for (MapObject* object : objects) {
        if (str::Compare(name, object->name2) == 0) object->SetVisibilityLevel(value);
    }
    // The original's test counts a NaN as equal; the audit checks that none arrives.
    if (value == 2.0f && str::Find(name, dot) < 0) SummonBird();
}

std::string WorldClient::ObjectVariableName(std::string_view name) {
    std::string variable = kQuestPrefixText;
    variable += LocalPlayerZone();
    variable += kDotText;
    variable += name;
    return variable;
}

void WorldClient::ApplyObjectVariables() {
    NamedFloatTable* variables = &session->GetParty()->variables;
    for (MapObject* object : objects) {
        const std::string& name = object->name2;
        if (name.empty()) continue;
        object->SetVisibilityLevel(variables->Get(ObjectVariableName(name)));
    }
}

Vector2 WorldClient::ExitCentre(int index) {
    const ZoneExit& exit = exits[index];
    float y = (exit.y2 + exit.y1) * 0.5f;
    float x = (exit.x2 + exit.x1) * 0.5f;
    return {x, y};
}

std::uint32_t WorldClient::EnvironmentFlags(Unit* unit) {
    std::uint32_t stateFlags = 0;
    switch (unit->moveMode2) {
    case MoveMode::Lie: stateFlags = lib::Bits(EnvironmentFlag::MoveMode0); break;
    case MoveMode::Crawl: stateFlags = lib::Bits(EnvironmentFlag::Walk); break;
    case MoveMode::Walk: stateFlags = lib::Bits(EnvironmentFlag::Run); break;
    case MoveMode::Run: stateFlags = lib::Bits(EnvironmentFlag::MoveMode3); break;
    case MoveMode::Stop: break;
    }
    if (str::Compare(zoneName, "gipat") == 0) stateFlags |= EnvironmentFlag::Gipat;
    else if (str::Compare(zoneName, "ingos") == 0) stateFlags |= EnvironmentFlag::Ingos;
    else if (str::Compare(zoneName, "suslanger") == 0) stateFlags |= EnvironmentFlag::Suslanger;
    stateFlags |= IsNight() ? EnvironmentFlag::Night : EnvironmentFlag::Day;
    stateFlags |= cave == 1 ? EnvironmentFlag::Cave : EnvironmentFlag::Outdoors;
    const int weather = rainSnow.mode;
    if (weather == 1) return stateFlags | EnvironmentFlag::Rain;
    if (weather == 2) return stateFlags | EnvironmentFlag::Snow;
    return stateFlags | EnvironmentFlag::NoWeather;
}

// ---- Segment --------------------------------------------------------------------------

bool WorldClient::RunTick() {
    int frames = ++ticksRun;
    if (frames == 10) {
        g_interface.OnStringEvent("autosave");
        sounds->SetEffectsVolume(options::g_options.audioSfx);
        sounds->SetSpeechVolume(options::g_options.audioSpeech);
        sounds->SetMusicVolume(options::g_options.audioStreaming);
    } else if (frames == 90 || (frames < 90 && partyDanger == 2)) {
        music->holding = 0;
    }
    if (terrain) terrain->RunTick();
    if (music) music->RunTick();
    if (sounds) sounds->Tick();
    if (!paused) {
        if (arrayManager) arrayManager->RunTick();
    }
    if (netlink::DriverSession* driver = netSession.Get()) driver->PumpDrivers(net::g_gameClient);
    if (Player* currentSession = session) {
        PlayerConnection* view = currentSession->connection;
        if (!paused) currentSession->Tick();
        if (view->Receive() == 0) {
            // The connection failed or the server's packet could not be read: the session ends, and the next pass's
            // idle check hands the flow back (RunPass: the multiplayer client's disconnect box, else the main menu).
            game::Warning("CWorldClient::Segment(): disconnecting the client: its connection failed or a packet could "
                          "not be read");
            SessionDisconnect(session);
            return false;
        }
        view->RunTick();
    }
    if (!paused) {
        ++tick;
        // The objects (dynamicObjects) whose update returns 0 leave dynamicNodes and the list.
        for (auto node = dynamicObjects.begin(); node != dynamicObjects.end();) {
            auto current = node;
            ++node;
            MapObject* object = *current;
            if (object->RunTick()) continue;
            auto key = static_cast<std::uint32_t>(object->id);
            if (auto found = dynamicNodes.find(key); found != dynamicNodes.end()) {
                dynamicObjects.erase(found->second);
                idtable::RemoveKey(dynamicNodes, key);
            }
        }
        ObjectListIterator timed;
        for (EngineObject* object = timed.First(TimedObjectManager()); object; object = timed.Next())
            object->RunTick();
        if (bird) bird->RunTick();
    }
    if (IsMultiplayerClient()) {
        if (static_cast<int>(std::ssize(g_interface.stack)) > 1) {
            float time = static_cast<float>(tick) + tickFraction;
            float now = time;
            for (auto node = dynamicObjects.begin(); node != dynamicObjects.end();) {
                auto current = node;
                ++node;
                (*current)->AdvanceTime(now);
            }
        }
    }
    if (particleManager) particleManager->TickSources(static_cast<char>(paused));
    if (!paused) {
        // The three print lists: finished ones (tick returns 0) go.
        RunEffects(bloodPrints);
        RunEffects(footprints);
        RunEffects(scorchMarks);
    }
    if (tick % 32 == 0) GetTextureManager()->TrimCache();
    // A handler that could not take the server's data asked the session to leave (PlayerClient::LeaveAfterTick):
    // the player disconnects now, and the next pass's idle check hands the flow back (RunPass: the multiplayer
    // client's disconnect box with the reason, else the main menu).
    if (PlayerClient* local = LocalView(); local && !local->leaveReason.empty()) {
        g_interface.disconnectReason = std::exchange(local->leaveReason, std::string());
        session->Disconnect();
    }
    return true;
}

// --- Class name and id ------------------------------------------------------------------------------

namespace {
constexpr const char* kWorldClientClassName = "CWorldClient";
} // namespace

const char* WorldClient::ClassName() { return kWorldClientClassName; }
ClassId WorldClient::GetClassId() { return ClassId::WorldClient; }

// --- Getters and small calls other code reaches through the world: the managers, the new texture /
// material, the camera, the value of message 0x12 and the water levels of message 0x73 --------------------

MaterialManager* World::GetMaterialManager() {
    WorldClient* client = ThisClientWorld();
    return client ? client->materialManager.get() : nullptr;
}
TextureManager* World::GetTextureManager() {
    WorldClient* client = ThisClientWorld();
    return client ? client->textureManager.get() : nullptr;
}
ParticleManager* World::ClientParticleManager() {
    WorldClient* client = ThisClientWorld();
    return client ? client->particleManager.get() : nullptr;
}

void WorldClient::MoveObject(MapObject* object, const float* position) {
    objectGrid.Move(object, position);
}

Texture* WorldClient::NewTexture(int textureType) {
    if (!textureManager) return nullptr;
    return textureManager->NewTextureOfType(textureType);
}

Material* WorldClient::NewMaterial(int materialType) {
    if (!materialManager) return nullptr;
    return materialManager->NewMaterial(materialType);
}

void WorldClient::PlayCameraPath(std::string_view name) {
    if (!camera->LoadPath(name, 0)) return;
    camera->flags |= Camera::kPlaying;
    camera->keyIndex = 0;
    camera->timeOffset = (static_cast<float>(tick) + tickFraction) - camera->keys[0].time;
}

void WorldClient::ReadWaterLevels(ByteStream* stream) {
    countdowns.Load(stream);
    countdowns.ApplyTo(terrain.get());
}

namespace {
constexpr const char* kBridgeManagerName = "CBridgeManager";
} // namespace

const char* PlatformSet::ClassName() { return kBridgeManagerName; }
ClassId PlatformSet::GetClassId() { return ClassId::PlatformSet; }
