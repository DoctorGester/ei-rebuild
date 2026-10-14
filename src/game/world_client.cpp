// CWorldClient: terrain and platform height queries, unit and exit lookups. See world.h.
#include "world.h"
#include "world_data.h"

#include "core_maths.h"
#include "map_object.h"
#include "map_object_figures.h"
#include "net_classes.h"
#include "net_link.h"
#include "player_client.h"
#include "terrain.h"
#include "sound.h"
#include "spell.h"
#include "effects.h"
#include "game_strings.h"
#include "object_tables.h"
#include "party.h"
#include "player_messages.h"
#include "world_effect.h"
#include "unit.h"

#include "lib/crt.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "ei/x87.h"
#include "originals_core.h"
#include "originals_engine.h"
#include "originals_world.h"
#include "originals_library.h"
#include "originals_gameplay.h"
#include "originals_ui.h"
#include "originals_effects.h"

#include <cmath>
#include <cstring>
#include <new>


CWorldClient* WorldClient() { return reinterpret_cast<CWorldClient*>(0x007AF5B8); }

// ---- Heights -------------------------------------------------------------------------------------

float CWorldClient::ClampedGroundHeight(float x, float y) {
    // Branch-free in the original (FCOM flags turned into a select mask); NaN picks the first
    // operand of each comparison.
    float cx = !(0.0f >= x) ? x : 0.0f;
    float maxX = StoreFloat(terrain_->width - 1.0f);
    cx = !(maxX >= cx) ? maxX : cx;
    float cy = !(0.0f >= y) ? y : 0.0f;
    float maxY = StoreFloat(terrain_->height - 1.0f);
    cy = !(maxY >= cy) ? maxY : cy;
    return terrain_->HeightAt(cx, cy, 0);
}

float PlatformSet::SurfaceHeight(const Vector3* point, float height) {
    for (PlatformNode* node = list.head; node; node = node->next) {
        const Platform& p = node->data;
        float dx = point->x - p.x;
        float dy = point->y - p.y;
        // Every test is "skip when below or unordered" (FCOM + TEST AH,1), so NaN skips.
        if (!(p.radius * p.radius >= dy * dy + dx * dx)) continue;
        float a = point->x - p.origin[0];
        float b = point->y - p.origin[1];
        float c = point->z - p.origin[2];
        // Project onto the platform plane along z, then into its (u, v) frame.
        c = c - (p.normal[1] * b + p.normal[0] * a + p.normal[2] * c) / p.normal[2];
        float u = StoreFloat(p.axisU[1] * b + p.axisU[0] * a + p.axisU[2] * c);
        float v = StoreFloat(p.axisV[1] * b + p.axisV[0] * a + p.axisV[2] * c);
        if (!(u >= -0.5f) || !(v >= -0.5f)) continue;
        if (!(p.sizeU + 0.5f >= u) || !(p.sizeV + 0.5f >= v)) continue;
        float z = StoreFloat(p.axisV[2] * v + p.axisU[2] * u + p.origin[2]);
        height = !(height >= z) ? z : height;
    }
    return height;
}

bool PlatformSet::Contains(float x, float y) {
    for (PlatformNode* node = list.head; node; node = node->next) {
        const Platform& p = node->data;
        float dx = x - p.x;
        float dy = y - p.y;
        if (!(p.radius * p.radius >= dx * dx + dy * dy)) continue;
        float a = x - p.origin[0];
        float b = y - p.origin[1];
        float c = -p.origin[2];
        c = c - (p.normal[1] * b + p.normal[2] * c + p.normal[0] * a) / p.normal[2];
        float u = StoreFloat(p.axisU[1] * b + p.axisU[2] * c + p.axisU[0] * a);
        float v = StoreFloat(p.axisV[1] * b + p.axisV[2] * c + p.axisV[0] * a);
        if (!(u >= -0.1f) || !(v >= -0.1f)) continue;
        if (!(p.sizeU + 0.1f >= u) || !(p.sizeV + 0.1f >= v)) continue;
        return true;
    }
    return false;
}

