// CPlayerClient: the client's per-tick link traffic (sending the queued requests, receiving and
// running the server's messages) and its login. See player_client.h and docs/notes/objects.md.

#include "ai_map.h"
#include "map_object_shadow.h"
#include "object_tables.h"
#include "player_client.h"

#include "app_helpers.h"
#include "lib/crt_iostream.h"
#include "camera.h"
#include "originals_engine.h"
#include "originals_world.h"
#include "net_link.h"
#include "originals_gameplay.h"
#include "originals_library.h"
#include "lib/static_object.h"
#include "party.h"
#include "player_messages.h"
#include "sound.h"
#include "ui.h"
#include "unit.h"
#include "world.h"
#include "base_manager.h"
#include "effects.h"
#include "item.h"
#include "resource_manager.h"
#include "originals_ai.h"
#include "originals_effects.h"
#include "originals_ui.h"
#include "unit_client_models.h"
#include "unit_db.h"
#include "originals_units.h"
#include "console.h"
#include "map_object.h"
#include "map_object_figures.h"
#include "lib/crt.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "ei/x87.h"
#include "game_data.h"
#include "game_singletons.h"

#include <cmath>
#include <cstring>


namespace {

constexpr std::uint8_t kRequestLogin = 0;
constexpr std::uint8_t kRequestParty = 8;
constexpr std::uint8_t kRequestCamera = 0x40;
constexpr int kLoginVersion = 100;
constexpr int kStateConnecting = 0;
constexpr int kStateLoggedIn = 1;
constexpr int kStateSendingCamera = 2;

// The login request (message 0, 0x88 bytes). The original copies the two texts into stack buffers
// and sends the bytes after them uninitialised; here they are zero.
struct LoginRequest {
    int version;
    char name[64];
    char password[64];
    std::uint32_t partyId; // the player's partyId_ (+0xFC)
};
static_assert(sizeof(LoginRequest) == 0x88);

CCamera* Camera() { return ei::GameData<CCamera*>(0x007AF7C8); }
libcrt::Ostream* GameCout() { return ei::GamePointer<libcrt::Ostream>(0x007C56B0); }
// The texts the original reads from the exe: the traffic log, the login, the dispatch errors.
const char* TextClientToServer() { return "client to server, size = "; }
const char* TextSqueezedSize() { return " squeezed size = "; }
const char* TextLoginName() { return "Tango"; }
const char* TextLoginPassword() { return "Very top secret"; }
const char* TextReadPastEnd() { return "Pkt read error prevID=%x curID=%x\n"; }
const char* TextNoHandler() { return "Packet has no handler prevID=%x curID=%x\n"; }
const char* TextHandlerFailed() { return "Error during parsing update prevID=%x curID=%x\n"; }
const char* TextUnknownType() { return "Wrong packet ID prevID=%x curID=%x\n"; }

void WriteByte(BitStream* out, std::uint8_t value) { out->Write(&value, 1); }

// A CString the stream reader constructs in place (CStream::ReadString), destroyed at the end of
// the scope.
struct TextBuffer {
    alignas(4) unsigned char bytes[sizeof(mfc::CString)];
    ~TextBuffer() { Get()->~CString(); }
    mfc::CString* Get() { return reinterpret_cast<mfc::CString*>(bytes); }
};

const char* QuestPrefix() { return "q."; }

// A memory stream on the stack, as the original builds (0041DD70 inlined) and destroys it.
struct MemoryStream {
    alignas(4) unsigned char bytes[sizeof(CMemoryStream)];
    MemoryStream() { Get()->Construct(); }
    ~MemoryStream() { Get()->Destruct(); }
    CMemoryStream* Get() { return reinterpret_cast<CMemoryStream*>(bytes); }
};

// The client world's readers of the zone (00567AB0, Rebirth-patched) and two other state blocks
// (00567A60, 00567950); all RET 4 with the block.
__declspec(naked) int __fastcall ClientLoadZone(void* /*world*/, void*, CMemoryStream* /*block*/) EI_ORIGINAL(0x00567AB0)
// 005F7F30 (RET 0xC): the quest log (007C7D50) passes (list, a, b) to its top screen's slot 28.
void* EmptyListVtable() { return ei::GamePointer<void>(0x0073BE80); }   // the CPtrList passed to it
constexpr int kListBlockSize = 10;

// The quest log refreshed with an empty CPtrList built and destroyed on the stack.
void RefreshQuestLog() {
    mfc::CTypedPtrList<void> none{EmptyListVtable(), nullptr, nullptr, 0, nullptr, nullptr, kListBlockSize};
    TheInterface()->OnItemsPicked(&none, 0, 0);
    // The list's inlined destructor (RemoveAll; nothing was added).
    none.head = none.tail = nullptr;
    none.count = 0;
    none.free = nullptr;
    mfc::PlexFreeDataChain(none.blocks);
}

// 0056CE40 (RET 0xC): the client world shows a number over a unit (id, value, kind).
constexpr int kNumberExperience = 8;

// The experience shown for `amount`: amount * 0073E128 ^ (+0x33E - 0073E124), stored to a float,
// then rounded to the nearest integer (FISTP).
int ScaledExperience(const CUnit* unit, float amount) {
    double base = 1.04 /*0073E128*/;
    float offset = 25.0f /*0073E124*/;
    float scaled = static_cast<float>(std::pow(base, static_cast<double>(unit->attackRate) - offset) * amount);
    return x87::RoundToInt(scaled);
}

bool IsQuestVariable(const mfc::CString& name) {
    return name.GetLength() > 2 && std::strncmp(name, QuestPrefix(), 2) == 0;
}

} // namespace

