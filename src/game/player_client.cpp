// CPlayerClient: the client's per-tick link traffic (sending the queued requests, receiving and
// running the server's messages) and its login. See player_client.h and docs/notes/objects.md.

#include "player_client.h"

#include "ai_map.h"
#include "app_helpers.h"
#include "base_manager.h"
#include "camera.h"
#include "console.h"
#include "effects.h"
#include "game_singletons.h"
#include "item.h"
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

#include "lib/static_object.h"
#include "lib/str.h"

#include "ei/x87.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <list>
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
    std::uint32_t partyId; // the player's partyId_
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

void WriteByte(CMemoryStream* out, std::uint8_t value) { out->WriteValue(value); }

const char* QuestPrefix() { return "q."; }

// The quest log refreshed with an empty list built and destroyed on the stack.
void RefreshQuestLog() {
    std::list<void*> none;
    g_interface->OnItemsPicked(&none, 0, 0);
}

// The client world shows a number over a unit (id, value, kind).
constexpr int kNumberExperience = 8;

// The experience shown for `amount`, rounded to the nearest integer through a float.
int ScaledExperience(const CUnit* unit, float amount) {
    double base = 1.04;
    float offset = 25.0f;
    float scaled = static_cast<float>(std::pow(base, static_cast<double>(unit->params.intelligence) - offset) * amount);
    return x87::RoundToInt(scaled);
}

bool IsQuestVariable(const std::string& name) {
    return static_cast<int>(name.size()) > 2 && name.starts_with(std::string_view(QuestPrefix(), 2));
}

constexpr int kKindNoPath = 3;               // OnPathMarker: no path to read
constexpr int kPathParticles = 0x2039;
constexpr int kMarkerParticles = 0x203A;     // 0x203B with the flag
constexpr float kMarkerSpeedA = 0.5f;
constexpr std::uint32_t kMarkerSpeedB = 0x501502F9u;   // float bits (1e10)
constexpr std::uint8_t kRequestRecording = 2;

// Path markers are shown.
bool MarkersShown() { return options::g_options->drawPath_ != 0; }
const char* TextPartyReceived() { return "party received on client"; }

constexpr std::uint32_t kHeaderFlagHidden = 8;
constexpr std::uint32_t kFlagNoParts = 0x200;
constexpr std::uint32_t kFlagCow = 0x10000000;
constexpr std::uint32_t kPartFlagFigure = 8;
constexpr int kWeaponPike = 5;
constexpr int kWispParticles = 0x204D;
constexpr float kWispSize = 0.05f;
constexpr int kTextSize = 0x100;
constexpr int kSessionClient = 3;

