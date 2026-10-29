// WorldClient: terrain and platform height queries, unit and exit lookups. See world.h.
#include "camera.h"
#include "core_maths.h"
#include "effects.h"
#include "game_data.h"
#include "game_singletons.h"
#include "game_strings.h"
#include "hash_tables_400.h"
#include "map_object.h"
#include "map_object_figures.h"
#include "material.h"
#include "model.h"
#include "net_classes.h"
#include "net_link.h"
#include "object_tables.h"
#include "party.h"
#include "player_client.h"
#include "player_messages.h"
#include "sound.h"
#include "spell.h"
#include "terrain.h"
#include "texture.h"
#include "ui.h"
#include "unit.h"
#include "world.h"
#include "world_data.h"
#include "world_effect.h"

#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/nan_audit.h"
#include "lib/str.h"
#include "lib/text_case.h"

#include "ei/rebirth.h"
#include "ei/x87.h"

#include <cmath>
#include <cstring>
#include <iterator>
#include <list>
#include <memory>
#include <new>
#include <string>

// ---- Heights -------------------------------------------------------------------------------------

float WorldClient::ClampedGroundHeight(float x, float y) {
    // Branch-free (the comparison turned into a select mask); NaN picks the first operand of each comparison.
    float cx = 0.0f >= x ? 0.0f : x;
    float maxX = static_cast<float>(terrain->width - 1.0f);
    cx = maxX >= cx ? cx : maxX;
    float cy = 0.0f >= y ? 0.0f : y;
    float maxY = static_cast<float>(terrain->height - 1.0f);
    cy = maxY >= cy ? cy : maxY;
    return terrain->HeightAt(cx, cy, terrain::Layer::Ground);
}

double WorldClient::RawSurfaceHeightAt(float x, float y) { return terrain->SurfaceHeight(x, y); }

const char* WorldClient::AmbienceTimeOfDay() const {
    float hour = static_cast<float>(std::fmod(static_cast<double>(tick) * secondsPerTick + timeBase, 24.0));
    if (!(hour < 6.0f || hour > 18.0f)) return "day";   // also NaN
    if (hour >= 22.0f || hour <= 2.0f) return "night";
    return hour < 6.0f ? "morning" : "evening";
}

float PlatformSet::SurfaceHeight(const Vector3& point, float height) {
    for (auto& node : list) {
        const Platform& p = node;
        float dx = point.x - p.x;
        float dy = point.y - p.y;
        EI_NAN_AUDIT(p.radius, dy, dx);
        if (p.radius * p.radius < LengthSquared2DUnrounded(dy, dx)) continue;
        float a = point.x - p.origin.x;
        float b = point.y - p.origin.y;
        float c = point.z - p.origin.z;
        // Project onto the platform plane along z, then into its (u, v) frame.
        c = c - DotUnrounded<Sum3::XYZ, float>(p.normal.x, p.normal.y, p.normal.z, a, b, c) / p.normal.z;
        float u = static_cast<float>(DotUnrounded<Sum3::XYZ, float>(p.axisU.x, p.axisU.y, p.axisU.z, a, b, c));
        float v = static_cast<float>(DotUnrounded<Sum3::XYZ, float>(p.axisV.x, p.axisV.y, p.axisV.z, a, b, c));
        EI_NAN_AUDIT(u, v, p.sizeU, p.sizeV);
        if (u < -0.5f || v < -0.5f) continue;
        if (p.sizeU + 0.5f < u || p.sizeV + 0.5f < v) continue;
        float z = static_cast<float>(p.axisV.z * v + p.axisU.z * u + p.origin.z);
        height = height >= z ? height : z;
    }
    return height;
}

