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

#include "lib/rounded_ops.h"
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

constexpr std::uint8_t kRequestLogin = 0;
constexpr std::uint8_t kRequestParty = 8;
constexpr std::uint8_t kRequestCamera = 0x40;
constexpr int kLoginVersion = 100;
constexpr int kStateConnecting = 0;
constexpr int kStateLoggedIn = 1;
constexpr int kStateSendingCamera = 2;

// The login request (message 0). The original copies the two texts into stack buffers
// and sends the bytes after them uninitialised; here they are zero.
struct LoginRequest {
    int version;
    char name[64];
    char password[64];
    std::uint32_t partyId; // the player's partyId
};
static_assert(sizeof(LoginRequest) == 0x88);

// The texts the original reads from the exe: the traffic log, the login, the dispatch errors.
const char* TextClientToServer() { return "client to server, size = "; }
const char* TextSqueezedSize() { return " squeezed size = "; }
const char* TextLoginName() { return "Tango"; }
const char* TextLoginPassword() { return "Very top secret"; }
const char* TextReadPastEnd() { return "Pkt read error prevID=%x curID=%x\n"; }
const char* TextNoHandler() { return "Packet has no handler prevID=%x curID=%x\n"; }
const char* TextHandlerFailed() { return "Error during parsing update prevID=%x curID=%x\n"; }
const char* TextUnknownType() { return "Wrong packet ID prevID=%x curID=%x\n"; }

void WriteByte(MemoryStream* out, std::uint8_t value) { out->WriteValue(value); }

const char* QuestPrefix() { return "q."; }

// The quest log refreshed: no items picked up.
void RefreshQuestLog() { g_interface.OnItemsPicked({}, 0, false); }

// The client world shows a number over a unit (id, value, kind).
constexpr int kNumberExperience = 8;

// The experience shown for `amount`: amount times 1.04 ^ (intelligence - 25), the double power times the float
// rounded once to a float (the original's multiply at 24-bit precision), then to the nearest integer. std::pow
// here gives the same 256 doubles as the x87 build's (an equivalence test, docs/notes/cleanup-tooling.md).
int ScaledExperience(const CUnit* unit, float amount) {
    const double power = std::pow(1.04, static_cast<float>(unit->params.intelligence) - 25.0f);
    return lib::RoundEven(lib::RoundedProduct(amount, power));
}

bool IsQuestVariable(const std::string& name) {
    return static_cast<int>(name.size()) > 2 && name.starts_with(std::string_view(QuestPrefix(), 2));
}

constexpr int kKindNoPath = 3;               // OnPathMarker: no path to read
constexpr int kPathParticles = static_cast<int>(ParticleType::ShapeChange);
constexpr int kMarkerParticles = static_cast<int>(ParticleType::GroundDust1);     // 0x203B with the flag
constexpr float kMarkerSpeedA = 0.5f;
constexpr std::uint32_t kMarkerSpeedB = 0x501502F9u;   // float bits (1e10)
constexpr std::uint8_t kRequestRecording = 2;

// Path markers are shown.
bool MarkersShown() { return options::g_options.drawPath != 0; }
const char* TextPartyReceived() { return "party received on client"; }

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

// Model and part names the handler compares with (the exe's strings).
const char* TextCow1() { return "unmoco1"; }
const char* TextCow2() { return "unmoco2"; }
const char* TextHuman() { return "unhuma"; }
const char* TextHumanFemale() { return "unhufe"; }
const char* TextHairPart() { return "hr"; }
const char* TextHairFormat() { return "hr.%.2d"; }
const char* TextGoblin() { return "unmogo"; }
const char* TextAxePart() { return "rh3.axeth00"; }
const char* TextSwordPart() { return "rh3.sword00"; }
const char* TextPikePart() { return "rh3.pike00"; }
const char* TextSlingPart() { return "sling"; }
const char* TextWisp() { return "unmowi"; }
const char* TextSkinFrom() { return "mo%dp"; }
const char* TextSkinTo() { return "mo%ds"; }
const char* TextNone() { return "none"; }

bool Same(const char* a, const char* b) { return std::strcmp(a, b) == 0; }


// FindPart, then RemovePart when there is one (the original inlines both calls).
void RemovePartNamed(CObject3D* object, const char* name) {
    if (FigureMesh* part = object->FindPart(name)) object->RemovePart(part);
}

constexpr int kClientHandlerCount = 0x100;