// The header of message 0x40.
struct NewObjectHeader {
    int id;
    int type;
    std::uint32_t prototype;   // the object's prototypeId_
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

const UnitPrototypeRecord* PrototypeRecord(std::uint32_t index) {
    return unitdb::PrototypeRecord(index);
}

// FindPart, then RemovePart when there is one (the original inlines both calls).
void RemovePartNamed(CObject3D* object, const char* name) {
    if (CFigureMesh* part = object->FindPart(name)) object->RemovePart(part);
}

constexpr int kClientHandlerCount = 0x100;

// The client's message handlers by type.
constexpr HandlerEntry<CPlayerClient> kClientHandlers[] = {
    {0x00, &CPlayerClient::OnLoginAccepted}, {0x01, &CPlayerClient::OnDisconnect}, {0x02, &CPlayerClient::OnZone},
    {0x03, &CPlayerClient::OnWorldBlock3}, {0x04, &CPlayerClient::OnCameraPlacement}, {0x05, &CPlayerClient::OnWorldBlock5},
    {0x06, &CPlayerClient::OnItemsReceived}, {0x07, &CPlayerClient::OnGameState}, {0x08, &CPlayerClient::OnZoneChange},
    {0x09, &CPlayerClient::OnGameLost}, {0x0A, &CPlayerClient::OnItemRemoved}, {0x0B, &CPlayerClient::OnUnitSpeech},
    {0x0C, &CPlayerClient::OnObjectValues}, {0x0D, &CPlayerClient::OnJingle}, {0x0E, &CPlayerClient::OnPathMarker},
    {0x0F, &CPlayerClient::OnFreeCamera}, {0x12, &CPlayerClient::OnWorldValue}, {0x13, &CPlayerClient::OnStringEvent},
    {0x14, &CPlayerClient::OnItemValue}, {0x15, &CPlayerClient::OnExperienceAll}, {0x16, &CPlayerClient::OnWeaponRemoved},
    {0x17, &CPlayerClient::OnQuestItemsChanged}, {0x18, &CPlayerClient::OnExperience}, {0x19, &CPlayerClient::OnUnitControl},
    {0x40, &CPlayerClient::OnNewObject}, {0x41, &CPlayerClient::OnObjectChanged}, {0x42, &CPlayerClient::OnObjectRemoved},
    {0x43, &CPlayerClient::OnUnitDied}, {0x50, &CPlayerClient::OnTimedObjectAdded}, {0x51, &CPlayerClient::OnTimedObjectChanged},
    {0x52, &CPlayerClient::OnTimedObjectRemoved}, {0x60, &CPlayerClient::OnPartyState}, {0x70, &CPlayerClient::OnWeather},
    {0x71, &CPlayerClient::OnWorldTime}, {0x72, &CPlayerClient::OnWorldPoint}, {0x73, &CPlayerClient::OnWorldStream},
    {0x80, &CPlayerClient::OnParty}, {0x81, &CPlayerClient::OnSetVariable}, {0x82, &CPlayerClient::OnDeleteVariable},
    {0x83, &CPlayerClient::OnPartyUpdate},
};

constexpr std::uint32_t kUnitInClientParty = 0x400000;
constexpr int kSessionKindSingle = 1;

// Built by the static initialiser and destroyed by the exit function it registers (docs/notes/standalone.md).
constinit lib::StaticObject<std::unordered_set<int>> g_partyIdSet = /*007C8370*/ {};
void DestroyPartyIdSet() { g_partyIdSet.Destroy(); }
int ReadDword(CMemoryStream* in) {
    int value;
    in->ReadValue(value);
    return value;
}

} // namespace

int CPlayerClient::Pump() {
    netlink::NetLink* link = player_->link_;
    switch (state_) {
    case kStateConnecting: {
        if (link == nullptr || !link->Connected()) return 1;
        if (sendParty_) {
            WriteByte(&out_, kRequestParty);
            player_->Party()->SaveParty(&out_, 1);
        }
        LoginRequest login;
        std::memset(&login, 0, sizeof(login));
        login.version = kLoginVersion;
        std::memcpy(login.name, TextLoginName(), std::strlen(TextLoginName()) + 1);
        std::memcpy(login.password, TextLoginPassword(), std::strlen(TextLoginPassword()) + 1);
        login.partyId = player_->partyId_;
        WriteByte(&out_, kRequestLogin);
        out_.WriteValue(login);
        state_ = kStateLoggedIn;
        return SendToServer();
    }
    case kStateLoggedIn:
        return 1;
    case kStateSendingCamera: {
        const Vector3* anchor = CameraAnchor(g_clientWorld->camera_);
        float point[2] = {anchor->x, anchor->y};
        WriteByte(&out_, kRequestCamera);
        out_.Write(point, sizeof(point));
        return SendToServer();
    }
    default:
        return SendToServer();
    }
}

int CPlayerClient::SendToServer() {
    netlink::NetLink* link = player_->link_;
    if (link == nullptr) return 1;
    int sent = 0;
    if (link->Connected()) {
        CMemoryStream packed;
        out_.CompressTo(&compressor_, &packed);
        if (player_->logTraffic_ != 0) {
            std::uint32_t packedSize = packed.size_;
            std::uint32_t size = out_.Size();
            console::EndLine(console::g_netLogOut << TextClientToServer() << size << TextSqueezedSize() << packedSize);
        }
        sent = link->Send(&packed) ? 1 : 0;
    }
    out_.Resize(0);
    return sent;
}

int CPlayerClient::DispatchMessages() {
    in_.Seek(0);
    in_.ok_ = 1;
    std::uint8_t type = 0xFF;
    std::uint8_t previous = 0xFF;
    for (;;) {
        if (!in_.ok_) return PlayerMessageError(TextReadPastEnd(), previous, type);
        previous = type;
        if (in_.Read(&type, 1) == 0) return 1;
        if (static_cast<int>(type) >= std::ssize(handlers_)) return PlayerMessageError(TextUnknownType(), previous, type);
        auto handler = handlers_[type];
        if (handler == nullptr) return PlayerMessageError(TextNoHandler(), previous, type);
        if (!(this->*handler)()) return PlayerMessageError(TextHandlerFailed(), previous, type);
    }
}