bool PlatformSet::Contains(float x, float y) {
    for (auto& node : list) {
        const Platform& p = node;
        float dx = x - p.x;
        float dy = y - p.y;
        EI_NAN_AUDIT(p.radius, dx, dy);
        if (p.radius * p.radius < LengthSquared2DUnrounded(dx, dy)) continue;
        float a = x - p.origin.x;
        float b = y - p.origin.y;
        float c = -p.origin.z;
        c = c - DotUnrounded<Sum3::ZYX, float>(p.normal.x, p.normal.y, p.normal.z, a, b, c) / p.normal.z;
        float u = static_cast<float>(DotUnrounded<Sum3::ZYX, float>(p.axisU.x, p.axisU.y, p.axisU.z, a, b, c));
        float v = static_cast<float>(DotUnrounded<Sum3::ZYX, float>(p.axisV.x, p.axisV.y, p.axisV.z, a, b, c));
        EI_NAN_AUDIT(u, v, p.sizeU, p.sizeV);
        if (u < -0.1f || v < -0.1f) continue;
        if (p.sizeU + 0.1f < u || p.sizeV + 0.1f < v) continue;
        return true;
    }
    return false;
}

void PlatformSet::SurfacePlane(float x, float y, Vector3* normal, float* height) {
    *height = 0.0f;
    *normal = {0.0f, 0.0f, 1.0f};
    for (auto& node : list) {
        const Platform& p = node;
        EI_NAN_AUDIT(p.radius, x, y, p.x, p.y);
        if (p.radius * p.radius < DistanceSquared2D(Vector2{x, y}, Vector2{p.x, p.y})) continue;
        float a = x - p.origin.x;
        float b = y - p.origin.y;
        float c = -p.origin.z;
        c = c - DotUnrounded<Sum3::ZYX, float>(p.normal.x, p.normal.y, p.normal.z, a, b, c) / p.normal.z;
        float u = static_cast<float>(DotUnrounded<Sum3::ZYX, float>(p.axisU.x, p.axisU.y, p.axisU.z, a, b, c));
        float v = static_cast<float>(DotUnrounded<Sum3::ZYX, float>(p.axisV.x, p.axisV.y, p.axisV.z, a, b, c));
        EI_NAN_AUDIT(u, v, p.sizeU, p.sizeV);
        if (u < -0.5f || v < -0.5f) continue;
        if (p.sizeU + 0.5f < u || p.sizeV + 0.5f < v) continue;
        float z = p.axisV.z * v + p.axisU.z * u + p.origin.z;
        if (z > *height) {   // lower, equal or NaN keeps the old one
            *height = z;
            *normal = {p.normal.x, p.normal.y, p.normal.z};
        }
    }
}

// x is summed ZYX, y and z ZXY.
Vector3 MatrixRows::Transform(const Vector3& v) const { return TransformColumnNormal<Sum3::ZYX, Sum3::ZXY>(*this, v); }

// ---- Units and players ---------------------------------------------------------------------------

CUnit* WorldClient::NearestVisibleUnit(CUnit* unit) {
    PlayerClient* view = LocalView();
    if (!view || !unit) return nullptr;
    float best = 1e38f;
    CUnit* nearest = nullptr;
    for (int i = 0; i < std::ssize(view->partyUnitIds); ++i) {
        CUnit* found = FindUnit(view->partyUnitIds[i]);
        if (!found || found->GetClassId() != ClassId::Unit) continue;
        if (found->IsDead() != 0) continue;
        std::uint32_t side = 1u << (unit->side & 31);
        if ((found->relationsA & (side & found->relationsB)) == 0) continue;
        float dy = found->localPosition.y - unit->localPosition.y;
        float dx = found->localPosition.x - unit->localPosition.x;
        float distance = LengthSquared2DUnrounded(dx, dy);
        EI_NAN_AUDIT(distance, best);
        if (distance < best) {
            best = static_cast<float>(distance);
            nearest = found;
        }
    }
    return nearest;
}

FootprintPart* CObject3D::PartBox(int index) {
    return &parts[index]->box;
}