void PlatformSet::SurfacePlane(float x, float y, Vector3* normal, float* height) {
    *height = 0.0f;
    *normal = {0.0f, 0.0f, 1.0f};
    for (PlatformNode* node = list.head; node; node = node->next) {
        const Platform& p = node->data;
        if (!(p.radius * p.radius >= DistanceSquared2D(Vector2{x, y}, Vector2{p.x, p.y}))) continue;
        float a = x - p.origin[0];
        float b = y - p.origin[1];
        float c = -p.origin[2];
        c = c - (p.normal[1] * b + p.normal[2] * c + p.normal[0] * a) / p.normal[2];
        float u = StoreFloat(p.axisU[1] * b + p.axisU[2] * c + p.axisU[0] * a);
        float v = StoreFloat(p.axisV[1] * b + p.axisV[2] * c + p.axisV[0] * a);
        if (!(u >= -0.5f) || !(v >= -0.5f)) continue;
        if (!(p.sizeU + 0.5f >= u) || !(p.sizeV + 0.5f >= v)) continue;
        float z = p.axisV[2] * v + p.axisU[2] * u + p.origin[2];
        if (!(z > *height)) continue;   // FCOM + TEST AH,0x41: lower, equal or NaN keeps the old one
        *height = z;
        *normal = {p.normal[0], p.normal[1], p.normal[2]};
    }
}

// x is summed ZYX, y and z ZXY; every component is computed before the stores (out may be v).
Vector3* MatrixRows::Transform(Vector3* out, const Vector3* v) const {
    *out = TransformColumnNormal<Sum3::ZYX, Sum3::ZXY>(*this, *v);
    return out;
}

// ---- Units and players ---------------------------------------------------------------------------

CEngineObject* CWorldClient::NearestVisibleUnit(CEngineObject* unit) {
    CPlayerClient* view = Session()->LocalClient();
    if (!view || !unit) return nullptr;
    auto* self = static_cast<CUnit*>(unit);
    float best = 1e38f;
    CEngineObject* nearest = nullptr;
    for (int i = 0; i < view->partyUnitIds_.size; ++i) {
        CEngineObject* found = WorldFindUnit(this, nullptr, view->partyUnitIds_[i]);
        if (!found || found->GetClassId() != world::kClassUnit) continue;
        if (ObjectSlots(found)->IsDead() != 0) continue;
        auto* other = static_cast<CUnit*>(found);
        std::uint32_t side = 1u << (self->side & 31);
        if ((other->relationsA & (side & other->relationsB)) == 0) continue;
        float dy = other->position.y - self->position.y;
        float dx = other->position.x - self->position.x;
        float distance = dx * dx + dy * dy;
        if (!(distance >= best)) {
            best = StoreFloat(distance);
            nearest = other;
        }
    }
    return nearest;
}

FootprintPart* CObject3D::PartBox(int index) {
    void* part = index < parts.size ? parts.data[index] : pendingParts.data[index - parts.size];
    return reinterpret_cast<FootprintPart*>(&static_cast<FigurePart*>(part)->boxFlags);
}

std::uint8_t* CWorldClient::LocalView() {
    if (session_) return reinterpret_cast<std::uint8_t*>(Session()->localView);
    return nullptr;
}

bool CWorldClient::IsLocalViewMode2() {
    if (!session_) return false;
    CPlayerClient* view = Session()->LocalClient();
    if (!view) return false;
    return view->state_ == 2;
}

bool CWorldClient::IsLocalPlayerIdle() {
    if (!session_) return false;
    CPlayerConnection* view = Session()->localView;
    if (!view) return false;
    netlink::NetLink* link = view->player_->link_;
    if (link && !(link->flags7_ & CRefObject::kDetached) && link->Alive()) return false;
    return true;
}

// ---- Exits ---------------------------------------------------------------------------------------

BOOL CWorldClient::FindExitAt(const float* point, int* index) {
    for (int i = 0; i < exitCount_; ++i) {
        if (!exits_[i].Contains(point[0], point[1])) continue;
        if (GameMbscmp(exits_[i].target, "none") == 0) continue;
        if (index) *index = i;
        return 1;
    }
    return 0;
}

namespace {

constexpr int kExitParticles = 0x2038;   // "ZoneExit" (particle_emitters.cpp EmitZoneExit)
constexpr int kExitWarmUpTicks = 20;
constexpr float kOneHalf = 0.5f;          // 0073B86C
const char* DotText() { return reinterpret_cast<const char*>(0x00783D78); }   // "."
const char* QuestPrefixText() { return reinterpret_cast<const char*>(0x00783D7C); }   // "q."

// 0056F1A0 (library: counted pointer assignment, ECX the slot): references `object`, then
// releases the slot's old one.
void AssignCounted(CEngineObject** slot, CEngineObject* object) {
    CEngineObject* old = *slot;
    *slot = object;
    if (object) ++object->refCount_;
    if (old) old->Release();
}

} // namespace

