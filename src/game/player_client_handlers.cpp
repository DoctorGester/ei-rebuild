// CPlayerClient's remaining message handlers and its slot 14 (A-H23). See player_client.h and
// docs/notes/objects.md ("Players").
#include "effects.h"
#include "player_client.h"

#include "angles.h"
#include "camera.h"
#include "item.h"
#include "map_object.h"
#include "map_object_figures.h"
#include "net_classes.h"
#include "net_messages.h"
#include "originals_ui.h"
#include "originals_units.h"
#include "originals_world.h"
#include "party.h"
#include "player_manager.h"
#include "ui.h"
#include "unit.h"
#include "world.h"
#include "world_effect.h"
#include "lib/crt.h"
#include "lib/vecmath.h"

#include "ei/hook.h"
#include "game_data.h"
#include "game_singletons.h"

#include <cstring>

namespace {

constexpr int kStateLoggedIn = 2;
constexpr int kGameStateBetweenZones = 3;
constexpr int kUnitClassId = 0x50;
constexpr int kPlatformClassA = 0x54;
constexpr int kPlatformClassB = 0x5D;
constexpr std::uint32_t kFlagGone = 1;              // a removed object's flags_
constexpr std::uint32_t kUnitFlagHidden = 0x10;
constexpr std::uint32_t kUnitFlagLit = 0x100;
constexpr int kKeyEnter = 0xD;
constexpr float kSpecialZoneDistance = 23.0f;

int& WorldTick() { return ei::GameData<int>(0x007AF61C); }
CBaseManager* ItemManager() { return ei::GameData<CBaseManager* const>(0x007AF60C); }
void* Weather() { return ei::GamePointer<void>(0x007AFCAC); }   // "RainSnow"
// The zone whose camera placement keeps a fixed view ("gz20g").
const char* FixedViewZone() { return "gz20g"; }
// 007C8388: an identity rotation (set by the static initialiser 00666CC0).
const Quaternion& FixedViewRotation() { return ei::GameData<const Quaternion>(0x007C8388); }
// pi (00743148), 1/180 (0073BA30) and 30 (0073DF70): the fixed view's pitch, read from the exe.

CUnit* FindClientUnit(int id) { return WorldFindUnit(TheClientWorld(), nullptr, id); }

// CPlayerClient is a CPlayerConnection (vtable 00743180), not declared as one.
CPlayerConnection* AsConnection(CPlayerClient* client) { return reinterpret_cast<CPlayerConnection*>(client); }

} // namespace

int CPlayerClient::OnLoginAccepted() {
    int id;
    in_.Read(&id, 4);
    state_ = kStateLoggedIn;
    CPlayer* player = Owner();
    static_cast<CPlayerManager*>(player->owner_)->ChangeId(player, id);
    return 1;
}

int CPlayerClient::OnDisconnect() {
    Owner()->Disconnect();
    return 1;
}

int CPlayerClient::OnCameraPlacement() {
    Vector3 point;
    in_.Read(&point, 12);
    float heading;
    in_.Read(&heading, 4);
    int mode;
    const int result = static_cast<int>(in_.Read(&mode, 4));
    TheClientWorld()->CentreOn(&point);
    CCamera* camera = TheCamera();
    const char* zone = Owner()->Party()->zone_;
    if (gamecrt::mbscmp(zone,
                        FixedViewZone()) == 0) {
        CameraView view;
        view.distance = kSpecialZoneDistance;
        view.rotation = FixedViewRotation();
        view.pitch = static_cast<float>(ei::kRadiansPerDegree * 30.0 /*0073DF70*/);
        camera->SetDistancePitch(&view);
    } else {
        camera->RestoreView(mode == 3 ? 6 : 7);
    }
    camera->SetHeading(heading);
    return result;
}

int CPlayerClient::OnGameState() {
    CPlayer* player = Owner();
    player->lastScreen_ = static_cast<std::uint32_t>(player->sessionKind100_);
    int state;
    in_.Read(&state, 4);
    int tick;
    in_.Read(&tick, 4);
    if (localSession_) {
        TheInterface()->OnKeyDown(-1, kKeyEnter, 0);
        return 1;
    }
    player->sessionKind100_ = state;
    TheInterface()->CreateScreen(state);
    WorldTick() = tick;
    if (state == kGameStateBetweenZones) TheClientWorld()->LoadUnitFaces();
    for (CPlayer::ObjectMap::Node* node = player->units_.First(); node && node->value; node = player->units_.After(node))
        AsConnection(this)->UnitChanged(static_cast<CUnit*>(node->value));
    if (IsMultiplayerClient()) FlowUpdateFromServerOriginal(TheGameFlow());
    return 1;
}

int CPlayerClient::OnGameLost() {
    TheInterface()->OnGameLost();
    return 1;
}

int CPlayerClient::OnUnitSpeech() {
    int values[2];
    in_.Read(values, 8);
    TheInterface()->OnUnitSpeech(values[0], static_cast<unsigned>(values[1]));
    return 1;
}

