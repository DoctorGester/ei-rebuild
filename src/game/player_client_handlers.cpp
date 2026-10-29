// CPlayerClient's remaining message handlers and one virtual. See player_client.h and
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
#include "party.h"
#include "player_manager.h"
#include "ui.h"
#include "unit.h"
#include "world.h"
#include "world_effect.h"
#include "lib/bytes.h"
#include "lib/vecmath.h"

#include "game_data.h"
#include "game_singletons.h"

#include <bit>
#include <cstring>

namespace {

constexpr int kStateLoggedIn = 2;
constexpr int kGameStateBetweenZones = 3;
constexpr std::uint32_t kUnitFlagHidden = 0x10;
constexpr std::uint32_t kUnitFlagLit = 0x100;
constexpr int kKeyEnter = 0xD;
constexpr float kSpecialZoneDistance = 23.0f;

// The zone whose camera placement keeps a fixed view ("gz20g").
const char* FixedViewZone() { return "gz20g"; }
// An identity rotation.
constexpr Quaternion kFixedViewRotation = /*007C8388*/ {1.0f, 0.0f, 0.0f, 0.0f};   // the identity (a static initialiser)

CUnit* FindClientUnit(int id) { return g_clientWorld.FindUnit(id); }


} // namespace

int CPlayerClient::OnLoginAccepted() {
    int id;
    in_.ReadValue(id);
    state_ = kStateLoggedIn;
    CPlayer* player = player_;
    static_cast<CPlayerManager*>(player->owner_)->ChangeId(player, id);
    return 1;
}

int CPlayerClient::OnDisconnect() {
    player_->Disconnect();
    return 1;
}

int CPlayerClient::OnCameraPlacement() {
    Vector3 point;
    in_.ReadValue(point);
    float heading;
    in_.ReadValue(heading);
    int mode;
    const int result = static_cast<int>(in_.Read(&mode, 4));
    g_clientWorld.CentreOn(point);
    CCamera* camera = &g_camera;
    if (player_->Party()->zone_ == FixedViewZone()) {
        CameraView view;
        view.distance = kSpecialZoneDistance;
        view.rotation = kFixedViewRotation;
        view.pitch = static_cast<float>(ei::kRadiansPerDegree * 30.0);
        camera->SetDistancePitch(&view);
    } else {
        camera->RestoreView(mode == 3 ? 6 : 7);
    }
    camera->SetHeading(heading);
    return result;
}

int CPlayerClient::OnGameState() {
    CPlayer* player = player_;
    player->lastScreen_ = static_cast<std::uint32_t>(player->sessionKind_);
    int state;
    in_.ReadValue(state);
    int tick;
    in_.ReadValue(tick);
    if (localSession_) {
        g_interface.OnKeyDown(-1, kKeyEnter, 0);
        return 1;
    }
    player->sessionKind_ = state;
    g_interface.CreateScreen(state);
    g_clientWorld.tick_ = tick;
    if (state == kGameStateBetweenZones) g_clientWorld.LoadUnitFaces();
    for (auto node = player->units_.begin(); node != player->units_.end() && node->second; ++node)
        UnitChanged(node->second);
    if (IsMultiplayerClient()) g_gameFlow.UpdateFromServer();
    return 1;
}

int CPlayerClient::OnGameLost() {
    g_interface.OnGameLost();
    return 1;
}

int CPlayerClient::OnUnitSpeech() {
    int values[2];
    in_.Read(values, 8);
    g_interface.OnUnitSpeech(values[0], static_cast<AckKind>(values[1]));
    return 1;
}

int CPlayerClient::OnObjectValues() {
    for (;;) {
        int id;
        in_.ReadValue(id);
        if (id == -1) return 1;
        if (!in_.ok_) return 0;
        CItem* item = FindItem(g_clientWorld.itemManager_, static_cast<unsigned>(id));
        if (!item) continue;   // the value is then read as the next id (kept)
        float value;
        in_.ReadValue(value);
        item->charge_ = value;
    }
}

int CPlayerClient::OnFreeCamera() {
    Vector3 from;
    in_.ReadValue(from);
    Vector3 to;
    in_.ReadValue(to);
    std::uint8_t free = 0;
    in_.ReadValue(free);
    g_camera.LookFrom(from, to);
    g_camera.SetFree(free != 0);
    return 1;
}

int CPlayerClient::OnWorldValue() {
    std::uint8_t bytes[5];
    in_.Read(bytes, 5);
    int value;
    lib::LoadBytes(value, bytes + 1);
    player_->ClientWorld()->SetWorldValue(value);
    return 1;
}

int CPlayerClient::OnItemValue() {
    int id;
    in_.ReadValue(id);
    float value;
    in_.ReadValue(value);
    CPlayer* player = player_;
    CItem* item = FindItem(player->Party()->ItemFactory(), static_cast<unsigned>(id));
    if (item) {
        item->charge_ = value;
        player->ItemUsed(item);
    }
    return 1;
}

int CPlayerClient::OnWeaponRemoved() {
    int unitId;
    in_.ReadValue(unitId);
    int index;
    in_.ReadValue(index);
    // The party (an embedded CParty) seen as its record array.
    PartyUnitRecord* record = player_->Party()->Find(unitId);
    if (record && std::ssize(record->weapons) > index)
        record->RemoveWeapon(record->weapons[index]);
    if (CUnit* unit = FindClientUnit(unitId)) unit->Controller()->ShowWeapon(0xFFFFFFFFu);
    return 1;
}