void CWorldClient::CreateExitMarkers() {
    // The counted array's inlined RemoveAll (00573220 releases each element).
    if (countedA0_.data) {
        for (int i = 0; i < countedA0_.size; ++i) {
            if (countedA0_[i]) countedA0_[i]->Release();
        }
    }
    countedA0_.SetSize(0);
    countedA0_.SetSize(exitCount_);
    for (int i = 0; i < exitCount_; ++i) {
        if (gamecrt::mbsicmp(reinterpret_cast<const unsigned char*>(exits_[i].target),
                             reinterpret_cast<const unsigned char*>(game::NoneText())) == 0) {
            continue;
        }
        auto* source = static_cast<CParticleSource*>(
            ParticleCreateSourceOriginal(ParticleManagerObject(), nullptr, kExitParticles));
        auto* marker = reinterpret_cast<CEngineObject*>(source);
        AssignCounted(&countedA0_[i], marker);
        const ZoneExit& exit = exits_[i];
        source->position = {(exit.x2 + exit.x1) * kOneHalf, (exit.y2 + exit.y1) * kOneHalf, 0.0f};
        source->alpha = (exit.x2 - exit.x1) * kOneHalf;   // half extents, read by EmitZoneExit
        source->unknown_11C = (exit.y2 - exit.y1) * kOneHalf;
        source->unknown_120 = static_cast<float>(std::cos(static_cast<double>(exit.angle)));
        source->unknown_124 = static_cast<float>(std::sin(static_cast<double>(exit.angle)));
        for (int tick = 0; tick < kExitWarmUpTicks; ++tick) marker->RunTick();
    }
    UpdateExitMarkers();
}

BOOL CWorldClient::PartyInExit(const RecordPtrArray* records, int exit, RecordPtrArray* inExit) {
    BOOL result = 1;
    int living = 0;
    inExit->SetSize(0);
    for (int i = 0; i < records->size; ++i) {
        std::uint8_t* record = (*records)[i];
        CEngineObject* found =
            WorldFindUnit(this, nullptr, static_cast<int>(reinterpret_cast<PartyUnitRecord*>(record)->unknown_01C));
        if (!found || ObjectSlots(found)->IsDead() != 0) continue;
        ++living;
        int index = 0;
        if (!FindExitAt(&static_cast<CUnit*>(found)->position.x, &index) || index != exit) {
            result = 0;
            continue;
        }
        const int last = inExit->size;
        inExit->SetSize(last + 1);
        (*inExit)[last] = record;
    }
    return living != 0 ? result : 0;
}

// ---- The bird and quest variables ----------------------------------------------------------------

void CWorldClient::SummonBird() {
    if (!owned1B0_) {
        auto* memory = static_cast<CEffectMoshka*>(gamecrt::operator_new(sizeof(CEffectMoshka)));
        owned1B0_ = memory ? memory->Construct() : nullptr;
    }
    TimedSlots(owned1B0_)->Reset();
    auto* leader = reinterpret_cast<CPlayer*>(session_)->Party()->members_.data;
    static_cast<CEffectMoshka*>(owned1B0_)->Follow(static_cast<int>(leader->unknown_01C));
}

void CWorldClient::OnQuestVariable(const char* variable, float value) {
    const char* dot = DotText();
    mfc::CString name(variable);
    const int length = name.GetLength();
    const int second = name.Find(dot, name.Find(dot) + 1);
    name = name.Right(length - second - 1);
    for (SectorListNode* node = objects_.First(); node != objects_.End(); node = node->next) {
        auto* object = static_cast<CMapObject*>(node->data);
        if (GameMbscmp(name, object->name2) == 0) object->SetVisibilityLevel(value);
    }
    // FCOMP + TEST AH,0x40: equal or unordered.
    if ((value == 2.0f || x87::IsNaN(value)) && name.Find(dot) < 0) SummonBird();
}

mfc::CString* CWorldClient::ObjectVariableName(mfc::CString* out, const char* name) {
    alignas(mfc::CString) unsigned char storage[sizeof(mfc::CString)];
    mfc::CString* player = LocalPlayerName(reinterpret_cast<mfc::CString*>(storage));
    new (out) mfc::CString(QuestPrefixText());
    *out += *player;
    *out += DotText();
    *out += name;
    player->~CString();
    return out;
}