int CPlayerClient::ReceiveMessages() {
    netlink::NetLink* link = player_->link_;
    if (link == nullptr) return 0;
    if (!link->HasIncoming()) return 1;
    CMemoryStream packet;
    if (!link->Receive(&packet)) return 0;
    packet.DecompressTo(&decompressor_, &in_);
    return DispatchMessages() != 0 ? 1 : 0;
}

std::string* CPlayerClient::ReadText(std::string* out) {
    in_.ReadString(out);
    return out;
}

int CPlayerClient::OnSetVariable() {
    std::string name;
    in_.ReadString(&name);
    std::uint32_t value;
    in_.ReadValue(value);
    NamedFloatTable& variables = player_->Party()->variables_;
    variables.Get(name);
    variables.Set(name, std::bit_cast<float>(value));
    if (IsQuestVariable(name)) {
        g_interface->OnQuestVariable(name, std::bit_cast<float>(value));
        g_clientWorld->OnQuestVariable(name, std::bit_cast<float>(value));
    }
    return 1;
}

int CPlayerClient::OnDeleteVariable() {
    std::string name;
    in_.ReadString(&name);
    player_->Party()->variables_.Remove(name);
    if (IsQuestVariable(name)) {
        g_interface->OnQuestVariable(name, 0.0f);
        g_clientWorld->OnQuestVariable(name, 0.0f);
    }
    return 1;
}

int CPlayerClient::OnZoneChange() {
    std::string zone;
    ReadText(&zone);
    int entrance;
    in_.ReadValue(entrance);
    g_gameFlow->RequestZoneChange(zone, entrance);
    return 1;
}

int CPlayerClient::OnJingle() {
    std::string name;
    in_.ReadString(&name);
    g_clientWorld->music_->PlayJingle(name);
    return 1;
}

int CPlayerClient::OnZone() {
    CMemoryStream block;
    if (in_.ReadStream(&block) == 0) return 0;
    player_->Clear();
    g_clientWorld->LoadZone(&block);
    return 1;
}

int CPlayerClient::OnWorldBlock3() {
    CMemoryStream block;
    if (in_.ReadStream(&block) == 0) return 0;
    g_clientWorld->LoadItemsAndSpells(&block);
    return 1;
}

int CPlayerClient::OnWorldBlock5() {
    CMemoryStream block;
    if (in_.ReadStream(&block) == 0) return 0;
    g_clientWorld->LoadNewItemsAndSpells(&block);
    return 1;
}

int CPlayerClient::OnItemRemoved() {
    int id;
    in_.ReadValue(id);
    player_->TakeItem(id);
    RefreshQuestLog();
    return 1;
}

int CPlayerClient::OnQuestItemsChanged() {
    player_->TakeAllItems();
    RefreshQuestLog();
    return 1;
}

int CPlayerClient::OnExperienceAll() {
    float amount;
    in_.ReadValue(amount);
    CPlayer::UnitMap& units = player_->units_;
    for (auto node = units.begin(); node != units.end() && node->second; ++node) {
        CUnit* unit = node->second;
        g_clientWorld->ShowNumber(unit->id_, ScaledExperience(unit, amount), kNumberExperience);
    }
    return 1;
}

int CPlayerClient::OnExperience() {
    int id;
    float amount;
    in_.ReadValue(id);
    in_.ReadValue(amount);
    auto* unit = g_clientWorld->FindUnit(id);
    if (unit != nullptr) g_clientWorld->ShowNumber(id, ScaledExperience(unit, amount), kNumberExperience);
    return 1;
}

// --- Items, party, path markers, string events, recording ---------------------------------------

int CPlayerClient::OnItemsReceived() {
    std::list<CItem*> items;
    int& money = player_->Party()->money_;
    int before = money;
    in_.ReadValue(money);
    std::uint8_t flag = 0;   // the original passes this byte with three bytes of stack memory above it
    in_.ReadValue(flag);
    int result = 1;
    for (;;) {
        int id;
        in_.ReadValue(id);
        if (id == -1) break;
        if (!in_.ok_) {
            result = 0;
            break;
        }
        auto* manager = g_clientWorld->itemManager_;
        if (CItem* item = FindItem(manager, static_cast<unsigned>(id))) items.push_front(item);
    }
    if (result != 0) {
        player_->GiveItems(&items, flag);
        g_interface->OnItemsPicked(&items, money - before, flag);
    }
    items.clear();
    return result;
}

