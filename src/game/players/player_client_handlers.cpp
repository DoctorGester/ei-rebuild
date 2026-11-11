// PlayerClient's remaining message handlers and one virtual. See player_client.h and
// docs/notes/objects.md ("Players").
#include "game/effects/effects.h"
#include "game/app/game_flow.h"
#include "game/items/item_manager.h"
#include "game/players/player_client.h"

#include "engine/core/core_maths.h"
#include "game/world/camera.h"
#include "game/items/item.h"
#include "game/objects/map_object.h"
#include "game/objects/object3d.h"
#include "game/net/net_session.h"
#include "game/net/net_messages.h"
#include "game/players/party.h"
#include "game/players/player_manager.h"
#include "game/ui/ui.h"
#include "game/units/unit.h"
#include "game/world/world.h"
#include "game/effects/world_effect.h"
#include "lib/bytes.h"
#include "lib/vecmath.h"

#include "game/app/singletons.h"
#include "game/players/player.h"

#include <bit>
#include <cstring>

namespace {

constexpr int kGameStateBetweenZones = 3;
constexpr float kSpecialZoneDistance = 23.0f;

// The zone whose camera placement keeps a fixed view ("gz20g").
// An identity rotation.
constexpr Quaternion kFixedViewRotation = /*007C8388*/ {1.0f, 0.0f, 0.0f, 0.0f};   // the identity (a static initialiser)



} // namespace

bool PlayerClient::OnLoginAccepted() {
    int objectId;
    in.ReadValue(objectId);
    state = State::SendingCamera;
    Player* currentPlayer = player;
    currentPlayer->GetManagerContext()->PlayerList()->ChangeId(currentPlayer, objectId);
    return true;
}

// Unreached in the shipped game: our server never sends message 0x01 (kept for a peer that does).
bool PlayerClient::OnDisconnect() {
    player->Disconnect();
    return true;
}

bool PlayerClient::OnError() { return LeaveAfterTick(in.ReadString()); }

bool PlayerClient::OnCameraPlacement() {
    Vector3 point;
    in.ReadValue(point);
    float heading;
    in.ReadValue(heading);
    int mode;
    const bool read = in.Read(&mode, 4) != 0;
    g_clientWorld.CentreOn(point);
    Camera* camera = &g_camera;
    if (player->GetParty()->zone == "gz20g") {
        CameraView view;
        view.distance = kSpecialZoneDistance;
        view.rotation = kFixedViewRotation;
        view.pitch = ei::kRadiansPerDegree * 30.0f;
        camera->SetDistancePitch(&view);
    } else {
        camera->RestoreView(mode == 3 ? 6 : 7);
    }
    camera->SetHeading(heading);
    return read;
}

bool PlayerClient::OnGameState() {
    Player* currentPlayer = player;
    currentPlayer->lastScreen = static_cast<std::uint32_t>(currentPlayer->sessionKind);
    int gameState;
    in.ReadValue(gameState);
    int tick;
    in.ReadValue(tick);
    if (localSession) {
        g_interface.OnKeyDown(platform::Key::None, 0, false);   // a press of no key (the original's -1, scan code 0)
        return true;
    }
    currentPlayer->sessionKind = gameState;
    g_interface.CreateScreen(gameState);
    g_clientWorld.tick = tick;
    if (gameState == kGameStateBetweenZones) g_clientWorld.LoadUnitFaces();
    for (auto node = currentPlayer->units.begin(); node != currentPlayer->units.end() && node->second; ++node)
        UnitChanged(node->second);
    if (IsMultiplayerClient()) g_gameFlow.UpdateFromServer();
    return true;
}

bool PlayerClient::OnGameLost() {
    g_interface.OnGameLost();
    return true;
}

bool PlayerClient::OnUnitSpeech() {
    int values[2];
    in.Read(values, 8);
    g_interface.OnUnitSpeech(values[0], static_cast<AckKind>(values[1]));
    return true;
}

// Unreached in the shipped game: our server never sends message 0x0C (kept for a peer that does).
bool PlayerClient::OnObjectValues() {
    for (;;) {
        const int objectId = in.ReadId();
        if (objectId == -1) return true;
        if (!in.ok) return false;
        Item* item = g_clientWorld.itemManager.get()->FindObjectAs<Item>(static_cast<unsigned>(objectId));
        if (!item) continue;   // the value is then read as the next id (kept)
        float value;
        in.ReadValue(value);
        item->charge = value;
    }
}

bool PlayerClient::OnFreeCamera() {
    Vector3 from;
    in.ReadValue(from);
    Vector3 to;
    in.ReadValue(to);
    std::uint8_t free = 0;
    in.ReadValue(free);
    g_camera.LookFrom(from, to);
    g_camera.SetFree(free != 0);
    return true;
}

bool PlayerClient::OnWorldValue() {
    std::uint8_t bytes[5];
    in.Read(bytes, 5);   // the world value (bytes 1-4): nothing reads it
    return true;
}

bool PlayerClient::OnItemValue() {
    const int objectId = in.ReadId();
    float value;
    in.ReadValue(value);
    Player* currentPlayer = player;
    Item* item = currentPlayer->GetParty()->ItemFactory()->FindObjectAs<Item>(static_cast<unsigned>(objectId));
    if (item) {
        item->charge = value;
        currentPlayer->ItemUsed(item);
    }
    return true;
}

