// PlayerClient: the client's per-tick link traffic (sending the queued requests, receiving and
// running the server's messages) and its login. See player_client.h and docs/notes/objects.md.

#include "game/players/player_client.h"
#include "game/players/saved_parties.h"

#include "game/ai/ai_map.h"
#include "engine/core/base_manager.h"
#include "engine/scene/camera.h"
#include "game/console/console.h"
#include "game/effects/effects.h"
#include "engine/particles/particle_data.h"
#include "game/app/game_flow.h"
#include "game/app/singletons.h"
#include "game/items/item.h"
#include "game/items/item_manager.h"
#include "game/objects/map_object.h"
#include "game/objects/object3d.h"
#include "game/objects/map_object_shadow.h"
#include "game/net/net_link.h"
#include "engine/core/named_floats.h"
#include "engine/data/options.h"
#include "game/players/party.h"
#include "game/players/player_messages.h"
#include "game/players/player.h"
#include "engine/data/resource_manager.h"
#include "game/sound/sound.h"
#include "game/ui/ui.h"
#include "game/units/unit.h"
#include "game/units/unit_client_models.h"
#include "game/units/unit_db.h"
#include "game/world/world.h"

#include "lib/maths.h"
#include "lib/rounding.h"
#include "lib/str.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

constexpr int kLoginVersion = 100;

// The reason a client leaves when the server's items or spells name records this game's databases lack.
constexpr const char* kOtherDatabaseText =
    "The server's items or spells are not in this game's item and spell databases: the server uses another "
    "items.idb or spells.sdb.";

// The login request (message 0). The bytes after the two texts are zero (the original sent whatever its buffers
// held).
struct LoginRequest {
    int version = 0;
    char name[64] = {};
    char password[64] = {};
    std::uint32_t partyId = 0; // the player's partyId
};
static_assert(sizeof(LoginRequest) == 0x88);   // sent as its bytes

// The texts the original reads from the exe: the traffic log, the login, the dispatch errors.
constexpr const char* kLoginName = "Tango";
constexpr const char* kLoginPassword = "Very top secret";

// The quest log refreshed: no items picked up.
void RefreshQuestLog() { g_interface.OnItemsPicked({}, 0, false); }

// The client world shows a number over a unit (id, value, kind).
constexpr int kNumberExperience = 8;

// The experience shown for `amount`: amount times 1.04 ^ (intelligence - 25), rounded to the nearest integer.
int ScaledExperience(const Unit* unit, float amount) {
    const float power = lib::Pow(1.04f, static_cast<float>(unit->params.intelligence) - 25.0f);
    return lib::RoundEven(amount * power);
}

bool IsQuestVariable(const std::string& name) {
    return static_cast<int>(name.size()) > 2 && name.starts_with("q.");
}

constexpr int kKindNoPath = 3;               // OnPathMarker: no path to read
constexpr int kPathParticles = static_cast<int>(ParticleType::ShapeChange);
constexpr int kMaxPathGlows = 2500;          // the glows ShapeChange makes at most (its [emit] rate)
constexpr int kMarkerParticles = static_cast<int>(ParticleType::GroundDust1);     // 0x203B with the flag
constexpr float kMarkerSpeedA = 0.5f;
constexpr std::uint32_t kMarkerSpeedB = 0x501502F9u;   // float bits (1e10)

// Path markers are shown.
bool MarkersShown() { return options::g_options.drawPath != 0; }

constexpr std::uint32_t kHeaderFlagHidden = 8;
constexpr int kWeaponPike = 5;
constexpr int kWispParticles = static_cast<int>(ParticleType::Stun);
constexpr float kWispSize = 0.05f;
constexpr int kTextSize = 0x100;
constexpr int kSessionClient = 3;

// The header of message 0x40.
struct NewObjectHeader {
    int id = 0;
    int type = 0;
    std::uint32_t prototype = 0;  // the object's prototypeId
    std::uint32_t flags = 0;
};

bool Same(const char* a, const char* b) { return std::strcmp(a, b) == 0; }