PlayerClient* WorldClient::LocalView() {
    if (session) return static_cast<PlayerClient*>(session->connection);
    return nullptr;
}

bool WorldClient::IsLocalViewMode2() {
    PlayerClient* view = LocalView();
    if (!view) return false;
    return view->state == 2;
}

bool WorldClient::IsLocalPlayerIdle() {
    if (!session) return false;
    PlayerConnection* view = session->connection;
    if (!view) return false;
    netlink::NetLink* link = view->player->link;
    if (link && !(link->flags7 & RefObject::kDetached) && link->Alive()) return false;
    return true;
}

// ---- Exits ---------------------------------------------------------------------------------------

bool WorldClient::FindExitAt(const float* point, int* index) {
    for (int i = 0; i < std::ssize(exits); ++i) {
        if (!exits[i].Contains(point[0], point[1])) continue;
        if (str::Compare(exits[i].target, "none") == 0) continue;
        if (index) *index = i;
        return true;
    }
    return false;
}

namespace {

constexpr int kExitParticles = static_cast<int>(ParticleType::ZoneExit);   // "ZoneExit" (particle_emitters.cpp EmitZoneExit)
constexpr int kExitWarmUpTicks = 20;
constexpr float kOneHalf = 0.5f;
const char* DotText() { return "."; }
const char* QuestPrefixText() { return "q."; }

void SessionDisconnect(CPlayer* session) { session->Disconnect(); }

// One print list ticked: the prints whose tick returns 0 are erased.
template <typename T>
void RunEffects(std::list<T>& list) {
    for (auto it = list.begin(); it != list.end();) it = it->Tick() == 0 ? list.erase(it) : std::next(it);
}

void EndWalk(ObjectListIterator& it) {
    if (it.manager) {
        --it.manager->refCount;
        it.manager = nullptr;
    }
}

} // namespace

void WorldClient::CreateExitMarkers() {
    exitMarkers.clear();
    exitMarkers.resize(exits.size());
    for (int i = 0; i < std::ssize(exits); ++i) {
        if (text::CompareIgnoringCase(exits[i].target,
                            game::NoneText()) == 0) {
            continue;
        }
        auto* source = g_clientWorld.particleManager->CreateSource(kExitParticles);
        exitMarkers[i] = source;
        const ZoneExit& exit = exits[i];
        source->localPosition = {(exit.x2 + exit.x1) * kOneHalf, (exit.y2 + exit.y1) * kOneHalf, 0.0f};
        source->alpha = (exit.x2 - exit.x1) * kOneHalf;   // half extents, read by EmitZoneExit
        source->typeParam = (exit.y2 - exit.y1) * kOneHalf;
        source->turnCos = static_cast<float>(x87::fcos(static_cast<double>(exit.angle)));
        source->turnSin = static_cast<float>(x87::fsin(static_cast<double>(exit.angle)));
        for (int warmUpTick = 0; warmUpTick < kExitWarmUpTicks; ++warmUpTick) source->RunTick();
    }
    UpdateExitMarkers();
}

bool WorldClient::PartyInExit(const std::vector<PartyUnitRecord*>* records, int exit, std::vector<PartyUnitRecord*>* inExit) {
    bool result = 1;
    int living = 0;
    inExit->clear();
    for (int i = 0; i < std::ssize(*records); ++i) {
        PartyUnitRecord* record = (*records)[i];
        CUnit* found =
            FindUnit(static_cast<int>(record->unitId));
        if (!found || found->IsDead() != 0) continue;
        ++living;
        int index = 0;
        if (!FindExitAt(&found->localPosition.x, &index) || index != exit) {
            result = 0;
            continue;
        }
        const int last = std::ssize(*inExit);
        inExit->resize(last + 1);
        (*inExit)[last] = record;
    }
    return living != 0 ? result : 0;
}

// ---- The bird and quest variables ----------------------------------------------------------------