int CPlayerClient::OnObjectValues() {
    for (;;) {
        int id;
        in_.Read(&id, 4);
        if (id == -1) return 1;
        if (!in_.ok_) return 0;
        auto* item = static_cast<CItem*>(ItemManager()->FindObject(static_cast<unsigned>(id)));
        if (!item) continue;   // the value is then read as the next id (kept)
        float value;
        in_.Read(&value, 4);
        item->charge_ = value;
    }
}

int CPlayerClient::OnFreeCamera() {
    Vector3 from;
    in_.Read(&from, 12);
    Vector3 to;
    in_.Read(&to, 12);
    std::uint8_t free = 0;
    in_.Read(&free, 1);
    TheCamera()->LookFrom(&from, &to, 0);
    TheCamera()->SetFree(free != 0);
    return 1;
}

int CPlayerClient::OnWorldValue() {
    std::uint8_t bytes[5];
    in_.Read(bytes, 5);
    int value;
    std::memcpy(&value, bytes + 1, 4);
    static_cast<CWorldClient*>(Owner()->ManagerContext())->SetWorldValue(value);
    return 1;
}

int CPlayerClient::OnItemValue() {
    int id;
    in_.Read(&id, 4);
    float value;
    in_.Read(&value, 4);
    CPlayer* player = Owner();
    auto* item = static_cast<CItem*>(player->Party()->ItemManager()->FindObject(static_cast<unsigned>(id)));
    if (item) {
        item->charge_ = value;
        player->ItemUsed(item);
    }
    return 1;
}

int CPlayerClient::OnWeaponRemoved() {
    int unitId;
    in_.Read(&unitId, 4);
    int index;
    in_.Read(&index, 4);
    // The party (an embedded CParty) seen as its record array.
    PartyUnitRecord* record = reinterpret_cast<PartyRecords*>(Owner()->Party())->Find(unitId);
    if (record && static_cast<int>(record->weaponCount) > index)
        record->RemoveWeapon(static_cast<CItem*>(record->weapons[index]));
    if (CUnit* unit = FindClientUnit(unitId)) unit->Controller()->ShowWeapon(0xFFFFFFFFu);
    return 1;
}

int CPlayerClient::OnUnitControl() {
    int kind = 0;
    in_.Read(&kind, 1);
    int unitId;
    in_.Read(&unitId, 4);
    auto* unit = FindClientUnit(unitId);
    switch (kind) {
    case 0:
        if (unit) static_cast<CWorldClient*>(TheClientWorld())->Select(unit);
        return 1;
    case 1:
    case 2: {
        if (!unit || (unit->flags_ & kUnitFlagHidden)) return 1;
        const bool lit = (unit->flags_ & kUnitFlagLit) != 0;
        if (lit == (kind == 1)) return 1;
        unit->flags_ ^= kUnitFlagLit;
        static_cast<CObject3DSpecific*>(unit->Object3D()->attached)->SetLit(kind == 1 ? 1 : 0);
        return 1;
    }
    case 3:
        TheClientWorld()->Deselect(nullptr);
        return 1;
    default:
        return 0;
    }
}

int CPlayerClient::OnObjectChanged() {
    int id;
    in_.Read(&id, 4);
    CPlayer::ObjectMap::Node* node = Owner()->knownObjects_.Find(id);
    if (!node) return 0;
    CEngineObject* object = node->value;
    if (static_cast<CMapObject*>(object)->ReadState(1, &in_) == 0) return 0;
    const int classId = object->GetClassId();
    if (classId == kPlatformClassA || classId == kPlatformClassB)
        static_cast<CWorldClient*>(AsConnection(this)->ManagerContext())->RefreshPlatforms(object);   // the client's own
    return 1;
}

int CPlayerClient::OnObjectRemoved() {
    int id;
    in_.Read(&id, 4);
    CPlayer* player = Owner();
    if (CPlayer::ObjectMap::Node* unit = player->units_.Find(id)) static_cast<CUnit*>(unit->value)->SetPlayer(nullptr);
    CPlayer::ObjectMap::Node* node = player->knownObjects_.Find(id);
    if (!node) return 0;
    CEngineObject* object = node->value;
    player->knownObjects_.Erase(player->knownObjects_.Find(id));
    object->flags_ |= kFlagGone;
    object->Release();
    TheClientWorld()->RemoveObject(object->id_);
    return 1;
}

int CPlayerClient::OnUnitDied() {
    int id;
    in_.Read(&id, 4);
    CEngineObject* object = TheObjectManager()->FindObject(static_cast<unsigned>(id));
    if (object && object->GetClassId() == kUnitClassId) static_cast<CUnit*>(object)->Controller()->Died();
    return 1;
}