// FindPart, then RemovePart when there is one (the original inlines both calls).
void RemovePartNamed(Object3D* object, const char* name) {
    if (FigureMesh* part = object->FindPart(name)) object->RemovePart(part);
}

constexpr int kSessionKindSingle = 1;

std::unordered_set<int> g_partyIdSet;

} // namespace

bool PlayerClient::RunTick() {
    netlink::NetLink* link = player->link;
    switch (state) {
    case State::Connecting: {
        if (link == nullptr || !link->Connected()) return true;
        if (sendParty) {
            WriteType(&out, ClientMessage::NewParty);
            player->GetParty()->SaveParty(&out, true);
        }
        LoginRequest login;
        std::memset(&login, 0, sizeof(login));
        login.version = kLoginVersion;
        std::memcpy(login.name, kLoginName, std::strlen(kLoginName) + 1);
        std::memcpy(login.password, kLoginPassword, std::strlen(kLoginPassword) + 1);
        std::memcpy(login.password + kCompanionMarkAt, kCompanionLoginMark, sizeof(kCompanionLoginMark));
        login.partyId = player->partyId;
        WriteType(&out, ClientMessage::Login);
        out.WriteValue(login);
        state = State::LoginSent;
        return SendToServer();
    }
    case State::LoginSent:
        return true;
    case State::SendingCamera: {
        const Vector3* anchor = g_clientWorld.camera->Anchor();
        Vector2 point = XY(*anchor);   // sent as two floats
        WriteType(&out, ClientMessage::LastPosition);
        out.WriteValue(point);
        RestoreCompanions();
        return SendToServer();
    }
    default:
        return SendToServer();
    }
}

bool PlayerClient::SendToServer() {
    netlink::NetLink* link = player->link;
    if (link == nullptr) return true;
    bool sent = false;
    if (link->Connected()) {
        MemoryStream packed;
        out.CompressTo(&compressor, &packed);
        if (player->logTraffic != 0) {
            std::uint32_t packedSize = packed.size;
            std::uint32_t size = out.Size();
            console::EndLine(console::g_netLogOut << "client to server, size = " << size << " squeezed size = " << packedSize);
        }
        sent = link->Send(&packed);
    }
    out.Resize(0);
    return sent;
}