bool PlayerClient::OnWeaponRemoved() {
    const int unitId = in.ReadId();
    int index;
    in.ReadValue(index);
    // The party (an embedded Party) seen as its record array.
    PartyUnitRecord* record = player->GetParty()->Find(unitId);
    if (record && std::ssize(record->weapons) > index)
        record->RemoveWeapon(record->weapons[index]);
    if (Unit* unit = g_clientWorld.FindUnit(unitId)) unit->controller->ShowWeapon(0xFFFFFFFFu);
    return true;
}

bool PlayerClient::OnUnitControl() {
    int kind = 0;
    in.Read(&kind, 1);
    const int unitId = in.ReadId();
    auto* unit = g_clientWorld.FindUnit(unitId);
    switch (kind) {
    case 0:
        if (unit) g_clientWorld.Select(unit);
        return true;
    case 1:
    case 2:
        if (unit) g_clientWorld.MarkPerson(unit, kind == 1);
        return true;
    case 3:
        g_clientWorld.Deselect();
        return true;
    default:
        return false;
    }
}

bool PlayerClient::OnObjectChanged() {
    const int objectId = in.ReadId();
    Player::MapObjectMap& known = player->knownObjects;
    auto node = known.find(objectId);
    if (node == known.end()) return false;
    MapObject* object = node->second;
    if (!object->ReadState(true, &in)) return false;
    const ClassId classId = object->GetClassId();
    if (classId == ClassId::Building || classId == ClassId::Lever)
        ClientWorld()->RefreshPlatforms(object);   // the client's own
    return true;
}

bool PlayerClient::OnObjectRemoved() {
    const int objectId = in.ReadId();
    Player* currentPlayer = player;
    if (auto unit = currentPlayer->units.find(objectId); unit != currentPlayer->units.end()) unit->second->SetPlayer(nullptr);
    auto node = currentPlayer->knownObjects.find(objectId);
    if (node == currentPlayer->knownObjects.end()) return false;
    MapObject* object = node->second;
    currentPlayer->knownObjects.erase(objectId);
    object->flags |= MapObjectFlag::Removed;
    object->Release();
    g_clientWorld.RemoveObject(object->id);
    return true;
}

bool PlayerClient::OnUnitDied() {
    const int objectId = in.ReadId();
    EngineObject* object = g_clientWorld.objectManager->FindObject(static_cast<unsigned>(objectId));
    if (Unit* unit = UnitOrNull(object)) unit->controller->Died();
    return true;
}

bool PlayerClient::OnTimedObjectAdded() {
    int message[2];   // id, type
    in.Read(message, 8);
    const int objectId = message[0];
    Player* currentPlayer = player;
    BaseManager* manager = currentPlayer->GetManagerContext()->TimedObjectManager();
    auto* object = manager->CreateObjectAs<Effect>(message[1], objectId);
    if (!object) return false;
    if (!object->Receive(0, &in)) return false;
    if (objectId > currentPlayer->maxTimedId) currentPlayer->maxTimedId = objectId;
    currentPlayer->knownTimed[objectId] = object;
    object->AddRef();
    return true;
}

bool PlayerClient::OnTimedObjectChanged() {
    const int objectId = in.ReadId();
    Player::TimedObjectMap& known = player->knownTimed;
    auto node = known.find(objectId);
    if (node == known.end()) return false;
    return node->second->Receive(1, &in) != 0;
}

bool PlayerClient::OnTimedObjectRemoved() {
    const int objectId = in.ReadId();
    Player* currentPlayer = player;
    auto node = currentPlayer->knownTimed.find(objectId);
    if (node == currentPlayer->knownTimed.end()) return false;
    Effect* object = node->second;
    currentPlayer->knownTimed.erase(objectId);
    object->flags |= MapObjectFlag::Removed;
    object->Release();
    return true;
}

bool PlayerClient::OnWeather() {
    std::uint8_t bytes[3];
    in.Read(bytes, 3);
    const int strength = bytes[1] | (bytes[2] << 8);
    g_clientWorld.rainSnow.Start(bytes[0], static_cast<float>(strength));
    return true;
}

bool PlayerClient::OnWorldTime() {
    float values[2];   // time, speed
    in.Read(values, 8);
    WorldClient* world = &g_clientWorld;
    world->World::SetTimeSpeed(values[1]);
    world->World::SetTime(values[0]);
    world->UpdateLighting();
    return true;
}

bool PlayerClient::OnWind() {
    Vector3 point;
    in.ReadValue(point);
    int value;
    in.ReadValue(value);
    g_clientWorld.ApplyWind(point, std::bit_cast<float>(value));
    return true;
}

bool PlayerClient::OnCountdowns() {
    g_clientWorld.ReadWaterLevels(&in);
    return true;
}

void PlayerClient::UnitChanged(Unit* unit) {
    if (unit->player) g_interface.OnUnitJoined(unit);
    else g_interface.OnUnitLeft(unit);
    if (!IsMultiplayerClient()) return;
    Party* party = player->GetParty();
    if (party->members.empty() || static_cast<int>(party->members[0].unitId) != unit->id) return;
    auto* login = net::g_gameClient->player.Get();
    login->id = unit->player ? unit->id : -1;
    ++login->version;
}