int CPlayerClient::OnUnitControl() {
    int kind = 0;
    in_.Read(&kind, 1);
    int unitId;
    in_.ReadValue(unitId);
    auto* unit = FindClientUnit(unitId);
    switch (kind) {
    case 0:
        if (unit) g_clientWorld.Select(unit);
        return 1;
    case 1:
    case 2: {
        if (!unit || (unit->flags_ & kUnitFlagHidden)) return 1;
        const bool lit = (unit->flags_ & kUnitFlagLit) != 0;
        if (lit == (kind == 1)) return 1;
        unit->flags_ ^= kUnitFlagLit;
        unit->Object3D()->attached->SetLit(kind == 1);
        return 1;
    }
    case 3:
        g_clientWorld.Deselect(nullptr);
        return 1;
    default:
        return 0;
    }
}

int CPlayerClient::OnObjectChanged() {
    int id;
    in_.ReadValue(id);
    CPlayer::MapObjectMap& known = player_->knownObjects_;
    auto node = known.find(id);
    if (node == known.end()) return 0;
    CMapObject* object = node->second;
    if (object->ReadState(1, &in_) == 0) return 0;
    const ClassId classId = object->GetClassId();
    if (classId == ClassId::Building || classId == ClassId::Lever)
        ClientWorld()->RefreshPlatforms(object);   // the client's own
    return 1;
}

int CPlayerClient::OnObjectRemoved() {
    int id;
    in_.ReadValue(id);
    CPlayer* player = player_;
    if (auto unit = player->units_.find(id); unit != player->units_.end()) unit->second->SetPlayer(nullptr);
    auto node = player->knownObjects_.find(id);
    if (node == player->knownObjects_.end()) return 0;
    CMapObject* object = node->second;
    player->knownObjects_.erase(id);
    object->flags_ |= MapObjectFlag::Removed;
    object->Release();
    g_clientWorld.RemoveObject(object->id_);
    return 1;
}

int CPlayerClient::OnUnitDied() {
    int id;
    in_.ReadValue(id);
    CEngineObject* object = g_clientWorld.objectManager_->FindObject(static_cast<unsigned>(id));
    if (CUnit* unit = UnitOrNull(object)) unit->Controller()->Died();
    return 1;
}

int CPlayerClient::OnTimedObjectAdded() {
    int message[2];   // id, type
    in_.Read(message, 8);
    const int id = message[0];
    CPlayer* player = player_;
    CBaseManager* manager = player->WorldContext()->TimedObjectManager();
    auto* object = manager->CreateObjectAs<CEffect>(message[1], id);
    if (!object) return 0;
    if (!object->Receive(0, &in_)) return 0;
    if (id > player->maxTimedId_) player->maxTimedId_ = id;
    player->knownTimed_[id] = object;
    object->AddRef();
    return 1;
}

int CPlayerClient::OnTimedObjectChanged() {
    int id;
    in_.ReadValue(id);
    CPlayer::TimedObjectMap& known = player_->knownTimed_;
    auto node = known.find(id);
    if (node == known.end()) return 0;
    return node->second->Receive(1, &in_) != 0 ? 1 : 0;
}

int CPlayerClient::OnTimedObjectRemoved() {
    int id;
    in_.ReadValue(id);
    CPlayer* player = player_;
    auto node = player->knownTimed_.find(id);
    if (node == player->knownTimed_.end()) return 0;
    CEffect* object = node->second;
    player->knownTimed_.erase(id);
    object->flags_ |= MapObjectFlag::Removed;
    object->Release();
    return 1;
}

int CPlayerClient::OnWeather() {
    std::uint8_t bytes[3];
    in_.Read(bytes, 3);
    const int strength = bytes[1] | (bytes[2] << 8);
    g_clientWorld.rainSnow_.Start(bytes[0], static_cast<float>(strength));
    return 1;
}

int CPlayerClient::OnWorldTime() {
    float values[2];   // time, speed
    in_.Read(values, 8);
    CWorldClient* world = &g_clientWorld;
    world->CWorld::SetTimeSpeed(values[1]);
    world->CWorld::SetTime(values[0]);
    world->UpdateLighting();
    return 1;
}

int CPlayerClient::OnWorldPoint() {
    Vector3 point;
    in_.ReadValue(point);
    int value;
    in_.ReadValue(value);
    g_clientWorld.ApplyWind(point, std::bit_cast<float>(value));
    return 1;
}

int CPlayerClient::OnWorldStream() {
    g_clientWorld.ReadWaterLevels(&in_);
    return 1;
}

void CPlayerClient::NotifyUnitChanged(CUnit* unit) {
    if (unit->player) g_interface.OnUnitJoined(unit);
    else g_interface.OnUnitLeft(unit);
    if (!IsMultiplayerClient()) return;
    CParty* party = player_->Party();
    if (std::ssize(party->members_) != 1 || static_cast<int>(party->members_[0].unitId) != unit->id_) return;
    auto* login = net::g_gameClient->player_.Get();
    login->id_ = unit->player ? unit->id_ : -1;
    ++login->version_;
}