int PlayerClient::DispatchMessages() {
    in.Seek(0);
    in.ok = true;
    std::uint8_t messageType = 0xFF;
    std::uint8_t previous = 0xFF;
    for (;;) {
        if (!in.ok) return PlayerMessageError("Pkt read error prevID=%x curID=%x\n", previous, messageType);
        if (!leaveReason.empty()) return 1;
        previous = messageType;
        if (in.Read(&messageType, 1) == 0) return 1;
        bool handled;
        switch (static_cast<ServerMessage>(messageType)) {
        case ServerMessage::LoginAccepted: handled = OnLoginAccepted(); break;
        case ServerMessage::Disconnect: handled = OnDisconnect(); break;
        case ServerMessage::Error: handled = OnError(); break;
        case ServerMessage::Zone: handled = OnZone(); break;
        case ServerMessage::ItemsAndSpells: handled = OnItemsAndSpells(); break;
        case ServerMessage::CameraPlacement: handled = OnCameraPlacement(); break;
        case ServerMessage::NewItemsAndSpells: handled = OnNewItemsAndSpells(); break;
        case ServerMessage::ItemsReceived: handled = OnItemsReceived(); break;
        case ServerMessage::GameState: handled = OnGameState(); break;
        case ServerMessage::ZoneChange: handled = OnZoneChange(); break;
        case ServerMessage::GameLost: handled = OnGameLost(); break;
        case ServerMessage::ItemRemoved: handled = OnItemRemoved(); break;
        case ServerMessage::UnitSpeech: handled = OnUnitSpeech(); break;
        case ServerMessage::ObjectValues: handled = OnObjectValues(); break;
        case ServerMessage::Jingle: handled = OnJingle(); break;
        case ServerMessage::PathMarker: handled = OnPathMarker(); break;
        case ServerMessage::FreeCamera: handled = OnFreeCamera(); break;
        case ServerMessage::WorldValue: handled = OnWorldValue(); break;
        case ServerMessage::StringEvent: handled = OnStringEvent(); break;
        case ServerMessage::ItemValue: handled = OnItemValue(); break;
        case ServerMessage::ExperienceAll: handled = OnExperienceAll(); break;
        case ServerMessage::WeaponRemoved: handled = OnWeaponRemoved(); break;
        case ServerMessage::QuestItemsChanged: handled = OnQuestItemsChanged(); break;
        case ServerMessage::Experience: handled = OnExperience(); break;
        case ServerMessage::UnitControl: handled = OnUnitControl(); break;
        case ServerMessage::ObjectAdded: handled = OnNewObject(); break;
        case ServerMessage::ObjectChanged: handled = OnObjectChanged(); break;
        case ServerMessage::ObjectRemoved: handled = OnObjectRemoved(); break;
        case ServerMessage::UnitDied: handled = OnUnitDied(); break;
        case ServerMessage::TimedAdded: handled = OnTimedObjectAdded(); break;
        case ServerMessage::TimedChanged: handled = OnTimedObjectChanged(); break;
        case ServerMessage::TimedRemoved: handled = OnTimedObjectRemoved(); break;
        case ServerMessage::PartyState: handled = OnPartyState(); break;
        case ServerMessage::Weather: handled = OnWeather(); break;
        case ServerMessage::Time: handled = OnWorldTime(); break;
        case ServerMessage::Wind: handled = OnWind(); break;
        case ServerMessage::Countdowns: handled = OnCountdowns(); break;
        case ServerMessage::Party: handled = OnParty(); break;
        case ServerMessage::SetVariable: handled = OnSetVariable(); break;
        case ServerMessage::DeleteVariable: handled = OnDeleteVariable(); break;
        case ServerMessage::PartyUpdate: handled = OnPartyUpdate(); break;
        case ServerMessage::Companions: handled = OnCompanions(); break;
        default: return PlayerMessageError("Packet has no handler prevID=%x curID=%x\n", previous, messageType);
        }
        if (!handled) return PlayerMessageError("Error during parsing update prevID=%x curID=%x\n", previous, messageType);
    }
}

int PlayerClient::Receive() {
    netlink::NetLink* link = player->link;
    if (link == nullptr) return 0;
    if (!link->HasIncoming()) return 1;
    MemoryStream packet;
    if (!link->Receive(&packet)) return 0;
    packet.DecompressTo(&decompressor, &in);
    return DispatchMessages() != 0 ? 1 : 0;
}

bool PlayerClient::OnSetVariable() {
    std::string name = in.ReadString();
    std::uint32_t value;
    in.ReadValue(value);
    NamedFloatTable& variables = player->GetParty()->variables;
    variables.Get(name);
    variables.Set(name, std::bit_cast<float>(value));
    if (IsQuestVariable(name)) {
        g_interface.OnQuestVariable(name, std::bit_cast<float>(value));
        g_clientWorld.OnQuestVariable(name, std::bit_cast<float>(value));
    }
    return true;
}

bool PlayerClient::OnDeleteVariable() {
    std::string name = in.ReadString();
    player->GetParty()->variables.Remove(name);
    if (IsQuestVariable(name)) {
        g_interface.OnQuestVariable(name, 0.0f);
        g_clientWorld.OnQuestVariable(name, 0.0f);
    }
    return true;
}

bool PlayerClient::OnZoneChange() {
    std::string zone = in.ReadString();
    int entrance;   // the server's entrance number: the zone start takes none
    in.ReadValue(entrance);
    g_gameFlow.RequestZoneChange(zone);
    return true;
}

bool PlayerClient::OnJingle() {
    std::string name = in.ReadString();
    g_clientWorld.music->PlayJingle(name);
    return true;
}