// The client's message handlers by type.
constexpr HandlerEntry<PlayerClient> kClientHandlers[] = {
    {0x00, &PlayerClient::OnLoginAccepted}, {0x01, &PlayerClient::OnDisconnect}, {0x02, &PlayerClient::OnZone},
    {0x03, &PlayerClient::OnWorldBlock3}, {0x04, &PlayerClient::OnCameraPlacement}, {0x05, &PlayerClient::OnWorldBlock5},
    {0x06, &PlayerClient::OnItemsReceived}, {0x07, &PlayerClient::OnGameState}, {0x08, &PlayerClient::OnZoneChange},
    {0x09, &PlayerClient::OnGameLost}, {0x0A, &PlayerClient::OnItemRemoved}, {0x0B, &PlayerClient::OnUnitSpeech},
    {0x0C, &PlayerClient::OnObjectValues}, {0x0D, &PlayerClient::OnJingle}, {0x0E, &PlayerClient::OnPathMarker},
    {0x0F, &PlayerClient::OnFreeCamera}, {0x12, &PlayerClient::OnWorldValue}, {0x13, &PlayerClient::OnStringEvent},
    {0x14, &PlayerClient::OnItemValue}, {0x15, &PlayerClient::OnExperienceAll}, {0x16, &PlayerClient::OnWeaponRemoved},
    {0x17, &PlayerClient::OnQuestItemsChanged}, {0x18, &PlayerClient::OnExperience}, {0x19, &PlayerClient::OnUnitControl},
    {0x40, &PlayerClient::OnNewObject}, {0x41, &PlayerClient::OnObjectChanged}, {0x42, &PlayerClient::OnObjectRemoved},
    {0x43, &PlayerClient::OnUnitDied}, {0x50, &PlayerClient::OnTimedObjectAdded}, {0x51, &PlayerClient::OnTimedObjectChanged},
    {0x52, &PlayerClient::OnTimedObjectRemoved}, {0x60, &PlayerClient::OnPartyState}, {0x70, &PlayerClient::OnWeather},
    {0x71, &PlayerClient::OnWorldTime}, {0x72, &PlayerClient::OnWorldPoint}, {0x73, &PlayerClient::OnWorldStream},
    {0x80, &PlayerClient::OnParty}, {0x81, &PlayerClient::OnSetVariable}, {0x82, &PlayerClient::OnDeleteVariable},
    {0x83, &PlayerClient::OnPartyUpdate},
};

constexpr int kSessionKindSingle = 1;

std::unordered_set<int> g_partyIdSet;
int ReadDword(MemoryStream* in) {
    int value;
    in->ReadValue(value);
    return value;
}

} // namespace

