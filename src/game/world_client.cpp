// CWorldClient: terrain and platform height queries, unit and exit lookups. See world.h.
#include "texture.h"
#include "world.h"
#include "world_data.h"
#include "ui.h"
#include "hash_tables_400.h"

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
#include "lib/nan_audit.h"
#include "game_data.h"
#include "game_singletons.h"
#include "lib/str.h"

#include <cmath>
#include <cstring>
#include <memory>
#include <new>
#include <string>



// ---- Heights -------------------------------------------------------------------------------------

float CWorldClient::ClampedGroundHeight(float x, float y) {
    // Branch-free (FCOM flags turned into a select mask); NaN picks the first operand of each comparison.
    float cx = 0.0f >= x ? 0.0f : x;
    float maxX = static_cast<float>(terrain_->width - 1.0f);
    cx = maxX >= cx ? cx : maxX;
    float cy = 0.0f >= y ? 0.0f : y;
    float maxY = static_cast<float>(terrain_->height - 1.0f);
    cy = maxY >= cy ? cy : maxY;
    return terrain_->HeightAt(cx, cy, 0);
}

float PlatformSet::SurfaceHeight(const Vector3* point, float height) {
    for (PlatformNode* node = list.head; node; node = node->next) {
        const Platform& p = node->data;
        float dx = point->x - p.x;
        float dy = point->y - p.y;
        EI_NAN_AUDIT(p.radius, dy, dx);
        if (p.radius * p.radius < LengthSquared2DUnrounded(dy, dx)) continue;
        float a = point->x - p.origin.x;
        float b = point->y - p.origin.y;
        float c = point->z - p.origin.z;
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
    for (PlatformNode* node = list.head; node; node = node->next) {
        const Platform& p = node->data;
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
    for (PlatformNode* node = list.head; node; node = node->next) {
        const Platform& p = node->data;
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

// x is summed ZYX, y and z ZXY; every component is computed before the stores (out may be v).
Vector3* MatrixRows::Transform(Vector3* out, const Vector3* v) const {
    *out = TransformColumnNormal<Sum3::ZYX, Sum3::ZXY>(*this, *v);
    return out;
}

// ---- Units and players ---------------------------------------------------------------------------

CUnit* CWorldClient::NearestVisibleUnit(CUnit* unit) {
    CPlayerClient* view = LocalView();
    if (!view || !unit) return nullptr;
    float best = 1e38f;
    CUnit* nearest = nullptr;
    for (int i = 0; i < view->partyUnitIds_.size; ++i) {
        CUnit* found = FindUnit(view->partyUnitIds_[i]);
        if (!found || found->GetClassId() != world::kClassUnit) continue;
        if (found->IsDead() != 0) continue;
        std::uint32_t side = 1u << (unit->side & 31);
        if ((found->relationsA & (side & found->relationsB)) == 0) continue;
        float dy = found->localPosition_.y - unit->localPosition_.y;
        float dx = found->localPosition_.x - unit->localPosition_.x;
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
    CFigureMesh* part = index < parts.size ? parts.data[index] : pendingParts.data[index - parts.size];
    return &part->box_;
}

CPlayerClient* CWorldClient::LocalView() {
    if (session_) return static_cast<CPlayerClient*>(Session()->connection_);
    return nullptr;
}

bool CWorldClient::IsLocalViewMode2() {
    CPlayerClient* view = LocalView();
    if (!view) return false;
    return view->state_ == 2;
}

bool CWorldClient::IsLocalPlayerIdle() {
    if (!session_) return false;
    CPlayerConnection* view = Session()->connection_;
    if (!view) return false;
    netlink::NetLink* link = view->player_->link_;
    if (link && !(link->flags7_ & CRefObject::kDetached) && link->Alive()) return false;
    return true;
}

// ---- Exits ---------------------------------------------------------------------------------------

BOOL CWorldClient::FindExitAt(const float* point, int* index) {
    for (int i = 0; i < exits_.size; ++i) {
        if (!exits_[i].Contains(point[0], point[1])) continue;
        if (gamecrt::mbscmp(exits_[i].target, "none") == 0) continue;
        if (index) *index = i;
        return 1;
    }
    return 0;
}

namespace {

constexpr int kExitParticles = 0x2038;   // "ZoneExit" (particle_emitters.cpp EmitZoneExit)
constexpr int kExitWarmUpTicks = 20;
constexpr float kOneHalf = 0.5f;
const char* DotText() { return "."; }
const char* QuestPrefixText() { return "q."; }

// Library: counted pointer assignment: references `object`, then releases the slot's old one.
template <class T>
void AssignCounted(T** slot, T* object) {
    T* old = *slot;
    *slot = object;
    if (object) ++object->refCount_;
    if (old) old->Release();
}

} // namespace

void CWorldClient::CreateExitMarkers() {
    if (countedA0_.data) {
        for (int i = 0; i < countedA0_.size; ++i) {
            if (countedA0_[i]) countedA0_[i]->Release();
        }
    }
    countedA0_.SetSize(0);
    countedA0_.SetSize(exits_.size);
    for (int i = 0; i < exits_.size; ++i) {
        if (gamecrt::mbsicmp(exits_[i].target,
                            game::NoneText()) == 0) {
            continue;
        }
        auto* source = TheParticleManager()->CreateSource(kExitParticles);
        AssignCounted(&countedA0_[i], source);
        const ZoneExit& exit = exits_[i];
        source->localPosition_ = {(exit.x2 + exit.x1) * kOneHalf, (exit.y2 + exit.y1) * kOneHalf, 0.0f};
        source->alpha = (exit.x2 - exit.x1) * kOneHalf;   // half extents, read by EmitZoneExit
        source->typeParam = (exit.y2 - exit.y1) * kOneHalf;
        source->turnCos = static_cast<float>(x87::fcos(static_cast<double>(exit.angle)));
        source->turnSin = static_cast<float>(x87::fsin(static_cast<double>(exit.angle)));
        for (int tick = 0; tick < kExitWarmUpTicks; ++tick) source->RunTick();
    }
    UpdateExitMarkers();
}

BOOL CWorldClient::PartyInExit(const RecordPtrArray* records, int exit, RecordPtrArray* inExit) {
    BOOL result = 1;
    int living = 0;
    inExit->SetSize(0);
    for (int i = 0; i < records->size; ++i) {
        PartyUnitRecord* record = (*records)[i];
        CUnit* found =
            FindUnit(static_cast<int>(record->unitId));
        if (!found || found->IsDead() != 0) continue;
        ++living;
        int index = 0;
        if (!FindExitAt(&found->localPosition_.x, &index) || index != exit) {
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
    if (!bird_) bird_ = new CEffectMoshka();
    bird_->Reset();
    auto* leader = session_->Party()->members_.data;
    bird_->Follow(static_cast<int>(leader->unitId));
}

void CWorldClient::OnQuestVariable(const char* variable, float value) {
    const char* dot = DotText();
    std::string name = variable;
    const int length = name.size();
    const int second = str::Find(name, dot, str::Find(name, dot) + 1);
    name = str::Right(name, length - second - 1);
    for (MapObjectList::Node* node = objects_.First(); node != objects_.End(); node = node->next) {
        CMapObject* object = node->data;
        if (gamecrt::mbscmp(name, object->name2) == 0) object->SetVisibilityLevel(value);
    }
    // The original's test counts a NaN as equal; the audit checks that none arrives.
    EI_NAN_AUDIT(value);
    if (value == 2.0f && str::Find(name, dot) < 0) SummonBird();
}

std::string CWorldClient::ObjectVariableName(const char* name) {
    std::string variable = QuestPrefixText();
    variable += LocalPlayerZone();
    variable += DotText();
    variable += name;
    return variable;
}

void CWorldClient::ApplyObjectVariables() {
    NamedFloatTable* variables = &session_->Party()->variables_;
    for (MapObjectList::Node* node = objects_.First(); node != objects_.End(); node = node->next) {
        CMapObject* object = node->data;
        const char* name = object->name2.c_str();
        if (!name || !*name) continue;
        object->SetVisibilityLevel(variables->Get(ObjectVariableName(name).c_str()));
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

std::uint32_t CWorldClient::EnvironmentFlags(CUnit* unit) {
    std::uint32_t flags = 0;
    switch (static_cast<std::uint32_t>(unit->moveMode2)) {
    case 0: flags = 8; break;
    case 1: flags = 4; break;
    case 2: flags = 2; break;
    case 3: flags = 1; break;
    }
    if (gamecrt::mbscmp(zoneName_, "gipat") == 0) flags |= 0x10;
    else if (gamecrt::mbscmp(zoneName_, "ingos") == 0) flags |= 0x20;
    else if (gamecrt::mbscmp(zoneName_, "suslanger") == 0) flags |= 0x40;
    flags |= IsNight() ? 0x100 : 0x80;
    flags |= cave_ == 1 ? 0x400 : 0x200;
    const int weather = rainSnow_.mode;
    if (weather == 1) return flags | 0x800;
    if (weather == 2) return flags | 0x1000;
    return flags | 0x2000;
}

// ---- Segment --------------------------------------------------------------------------

namespace {

void __fastcall SessionDisconnect(CPlayer* session) { session->Disconnect(); }

// `ifLess` when a < b or unordered, else `otherwise`.
float SelectLess(float a, float b, float ifLess, float otherwise) {
    return a >= b ? otherwise : ifLess;
}

// One effect list: the records whose tick returns 0 are erased (through the list's own erase).
template <typename T>
using EraseFn = void**(__fastcall*)(stlp::list<T>*, void*, void**, typename stlp::list<T>::Node*);
template <typename T>
void RunEffects(stlp::list<T>& list, EraseFn<T> erase) {
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

void EndWalk(ObjectListIterator& it) {
    if (it.manager) {
        --it.manager->refCount_;
        it.manager = nullptr;
    }
}

// erase(pos) of a print list (nodes from operator new).
template <typename T>
void** ErasePrint(void** out, typename stlp::list<T>::Node* pos) {
    auto* next = pos->next;
    auto* prev = pos->prev;
    prev->next = next;
    next->prev = prev;
    pos->data.GroundPrint::Destruct();
    gamecrt::operator_delete(pos);
    *out = next;
    return out;
}

} // namespace

void** CWorldClient::EraseBloodPrint(stlp::list<BloodPrint>*, void*, void** out, stlp::list<BloodPrint>::Node* pos) {
    return ErasePrint<BloodPrint>(out, pos);
}
void** CWorldClient::EraseFootprint(stlp::list<Footprint>*, void*, void** out, stlp::list<Footprint>::Node* pos) {
    return ErasePrint<Footprint>(out, pos);
}
void** CWorldClient::EraseScorchMark(stlp::list<GroundPrint>*, void*, void** out, stlp::list<GroundPrint>::Node* pos) {
    return ErasePrint<GroundPrint>(out, pos);
}
EI_REWRITTEN(0x00572550, CWorldClient::EraseBloodPrint);
EI_REWRITTEN(0x00572580, CWorldClient::EraseFootprint);
EI_REWRITTEN(0x005725B0, CWorldClient::EraseScorchMark);

int CWorldClient::Segment() {
    ObjectListIterator objectsIt{nullptr, nullptr};
    int frames = ++ticksRun_;
    if (frames == 10) {
        TheInterface()->OnStringEvent(worlddata::AutosaveEventText());
        sounds_->SetEffectsVolume(worlddata::VolumeSettingA());
        sounds_->SetSpeechVolume(worlddata::VolumeSettingB());
        sounds_->SetMusicVolume(worlddata::MovieVolumeSetting());
    } else if (frames == 90 || (frames < 90 && worlddata::FightState() == 2)) {
        music_->holding = 0;
    }
    if (terrain_) terrain_->RunTick();
    if (music_) music_->RunTick();
    if (sounds_) sounds_->Tick();
    if (!paused_) {
        if (arrayManager_) arrayManager_->RunTick();
    }
    if (CWorldHandle* source = shared5C_.Get()) source->Pump();
    if (CClientHandle* source = handle_.Get()) source->Pump();
    if (netlink::DriverSession* session = netSession_.Get()) session->PumpDrivers(net::GameClient());
    if (CPlayer* session = Session()) {
        CPlayerConnection* view = session->connection_;
        if (!paused_) session->Tick();
        if (view->Receive() == 0) {
            // The connection is gone. Rebirth's DisableExitOnCrashReport
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
        // The objects (dynamicObjects_) whose update returns 0 leave dynamicNodes_ and the list.
        for (auto* node = dynamicObjects_.head; node;) {
            auto* current = node;
            node = node->next;
            CMapObject* object = current->data;
            int done = object->RunTick();
            if (done != 0) continue;
            auto key = static_cast<std::uint32_t>(object->id_);
            mfc::CList<CMapObject*>::Node* position = nullptr;
            if (dynamicNodes_.Lookup(&key, &position)) {
                dynamicObjects_.RemoveAt(position);
                hash400::RemoveKey(dynamicNodes_, key);
            }
        }
        for (CEngineObject* object = objectsIt.First(TimedObjectManager()); object; object = objectsIt.Next())
            object->RunTick();
        if (bird_) bird_->RunTick();
    }
    if (IsMultiplayerClient()) {
        if (static_cast<int>(TheInterface()->stack_.Finish() - TheInterface()->stack_.Start()) > 1) {
            float time = static_cast<float>(tick_) + tickFraction_;
            float now = time;
            for (auto* node = dynamicObjects_.head; node;) {
                auto* current = node;
                node = node->next;
                current->data->AdvanceTime(now);
            }
        }
    }
    if (particleManager_) particleManager_->TickSources(static_cast<char>(paused_));
    if (!paused_) {
        // The three print lists (std::list, sentinel head): finished ones (tick returns 0) go.
        RunEffects(bloodPrints_, &EraseBloodPrint);
        RunEffects(footprints_, &EraseFootprint);
        RunEffects(scorchMarks_, &EraseScorchMark);
        if (!paused_ && worlddata::LightLevelsSetting() != 0) {
            lightLevelsCountdown_ -= worlddata::WorldUpdatePeriod();
            if (lightLevelsCountdown_ <= 0) {
                lightLevelsCountdown_ = worlddata::LightLevelsPeriod();
                // Each blood print gets a grey level: the brightest of the sun at
                // the reference point and the lights whose range reaches the effect's point.
                CLight* sun = worlddata::SceneSun();
                for (auto* node = bloodPrints_.head->next; node != bloodPrints_.head; node = node->next) {
                    BloodPrint& effect = node->data;
                    float level = sun->GetIntensityAt(&worlddata::kOrigin);
                    ObjectListIteratorOf<CLight> lights{};
                    for (CLight* light = lights.First(ClientLightManager()); light; light = lights.Next()) {
                        if (light == sun || (light->flags_ & 0x80)) continue;
                        float nodeX = effect.position_.x, nodeY = effect.position_.y;
                        double dy = static_cast<double>(light->localPosition_.y) - nodeY;
                        double dx = static_cast<double>(light->localPosition_.x) - nodeX;
                        double squared = LengthSquared2DUnrounded(dx, dy);
                        double radius = light->rangeSquared_;
                        if (squared > radius) continue;
                        Vector3 point = {nodeX, nodeY, 0.0f};
                        point.z = static_cast<float>(terrain_->HeightAt(nodeX, nodeY, 0));
                        float intensity = light->GetIntensityAt(&point);
                        level = SelectLess(level, intensity, intensity, level);
                    }
                    float clamped = SelectLess(1.0f, level, 1.0f, level);
                    float floor = SelectLess(0.0f, clamped, clamped, 0.0f);
                    float scaled = floor * worlddata::ColourScale();
                    std::uint32_t grey = static_cast<std::uint32_t>(x87::fistp(scaled));
                    std::uint32_t colour = ((((grey | 0xFFFFFF00u) << 8) | grey) << 8) | grey;
                    effect.colour_ = colour;
                    EndWalk(lights);
                }
            }
        }
    }
    if (tick_ % 32 == 0) TextureManager()->TrimCache();
    EndWalk(objectsIt);
    return 1;
}
EI_REWRITTEN(0x005641F0, CWorldClient::Segment);

EI_REWRITTEN(0x00563F70, CWorldClient::ClampedGroundHeight);
EI_REWRITTEN(0x00560F00, PlatformSet::SurfaceHeight);
EI_REWRITTEN(0x00561050, PlatformSet::Contains);
EI_REWRITTEN(0x00561150, PlatformSet::SurfacePlane);
EI_REWRITTEN(0x00560E10, MatrixRows::Transform);
EI_REWRITTEN(0x005640A0, CWorldClient::NearestVisibleUnit);
EI_REWRITTEN(0x00564AF0, CObject3D::PartBox);
EI_REWRITTEN(0x005699F0, CWorldClient::LocalView);
EI_REWRITTEN(0x00566EC0, CWorldClient::IsLocalViewMode2);
EI_REWRITTEN(0x00566EF0, CWorldClient::IsLocalPlayerIdle);
EI_REWRITTEN(0x00567440, CWorldClient::FindExitAt);
EI_REWRITTEN(0x005674C0, CWorldClient::ExitCentre);
EI_REWRITTEN(0x0056D9F0, CWorldClient::EnvironmentFlags);
EI_REWRITTEN(0x00567500, CWorldClient::CreateExitMarkers);
EI_REWRITTEN(0x005677F0, CWorldClient::PartyInExit);
EI_REWRITTEN(0x00566C20, CWorldClient::SummonBird);
EI_REWRITTEN(0x00565380, CWorldClient::OnQuestVariable);
EI_REWRITTEN(0x005654F0, CWorldClient::ObjectVariableName);
EI_REWRITTEN(0x005652E0, CWorldClient::ApplyObjectVariables);