bool PlayerClient::OnZone() {
    MemoryStream block;
    if (in.ReadStream(&block) == 0) return false;
    player->Clear();
    g_clientWorld.LoadZone(&block);
    return true;
}

bool PlayerClient::OnItemsAndSpells() {
    MemoryStream block;
    if (in.ReadStream(&block) == 0) return false;
    if (!g_clientWorld.LoadItemsAndSpells(&block)) return LeaveAfterTick(kOtherDatabaseText);
    return true;
}

bool PlayerClient::OnNewItemsAndSpells() {
    MemoryStream block;
    if (in.ReadStream(&block) == 0) return false;
    if (!g_clientWorld.LoadNewItemsAndSpells(&block)) return LeaveAfterTick(kOtherDatabaseText);
    return true;
}

bool PlayerClient::OnItemRemoved() {
    const int objectId = in.ReadId();
    player->TakeItem(objectId);
    RefreshQuestLog();
    return true;
}

bool PlayerClient::OnQuestItemsChanged() {
    player->TakeAllItems();
    RefreshQuestLog();
    return true;
}

bool PlayerClient::OnExperienceAll() {
    float amount;
    in.ReadValue(amount);
    Player::UnitMap& units = player->units;
    for (auto node = units.begin(); node != units.end() && node->second; ++node) {
        Unit* unit = node->second;
        g_clientWorld.ShowNumber(unit->id, ScaledExperience(unit, amount), kNumberExperience);
    }
    return true;
}

bool PlayerClient::OnExperience() {
    const int objectId = in.ReadId();
    float amount;
    in.ReadValue(amount);
    auto* unit = g_clientWorld.FindUnit(objectId);
    if (unit != nullptr) g_clientWorld.ShowNumber(objectId, ScaledExperience(unit, amount), kNumberExperience);
    return true;
}

// --- Items, party, path markers, string events, recording ---------------------------------------

bool PlayerClient::OnItemsReceived() {
    std::vector<Item*> items;
    int& money = player->GetParty()->money;
    int before = money;
    in.ReadValue(money);
    std::uint8_t flag = 0;
    in.ReadValue(flag);
    bool complete = true;
    for (;;) {
        const int objectId = in.ReadId();
        if (objectId == -1) break;
        if (!in.ok) {
            complete = false;
            break;
        }
        auto* manager = g_clientWorld.itemManager.get();
        if (Item* item = manager->FindObjectAs<Item>(static_cast<unsigned>(objectId))) items.insert(items.begin(), item);
    }
    if (complete) {
        player->GiveItems(items, flag);
        g_interface.OnItemsPicked(items, money - before, flag != 0);
    }
    items.clear();
    return complete;
}

bool PlayerClient::OnParty() {
    Party* party = player->GetParty();
    NamedFloatTable saved(party->variables);
    {
        MemoryStream block;
        in.ReadSized(&block);
        block.MemoryStream::Seek(0);
        if (in.ok) {
            party->LoadParty(&block, true);
            if (!KeepParty(party)) return true;
            std::vector<std::uint32_t>& recharging = player->recharging;
            recharging.clear();
            ObjectListIteratorOf<Item> walk;
            for (Item* item = walk.First(party->ItemFactory()); item != nullptr; item = walk.Next()) {
                if (item->chargeMax > item->charge)
                    recharging.push_back(static_cast<std::uint32_t>(item->id));
            }
        }
        if (IsMultiplayerClient()) {
            // A multiplayer client keeps its variables.
            party->variables = saved;
        }
        g_clientWorld.ApplyObjectVariables();
        console::EndLine(console::g_netLogOut << "party received on client");
        g_gameFlow.ReturnLocalParty();
    }
    return true;
}

bool PlayerClient::OnPartyUpdate() {
    MemoryStream block;
    in.ReadSized(&block);
    block.MemoryStream::Seek(0);
    if (in.ok) {
        player->GetParty()->LoadParty(&block, false);
        KeepParty(player->GetParty());
    }
    return true;
}