void CWorldClient::ApplyObjectVariables() {
    auto* variables = reinterpret_cast<NamedFloatTable*>(reinterpret_cast<WorldPlayer*>(session_)->variables);
    for (SectorListNode* node = objects_.First(); node != objects_.End(); node = node->next) {
        auto* object = static_cast<CMapObject*>(node->data);
        const char* name = object->name2;
        if (!name || !*name) continue;
        alignas(mfc::CString) unsigned char storage[sizeof(mfc::CString)];
        mfc::CString* variable = ObjectVariableName(reinterpret_cast<mfc::CString*>(storage), name);
        object->SetVisibilityLevel(variables->Get(*variable));
        variable->~CString();
    }
}

float* CWorldClient::ExitCentre(float* out, int index) {
    const ZoneExit& exit = exits_[index];
    float y = (exit.y2 + exit.y1) * 0.5f;
    float x = (exit.x2 + exit.x1) * 0.5f;
    out[0] = x;
    out[1] = y;
    return out;
}

std::uint32_t CWorldClient::EnvironmentFlags(CEngineObject* unit) {
    std::uint32_t flags = 0;
    switch (static_cast<std::uint32_t>(static_cast<CUnit*>(unit)->moveMode2)) {
    case 0: flags = 8; break;
    case 1: flags = 4; break;
    case 2: flags = 2; break;
    case 3: flags = 1; break;
    }
    static_assert(sizeof(rainSnow_) == sizeof(CRainSnow) && offsetof(CWorldClient, rainSnow_) + offsetof(CRainSnow, mode) == 0x7A8);
    if (GameMbscmp(zoneName_, "gipat") == 0) flags |= 0x10;
    else if (GameMbscmp(zoneName_, "ingos") == 0) flags |= 0x20;
    else if (GameMbscmp(zoneName_, "suslanger") == 0) flags |= 0x40;
    flags |= IsNight() ? 0x100 : 0x80;
    flags |= cave_ == 1 ? 0x400 : 0x200;
    const int weather = RainSnow()->mode;
    if (weather == 1) return flags | 0x800;
    if (weather == 2) return flags | 0x1000;
    return flags | 0x2000;
}

// ---- Segment (005641F0) --------------------------------------------------------------------------

namespace {

// Callees, through their addresses (thiscall as fastcall).
// 005F8070: InterfaceStringEventOriginal (originals_ui.h).
__declspec(naked) void __fastcall SoundSetVolumeA(CSoundManager*, void*, int) EI_ORIGINAL(0x006B7740)
__declspec(naked) void __fastcall SoundSetVolumeB(CSoundManager*, void*, int) EI_ORIGINAL(0x006B78A0)
__declspec(naked) void __fastcall SoundSetVolumeC(CSoundManager*, void*, int) EI_ORIGINAL(0x006B7A00)
__declspec(naked) void __fastcall SoundTick(CSoundManager*) EI_ORIGINAL(0x006B9B10)
// 00665A10 CPlayer::Disconnect on the local session.
void __fastcall SessionDisconnect(WorldSession* session) { PlayerDisconnectOriginal(session); }
__declspec(naked) int __fastcall PendingLookup(PendingMap*, void*, const int*, void**) EI_ORIGINAL(0x0056FC60)
__declspec(naked) void __fastcall PendingListRemoveAt(mfc::CList<CEngineObject*>*, void*, void*) EI_ORIGINAL(0x005704A0)
__declspec(naked) int __fastcall ParticlesTickAt(CParticleManager*, void*, char) EI_ORIGINAL(0x00498480)
__declspec(naked) void __fastcall EffectListErase0(void*, void*, void** /*out*/, void* /*node*/) EI_ORIGINAL(0x00572550)
__declspec(naked) void __fastcall EffectListErase1(void*, void*, void** /*out*/, void* /*node*/) EI_ORIGINAL(0x00572580)
__declspec(naked) void __fastcall EffectListErase2(void*, void*, void** /*out*/, void* /*node*/) EI_ORIGINAL(0x005725B0)
// 0068D730 CLight::GetIntensityAt (spell.h), unrounded in ST0.
__declspec(naked) double __fastcall LightIntensityAt(CLight*, void*, const void*) EI_ORIGINAL(0x0068D730)
__declspec(naked) double __fastcall TerrainHeightAt(Terrain*, void*, float, float, int) EI_ORIGINAL(0x006C8680)
__declspec(naked) void __fastcall MapTick(CTextureManager*) EI_ORIGINAL(0x00694020)

bool NaN(double v) {
    std::uint64_t bits;
    std::memcpy(&bits, &v, 8);
    return (bits & 0x7FF0000000000000ull) == 0x7FF0000000000000ull && (bits & 0x000FFFFFFFFFFFFFull) != 0;
}
// FCOMP a, b; C0 (a < b, or unordered) selects `ifLess`, else `otherwise` (the original's bit trick).
float SelectLess(float a, float b, float ifLess, float otherwise) {
    return (NaN(a) || NaN(b) || a < b) ? ifLess : otherwise;
}

// One effect list: the records whose slot 0 returns 0 are erased (through the list's own erase).
using EraseFn = void(__fastcall*)(void*, void*, void**, void*);
template <typename T>
void RunEffects(stlp::list<T>& list, EraseFn erase) {
    auto* head = list.head;
    for (auto* node = head->next; node != head;) {
        auto* current = node;
        node = node->next;
        if (current->data.Tick() == 0) {
            void* out;
            erase(&list, nullptr, &out, current);
        }
    }
}

// The original's inlined iterator destructor: the manager's count drops without a release.
void EndWalk(ObjectListIterator& it) {
    if (it.manager) {
        --it.manager->refCount_;
        it.manager = nullptr;
    }
}

} // namespace