int CPlayerClient::OnTimedObjectAdded() {
    int message[2];   // id, type
    in_.Read(message, 8);
    const int id = message[0];
    CPlayer* player = Owner();
    CBaseManager* manager = static_cast<CWorld*>(player->ManagerContext())->TimedObjectManager();
    auto* object = static_cast<CEffect*>(manager->CreateObject(message[1], id));
    if (!object) return 0;
    if (!object->Receive(0, &in_)) return 0;
    if (id > player->maxTimedId_) player->maxTimedId_ = id;
    player->knownTimed_[id] = object;
    ++object->refCount_;
    return 1;
}

int CPlayerClient::OnTimedObjectChanged() {
    int id;
    in_.Read(&id, 4);
    CPlayer::ObjectMap::Node* node = Owner()->knownTimed_.Find(id);
    if (!node) return 0;
    return static_cast<CEffect*>(node->value)->Receive(1, &in_) != 0 ? 1 : 0;
}

int CPlayerClient::OnTimedObjectRemoved() {
    int id;
    in_.Read(&id, 4);
    CPlayer* player = Owner();
    CPlayer::ObjectMap::Node* node = player->knownTimed_.Find(id);
    if (!node) return 0;
    CEngineObject* object = node->value;
    player->knownTimed_.Erase(player->knownTimed_.Find(id));
    object->flags_ |= kFlagGone;
    object->Release();
    return 1;
}

int CPlayerClient::OnWeather() {
    std::uint8_t bytes[3];
    in_.Read(bytes, 3);
    const int strength = bytes[1] | (bytes[2] << 8);
    static_cast<CRainSnow*>(Weather())->Start(bytes[0], static_cast<float>(strength));
    return 1;
}

int CPlayerClient::OnWorldTime() {
    float values[2];   // time, speed
    in_.Read(values, 8);
    CWorldClient* world = TheClientWorld();
    world->CWorld::SetTimeSpeedImpl(values[1]);
    world->CWorld::SetTimeImpl(values[0]);
    world->UpdateLighting();
    return 1;
}

int CPlayerClient::OnWorldPoint() {
    Vector3 point;
    in_.Read(&point, 12);
    int value;
    in_.Read(&value, 4);
    TheClientWorld()->ApplyWind(&point, std::bit_cast<float>(value));
    return 1;
}

int CPlayerClient::OnWorldStream() {
    TheClientWorld()->ReadWaterLevels(&in_);
    return 1;
}

void CPlayerClient::NotifyUnitChanged(CUnit* unit) {
    if (unit->player) TheInterface()->OnUnitJoined(unit);
    else TheInterface()->OnUnitLeft(unit);
    if (!IsMultiplayerClient()) return;
    CParty* party = Owner()->Party();
    if (party->members_.size != 1 || static_cast<int>(party->members_.data[0].unitId) != unit->id_) return;
    auto* login = static_cast<net::LoginMessage*>(net::GameClient()->player_.ptr);
    login->id_ = unit->player ? unit->id_ : -1;
    ++login->version_;
}

EI_REPLACE_METHOD(0x00667DA0, CPlayerClient::OnLoginAccepted);
EI_REPLACE_METHOD(0x00667DE0, CPlayerClient::OnDisconnect);
EI_REPLACE_METHOD(0x00667C20, CPlayerClient::OnCameraPlacement);
EI_REPLACE_METHOD(0x00669D50, CPlayerClient::OnGameState);
EI_REPLACE_METHOD(0x00669F30, CPlayerClient::OnGameLost);
EI_REPLACE_METHOD(0x00667DF0, CPlayerClient::OnUnitSpeech);
EI_REPLACE_METHOD(0x006696A0, CPlayerClient::OnObjectValues);
EI_REPLACE_METHOD(0x00667D10, CPlayerClient::OnFreeCamera);
EI_REPLACE_METHOD(0x00667E20, CPlayerClient::OnWorldValue);
EI_REPLACE_METHOD(0x0066A420, CPlayerClient::OnItemValue);
EI_REPLACE_METHOD(0x00669400, CPlayerClient::OnWeaponRemoved);
EI_REPLACE_METHOD(0x0066A620, CPlayerClient::OnUnitControl);
EI_REPLACE_METHOD(0x006687B0, CPlayerClient::OnObjectChanged);
EI_REPLACE_METHOD(0x00668850, CPlayerClient::OnObjectRemoved);
EI_REPLACE_METHOD(0x0066A3B0, CPlayerClient::OnUnitDied);
EI_REPLACE_METHOD(0x00668940, CPlayerClient::OnTimedObjectAdded);
EI_REPLACE_METHOD(0x00668A30, CPlayerClient::OnTimedObjectChanged);
EI_REPLACE_METHOD(0x00668AA0, CPlayerClient::OnTimedObjectRemoved);
EI_REPLACE_METHOD(0x00669760, CPlayerClient::OnWeather);
EI_REPLACE_METHOD(0x006697B0, CPlayerClient::OnWorldTime);
EI_REPLACE_METHOD(0x00669800, CPlayerClient::OnWorldPoint);
EI_REPLACE_METHOD(0x0066A400, CPlayerClient::OnWorldStream);
EI_REPLACE_METHOD(0x00667BA0, CPlayerClient::NotifyUnitChanged);