bool PlayerClient::OnCompanions() {
    in.ReadValue(companionLimit);
    console::EndLine(console::g_netLogOut << "client: the host takes " << companionLimit << " companions");
    restoreCompanions = !g_savedParties.companions.empty();
    return true;
}

void PlayerClient::RestoreCompanions() {
    if (!restoreCompanions) return;
    const Party* party = player->GetParty();
    if (party->members.empty()) return;
    Unit* hero = g_clientWorld.FindUnit(static_cast<int>(party->members[0].unitId));
    if (!hero || hero->IsDead()) return;
    restoreCompanions = false;
    // Beyond this host's limit the session's companions are dropped.
    std::vector<std::string>& files = g_savedParties.companions;
    if (std::ssize(files) > static_cast<std::ptrdiff_t>(companionLimit)) files.resize(companionLimit);
    for (const std::string& file : files)
        for (MpHeroRecord& entry : g_savedParties.entries)
            if (entry.file == file && entry.party.members.size() == 1) SendAddCompanion(entry.GetParty(), true);
}

bool PlayerClient::KeepParty(Party* party) {
    if (HasEveryRecord(party)) return true;
    party->Clear(false, false);
    LeaveAfterTick(kOtherDatabaseText);
    return false;
}

bool PlayerClient::LeaveAfterTick(std::string reason) {
    if (leaveReason.empty()) leaveReason = std::move(reason);
    console::EndLine(console::g_netLogOut << "client: leaving the server: " << leaveReason);
    return true;
}

bool PlayerClient::OnPathMarker() {
    DeltaField field;
    ParticleSource* pathSource = nullptr;
    int kind;
    in.ReadValue(kind);
    int flag;
    in.ReadValue(flag);
    Vector2 xy{0.0f, 0.0f};
    in.ReadValue(xy);
    Vector3 at = WithZ(xy, 0.0f);
    const int unitId = in.ReadId();
    float value;
    in.ReadValue(value);
    if (kind != kKindNoPath) {
        auto* walker = g_clientWorld.objectManager->CreateObjectAs<Unit>(MapObjectType::Hero, -1);
        walker->AddRef();
        PathOrder* path = &walker->ClientController()->motion;
        DeltaWriter writing(field, DeltaWrite::Full);
        path->WriteState(writing);
        writing.End();
        DeltaReader reading(field, 0, &in);
        path->ReadState(reading);
        reading.End();
        if (MarkersShown()) {
            walker->SetMotionSpeeds(kMarkerSpeedA, std::bit_cast<float>(kMarkerSpeedB));
            auto* source = g_clientWorld.particleManager->CreateSource(kPathParticles);
            pathSource = source;
            source->Attach(walker);
            // The glows' points: the walker's path from now, up to `value` ahead (at most the source's 2500).
            std::vector<Vector3> points;
            for (int index = 0; index < kMaxPathGlows && !(static_cast<float>(index) > value); ++index)
                points.push_back(walker->PositionAt(index + g_clientWorld.tick));
            effectdata::SetInput(*source, "path", points);
            if (kind >= 0 && kind <= 2) effectdata::SetInput(*source, "kind", static_cast<float>(kind));
            pathSource->RunTick();
            source->Attach(nullptr);
        }
        walker->Release();
    }
    if (MarkersShown()) {
        auto* marker = g_clientWorld.particleManager->CreateSource(flag != 0 ? kMarkerParticles + 1 : kMarkerParticles);
        marker->localPosition = at;
        EngineObject* ownerObject = player;
        Unit* unit = ownerObject->OwningWorld()->FindUnit(unitId);
        if (unit != nullptr) unit->ClientController()->SetMarkers(pathSource, marker);
    }
    return true;
}

bool PlayerClient::OnStringEvent() {
    std::string text = in.ReadString();
    g_interface.OnStringEvent(text);
    return true;
}

// --- A new map object (message 0x40, multiplayer) ------------------------------------------

