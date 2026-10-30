// PlayerClient's remaining message handlers and one virtual. See player_client.h and
// docs/notes/objects.md ("Players").
#include "effects.h"
#include "item_manager.h"
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



} // namespace

int PlayerClient::OnLoginAccepted() {
    int objectId;
    in.ReadValue(objectId);
    state = kStateLoggedIn;
    CPlayer* currentPlayer = player;
    static_cast<PlayerManager*>(currentPlayer->owner)->ChangeId(currentPlayer, objectId);
    return 1;
}

int PlayerClient::OnDisconnect() {
    player->Disconnect();
    return 1;
}

int PlayerClient::OnCameraPlacement() {
    Vector3 point;
    in.ReadValue(point);
    float heading;
    in.ReadValue(heading);
    int mode;
    const int result = static_cast<int>(in.Read(&mode, 4));
    g_clientWorld.CentreOn(point);
    Camera* camera = &g_camera;
    if (player->GetParty()->zone == FixedViewZone()) {
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

int PlayerClient::OnGameState() {
    CPlayer* currentPlayer = player;
    currentPlayer->lastScreen = static_cast<std::uint32_t>(currentPlayer->sessionKind);
    int gameState;
    in.ReadValue(gameState);
    int tick;
    in.ReadValue(tick);
    if (localSession) {
        g_interface.OnKeyDown(-1, kKeyEnter, 0);
        return 1;
    }
    currentPlayer->sessionKind = gameState;
    g_interface.CreateScreen(gameState);
    g_clientWorld.tick = tick;
    if (gameState == kGameStateBetweenZones) g_clientWorld.LoadUnitFaces();
    for (auto node = currentPlayer->units.begin(); node != currentPlayer->units.end() && node->second; ++node)
        UnitChanged(node->second);
    if (IsMultiplayerClient()) g_gameFlow.UpdateFromServer();
    return 1;
}

int PlayerClient::OnGameLost() {
    g_interface.OnGameLost();
    return 1;
}

int PlayerClient::OnUnitSpeech() {
    int values[2];
    in.Read(values, 8);
    g_interface.OnUnitSpeech(values[0], static_cast<AckKind>(values[1]));
    return 1;
}

int PlayerClient::OnObjectValues() {
    for (;;) {
        int objectId;
        in.ReadValue(objectId);
        if (objectId == -1) return 1;
        if (!in.ok) return 0;
        Item* item = FindItem(g_clientWorld.itemManager, static_cast<unsigned>(objectId));
        if (!item) continue;   // the value is then read as the next id (kept)
        float value;
        in.ReadValue(value);
        item->charge = value;
    }
}

int PlayerClient::OnFreeCamera() {
    Vector3 from;
    in.ReadValue(from);
    Vector3 to;
    in.ReadValue(to);
    std::uint8_t free = 0;
    in.ReadValue(free);
    g_camera.LookFrom(from, to);
    g_camera.SetFree(free != 0);
    return 1;
}

int PlayerClient::OnWorldValue() {
    std::uint8_t bytes[5];
    in.Read(bytes, 5);
    int value;
    lib::LoadBytes(value, bytes + 1);
    player->ClientWorld()->SetWorldValue(value);
    return 1;
}

int PlayerClient::OnItemValue() {
    int objectId;
    in.ReadValue(objectId);
    float value;
    in.ReadValue(value);
    CPlayer* currentPlayer = player;
    Item* item = FindItem(currentPlayer->GetParty()->ItemFactory(), static_cast<unsigned>(objectId));
    if (item) {
        item->charge = value;
        currentPlayer->ItemUsed(item);
    }
    return 1;
}

int PlayerClient::OnWeaponRemoved() {
    int unitId;
    in.ReadValue(unitId);
    int index;
    in.ReadValue(index);
    // The party (an embedded CParty) seen as its record array.
    PartyUnitRecord* record = player->GetParty()->Find(unitId);
    if (record && std::ssize(record->weapons) > index)
        record->RemoveWeapon(record->weapons[index]);
    if (CUnit* unit = g_clientWorld.FindUnit(unitId)) unit->Controller()->ShowWeapon(0xFFFFFFFFu);
    return 1;
}

int PlayerClient::OnUnitControl() {
    int kind = 0;
    in.Read(&kind, 1);
    int unitId;
    in.ReadValue(unitId);
    auto* unit = g_clientWorld.FindUnit(unitId);
    switch (kind) {
    case 0:
        if (unit) g_clientWorld.Select(unit);
        return 1;
    case 1:
    case 2: {
        if (!unit || (unit->flags & kUnitFlagHidden)) return 1;
        const bool lit = (unit->flags & kUnitFlagLit) != 0;
        if (lit == (kind == 1)) return 1;
        unit->flags ^= kUnitFlagLit;
        unit->AsObject3D()->attached->SetLit(kind == 1);
        return 1;
    }
    case 3:
        g_clientWorld.Deselect();
        return 1;
    default:
        return 0;
    }
}

int PlayerClient::OnObjectChanged() {
    int objectId;
    in.ReadValue(objectId);
    CPlayer::MapObjectMap& known = player->knownObjects;
    auto node = known.find(objectId);
    if (node == known.end()) return 0;
    CMapObject* object = node->second;
    if (object->ReadState(1, &in) == 0) return 0;
    const ClassId classId = object->GetClassId();
    if (classId == ClassId::Building || classId == ClassId::Lever)
        ClientWorld()->RefreshPlatforms(object);   // the client's own
    return 1;
}

int PlayerClient::OnObjectRemoved() {
    int objectId;
    in.ReadValue(objectId);
    CPlayer* currentPlayer = player;
    if (auto unit = currentPlayer->units.find(objectId); unit != currentPlayer->units.end()) unit->second->SetPlayer(nullptr);
    auto node = currentPlayer->knownObjects.find(objectId);
    if (node == currentPlayer->knownObjects.end()) return 0;
    CMapObject* object = node->second;
    currentPlayer->knownObjects.erase(objectId);
    object->flags |= MapObjectFlag::Removed;
    object->Release();
    g_clientWorld.RemoveObject(object->id);
    return 1;
}

int PlayerClient::OnUnitDied() {
    int objectId;
    in.ReadValue(objectId);
    EngineObject* object = g_clientWorld.objectManager->FindObject(static_cast<unsigned>(objectId));
    if (CUnit* unit = UnitOrNull(object)) unit->Controller()->Died();
    return 1;
}

int PlayerClient::OnTimedObjectAdded() {
    int message[2];   // id, type
    in.Read(message, 8);
    const int objectId = message[0];
    CPlayer* currentPlayer = player;
    BaseManager* manager = currentPlayer->WorldContext()->TimedObjectManager();
    auto* object = manager->CreateObjectAs<CEffect>(message[1], objectId);
    if (!object) return 0;
    if (!object->Receive(0, &in)) return 0;
    if (objectId > currentPlayer->maxTimedId) currentPlayer->maxTimedId = objectId;
    currentPlayer->knownTimed[objectId] = object;
    object->AddRef();
    return 1;
}

int PlayerClient::OnTimedObjectChanged() {
    int objectId;
    in.ReadValue(objectId);
    CPlayer::TimedObjectMap& known = player->knownTimed;
    auto node = known.find(objectId);
    if (node == known.end()) return 0;
    return node->second->Receive(1, &in) != 0 ? 1 : 0;
}

int PlayerClient::OnTimedObjectRemoved() {
    int objectId;
    in.ReadValue(objectId);
    CPlayer* currentPlayer = player;
    auto node = currentPlayer->knownTimed.find(objectId);
    if (node == currentPlayer->knownTimed.end()) return 0;
    CEffect* object = node->second;
    currentPlayer->knownTimed.erase(objectId);
    object->flags |= MapObjectFlag::Removed;
    object->Release();
    return 1;
}

int PlayerClient::OnWeather() {
    std::uint8_t bytes[3];
    in.Read(bytes, 3);
    const int strength = bytes[1] | (bytes[2] << 8);
    g_clientWorld.rainSnow.Start(bytes[0], static_cast<float>(strength));
    return 1;
}

int PlayerClient::OnWorldTime() {
    float values[2];   // time, speed
    in.Read(values, 8);
    WorldClient* world = &g_clientWorld;
    world->CWorld::SetTimeSpeed(values[1]);
    world->CWorld::SetTime(values[0]);
    world->UpdateLighting();
    return 1;
}

int PlayerClient::OnWorldPoint() {
    Vector3 point;
    in.ReadValue(point);
    int value;
    in.ReadValue(value);
    g_clientWorld.ApplyWind(point, std::bit_cast<float>(value));
    return 1;
}

int PlayerClient::OnWorldStream() {
    g_clientWorld.ReadWaterLevels(&in);
    return 1;
}

void PlayerClient::NotifyUnitChanged(CUnit* unit) {
    if (unit->player) g_interface.OnUnitJoined(unit);
    else g_interface.OnUnitLeft(unit);
    if (!IsMultiplayerClient()) return;
    CParty* party = player->GetParty();
    if (std::ssize(party->members) != 1 || static_cast<int>(party->members[0].unitId) != unit->id) return;
    auto* login = net::g_gameClient->player.Get();
    login->id = unit->player ? unit->id : -1;
    ++login->version;
}