void WorldClient::SummonBird() {
    if (!bird) bird = new EffectMoshka();
    bird->Reset();
    auto* leader = session->Party()->members.data();
    bird->Follow(static_cast<int>(leader->unitId));
}

void WorldClient::OnQuestVariable(std::string_view variable, float value) {
    const char* dot = DotText();
    std::string name(variable);
    const int length = name.size();
    const int second = str::Find(name, dot, str::Find(name, dot) + 1);
    name = str::Right(name, length - second - 1);
    for (CMapObject* object : objects) {
        if (str::Compare(name, object->name2) == 0) object->SetVisibilityLevel(value);
    }
    // The original's test counts a NaN as equal; the audit checks that none arrives.
    EI_NAN_AUDIT(value);
    if (value == 2.0f && str::Find(name, dot) < 0) SummonBird();
}

std::string WorldClient::ObjectVariableName(std::string_view name) {
    std::string variable = QuestPrefixText();
    variable += LocalPlayerZone();
    variable += DotText();
    variable += name;
    return variable;
}

void WorldClient::ApplyObjectVariables() {
    NamedFloatTable* variables = &session->Party()->variables;
    for (CMapObject* object : objects) {
        const std::string& name = object->name2;
        if (name.empty()) continue;
        object->SetVisibilityLevel(variables->Get(ObjectVariableName(name)));
    }
}

float* WorldClient::ExitCentre(float* out, int index) {
    const ZoneExit& exit = exits[index];
    float y = (exit.y2 + exit.y1) * 0.5f;
    float x = (exit.x2 + exit.x1) * 0.5f;
    out[0] = x;
    out[1] = y;
    return out;
}