bool PlayerClient::OnNewObject() {
    std::vector<std::string> parts;
    std::string scriptName;
    std::string secondName;
    NewObjectHeader header;
    in.ReadValue(header);
    Vector3 scale;
    in.ReadValue(scale);
    secondName = in.ReadString();
    if (!in.ok) return false;
    char model[kTextSize];
    char textureFrom[kTextSize];
    char textureTo[kTextSize];
    char part[kTextSize];
    in.ReadText(model, kTextSize);
    in.ReadText(textureFrom, kTextSize);
    in.ReadText(textureTo, kTextSize);
    std::uint16_t count = 0;
    in.ReadValue(count);
    parts.resize(count);
    for (std::uint16_t i = 0; i < count; ++i) {
        in.ReadText(part, kTextSize);
        parts[i] = part;
    }
    scriptName = in.ReadString();
    int prototypeValue;
    in.ReadValue(prototypeValue);

    auto* world = player->OwningWorld();
    MapObject* created = world->objectManager->CreateObjectAs<MapObject>(static_cast<MapObjectType>(header.type), header.id);
    if (created == nullptr) return false;
    MapObject* object = created;
    Object3D* figure = object;
    object->name = model;
    bool isUnit = created->GetClassId() == ClassId::Unit;
    created->prototypeId = header.prototype;
    figure->SetFigureScale(scale);
    object->baseScale = scale;
    object->ApplyBaseScale();
    object->name2 = secondName.substr(0, secondName.find('\0'));   // up to the first NUL, as a C string
    if (isUnit) {
        Unit* unit = UnitOrNull(created);
        const UnitPrototypeRecord* prototype = unitdb::PrototypeRecord(header.prototype);
        auto* modelRecord = unitdb::ModelRecord(static_cast<std::uint32_t>(prototype->modelIndex));
        unit->defaultScale = {0.0f, modelRecord->height, 0.0f};
        std::memcpy(unit->actionSpeeds, modelRecord->actionSpeeds, sizeof(unit->actionSpeeds));
        unit->SetSecondPrototypeIndex(prototypeValue);
    }
    if (header.flags & kHeaderFlagHidden) created->flags |= kHeaderFlagHidden;
    if (Same(model, "unmoco1") || Same(model, "unmoco2")) created->flags |= MapObjectFlag::NoCasting;   // a cow
    if (created->MapType() == MapObjectType::Hero && (Same(model, "unhuma") || Same(model, "unhufe"))) {
        // The head part takes the prototype's hair, or goes without one.
        for (std::size_t i = 0; i < parts.size(); ++i) {
            if (str::Compare(parts[i], "hr") != 0) continue;
            int hair = unitdb::PrototypeRecord(header.prototype)->hair;
            if (hair == -1) parts.erase(parts.begin() + static_cast<std::ptrdiff_t>(i));
            else parts[i] = str::Format("hr.%.2d", hair);
            break;
        }
    }
    figure->MakeFigure(model, &parts);
    if (Same(model, "unmogo")) {
        RemovePartNamed(figure, "rh3.axeth00");
        RemovePartNamed(figure, "rh3.sword00");
        bool pike = static_cast<int>(unitdb::PrototypeRecord(created->prototypeId)->naturalWeapon) == kWeaponPike;
        RemovePartNamed(figure, pike ? "rh3.pike00" : "sling");
    }
    if (Same(model, "unmowi")) {
        auto* source =
            g_clientWorld.particleManager->CreateSource(kWispParticles);
        source->Attach(object);
        source->ScaleSize(kWispSize / source->baseSize);
        source->attachPoint = AttachPoint::EffectPoint;
    }
    if (!isUnit) {
        for (int i = 0; i < std::ssize(figure->parts); ++i) figure->parts[i]->flags |= FigureMeshFlag::Stationary;
        created->flags |= MapObjectFlag::Stationary;
    }
    Object3DClientSpecific* attached = figure->ClientSpecific();
    attached->RegisterPartMaterials();
    // (The client world's two functions around the texture swap are empty.)
    figure->PlayAnimation(nullptr);
    figure->RestartPose();
    std::optional<std::string_view> to = std::string_view(textureTo);
    if (created->MapType() == MapObjectType::Hero) {
        std::snprintf(textureFrom, sizeof(textureFrom), "mo%dp", created->id);
        std::snprintf(textureTo, sizeof(textureTo), "mo%ds", created->id);
    } else if (Same(textureTo, "none")) {
        to = std::nullopt;
    }
    attached->SetSkins(textureFrom, to);
    if (isUnit) created->SetScriptName(scriptName);

    // The player keeps it, counted.
    Player* currentPlayer = player;
    if (header.id > currentPlayer->maxObjectId) currentPlayer->maxObjectId = header.id;
    currentPlayer->knownObjects[header.id] = object;
    created->AddRef();
    if (Unit* unit = UnitOrNull(created))
        object->TakePrototype(unit->GetPrototype()->name);
    else
        object->TakePrototype(model);
    object->WriteFullState();
    if (!object->ReadState(false, &in)) return false;
    g_clientWorld.AddObject(object);
    if (object->name2[0] != 0) {
        const std::string variable = g_clientWorld.ObjectVariableName(object->name2);
        object->SetVisibilityLevel(currentPlayer->GetParty()->variables.Get(variable));
    }
    if (currentPlayer->sessionKind == kSessionClient) {
        if (Unit* unit = UnitOrNull(created)) unit->ClientController()->LoadFaceTexture();
    }
    return true;
}