int CPlayerClient::OnParty() {
    CParty* party = player_->Party();
    NamedFloatTable saved(party->variables_);
    {
        CMemoryStream block;
        in_.ReadSized(&block);
        block.CMemoryStream::Seek(0);
        if (in_.ok_) {
            party->LoadParty(&block, 1);
            std::list<std::uint32_t>& recharging = player_->recharging_;
            recharging.clear();
            ObjectListIteratorOf<CItem> walk{};
            for (CItem* item = walk.First(party->ItemFactory()); item != nullptr; item = walk.Next()) {
                if (item->chargeMax_ > item->charge_)
                    recharging.push_back(static_cast<std::uint32_t>(item->id_));
            }
            if (walk.manager != nullptr) --walk.manager->refCount_;   // the iterator's destructor
        }
        if (IsMultiplayerClient()) {
            // A multiplayer client keeps its variables.
            party->variables_ = saved;
        }
        g_clientWorld->ApplyObjectVariables();
        console::EndLine(console::g_netLogOut << TextPartyReceived());
        g_gameFlow->ReturnLocalParty();
    }
    return 1;
}

int CPlayerClient::OnPartyUpdate() {
    CMemoryStream block;
    in_.ReadSized(&block);
    block.CMemoryStream::Seek(0);
    if (in_.ok_) player_->Party()->LoadParty(&block, 0);
    return 1;
}