int CWorldClient::Segment() {
    ObjectListIterator objectsIt{nullptr, nullptr};   // [ebp-0x58]
    int frames = ++ticksRun_;
    if (frames == 10) {
        InterfaceStringEventOriginal(worlddata::InterfaceManager(), nullptr, worlddata::AutosaveEventText());
        SoundSetVolumeA(sounds_, nullptr, worlddata::VolumeSettingA());
        SoundSetVolumeB(sounds_, nullptr, worlddata::VolumeSettingB());
        SoundSetVolumeC(sounds_, nullptr, worlddata::MovieVolumeSetting());
    } else if (frames == 90 || (frames < 90 && worlddata::FightState() == 2)) {
        music_->holding = 0;
    }
    if (terrain_) terrain_->RunTick();
    if (music_) music_->RunTick();
    if (sounds_) SoundTick(sounds_);
    if (!paused_) {
        if (arrayManager_) arrayManager_->RunTick();
    }
    if (shared5C_.ptr) static_cast<CWorldHandle*>(shared5C_.ptr)->Pump();
    if (handle_.ptr) static_cast<CWorldHandle*>(handle_.ptr)->Pump();
    if (void* p = unknown60_) SessionPump(p, nullptr, net::GameClient());
    if (WorldSession* session = Session()) {
        CPlayerConnection* view = session->localView;
        if (!paused_) PlayerTickOriginal(session);
        if (view->Receive() == 0) {
            // The connection is gone. Rebirth's DisableExitOnCrashReport (5 NOPs at 00564330)
            // keeps the session: only the error is reported.
            if (!ei::rebirth::Enabled(ei::rebirth::Patch::DisableExitOnCrashReport)) SessionDisconnect(Session());
            game::FatalError(worlddata::SegmentDisconnectedText());
            EndWalk(objectsIt);
            return 0;
        }
        view->RunTick();
    }
    if (!paused_) {
        ++tick_;
        // The objects (list +0xBC) whose update returns 0 leave the pending map (+0xD8) and list.
        for (auto* node = dynamicObjects_.head; node;) {
            auto* current = node;
            node = node->next;
            CEngineObject* object = current->data;
            int done = object->RunTick();
            if (done != 0) continue;
            int key = object->id_;
            void* position = nullptr;
            if (PendingLookup(&dynamicNodes_, nullptr, &key, &position)) {
                PendingListRemoveAt(&dynamicObjects_, nullptr, position);
                IdMapRemoveKeyOriginal(&dynamicNodes_, nullptr, &key);
            }
        }
        for (CEngineObject* object = objectsIt.First(TimedObjectManager()); object; object = objectsIt.Next())
            object->RunTick();
        if (owned1B0_) owned1B0_->RunTick();
    }
    if (IsNetworkClientOriginal()) {
        if (static_cast<int>(worlddata::PeerList().size()) > 1) {
            volatile float time = static_cast<float>(tick_) + tickFraction_;
            float now = time;
            for (auto* node = dynamicObjects_.head; node;) {
                auto* current = node;
                node = node->next;
                ObjectSlots(current->data)->AdvanceTime(now);
            }
        }
    }
    if (particleManager_) ParticlesTickAt(particleManager_, nullptr, static_cast<char>(paused_));
    if (!paused_) {
        // The three print lists (std::list, sentinel head): finished ones (slot 0 returns 0) go.
        RunEffects(bloodPrints_, EffectListErase0);
        RunEffects(footprints_, EffectListErase1);
        RunEffects(scorchMarks_, EffectListErase2);
        if (!paused_ && worlddata::LightLevelsSetting() != 0) {
            unknown1C4_ -= worlddata::WorldUpdatePeriod();
            if (unknown1C4_ <= 0) {
                unknown1C4_ = worlddata::LightLevelsPeriod();
                // Each blood print gets a grey level: the brightest of the sun at
                // the reference point and the lights whose range reaches the effect's point.
                CLight* sun = worlddata::SceneSun();
                for (auto* node = bloodPrints_.head->next; node != bloodPrints_.head; node = node->next) {
                    BloodPrint& effect = node->data;
                    float level = static_cast<float>(LightIntensityAt(sun, nullptr, worlddata::LightReferencePoint()));
                    ObjectListIterator lights{nullptr, nullptr};   // [ebp-0x60]
                    for (auto* light = static_cast<CLight*>(lights.First(ClientLightManager())); light;
                         light = static_cast<CLight*>(lights.Next())) {
                        if (light == sun || (light->flags_ & 0x80)) continue;
                        float nodeX = effect.position_.x, nodeY = effect.position_.y;
                        double dy = static_cast<double>(light->position_.y) - nodeY;
                        double dx = static_cast<double>(light->position_.x) - nodeX;
                        double squared = dx * dx + dy * dy;
                        double radius = light->rangeSquared_;
                        if (!NaN(squared) && !NaN(radius) && squared > radius) continue;
                        float point[3] = {nodeX, nodeY, 0.0f};
                        point[2] = static_cast<float>(TerrainHeightAt(terrain_, nullptr, nodeX, nodeY, 0));
                        float intensity = static_cast<float>(LightIntensityAt(light, nullptr, point));
                        level = SelectLess(level, intensity, intensity, level);
                    }
                    float clamped = SelectLess(1.0f, level, 1.0f, level);
                    float floor = SelectLess(0.0f, clamped, clamped, 0.0f);
                    volatile float scaled = floor * worlddata::ColourScale();
                    std::uint32_t grey = static_cast<std::uint32_t>(x87::fistp(scaled));
                    std::uint32_t colour = ((((grey | 0xFFFFFF00u) << 8) | grey) << 8) | grey;
                    effect.colour_ = colour;
                    EndWalk(lights);
                }
            }
        }
    }
    if (tick_ % 32 == 0) MapTick(TextureManager());
    EndWalk(objectsIt);
    return 1;
}
EI_REPLACE_METHOD(0x005641F0, CWorldClient::Segment);
EI_REBIRTH_IMPLEMENTS(0x005641F0, ei::rebirth::Patch::DisableExitOnCrashReport);