bool PlayerClient::Pump() {
    netlink::NetLink* link = player->link;
    switch (state) {
    case kStateConnecting: {
        if (link == nullptr || !link->Connected()) return true;
        if (sendParty) {
            WriteByte(&out, kRequestParty);
            player->Party()->SaveParty(&out, 1);
        }
        LoginRequest login;
        std::memset(&login, 0, sizeof(login));
        login.version = kLoginVersion;
        std::memcpy(login.name, TextLoginName(), std::strlen(TextLoginName()) + 1);
        std::memcpy(login.password, TextLoginPassword(), std::strlen(TextLoginPassword()) + 1);
        login.partyId = player->partyId;
        WriteByte(&out, kRequestLogin);
        out.WriteValue(login);
        state = kStateLoggedIn;
        return SendToServer();
    }
    case kStateLoggedIn:
        return true;
    case kStateSendingCamera: {
        const Vector3* anchor = CameraAnchor(g_clientWorld.camera);
        float point[2] = {anchor->x, anchor->y};
        WriteByte(&out, kRequestCamera);
        out.Write(point, sizeof(point));
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
            console::EndLine(console::g_netLogOut << TextClientToServer() << size << TextSqueezedSize() << packedSize);
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
        if (!in.ok) return PlayerMessageError(TextReadPastEnd(), previous, messageType);
        previous = messageType;
        if (in.Read(&messageType, 1) == 0) return 1;
        if (static_cast<int>(messageType) >= std::ssize(handlers)) return PlayerMessageError(TextUnknownType(), previous, messageType);
        auto handler = handlers[messageType];
        if (handler == nullptr) return PlayerMessageError(TextNoHandler(), previous, messageType);
        if (!(this->*handler)()) return PlayerMessageError(TextHandlerFailed(), previous, messageType);
    }
}

int PlayerClient::ReceiveMessages() {
    netlink::NetLink* link = player->link;
    if (link == nullptr) return 0;
    if (!link->HasIncoming()) return 1;
    MemoryStream packet;
    if (!link->Receive(&packet)) return 0;
    packet.DecompressTo(&decompressor, &in);
    return DispatchMessages() != 0 ? 1 : 0;
}

int PlayerClient::OnSetVariable() {
    std::string name = in.ReadString();
    std::uint32_t value;
    in.ReadValue(value);
    NamedFloatTable& variables = player->Party()->variables;
    variables.Get(name);
    variables.Set(name, std::bit_cast<float>(value));
    if (IsQuestVariable(name)) {
        g_interface.OnQuestVariable(name, std::bit_cast<float>(value));
        g_clientWorld.OnQuestVariable(name, std::bit_cast<float>(value));
    }
    return 1;
}

int PlayerClient::OnDeleteVariable() {
    std::string name = in.ReadString();
    player->Party()->variables.Remove(name);
    if (IsQuestVariable(name)) {
        g_interface.OnQuestVariable(name, 0.0f);
        g_clientWorld.OnQuestVariable(name, 0.0f);
    }
    return 1;
}

int PlayerClient::OnZoneChange() {
    std::string zone = in.ReadString();
    int entrance;
    in.ReadValue(entrance);
    g_gameFlow.RequestZoneChange(zone, entrance);
    return 1;
}

int PlayerClient::OnJingle() {
    std::string name = in.ReadString();
    g_clientWorld.music->PlayJingle(name);
    return 1;
}

int PlayerClient::OnZone() {
    MemoryStream block;
    if (in.ReadStream(&block) == 0) return 0;
    player->Clear();
    g_clientWorld.LoadZone(&block);
    return 1;
}

int PlayerClient::OnWorldBlock3() {
    MemoryStream block;
    if (in.ReadStream(&block) == 0) return 0;
    g_clientWorld.LoadItemsAndSpells(&block);
    return 1;
}

int PlayerClient::OnWorldBlock5() {
    MemoryStream block;
    if (in.ReadStream(&block) == 0) return 0;
    g_clientWorld.LoadNewItemsAndSpells(&block);
    return 1;
}

int PlayerClient::OnItemRemoved() {
    int objectId;
    in.ReadValue(objectId);
    player->TakeItem(objectId);
    RefreshQuestLog();
    return 1;
}

int PlayerClient::OnQuestItemsChanged() {
    player->TakeAllItems();
    RefreshQuestLog();
    return 1;
}

int PlayerClient::OnExperienceAll() {
    float amount;
    in.ReadValue(amount);
    CPlayer::UnitMap& units = player->units;
    for (auto node = units.begin(); node != units.end() && node->second; ++node) {
        CUnit* unit = node->second;
        g_clientWorld.ShowNumber(unit->id, ScaledExperience(unit, amount), kNumberExperience);
    }
    return 1;
}

int PlayerClient::OnExperience() {
    int objectId;
    float amount;
    in.ReadValue(objectId);
    in.ReadValue(amount);
    auto* unit = g_clientWorld.FindUnit(objectId);
    if (unit != nullptr) g_clientWorld.ShowNumber(objectId, ScaledExperience(unit, amount), kNumberExperience);
    return 1;
}

// --- Items, party, path markers, string events, recording ---------------------------------------

int PlayerClient::OnItemsReceived() {
    std::vector<Item*> items;
    int& money = player->Party()->money;
    int before = money;
    in.ReadValue(money);
    std::uint8_t flag = 0;   // the original passes this byte with three bytes of stack memory above it
    in.ReadValue(flag);
    int result = 1;
    for (;;) {
        int objectId;
        in.ReadValue(objectId);
        if (objectId == -1) break;
        if (!in.ok) {
            result = 0;
            break;
        }
        auto* manager = g_clientWorld.itemManager;
        if (Item* item = FindItem(manager, static_cast<unsigned>(objectId))) items.insert(items.begin(), item);
    }
    if (result != 0) {
        player->GiveItems(&items, flag);
        g_interface.OnItemsPicked(items, money - before, flag != 0);
    }
    items.clear();
    return result;
}

int PlayerClient::OnParty() {
    CParty* party = player->Party();
    NamedFloatTable saved(party->variables);
    {
        MemoryStream block;
        in.ReadSized(&block);
        block.MemoryStream::Seek(0);
        if (in.ok) {
            party->LoadParty(&block, 1);
            std::vector<std::uint32_t>& recharging = player->recharging;
            recharging.clear();
            ObjectListIteratorOf<Item> walk{};
            for (Item* item = walk.First(party->ItemFactory()); item != nullptr; item = walk.Next()) {
                if (item->chargeMax > item->charge)
                    recharging.push_back(static_cast<std::uint32_t>(item->id));
            }
            if (walk.manager != nullptr) --walk.manager->refCount;    // the iterator's destructor
        }
        if (IsMultiplayerClient()) {
            // A multiplayer client keeps its variables.
            party->variables = saved;
        }
        g_clientWorld.ApplyObjectVariables();
        console::EndLine(console::g_netLogOut << TextPartyReceived());
        g_gameFlow.ReturnLocalParty();
    }
    return 1;
}

int PlayerClient::OnPartyUpdate() {
    MemoryStream block;
    in.ReadSized(&block);
    block.MemoryStream::Seek(0);
    if (in.ok) player->Party()->LoadParty(&block, 0);
    return 1;
}

int PlayerClient::OnPathMarker() {
    DeltaField field;
    ParticleSource* pathSource = nullptr;
    int kind;
    in.ReadValue(kind);
    int flag;
    in.ReadValue(flag);
    Vector3 at{0.0f, 0.0f, 0.0f};
    in.ReadValue(at.x);
    in.ReadValue(at.y);
    int unitId;
    in.ReadValue(unitId);
    float value;
    in.ReadValue(value);
    if (kind != kKindNoPath) {
        auto* walker = g_clientWorld.objectManager->CreateObjectAs<CUnit>(MapObjectType::Hero, -1);
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
        CUnit* unit = ownerObject->WorldContext()->FindUnit(unitId);
        if (unit != nullptr) unit->ClientController()->SetMarkers(pathSource, marker);
    }
    return 1;
}

int PlayerClient::OnStringEvent() {
    std::string text = in.ReadString();
    g_interface.OnStringEvent(text);
    return 1;
}

void PlayerClient::StartRecording(const char* path) {
    netlink::NetLink* link = player->link;
    if (link == nullptr) return;
    WriteByte(&out, kRequestRecording);
    link->OpenRecording(path);
}

void PlayerClient::StopRecording() {
    netlink::NetLink* link = player->link;
    if (link != nullptr) link->CloseRecording();
}

// --- A new map object (message 0x40, multiplayer) ------------------------------------------

int PlayerClient::OnNewObject() {
    std::vector<std::string> parts;
    std::string scriptName;
    std::string secondName;
    NewObjectHeader header;
    in.ReadValue(header);
    Vector3 scale;
    in.ReadValue(scale);
    secondName = in.ReadString();
    if (!in.ok) return 0;
    char model[kTextSize];
    char textureFrom[kTextSize];
    char textureTo[kTextSize];
    char part[kTextSize];
    in.ReadText(model, kTextSize);
    in.ReadText(textureFrom, kTextSize);
    in.ReadText(textureTo, kTextSize);
    std::uint16_t count = 0;   // read into the low half of a stack dword
    in.ReadValue(count);
    parts.resize(count);
    for (std::uint16_t i = 0; i < count; ++i) {
        in.ReadText(part, kTextSize);
        parts[i] = part;
    }
    scriptName = in.ReadString();
    int prototypeValue;
    in.ReadValue(prototypeValue);

    auto* world = player->WorldContext();
    CMapObject* created = world->objectManager->CreateObjectAs<CMapObject>(static_cast<MapObjectType>(header.type), header.id);
    if (created == nullptr) return 0;
    CMapObject* object = created;
    CObject3D* figure = object;
    object->name = model;
    bool isUnit = created->GetClassId() == ClassId::Unit;
    created->prototypeId = header.prototype;
    figure->SetFigureScale(&scale.x);
    object->baseScale = scale;
    object->ApplyBaseScale();
    object->name2 = secondName.c_str();
    if (isUnit) {
        CUnit* unit = UnitOrNull(created);
        const UnitPrototypeRecord* prototype = unitdb::PrototypeRecord(header.prototype);
        auto* modelRecord = unitdb::ModelRecord(static_cast<std::uint32_t>(prototype->modelIndex));
        unit->defaultScale.x = 0.0f;
        unit->defaultScale.y = modelRecord->height;
        unit->defaultScale.z = 0.0f;
        std::memcpy(unit->actionSpeeds, modelRecord->actionSpeeds, sizeof(unit->actionSpeeds));
        unit->SetSecondPrototypeIndex(prototypeValue);
    }
    if (header.flags & kHeaderFlagHidden) created->flags |= kHeaderFlagHidden;
    if (Same(model, TextCow1()) || Same(model, TextCow2())) created->flags |= kFlagCow;
    if (created->MapType() == MapObjectType::Hero && (Same(model, TextHuman()) || Same(model, TextHumanFemale()))) {
        // The head part takes the prototype's hair, or goes without one.
        for (std::size_t i = 0; i < parts.size(); ++i) {
            if (str::Compare(parts[i], TextHairPart()) != 0) continue;
            int hair = unitdb::PrototypeRecord(header.prototype)->hair;
            if (hair == -1) parts.erase(parts.begin() + static_cast<std::ptrdiff_t>(i));
            else parts[i] = str::Format(TextHairFormat(), hair);
            break;
        }
    }
    if (figure->MakeFigure(model, &parts, 1) == 0) return 0;   // the object is left made
    if (Same(model, TextGoblin())) {
        RemovePartNamed(figure, TextAxePart());
        RemovePartNamed(figure, TextSwordPart());
        bool pike = static_cast<int>(unitdb::PrototypeRecord(created->prototypeId)->naturalWeapon) == kWeaponPike;
        RemovePartNamed(figure, pike ? TextPikePart() : TextSlingPart());
    }
    if (Same(model, TextWisp())) {
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
        std::snprintf(textureFrom, sizeof(textureFrom), TextSkinFrom(), created->id);
        std::snprintf(textureTo, sizeof(textureTo), TextSkinTo(), created->id);
    } else if (Same(textureTo, TextNone())) {
        to = std::nullopt;
    }
    attached->SetSkins(textureFrom, to);
    if (isUnit) created->SetScriptName(scriptName);

    // The player keeps it, counted.
    CPlayer* currentPlayer = player;
    if (header.id > currentPlayer->maxObjectId) currentPlayer->maxObjectId = header.id;
    currentPlayer->knownObjects[header.id] = object;
    created->AddRef();
    if (CUnit* unit = UnitOrNull(created))
        object->TakePrototype(unit->GetPrototype()->name);
    else
        object->TakePrototype(model);
    object->WriteFullState();
    if (object->ReadState(0, &in) == 0) return 0;
    g_clientWorld.AddObject(object);
    if (object->name2[0] != 0) {
        const std::string variable = g_clientWorld.ObjectVariableName(object->name2);
        object->SetVisibilityLevel(currentPlayer->Party()->variables.Get(variable));
    }
    if (currentPlayer->sessionKind == kSessionClient) {
        if (CUnit* unit = UnitOrNull(created)) unit->ClientController()->LoadFaceTexture();
    }
    return 1;
}

// --- Clearing the connection ---------------------------------------------------------------

void PlayerClient::CreateClient() {
    state = 0;
    partyUnitIds.clear();
    in.Resize(0);
    out.Resize(0);
    localSession = 0;
    sendParty = 0;
    compressor.Reset();
    decompressor.Reset();
    handlers.resize(kClientHandlerCount);
    std::fill_n(handlers.data(), std::ssize(handlers), Handler{});
    for (const auto& entry : kClientHandlers)
        handlers[entry.type] = entry.handler;
}

void PlayerClient::Clear() {
    state = 0;
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

// The streams are members (constructed before the body; the original builds them after the handler table).
PlayerClient::PlayerClient() {
    Clear();
}

// The streams and coders are members, destroyed after the body (the original frees them before the handler table).
PlayerClient::~PlayerClient() {
    Clear();
}

const char* PlayerClient::ClassName() { return "CPlayerClient"; }
ClassId PlayerClient::GetClassId() { return ClassId::PlayerClient; }

// --- The party state ---------------------------------------------------------------------

int PlayerClient::OnPartyState() {
    CPlayer* currentPlayer = player;
    for (int i = 0; i < std::ssize(partyUnitIds); ++i) {
        g_partyIdSet.insert(partyUnitIds[i]);
        CUnit* unit = currentPlayer->WorldContext()->FindUnit(partyUnitIds[i]);
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
        auto* unit = currentPlayer->WorldContext()->FindUnit(objectId);
        if (unit == nullptr) continue;
        unit->flags |= MapObjectFlag::InParty;
        if (!g_partyIdSet.contains(objectId) && currentPlayer->sessionKind == kSessionKindSingle && unit->player == nullptr &&
            g_clientWorld.ticksRun >= 1)
            unit->Controller()->RestartFadeIn();
    }
    g_partyIdSet.clear();

    currentPlayer->ClientWorld()->partyDanger = ReadDword(&in);
    int others = ReadDword(&in);
    otherUnitIds.resize(others);
    for (int i = 0; i < others; ++i) in.ReadValue(otherUnitIds[i]);
    return 1;
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