int CPlayerClient::OnPathMarker() {
    DeltaField field;
    CParticleSource* pathSource = nullptr;
    int kind;
    in_.ReadValue(kind);
    int flag;
    in_.ReadValue(flag);
    Vector3 at{0.0f, 0.0f, 0.0f};
    in_.ReadValue(at.x);
    in_.ReadValue(at.y);
    int unitId;
    in_.ReadValue(unitId);
    float value;
    in_.ReadValue(value);
    if (kind != kKindNoPath) {
        auto* walker = g_clientWorld->objectManager_->CreateObjectAs<CUnit>(MapObjectType::Hero, -1);
        ++walker->refCount_;
        PathOrder* path = &walker->ClientController()->motion;
        DeltaField* writing = field.BeginWriteB(0);
        path->WriteState(writing);
        writing->EndField();
        DeltaField* reading = field.BeginReadB(0, &in_);
        path->ReadState(reading);
        reading->EndField();
        if (MarkersShown()) {
            walker->SetMotionSpeeds(kMarkerSpeedA, std::bit_cast<float>(kMarkerSpeedB));
            auto* source = g_clientWorld->particleManager_->CreateSource(kPathParticles);
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
        auto* marker = g_clientWorld->particleManager_->CreateSource(flag != 0 ? kMarkerParticles + 1 : kMarkerParticles);
        marker->localPosition_ = at;
        CEngineObject* owner = player_;
        CUnit* unit = owner->WorldContext()->FindUnit(unitId);
        if (unit != nullptr) unit->ClientController()->SetMarkers(pathSource, marker);
    }
    return 1;
}

int CPlayerClient::OnStringEvent() {
    std::string text;
    ReadText(&text);
    g_interface->OnStringEvent(text);
    return 1;
}

void CPlayerClient::StartRecording(const char* path) {
    netlink::NetLink* link = player_->link_;
    if (link == nullptr) return;
    WriteByte(&out_, kRequestRecording);
    link->OpenRecording(path);
}

void CPlayerClient::StopRecording() {
    netlink::NetLink* link = player_->link_;
    if (link != nullptr) link->CloseRecording();
}

// --- A new map object (message 0x40, multiplayer) ------------------------------------------

int CPlayerClient::OnNewObject() {
    app::DelayedProtectionCheck();
    std::vector<std::string> parts;
    std::string scriptName;
    std::string secondName;
    NewObjectHeader header;
    in_.ReadValue(header);
    Vector3 scale;
    in_.ReadValue(scale);
    in_.ReadString(&secondName);
    if (!in_.ok_) return 0;
    char model[kTextSize];
    char textureFrom[kTextSize];
    char textureTo[kTextSize];
    char part[kTextSize];
    in_.ReadText(model, kTextSize);
    in_.ReadText(textureFrom, kTextSize);
    in_.ReadText(textureTo, kTextSize);
    std::uint16_t count = 0;   // read into the low half of a stack dword
    in_.ReadValue(count);
    parts.resize(count);
    for (std::uint16_t i = 0; i < count; ++i) {
        in_.ReadText(part, kTextSize);
        parts[i] = part;
    }
    in_.ReadString(&scriptName);
    int prototypeValue;
    in_.ReadValue(prototypeValue);

    auto* world = player_->WorldContext();
    CMapObject* created = world->objectManager_->CreateObjectAs<CMapObject>(static_cast<MapObjectType>(header.type), header.id);
    if (created == nullptr) return 0;
    CMapObject* object = created;
    CObject3D* figure = object;
    object->name = model;
    bool isUnit = created->GetClassId() == kClassUnit;
    created->prototypeId_ = header.prototype;
    figure->SetFigureScale(&scale.x);
    object->baseScale = scale;
    object->ApplyBaseScale();
    object->name2 = secondName.c_str();
    if (isUnit) {
        CUnit* unit = UnitOrNull(created);
        const UnitPrototypeRecord* prototype = PrototypeRecord(header.prototype);
        auto* modelRecord = unitdb::ModelRecord(static_cast<std::uint32_t>(prototype->modelIndex));
        unit->defaultScale.x = 0.0f;
        unit->defaultScale.y = modelRecord->height;
        unit->defaultScale.z = 0.0f;
        std::memcpy(unit->actionSpeeds, modelRecord->actionSpeeds, sizeof(unit->actionSpeeds));
        unit->SetSecondPrototypeIndex(prototypeValue);
    }
    if (header.flags & kHeaderFlagHidden) created->flags_ |= kHeaderFlagHidden;
    if (Same(model, TextCow1()) || Same(model, TextCow2())) created->flags_ |= kFlagCow;
    if (created->MapType() == MapObjectType::Hero && (Same(model, TextHuman()) || Same(model, TextHumanFemale()))) {
        // The head part takes the prototype's hair, or goes without one.
        for (std::size_t i = 0; i < parts.size(); ++i) {
            if (str::Compare(parts[i], TextHairPart()) != 0) continue;
            int hair = PrototypeRecord(header.prototype)->hair;
            if (hair == -1) parts.erase(parts.begin() + static_cast<std::ptrdiff_t>(i));
            else parts[i] = str::Format(TextHairFormat(), hair);
            break;
        }
    }
    if (figure->MakeFigure(model, &parts, 1) == 0) return 0;   // the object is left made
    if (Same(model, TextGoblin())) {
        RemovePartNamed(figure, TextAxePart());
        RemovePartNamed(figure, TextSwordPart());
        bool pike = static_cast<int>(PrototypeRecord(created->prototypeId_)->naturalWeapon) == kWeaponPike;
        RemovePartNamed(figure, pike ? TextPikePart() : TextSlingPart());
    }
    if (Same(model, TextWisp())) {
        auto* source =
            g_clientWorld->particleManager_->CreateSource(kWispParticles);
        source->Attach(object);
        source->ScaleSize(kWispSize / source->baseSize);
        source->attachPoint = 0;
    }
    if (!isUnit) {
        for (int i = 0; i < std::ssize(figure->parts); ++i) figure->parts[i]->flags_ |= kPartFlagFigure;
        created->flags_ |= kFlagNoParts;
    }
    CObject3DClientSpecific* attached = figure->ClientSpecific();
    attached->RegisterPartMaterials();
    // (The client world's two functions around the texture swap are empty.)
    figure->PlayAnimation(nullptr);
    figure->RestartPose();
    std::optional<std::string_view> to = std::string_view(textureTo);
    if (created->MapType() == MapObjectType::Hero) {
        std::snprintf(textureFrom, sizeof(textureFrom), TextSkinFrom(), created->id_);
        std::snprintf(textureTo, sizeof(textureTo), TextSkinTo(), created->id_);
    } else if (Same(textureTo, TextNone())) {
        to = std::nullopt;
    }
    attached->SetSkins(textureFrom, to);
    if (isUnit) created->SetScriptName(scriptName);

    // The player keeps it, counted.
    CPlayer* player = player_;
    if (header.id > player->maxObjectId_) player->maxObjectId_ = header.id;
    player->knownObjects_[header.id] = object;
    ++created->refCount_;
    if (CUnit* unit = UnitOrNull(created))
        object->TakePrototype(unit->GetPrototype()->name);
    else
        object->TakePrototype(model);
    object->WriteFullState();
    if (object->ReadState(0, &in_) == 0) return 0;
    g_clientWorld->AddObject(object);
    if (object->name2[0] != 0) {
        const std::string variable = g_clientWorld->ObjectVariableName(object->name2);
        object->SetVisibilityLevel(player->Party()->variables_.Get(variable));
    }
    if (player->sessionKind_ == kSessionClient) {
        if (CUnit* unit = UnitOrNull(created)) unit->ClientController()->LoadFaceTexture();
    }
    return 1;
}

// --- Clearing the connection ---------------------------------------------------------------

void CoderState::Reset() {
    out.End();
    Construct();
}

BOOL CPlayerClient::CreateClient() {
    state_ = 0;
    partyUnitIds_.clear();
    in_.Resize(0);
    out_.Resize(0);
    localSession_ = 0;
    sendParty_ = 0;
    compressor_.Reset();
    decompressor_.Reset();
    handlers_.resize(kClientHandlerCount);
    std::fill_n(handlers_.data(), std::ssize(handlers_), Handler{});
    for (const auto& entry : kClientHandlers)
        handlers_[entry.type] = entry.handler;
    return 1;
}

void CPlayerClient::Clear() {
    state_ = 0;
    partyUnitIds_.clear();
    in_.Resize(0);
    out_.Resize(0);
    localSession_ = 0;
    sendParty_ = 0;
    compressor_.Reset();
    decompressor_.Reset();
}

// --- Constructor and destructor ------------------------------------------------------------

CPlayerConnection::CPlayerConnection() {
    ClearFields();
    player_ = nullptr;
}

CPlayerConnection::~CPlayerConnection() { player_ = nullptr; }

// The streams are members (constructed before the body; the original builds them after the handler table).
CPlayerClient::CPlayerClient() {
    compressor_.Construct();
    decompressor_.Construct();
    Clear();
}

// The streams are members, destroyed after the body (the original frees them before the handler table).
CPlayerClient::~CPlayerClient() {
    Clear();
    decompressor_.out.End();
    compressor_.out.End();
}

const char* CPlayerClient::ClassName() { return "CPlayerClient"; }
ClassId CPlayerClient::GetClassId() { return kClassPlayerClient; }

// --- The party state ---------------------------------------------------------------------

void ConstructPartyIdSet() {
    g_partyIdSet.Construct();
    std::atexit(DestroyPartyIdSet);
}

int CPlayerClient::OnPartyState() {
    CPlayer* player = player_;
    for (int i = 0; i < std::ssize(partyUnitIds_); ++i) {
        g_partyIdSet->insert(partyUnitIds_[i]);
        CUnit* unit = player->WorldContext()->FindUnit(partyUnitIds_[i]);
        if (unit != nullptr) unit->flags_ &= ~kUnitInClientParty;
    }

    std::uint8_t paused;
    in_.ReadValue(paused);
    player->ClientWorld()->paused_ = paused != 0 ? 1 : 0;
    ReadDword(&in_);   // the server's possessedId_, not used

    int count = ReadDword(&in_);
    partyUnitIds_.resize(count);
    for (int i = 0; i < count; ++i) {
        int id = ReadDword(&in_);
        partyUnitIds_[i] = id;
        auto* unit = player->WorldContext()->FindUnit(id);
        if (unit == nullptr) continue;
        unit->flags_ |= kUnitInClientParty;
        if (!g_partyIdSet->contains(id) && player->sessionKind_ == kSessionKindSingle && unit->player == nullptr &&
            g_clientWorld->ticksRun_ >= 1)
            unit->Controller()->RestartFadeIn();
    }
    g_partyIdSet->clear();

    player->ClientWorld()->partyDanger_ = ReadDword(&in_);
    int others = ReadDword(&in_);
    otherUnitIds_.resize(others);
    for (int i = 0; i < others; ++i) in_.ReadValue(otherUnitIds_[i]);
    return 1;
}

// --- Messages appended to the received stream -----------------------------------------------

void CPlayerClient::AppendReceived(CMemoryStream* source) {
    const std::uint32_t position = in_.Position();
    in_.Seek(in_.Size());
    in_.EndBits();
    source->Seek(0);
    in_.Write(source->data_, source->Size());
    in_.Seek(position);
}