EI_REPLACE_METHOD(0x00563F70, CWorldClient::ClampedGroundHeight);
EI_REPLACE_METHOD(0x00560F00, PlatformSet::SurfaceHeight);
EI_REPLACE_METHOD(0x00561050, PlatformSet::Contains);
EI_REPLACE_METHOD(0x00561150, PlatformSet::SurfacePlane);
EI_REPLACE_METHOD(0x00560E10, MatrixRows::Transform);
EI_REPLACE_METHOD(0x005640A0, CWorldClient::NearestVisibleUnit);
EI_REPLACE_METHOD(0x00564AF0, CObject3D::PartBox);
EI_REPLACE_METHOD(0x005699F0, CWorldClient::LocalView);
EI_REPLACE_METHOD(0x00566EC0, CWorldClient::IsLocalViewMode2);
EI_REPLACE_METHOD(0x00566EF0, CWorldClient::IsLocalPlayerIdle);
EI_REPLACE_METHOD(0x00567440, CWorldClient::FindExitAt);
EI_REPLACE_METHOD(0x005674C0, CWorldClient::ExitCentre);
EI_REPLACE_METHOD(0x0056D9F0, CWorldClient::EnvironmentFlags);
EI_REPLACE_METHOD(0x00567500, CWorldClient::CreateExitMarkers);
EI_REPLACE_METHOD(0x005677F0, CWorldClient::PartyInExit);
EI_REPLACE_METHOD(0x00566C20, CWorldClient::SummonBird);
EI_REPLACE_METHOD(0x00565380, CWorldClient::OnQuestVariable);
EI_REPLACE_METHOD(0x005654F0, CWorldClient::ObjectVariableName);
EI_REPLACE_METHOD(0x005652E0, CWorldClient::ApplyObjectVariables);
