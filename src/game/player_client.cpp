// PlayerClient: the client's per-tick link traffic (sending the queued requests, receiving and
// running the server's messages) and its login. See player_client.h and docs/notes/objects.md.

#include "player_client.h"

#include "ai_map.h"
#include "base_manager.h"
#include "camera.h"
#include "console.h"
#include "effects.h"
#include "game_singletons.h"
#include "item.h"
#include "item_manager.h"
#include "map_object.h"
#include "map_object_figures.h"
#include "map_object_shadow.h"
#include "net_link.h"
#include "object_tables.h"
#include "options.h"
#include "party.h"
#include "player_messages.h"
#include "resource_manager.h"
#include "sound.h"
#include "ui.h"
#include "unit.h"
#include "unit_client_models.h"
#include "unit_db.h"
#include "world.h"

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
#include <vector>

namespace {

constexpr int kLoginVersion = 100;

// The login request (message 0). The bytes after the two texts are zero (the original sent whatever its buffers
// held).
struct LoginRequest {
    int version;
    char name[64];
    char password[64];
    std::uint32_t partyId; // the player's partyId
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
constexpr int kMarkerParticles = static_cast<int>(ParticleType::GroundDust1);     // 0x203B with the flag
constexpr float kMarkerSpeedA = 0.5f;
constexpr std::uint32_t kMarkerSpeedB = 0x501502F9u;   // float bits (1e10)

// Path markers are shown.
bool MarkersShown() { return options::g_options.drawPath != 0; }

constexpr std::uint32_t kHeaderFlagHidden = 8;
constexpr std::uint32_t kFlagCow = 0x10000000;
constexpr int kWeaponPike = 5;
constexpr int kWispParticles = static_cast<int>(ParticleType::Stun);
constexpr float kWispSize = 0.05f;
constexpr int kTextSize = 0x100;
constexpr int kSessionClient = 3;

// The header of message 0x40.
struct NewObjectHeader {
    int id;
    int type;
    std::uint32_t prototype;   // the object's prototypeId
    std::uint32_t flags;
};

bool Same(const char* a, const char* b) { return std::strcmp(a, b) == 0; }


// FindPart, then RemovePart when there is one (the original inlines both calls).
void RemovePartNamed(Object3D* object, const char* name) {
    if (FigureMesh* part = object->FindPart(name)) object->RemovePart(part);
}

constexpr int kSessionKindSingle = 1;

std::unordered_set<int> g_partyIdSet;
int ReadDword(MemoryStream* in) {
    int value;
    in->ReadValue(value);
    return value;
}

} // namespace

bool PlayerClient::RunTick() {
    netlink::NetLink* link = player->link;
    switch (state) {
    case State::Connecting: {
        if (link == nullptr || !link->Connected()) return true;
        if (sendParty) {
            WriteType(&out, ClientMessage::NewParty);
            player->GetParty()->SaveParty(&out, 1);
        }
        LoginRequest login;
        std::memset(&login, 0, sizeof(login));
        login.version = kLoginVersion;
        std::memcpy(login.name, kLoginName, std::strlen(kLoginName) + 1);
        std::memcpy(login.password, kLoginPassword, std::strlen(kLoginPassword) + 1);
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
        out.Write(&point, sizeof(point));
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
    in.ok = 1;
    std::uint8_t messageType = 0xFF;
    std::uint8_t previous = 0xFF;
    for (;;) {
        if (!in.ok) return PlayerMessageError("Pkt read error prevID=%x curID=%x\n", previous, messageType);
        previous = messageType;
        if (in.Read(&messageType, 1) == 0) return 1;
        bool handled;
        switch (static_cast<ServerMessage>(messageType)) {
        case ServerMessage::LoginAccepted: handled = OnLoginAccepted(); break;
        case ServerMessage::Disconnect: handled = OnDisconnect(); break;
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
    g_clientWorld.LoadItemsAndSpells(&block);
    return true;
}

bool PlayerClient::OnNewItemsAndSpells() {
    MemoryStream block;
    if (in.ReadStream(&block) == 0) return false;
    g_clientWorld.LoadNewItemsAndSpells(&block);
    return true;
}

bool PlayerClient::OnItemRemoved() {
    int objectId;
    in.ReadValue(objectId);
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
    int objectId;
    float amount;
    in.ReadValue(objectId);
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
        int objectId;
        in.ReadValue(objectId);
        if (objectId == -1) break;
        if (!in.ok) {
            complete = false;
            break;
        }
        auto* manager = g_clientWorld.itemManager.get();
        if (Item* item = FindItem(manager, static_cast<unsigned>(objectId))) items.insert(items.begin(), item);
    }
    if (complete) {
        player->GiveItems(&items, flag);
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
            party->LoadParty(&block, 1);
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
    if (in.ok) player->GetParty()->LoadParty(&block, 0);
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
    int unitId;
    in.ReadValue(unitId);
    float value;
    in.ReadValue(value);
    if (kind != kKindNoPath) {
        auto* walker = g_clientWorld.objectManager->CreateObjectAs<Unit>(MapObjectType::Hero, -1);
        walker->AddRef();
        PathOrder* path = &walker->ClientController()->motion;
        DeltaField* writing = field.BeginWriteB(0);
        path->WriteState(writing);
        writing->EndField();
        DeltaField* reading = field.BeginReadB(0, &in);
        path->ReadState(reading);
        reading->EndField();
        if (MarkersShown()) {
            walker->SetMotionSpeeds(kMarkerSpeedA, std::bit_cast<float>(kMarkerSpeedB));
            auto* source = g_clientWorld.particleManager->CreateSource(kPathParticles);
            pathSource = source;
            source->Attach(walker);
            source->typeParam = value;
            if (kind == 0) source->alpha = 1.0f;
            else if (kind == 1) source->alpha = 0.0f;
            else if (kind == 2) source->alpha = 2.0f;
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
    object->name2 = secondName.c_str();
    if (isUnit) {
        Unit* unit = UnitOrNull(created);
        const UnitPrototypeRecord* prototype = unitdb::PrototypeRecord(header.prototype);
        auto* modelRecord = unitdb::ModelRecord(static_cast<std::uint32_t>(prototype->modelIndex));
        unit->defaultScale = {0.0f, modelRecord->height, 0.0f};
        std::memcpy(unit->actionSpeeds, modelRecord->actionSpeeds, sizeof(unit->actionSpeeds));
        unit->SetSecondPrototypeIndex(prototypeValue);
    }
    if (header.flags & kHeaderFlagHidden) created->flags |= kHeaderFlagHidden;
    if (Same(model, "unmoco1") || Same(model, "unmoco2")) created->flags |= kFlagCow;
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
        source->attachPoint = 0;
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
    compressor.Reset();
    decompressor.Reset();
}

// --- Constructor and destructor ------------------------------------------------------------

PlayerConnection::PlayerConnection() {
    player = nullptr;
}

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
    ReadDword(&in);    // the server's possessedId, not used

    int count = ReadDword(&in);
    partyUnitIds.resize(count);
    for (int i = 0; i < count; ++i) {
        int objectId = ReadDword(&in);
        partyUnitIds[i] = objectId;
        auto* unit = currentPlayer->OwningWorld()->FindUnit(objectId);
        if (unit == nullptr) continue;
        unit->flags |= MapObjectFlag::InParty;
        if (!g_partyIdSet.contains(objectId) && currentPlayer->sessionKind == kSessionKindSingle && unit->player == nullptr &&
            g_clientWorld.ticksRun >= 1)
            unit->controller->RestartFadeIn();
    }
    g_partyIdSet.clear();

    currentPlayer->ClientWorld()->partyDanger = ReadDword(&in);
    int others = ReadDword(&in);
    otherUnitIds.resize(others);
    for (int i = 0; i < others; ++i) in.ReadValue(otherUnitIds[i]);
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