// --- Clearing the connection ---------------------------------------------------------------

void PlayerClient::ResetState() {
    state = State::Connecting;
    partyUnitIds.clear();
    in.Resize(0);
    out.Resize(0);
    localSession = 0;
    sendParty = 0;
    companionLimit = 0;
    restoreCompanions = false;
    compressor.Reset();
    decompressor.Reset();
}

// --- Constructor and destructor ------------------------------------------------------------

PlayerConnection::~PlayerConnection() { player = nullptr; }

PlayerClient::PlayerClient() {
    ResetState();
}

PlayerClient::~PlayerClient() {
    ResetState();
}

const char* PlayerClient::ClassName() { return "CPlayerClient"; }
ClassId PlayerClient::GetClassId() { return ClassId::PlayerClient; }

// --- The party state ---------------------------------------------------------------------

bool PlayerClient::OnPartyState() {
    Player* currentPlayer = player;
    for (int i = 0; i < std::ssize(partyUnitIds); ++i) {
        g_partyIdSet.insert(partyUnitIds[i]);
        Unit* unit = currentPlayer->OwningWorld()->FindUnit(partyUnitIds[i]);
        if (unit != nullptr) unit->flags &= ~MapObjectFlag::InParty;
    }

    std::uint8_t paused;
    in.ReadValue(paused);
    currentPlayer->ClientWorld()->paused = paused != 0 ? 1 : 0;
    in.Skip(4);    // the server's possessedId, not used

    const int count = in.ReadCount();
    partyUnitIds.resize(count);
    for (int i = 0; i < count; ++i) {
        const int objectId = in.ReadId();
        partyUnitIds[i] = objectId;
        auto* unit = currentPlayer->OwningWorld()->FindUnit(objectId);
        if (unit == nullptr) continue;
        unit->flags |= MapObjectFlag::InParty;
        if (!g_partyIdSet.contains(objectId) && currentPlayer->sessionKind == kSessionKindSingle && unit->player == nullptr &&
            g_clientWorld.ticksRun >= 1)
            unit->controller->RestartFadeIn();
    }
    g_partyIdSet.clear();

    currentPlayer->ClientWorld()->partyDanger = in.ReadAs<std::int32_t>();
    otherUnitIds.resize(in.ReadCount());
    for (auto& unitId : otherUnitIds) in.ReadValue(unitId);
    return true;
}

// --- Messages appended to the received stream -----------------------------------------------

void PlayerClient::AppendReceived(MemoryStream* source) {
    const std::uint32_t position = in.Position();
    in.Seek(in.Size());
    in.EndBits();
    source->Seek(0);
    in.Write(source->data.data(), source->Size());
    in.Seek(position);
}