int CPlayerClient::Pump() {
    netlink::NetLink* link = Owner()->link_;
    switch (state_) {
    case kStateConnecting: {
        if (link == nullptr || !link->Connected()) return 1;
        if (sendParty_) {
            WriteByte(&out_, kRequestParty);
            Owner()->Party()->SaveParty(&out_, 1);
        }
        LoginRequest login;
        std::memset(&login, 0, sizeof(login));
        login.version = kLoginVersion;
        std::memcpy(login.name, TextLoginName(), std::strlen(TextLoginName()) + 1);
        std::memcpy(login.password, TextLoginPassword(), std::strlen(TextLoginPassword()) + 1);
        login.partyId = Owner()->partyId_;
        WriteByte(&out_, kRequestLogin);
        out_.Write(&login, sizeof(login));
        state_ = kStateLoggedIn;
        return SendToServer();
    }
    case kStateLoggedIn:
        return 1;
    case kStateSendingCamera: {
        const Vector3* anchor = CameraAnchor(Camera());
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
    netlink::NetLink* link = Owner()->link_;
    if (link == nullptr) return 1;
    int sent = 0;
    if (link->Connected()) {
        QueueStream packed;
        out_.CompressTo(&compressor_, packed.Get());
        if (Owner()->logTraffic_ != 0) {
            std::uint32_t packedSize = packed.Get()->size_;
            std::uint32_t size = out_.Size();
            GameCout()->PutText(TextClientToServer())->PutUnsigned(size)->PutText(TextSqueezedSize())
                ->PutUnsigned(packedSize)->PutChar('\n')->Apply(libcrt::FlushManipulator);
        }
        sent = link->Send(packed.Get()) ? 1 : 0;
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
        if (!in_.ok_) return PlayerMessageErrorOriginal(TextReadPastEnd(), previous, type);
        previous = type;
        if (in_.Read(&type, 1) == 0) return 1;
        if (static_cast<int>(type) >= handlers_.size) return PlayerMessageErrorOriginal(TextUnknownType(), previous, type);
        auto handler = handlers_[type];
        if (handler == nullptr) return PlayerMessageErrorOriginal(TextNoHandler(), previous, type);
        if (!handler(this, nullptr)) return PlayerMessageErrorOriginal(TextHandlerFailed(), previous, type);
    }
}

int CPlayerClient::ReceiveMessages() {
    netlink::NetLink* link = Owner()->link_;
    if (link == nullptr) return 0;
    if (!link->HasIncoming()) return 1;
    QueueStream packet;
    if (!link->Receive(packet.Get())) return 0;
    packet.Get()->DecompressTo(&decompressor_, &in_);
    return DispatchMessages() != 0 ? 1 : 0;
}

void* CPlayerClient::ReadText(void* out) {
    in_.ReadString(out);
    return out;
}

int CPlayerClient::OnSetVariable() {
    mfc::CString name;
    {
        TextBuffer text;
        in_.ReadString(text.bytes);
        name = *text.Get();
    }
    std::uint32_t value;
    in_.Read(&value, 4);
    NamedFloatTable& variables = Owner()->Party()->variables_;
    variables.Get(name);
    variables.Set(name, std::bit_cast<float>(value));
    if (IsQuestVariable(name)) {
        TheInterface()->OnQuestVariable(name, std::bit_cast<float>(value));
        TheClientWorld()->OnQuestVariable(name, std::bit_cast<float>(value));
    }
    return 1;
}

int CPlayerClient::OnDeleteVariable() {
    mfc::CString name;
    {
        TextBuffer text;
        in_.ReadString(text.bytes);
        name = *text.Get();
    }
    Owner()->Party()->variables_.Remove(name);
    if (IsQuestVariable(name)) {
        TheInterface()->OnQuestVariable(name, 0.0f);
        TheClientWorld()->OnQuestVariable(name, 0.0f);
    }
    return 1;
}

int CPlayerClient::OnZoneChange() {
    TextBuffer zone;
    ReadText(zone.bytes);
    int entrance;
    in_.Read(&entrance, 4);
    TheGameFlow()->RequestZoneChange(*zone.Get(), entrance);
    return 1;
}

int CPlayerClient::OnJingle() {
    TextBuffer name;
    in_.ReadString(name.bytes);
    TheMusicSystem()->PlayJingle(*name.Get());
    return 1;
}

int CPlayerClient::OnZone() {
    MemoryStream block;
    if (in_.ReadStream(block.Get()) == 0) return 0;
    PlayerClearOriginal(Owner());
    ClientLoadZone(TheClientWorld(), nullptr, block.Get());
    return 1;
}

int CPlayerClient::OnWorldBlock3() {
    MemoryStream block;
    if (in_.ReadStream(block.Get()) == 0) return 0;
    TheClientWorld()->LoadItemsAndSpells(block.Get());
    return 1;
}

int CPlayerClient::OnWorldBlock5() {
    MemoryStream block;
    if (in_.ReadStream(block.Get()) == 0) return 0;
    TheClientWorld()->LoadNewItemsAndSpells(block.Get());
    return 1;
}

int CPlayerClient::OnItemRemoved() {
    int id;
    in_.Read(&id, 4);
    Owner()->TakeItem(id);
    RefreshQuestLog();
    return 1;
}

int CPlayerClient::OnQuestItemsChanged() {
    Owner()->TakeAllItems();
    RefreshQuestLog();
    return 1;
}

int CPlayerClient::OnExperienceAll() {
    float amount;
    in_.Read(&amount, 4);
    CPlayer::UnitMap& units = Owner()->units_;
    for (CPlayer::UnitMap::Node* node = units.First(); node && node->value; node = units.After(node)) {
        CUnit* unit = node->value;
        TheClientWorld()->ShowNumber(unit->id_, ScaledExperience(unit, amount), kNumberExperience);
    }
    return 1;
}

int CPlayerClient::OnExperience() {
    int id;
    float amount;
    in_.Read(&id, 4);
    in_.Read(&amount, 4);
    auto* unit = WorldFindUnit(TheClientWorld(), nullptr, id);
    if (unit != nullptr) TheClientWorld()->ShowNumber(id, ScaledExperience(unit, amount), kNumberExperience);
    return 1;
}

EI_REPLACE_METHOD(0x00669350, CPlayerClient::OnQuestItemsChanged);
EI_REPLACE_METHOD(0x0066A480, CPlayerClient::OnExperienceAll);
EI_REPLACE_METHOD(0x0066A5A0, CPlayerClient::OnExperience);
EI_REPLACE_METHOD(0x00669010, CPlayerClient::OnZone);
EI_REPLACE_METHOD(0x006690F0, CPlayerClient::OnWorldBlock3);
EI_REPLACE_METHOD(0x006691C0, CPlayerClient::OnWorldBlock5);
EI_REPLACE_METHOD(0x00669290, CPlayerClient::OnItemRemoved);
EI_REWRITTEN(0x00667D80, CPlayerClient::ReadText);
EI_REPLACE_METHOD(0x00669850, CPlayerClient::OnSetVariable);
EI_REPLACE_METHOD(0x00669960, CPlayerClient::OnDeleteVariable);
EI_REPLACE_METHOD(0x00669EB0, CPlayerClient::OnZoneChange);
EI_REPLACE_METHOD(0x00669F40, CPlayerClient::OnJingle);
EI_REPLACE_METHOD(0x00667680, CPlayerClient::Pump);
EI_REWRITTEN(0x00667810, CPlayerClient::SendToServer);
EI_REWRITTEN(0x00667970, CPlayerClient::DispatchMessages);
EI_REPLACE_METHOD(0x00667A30, CPlayerClient::ReceiveMessages);

// --- A-H10: items, party, path markers, string events, recording ---------------------------------

namespace {

constexpr int kClassScratchUnit = 0x32;            // the object manager's class of the scratch unit
constexpr int kKindNoPath = 3;               // OnPathMarker: no path to read
constexpr int kPathParticles = 0x2039;
constexpr int kMarkerParticles = 0x203A;     // 0x203B with the flag
constexpr std::uint32_t kMarkerSpeedA = 0x3F000000u;   // 0074317C (0.5)
constexpr std::uint32_t kMarkerSpeedB = 0x501502F9u;   // float bits (1e10)
constexpr std::uint8_t kRequestRecording = 2;

// 0079BE70: path markers are shown (written by 006524A0).
bool MarkersShown() { return ei::GameData<const int>(0x0079BE70) != 0; }
const char* TextPartyReceived() { return "party received on client"; }


} // namespace

int CPlayerClient::OnItemsReceived() {
    mfc::CTypedPtrList<CEngineObject> items{EmptyListVtable(), nullptr, nullptr, 0, nullptr, nullptr, kListBlockSize};
    int& money = Owner()->Party()->Money();
    int before = money;
    in_.Read(&money, 4);
    std::uint8_t flag = 0;   // the original passes this byte with three bytes of the stack slot above it
    in_.Read(&flag, 1);
    int result = 1;
    for (;;) {
        int id;
        in_.Read(&id, 4);
        if (id == -1) break;
        if (!in_.ok_) {
            result = 0;
            break;
        }
        auto* manager = TheClientWorld()->itemManager_;
        if (CEngineObject* item = manager->FindObject(static_cast<unsigned>(id))) items.AddHead(item);
    }
    if (result != 0) {
        Owner()->GiveItems(&items, flag);
        TheInterface()->OnItemsPicked(&items, money - before, flag);
    }
    items.RemoveAll();   // the list's inlined destructor
    return result;
}

int CPlayerClient::OnParty() {
    CParty* party = Owner()->Party();
    NamedFloatTable saved;
    saved.CopyConstruct(party->variables_);
    {
        MemoryStream block;
        in_.ReadSized(block.Get());
        block.Get()->CMemoryStream::SeekImpl(0);
        if (in_.ok_) {
            party->LoadParty(block.Get(), 1);
            mfc::CTypedPtrList<void>& recharging = Owner()->objects_;
            recharging.RemoveAll();
            ObjectListIterator walk{nullptr, nullptr};
            for (CEngineObject* object = walk.First(party->ItemManager()); object != nullptr; object = walk.Next()) {
                auto* item = static_cast<CItem*>(object);
                if (item->chargeMax_ > item->charge_)
                    recharging.AddTail(reinterpret_cast<void*>(static_cast<std::intptr_t>(item->id_)));
            }
            if (walk.manager != nullptr) --walk.manager->refCount_;   // the iterator's inlined destructor
        }
        if (IsMultiplayerClient()) {
            // A multiplayer client keeps its variables (the table's three functor bytes included).
            NamedFloatTable& variables = party->variables_;
            variables.Clear();
            std::memcpy(variables.Functors(), saved.Functors(), 3);
            variables.CopyFrom(saved);
        }
        TheClientWorld()->ApplyObjectVariables();
        GameCout()->PutText(TextPartyReceived())->PutChar('\n')->Apply(libcrt::FlushManipulator);
        TheGameFlow()->ReturnLocalParty();
    }
    saved.Destroy();   // inlined: every node deleted (004616A0), then the bucket vector (00609F50)
    return 1;
}

int CPlayerClient::OnPartyUpdate() {
    MemoryStream block;
    in_.ReadSized(block.Get());
    block.Get()->CMemoryStream::SeekImpl(0);
    if (in_.ok_) Owner()->Party()->LoadParty(block.Get(), 0);
    return 1;
}

int CPlayerClient::OnPathMarker() {
    alignas(4) unsigned char fieldBytes[sizeof(DeltaField)];
    auto* field = reinterpret_cast<DeltaField*>(fieldBytes);
    field->Construct();
    CEngineObject* pathSource = nullptr;
    int kind;
    in_.Read(&kind, 4);
    int flag;
    in_.Read(&flag, 4);
    Vector3 at{0.0f, 0.0f, 0.0f};
    in_.Read(&at.x, 4);
    in_.Read(&at.y, 4);
    int unitId;
    in_.Read(&unitId, 4);
    float value;
    in_.Read(&value, 4);
    if (kind != kKindNoPath) {
        CEngineObject* walker = TheObjectManager()->CreateObject(kClassScratchUnit, -1);
        ++walker->refCount_;
        // The controller's slot 16 (00533DF0 for the client's): its path.
        AIPath* path = static_cast<CUnitClientSpecific*>(static_cast<CUnit*>(walker)->controller)->MotionPath();
        DeltaField* writing = field->BeginWriteB(0);
        PathWriteOriginal(path, nullptr, writing);
        writing->EndField();
        DeltaField* reading = field->BeginReadB(0, &in_);
        path->ReadState(reading);
        reading->EndField();
        if (MarkersShown()) {
            static_cast<CUnit*>(walker)->SetMotionSpeeds(std::bit_cast<float>(kMarkerSpeedA), std::bit_cast<float>(kMarkerSpeedB));
            auto* source = static_cast<CParticleSource*>(
                ParticleManagerObject()->CreateSource(kPathParticles));
            pathSource = reinterpret_cast<CEngineObject*>(source);
            source->Attach(walker);
            source->typeParam = value;
            if (kind == 0) source->alpha = 1.0f;
            else if (kind == 1) source->alpha = 0.0f;
            else if (kind == 2) source->alpha = 2.0f;
            pathSource->RunTick();   // one tick
            source->Attach(nullptr);
        }
        walker->Release();
    }
    if (MarkersShown()) {
        auto* marker = static_cast<CParticleSource*>(ParticleManagerObject()->CreateSource(flag != 0 ? kMarkerParticles + 1 : kMarkerParticles));
        marker->position = at;
        CEngineObject* owner = player_;
        CUnit* unit = WorldFindUnit(owner->ManagerContext(), nullptr, unitId);
        if (unit != nullptr) static_cast<CUnitClientSpecific*>(unit->controller)->SetMarkers(reinterpret_cast<CParticleSource*>(pathSource), reinterpret_cast<CParticleSource*>(marker));
    }
    field->Destruct();
    return 1;
}

int CPlayerClient::OnStringEvent() {
    TextBuffer text;
    ReadText(text.bytes);
    InterfaceStringEventOriginal(TheInterface(), nullptr, *text.Get());
    return 1;
}

void CPlayerClient::StartRecording(const char* path) {
    netlink::NetLink* link = Owner()->link_;
    if (link == nullptr) return;
    WriteByte(&out_, kRequestRecording);
    link->OpenRecording(path);
}

void CPlayerClient::StopRecording() {
    netlink::NetLink* link = Owner()->link_;
    if (link != nullptr) link->CloseRecording();
}

EI_REPLACE_METHOD(0x00669490, CPlayerClient::OnItemsReceived);
EI_REPLACE_METHOD(0x00669A30, CPlayerClient::OnParty);
EI_REPLACE_METHOD(0x00669C90, CPlayerClient::OnPartyUpdate);
EI_REPLACE_METHOD(0x00669FA0, CPlayerClient::OnPathMarker);
EI_REPLACE_METHOD(0x00667E60, CPlayerClient::OnStringEvent);
EI_REWRITTEN(0x00669710, CPlayerClient::StartRecording);
EI_REWRITTEN(0x00669750, CPlayerClient::StopRecording);

// --- A-H10: a new map object (message 0x40, multiplayer) ----------------------------------------

namespace {

constexpr int kClassUnitObject = 0x50;      // GetClassId of units
constexpr int kTypeHumanoid = 0x32;         // type_ of units with heads and the "mo%d" skins
constexpr std::uint32_t kHeaderFlagHidden = 8;
constexpr std::uint32_t kFlagNoParts = 0x200;
constexpr std::uint32_t kFlagCow = 0x10000000;
constexpr std::uint32_t kPartFlagFigure = 8;
constexpr int kWeaponPike = 5;
constexpr int kWispParticles = 0x204D;
constexpr float kWispSize = 0.05f;          // 0073C52C
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
    return reinterpret_cast<const UnitPrototypeRecord*>(unitdb::Prototypes().RecordAt(index, unitdb::kPrototypeSize));
}

// FindPart, then RemovePart when there is one (the original inlines both calls).
void RemovePartNamed(CObject3D* object, const char* name) {
    if (CFigureMesh* part = object->FindPart(name)) object->RemovePart(part);
}

} // namespace

int CPlayerClient::OnNewObject() {
    app::DelayedProtectionCheck();   // 00455B50 (thiscall on the client, not read)
    console::CStringArray parts;
    mfc::CString scriptName;
    mfc::CString secondName;
    NewObjectHeader header;
    in_.Read(&header, sizeof(header));
    Vector3 scale;
    in_.Read(&scale, sizeof(scale));
    {
        TextBuffer text;
        in_.ReadString(text.bytes);
        secondName = *text.Get();
    }
    if (!in_.ok_) return 0;
    char model[kTextSize];
    char textureFrom[kTextSize];
    char textureTo[kTextSize];
    char part[kTextSize];
    in_.ReadText(model, kTextSize);
    in_.ReadText(textureFrom, kTextSize);
    in_.ReadText(textureTo, kTextSize);
    std::uint16_t count = 0;   // read into the low half of a stack dword
    in_.Read(&count, 2);
    parts.SetSize(count, -1);
    for (std::uint16_t i = 0; i < count; ++i) {
        in_.ReadText(part, kTextSize);
        parts[i] = part;
    }
    {
        TextBuffer text;
        in_.ReadString(text.bytes);
        scriptName = *text.Get();
    }
    int prototypeValue;
    in_.Read(&prototypeValue, 4);

    // The world's slot 19 is its object manager (+0x40).
    auto* world = static_cast<CWorld*>(Owner()->ManagerContext());
    CEngineObject* created = world->manager40_->CreateObject(header.type, header.id);
    if (created == nullptr) return 0;
    auto* object = static_cast<CMapObject*>(created);
    auto* figure = static_cast<CObject3D*>(created);
    object->name = model;
    bool isUnit = created->GetClassId() == kClassUnitObject;
    created->prototypeId_ = header.prototype;
    figure->SetFigureScale(&scale.x);
    std::memcpy(&object->baseScale, &scale, sizeof(scale));
    ObjectSlots(created)->ApplyBaseScale();
    object->name2 = static_cast<const char*>(secondName);   // 0050F150
    if (isUnit) {
        auto* unit = static_cast<CUnit*>(created);
        const UnitPrototypeRecord* prototype = PrototypeRecord(header.prototype);
        auto* modelRecord = reinterpret_cast<const UnitModelRecord*>(
            unitdb::Models().RecordAt(static_cast<std::uint32_t>(prototype->modelIndex), unitdb::kModelSize));
        unit->defaultScale.x = 0.0f;
        unit->defaultScale.y = modelRecord->height;
        unit->defaultScale.z = 0.0f;
        std::memcpy(unit->actionSpeeds, modelRecord->actionSpeeds, sizeof(unit->actionSpeeds));
        unit->SetPrototype23CIndex(prototypeValue);
    }
    if (header.flags & kHeaderFlagHidden) created->flags_ |= kHeaderFlagHidden;
    if (Same(model, TextCow1()) || Same(model, TextCow2())) created->flags_ |= kFlagCow;
    if (created->type_ == kTypeHumanoid && (Same(model, TextHuman()) || Same(model, TextHumanFemale()))) {
        // The head part takes the prototype's hair, or goes without one.
        for (int i = 0; i < parts.GetSize(); ++i) {
            if (GameMbscmp(parts[i], TextHairPart()) != 0) continue;
            int hair = PrototypeRecord(header.prototype)->hair;
            if (hair == -1) parts.RemoveAt(i, 1);
            else parts[i].Format(TextHairFormat(), hair);
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
            static_cast<CParticleSource*>(ParticleManagerObject()->CreateSource(kWispParticles));
        source->Attach(created);
        source->ScaleSize(kWispSize / source->baseSize);
        source->attachPoint = 0;
    }
    if (!isUnit) {
        for (int i = 0; i < figure->parts.size; ++i) static_cast<CFigureMesh*>(figure->parts.data[i])->flags_ |= kPartFlagFigure;   // 00517910
        created->flags_ |= kFlagNoParts;
    }
    CEngineObject* attached = figure->attached;
    static_cast<CObject3DClientSpecific*>(attached)->RegisterPartMaterials();
    // (The client world's 00566310 and 00566320 around the texture swap are empty.)
    figure->PlayAnimationSecond(0, nullptr);   // (the original passes the model name as the unused argument)
    figure->RestartPose();
    const char* to = textureTo;
    if (created->type_ == kTypeHumanoid) {
        gamecrt::sprintf(textureFrom, TextSkinFrom(), created->id_);
        gamecrt::sprintf(textureTo, TextSkinTo(), created->id_);
    } else if (Same(textureTo, TextNone())) {
        to = nullptr;
    }
    static_cast<CObject3DClientSpecific*>(attached)->SetSkins(textureFrom, to);
    if (isUnit) ObjectSlots(created)->SetScriptName(scriptName);

    // The player keeps it, counted.
    CPlayer* player = Owner();
    if (header.id > player->maxObjectId_) player->maxObjectId_ = header.id;
    player->knownObjects_[header.id] = created;
    ++created->refCount_;
    if (created->GetClassId() == kClassUnitObject)
        ObjectSlots(created)->TakePrototype(reinterpret_cast<const UnitPrototypeRecord*>(static_cast<CUnit*>(created)->GetPrototype())->name);
    else
        ObjectSlots(created)->TakePrototype(model);
    static_cast<CMapObject*>(created)->WriteFullState();
    if (static_cast<CMapObject*>(created)->ReadState(0, &in_) == 0) return 0;
    TheClientWorld()->AddObject(created);
    if (object->name2[0] != 0) {
        TextBuffer variable;
        TheClientWorld()->ObjectVariableName(reinterpret_cast<mfc::CString*>(variable.bytes), object->name2);
        object->SetVisibilityLevel(player->Party()->variables_.Get(*variable.Get()));
    }
    if (player->sessionKind100_ == kSessionClient && created->GetClassId() == kClassUnitObject)
        static_cast<CUnitClientSpecific*>(static_cast<CUnit*>(created)->controller)->LoadFaceTexture();
    return 1;
}

EI_REPLACE_METHOD(0x00667EC0, CPlayerClient::OnNewObject);

// --- A-H10: clearing the connection ------------------------------------------------------------

void CoderState::Reset() {
    out.End();
    Construct();
}

namespace {

constexpr int kClientHandlerCount = 0x100;

// The client's message handlers (thiscall, RET 0) by type, as 00667230 stores them.
constexpr HandlerEntry kClientHandlers[] = {
    {0x00, std::uintptr_t(0x00667DA0)}, {0x01, std::uintptr_t(0x00667DE0)}, {0x02, std::uintptr_t(0x00669010)},
    {0x03, std::uintptr_t(0x006690F0)}, {0x04, std::uintptr_t(0x00667C20)}, {0x05, std::uintptr_t(0x006691C0)},
    {0x06, std::uintptr_t(0x00669490)}, {0x07, std::uintptr_t(0x00669D50)}, {0x08, std::uintptr_t(0x00669EB0)},
    {0x09, std::uintptr_t(0x00669F30)}, {0x0A, std::uintptr_t(0x00669290)}, {0x0B, std::uintptr_t(0x00667DF0)},
    {0x0C, std::uintptr_t(0x006696A0)}, {0x0D, std::uintptr_t(0x00669F40)}, {0x0E, std::uintptr_t(0x00669FA0)},
    {0x0F, std::uintptr_t(0x00667D10)}, {0x12, std::uintptr_t(0x00667E20)}, {0x13, std::uintptr_t(0x00667E60)},
    {0x14, std::uintptr_t(0x0066A420)}, {0x15, std::uintptr_t(0x0066A480)}, {0x16, std::uintptr_t(0x00669400)},
    {0x17, std::uintptr_t(0x00669350)}, {0x18, std::uintptr_t(0x0066A5A0)}, {0x19, std::uintptr_t(0x0066A620)},
    {0x40, std::uintptr_t(0x00667EC0)}, {0x41, std::uintptr_t(0x006687B0)}, {0x42, std::uintptr_t(0x00668850)},
    {0x43, std::uintptr_t(0x0066A3B0)}, {0x50, std::uintptr_t(0x00668940)}, {0x51, std::uintptr_t(0x00668A30)},
    {0x52, std::uintptr_t(0x00668AA0)}, {0x60, std::uintptr_t(0x00668C50)}, {0x70, std::uintptr_t(0x00669760)},
    {0x71, std::uintptr_t(0x006697B0)}, {0x72, std::uintptr_t(0x00669800)}, {0x73, std::uintptr_t(0x0066A400)},
    {0x80, std::uintptr_t(0x00669A30)}, {0x81, std::uintptr_t(0x00669850)}, {0x82, std::uintptr_t(0x00669960)},
    {0x83, std::uintptr_t(0x00669C90)},
};

} // namespace

BOOL CPlayerClient::CreateClient() {
    state_ = 0;
    partyUnitIds_.SetSize(0, -1);
    in_.Resize(0);
    out_.Resize(0);
    localSession_ = 0;
    sendParty_ = 0;
    compressor_.Reset();
    decompressor_.Reset();
    handlers_.SetSize(kClientHandlerCount);
    std::memset(static_cast<void*>(handlers_.data), 0, static_cast<std::size_t>(handlers_.size) * sizeof(Handler));
    for (const HandlerEntry& entry : kClientHandlers)
        handlers_[entry.type] = ei::Code<Handler>(entry.address);
    return 1;
}
EI_REPLACE_METHOD(0x00667230, CPlayerClient::CreateClient);

void CPlayerClient::Clear() {
    state_ = 0;
    partyUnitIds_.RemoveAll();
    in_.Resize(0);
    out_.Resize(0);
    localSession_ = 0;
    sendParty_ = 0;
    compressor_.Reset();
    decompressor_.Reset();
}

// --- A-H21: constructor and destructor -----------------------------------------------------------

namespace {
constexpr std::uintptr_t kClientVtable = std::uintptr_t(0x00743180);
constexpr std::uintptr_t kConnectionVtable = std::uintptr_t(0x007431E0);   // CPlayerConnection's own
constexpr const char* kClientClassName = "CPlayerClient";
constexpr int kClientClassId = 0x3C;

// The engine object part (CPlayerClient does not derive from CEngineObject in C++).
CEngineObject* EngineObject(CPlayerClient* client) { return reinterpret_cast<CEngineObject*>(client); }
} // namespace

CPlayerClient* CPlayerClient::Construct() {
    EngineObject(this)->ClearFields();
    vtable_ = ei::GamePointer<void>(kConnectionVtable);
    player_ = nullptr;
    handlers_.Construct();
    in_.ConstructQueueStream();
    out_.ConstructQueueStream();
    compressor_.Construct();
    decompressor_.Construct();
    partyUnitIds_.Construct();
    otherUnitIds_.Construct();
    vtable_ = ei::GamePointer<void>(kClientVtable);
    Clear();
    return this;
}

void CPlayerClient::DestructBody() {
    vtable_ = ei::GamePointer<void>(kClientVtable);
    Clear();
    otherUnitIds_.FreeData();
    partyUnitIds_.FreeData();
    decompressor_.out.End();
    compressor_.out.End();
    out_.Destruct();
    in_.Destruct();
    handlers_.FreeData();
    vtable_ = ei::GamePointer<void>(kConnectionVtable);
    player_ = nullptr;
    EngineObject(this)->DestructBase();
}

CPlayerClient* CPlayerClient::DeletingDestruct(unsigned flags) {
    DestructBody();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}

// 00666EC0 / 00666F70 (RET 4): CPlayerConnection's own destructor body (its vtable, no player,
// CEngineObject's) and deleting destructor.
void __fastcall ConnectionDestructBody(CPlayerConnection* self) {
    ei::SetVtable(self, kConnectionVtable);
    self->player_ = nullptr;
    self->DestructBase();
}
CPlayerConnection* CPlayerConnection::DeleteScalar(unsigned flags) {
    ConnectionDestructBody(this);
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}

// 00666F90 / 00666FA0 (slots of CEngineObject): "CPlayerClient", class id 0x3C.
const char* __fastcall PlayerClientClassName(CPlayerClient*) { return kClientClassName; }
int __fastcall PlayerClientClassId(CPlayerClient*) { return kClientClassId; }

EI_REWRITTEN(0x00666D10, CPlayerClient::Construct);
EI_REWRITTEN(0x00666FD0, CPlayerClient::DestructBody);
EI_REPLACE_METHOD(0x00666FB0, CPlayerClient::DeletingDestruct);
EI_REWRITTEN(0x00666EC0, ConnectionDestructBody);
EI_REPLACE_METHOD(0x00666F70, CPlayerConnection::DeleteScalar);
EI_REPLACE(0x00666F90, PlayerClientClassName);
EI_REPLACE(0x00666FA0, PlayerClientClassId);

EI_REWRITTEN(0x006671A0, CoderState::Reset);
EI_REPLACE_METHOD(0x00667510, CPlayerClient::Clear);
EI_REPLACE_METHOD(0x00667600, CPlayerClient::Clear);

// --- A-H17: the party state -------------------------------------------------------------------

namespace {

constexpr std::uint32_t kUnitInClientParty = 0x400000;
constexpr int kSessionKindSingle = 1;

// The client's party unit ids (hash_set<int>, 007C8370): insert_unique 0066B400, find 0066B490,
// clear 0066BA70.
using IntSet = stlp::hashtable<stlp::DwordKeyTraits<4>>;
// The set, empty at the start: built by the static initialiser 00668B40 in the list's order and destroyed by the exit
// function it registers (00668B60); both ported on the name and registered at their addresses
// (docs/notes/standalone.md, "The static initialisers' objects").
constinit lib::StaticObject<IntSet> g_partyIdSet = /*007C8370*/ {};
IntSet& PartyIdSet() { return *g_partyIdSet; }
void __cdecl ConstructPartyIdSet() {   // 00668B40
    g_partyIdSet->Construct(100, 0, 0, 0);
    GameAtExit(ei::Code<void(__cdecl*)()>(0x00668B60));
}
void __cdecl DestroyPartyIdSet() { g_partyIdSet->Destroy(); }   // 00668B60
// 007AFCA4: read only here (no code writes it); slot 43 runs when it is at least 1.
int FadeInGate() { return ei::GameData<const int>(0x007AFCA4); }

int ReadDword(BitStream* in) {
    int value;
    in->Read(&value, 4);
    return value;
}

} // namespace

EI_REPLACE(0x00668B40, ConstructPartyIdSet);
EI_REPLACE(0x00668B60, DestroyPartyIdSet);

int CPlayerClient::OnPartyState() {
    CPlayer* player = Owner();
    for (int i = 0; i < partyUnitIds_.size; ++i) {
        bool inserted;
        PartyIdSet().InsertUnique(stlp::DwordKeyed<4>{static_cast<std::uint32_t>(partyUnitIds_.data[i])}, inserted);
        CEngineObject* unit = WorldFindUnit(player->ManagerContext(), nullptr, partyUnitIds_.data[i]);
        if (unit != nullptr) unit->flags_ &= ~kUnitInClientParty;
    }

    std::uint8_t paused;
    in_.Read(&paused, 1);
    static_cast<CWorldClient*>(player->ManagerContext())->paused_ = paused != 0 ? 1 : 0;
    ReadDword(&in_);   // the server's +0x24, not used

    int count = ReadDword(&in_);
    partyUnitIds_.SetSizeDiscard(count);
    for (int i = 0; i < count; ++i) {
        int id = ReadDword(&in_);
        partyUnitIds_.data[i] = id;
        auto* unit = WorldFindUnit(player->ManagerContext(), nullptr, id);
        if (unit == nullptr) continue;
        unit->flags_ |= kUnitInClientParty;
        if (PartyIdSet().Find(static_cast<std::uint32_t>(id)) == nullptr && player->sessionKind100_ == kSessionKindSingle && unit->player == nullptr &&
            FadeInGate() >= 1)
            unit->Controller()->RestartFadeIn();
    }
    PartyIdSet().Clear();

    static_cast<CWorldClient*>(player->ManagerContext())->partyDanger_ = ReadDword(&in_);
    int others = ReadDword(&in_);
    otherUnitIds_.SetSize(others);
    for (int i = 0; i < others; ++i) in_.Read(&otherUnitIds_.data[i], 4);
    return 1;
}
EI_REPLACE_METHOD(0x00668C50, CPlayerClient::OnPartyState);

// --- 00667B40 (A-H25): messages appended to the received stream ---------------------------------------

void CPlayerClient::AppendReceived(BitStream* source) {
    const std::uint32_t position = in_.Position();
    in_.Seek(in_.Size());
    in_.EndBits();
    source->Seek(0);
    in_.Write(source->data_, source->Size());
    in_.Seek(position);
}
EI_REWRITTEN(0x00667B40, CPlayerClient::AppendReceived);