std::uint32_t WorldClient::EnvironmentFlags(CUnit* unit) {
    std::uint32_t stateFlags = 0;
    switch (static_cast<std::uint32_t>(unit->moveMode2)) {
    case 0: stateFlags = lib::Bits(EnvironmentFlag::MoveMode0); break;
    case 1: stateFlags = lib::Bits(EnvironmentFlag::Walk); break;
    case 2: stateFlags = lib::Bits(EnvironmentFlag::Run); break;
    case 3: stateFlags = lib::Bits(EnvironmentFlag::MoveMode3); break;
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
    ObjectListIterator objectsIt{nullptr, {}};
    int frames = ++ticksRun;
    if (frames == 10) {
        g_interface.OnStringEvent(worlddata::AutosaveEventText());
        sounds->SetEffectsVolume(options::g_options.audioSfx);
        sounds->SetSpeechVolume(options::g_options.audioSpeech);
        sounds->SetMusicVolume(options::g_options.audioStreaming);
    } else if (frames == 90 || (frames < 90 && g_clientWorld.partyDanger == 2)) {
        music->holding = 0;
    }
    if (terrain) terrain->RunTick();
    if (music) music->RunTick();
    if (sounds) sounds->Tick();
    if (!paused) {
        if (arrayManager) arrayManager->RunTick();
    }
    if (WorldHandle* source = worldHandle.Get()) source->Pump();
    if (ClientHandle* source = handle.Get()) source->Pump();
    if (netlink::DriverSession* driver = netSession.Get()) driver->PumpDrivers(net::g_gameClient);
    if (CPlayer* currentSession = session) {
        PlayerConnection* view = currentSession->connection;
        if (!paused) currentSession->Tick();
        if (view->Receive() == 0) {
            // The connection is gone. Rebirth's DisableExitOnCrashReport
            // keeps the session: only the error is reported.
            if (!ei::rebirth::Enabled(ei::rebirth::Patch::DisableExitOnCrashReport)) SessionDisconnect(session);
            game::FatalError(worlddata::SegmentDisconnectedText());
            EndWalk(objectsIt);
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
            CMapObject* object = *current;
            if (object->RunTick()) continue;
            auto key = static_cast<std::uint32_t>(object->id);
            std::list<CMapObject*>::iterator position{};
            if (dynamicNodes.Lookup(&key, &position)) {
                dynamicObjects.erase(position);
                hash400::RemoveKey(dynamicNodes, key);
            }
        }
        for (EngineObject* object = objectsIt.First(TimedObjectManager()); object; object = objectsIt.Next())
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
        if (!paused && options::g_options.enableBloodprintsRelight != 0) {
            lightLevelsCountdown -= worlddata::g_updatePeriod;
            if (lightLevelsCountdown <= 0) {
                lightLevelsCountdown = options::g_options.shadowLightingFreq;
                // Each blood print gets a grey level: the brightest of the sun at
                // the reference point and the lights whose range reaches the effect's point.
                CLight* sun = g_sceneRenderer.sun;
                for (BloodPrint& effect : bloodPrints) {
                    float level = sun->GetIntensityAt(worlddata::kOrigin);
                    ObjectListIteratorOf<CLight> lights{};
                    for (CLight* light = lights.First(ClientLightManager()); light; light = lights.Next()) {
                        if (light == sun || (light->flags & LightFlag::Hidden)) continue;
                        float nodeX = effect.position.x, nodeY = effect.position.y;
                        double dy = static_cast<double>(light->localPosition.y) - nodeY;
                        double dx = static_cast<double>(light->localPosition.x) - nodeX;
                        double squared = LengthSquared2DUnrounded(dx, dy);
                        double radius = light->rangeSquared;
                        if (squared > radius) continue;
                        Vector3 point = {nodeX, nodeY, 0.0f};
                        point.z = static_cast<float>(terrain->HeightAt(nodeX, nodeY, terrain::Layer::Ground));
                        float intensity = light->GetIntensityAt(point);
                        level = SelectLess(level, intensity, intensity, level);
                    }
                    float clamped = SelectLess(1.0f, level, 1.0f, level);
                    float floor = SelectLess(0.0f, clamped, clamped, 0.0f);
                    float scaled = floor * worlddata::kColourScale;
                    std::uint32_t grey = static_cast<std::uint32_t>(x87::fistp(scaled));
                    std::uint32_t colour = ((((grey | 0xFFFFFF00u) << 8) | grey) << 8) | grey;
                    effect.colour = colour;
                    EndWalk(lights);
                }
            }
        }
    }
    if (tick % 32 == 0) TextureManager()->TrimCache();
    EndWalk(objectsIt);
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

const char* WorldClient::TexturePath() { return texturePath.c_str(); }
CMaterialManager* WorldClient::MaterialManager() { return materialManager; }
CTextureManager* WorldClient::TextureManager() { return textureManager; }
BaseManager* WorldClient::ClientLightManager() { return lightFactory; }
ParticleManager* WorldClient::ClientParticleManager() { return particleManager; }

void WorldClient::MoveObject(CMapObject* object, const float* position) {
    objectGrid.Move(object, position);
}
void WorldClient::MoveObject2(CMapObject* object, const float* position) {
    secondGrid.Move(object, position);
}

CTexture* WorldClient::NewTexture(int textureType) {
    if (!textureManager) return nullptr;
    return textureManager->NewTextureOfType(textureType);
}

Material* WorldClient::NewMaterial(int materialType) {
    if (!materialManager) return nullptr;
    return materialManager->NewMaterial(materialType);
}

void WorldClient::SetCamera(Camera* newCamera) {
    if (camera) --camera->refCount;
    camera = newCamera;
    if (newCamera) newCamera->AddRef();
}

void WorldClient::SetWorldValue(int value) { worldValue = value; }

int WorldClient::ReadWaterLevels(ByteStream* stream) {
    countdowns.Load(stream, terrain);
    return 1;
}

namespace {
constexpr const char* kBridgeManagerName = "CBridgeManager";
} // namespace

const char* PlatformSet::ClassName() { return kBridgeManagerName; }
ClassId PlatformSet::GetClassId() { return ClassId::PlatformSet; }
