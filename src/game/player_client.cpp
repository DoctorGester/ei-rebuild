// CPlayerClient: the client's per-tick link traffic (sending the queued requests, receiving and
// running the server's messages) and its login. See player_client.h and docs/notes/objects.md.

#include "player_client.h"

#include "camera.h"
#include "originals_engine.h"
#include "originals_world.h"
#include "net_link.h"
#include "originals_gameplay.h"
#include "originals_library.h"
#include "party.h"
#include "sound.h"
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

#include <cmath>
#include <cstring>

// 0040FB20 (camera_object.cpp): the point the camera is placed by.
const Vector3* __fastcall CameraAnchor(const CCamera* self);

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
    std::uint32_t value;   // the player's +0xFC
};
static_assert(sizeof(LoginRequest) == 0x88);

CCamera* Camera() { return *reinterpret_cast<CCamera**>(0x007AF7C8); }
void* GameCout() { return reinterpret_cast<void*>(0x007C56B0); }
void* FlushManipulator() { return reinterpret_cast<void*>(0x00435000); }
// The texts the original reads from the exe: the traffic log, the login, the dispatch errors.
const char* TextClientToServer() { return reinterpret_cast<const char*>(0x00794CE8); }   // "client to server, size = "
const char* TextSqueezedSize() { return reinterpret_cast<const char*>(0x00794D04); }     // " squeezed size = "
const char* TextLoginName() { return reinterpret_cast<const char*>(0x00794CE0); }        // "Tango"
const char* TextLoginPassword() { return reinterpret_cast<const char*>(0x00794CD0); }    // "Very top secret"
const char* TextReadPastEnd() { return reinterpret_cast<const char*>(0x00794D98); }
const char* TextNoHandler() { return reinterpret_cast<const char*>(0x00794D6C); }
const char* TextHandlerFailed() { return reinterpret_cast<const char*>(0x00794D3C); }
const char* TextUnknownType() { return reinterpret_cast<const char*>(0x00794D18); }

void WriteByte(BitStream* out, std::uint8_t value) { out->Write(&value, 1); }

// A CString the stream reader constructs in place (CStream::ReadString), destroyed at the end of
// the scope.
struct TextBuffer {
    alignas(4) unsigned char bytes[sizeof(mfc::CString)];
    ~TextBuffer() { Get()->~CString(); }
    mfc::CString* Get() { return reinterpret_cast<mfc::CString*>(bytes); }
};

GameFlow* Flow() { return reinterpret_cast<GameFlow*>(0x007AFE20); }
CMusicSystem* Music() { return *reinterpret_cast<CMusicSystem**>(0x007AF760); }
void* QuestLog() { return reinterpret_cast<void*>(0x007C7D50); }
const char* QuestPrefix() { return reinterpret_cast<const char*>(0x00783D7C); }   // "q."

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
__declspec(naked) int __fastcall ClientWorldBlock3(void* /*world*/, void*, CMemoryStream* /*block*/) EI_ORIGINAL(0x00567A60)
__declspec(naked) int __fastcall ClientWorldBlock5(void* /*world*/, void*, CMemoryStream* /*block*/) EI_ORIGINAL(0x00567950)
// 005F7F30 (RET 0xC): the quest log (007C7D50) passes (list, a, b) to its top screen's slot 28.
__declspec(naked) void __fastcall QuestLogRefresh(void* /*log*/, void*, void* /*list*/, int, int) EI_ORIGINAL(0x005F7F30)
void* EmptyListVtable() { return reinterpret_cast<void*>(0x0073BE80); }   // the CPtrList passed to it
constexpr int kListBlockSize = 10;

// The quest log refreshed with an empty CPtrList built and destroyed on the stack.
void RefreshQuestLog() {
    mfc::CTypedPtrList<void> none{EmptyListVtable(), nullptr, nullptr, 0, nullptr, nullptr, kListBlockSize};
    QuestLogRefresh(QuestLog(), nullptr, &none, 0, 0);
    // The list's inlined destructor (RemoveAll; nothing was added).
    none.head = none.tail = nullptr;
    none.count = 0;
    none.free = nullptr;
    mfc::PlexFreeDataChain(none.blocks);
}

// 00665550 (plain RET): the player runs its connection's slot 18 and empties its list at +0xE0.
// 0056CE40 (RET 0xC): the client world shows a number over a unit (id, value, kind).
__declspec(naked) void __fastcall PlayerQuestItemsOriginal(void* /*player*/) EI_ORIGINAL(0x00665550)
__declspec(naked) void __fastcall ClientShowNumber(void* /*world*/, void*, int /*id*/, int /*value*/, int /*kind*/)
    EI_ORIGINAL(0x0056CE40)
constexpr int kNumberExperience = 8;

// The experience shown for `amount`: amount * 0073E128 ^ (+0x33E - 0073E124), stored to a float,
// then rounded to the nearest integer (FISTP).
int ScaledExperience(const CUnit* unit, float amount) {
    double base = *reinterpret_cast<const double*>(0x0073E128);
    float offset = *reinterpret_cast<const float*>(0x0073E124);
    volatile float scaled = static_cast<float>(std::pow(base, static_cast<double>(unit->attackRate) - offset) * amount);
    return static_cast<int>(std::lrint(scaled));
}

bool IsQuestVariable(const mfc::CString& name) {
    return name.GetLength() > 2 && std::strncmp(name, QuestPrefix(), 2) == 0;
}

// A memory stream on the stack with the network queue's vtable, as the original builds and
// destroys it inline.
struct QueueStream {
    alignas(4) unsigned char bytes[sizeof(CMemoryStream)];
    QueueStream() { Get()->ConstructQueueStream(); }
    ~QueueStream() { Get()->Destruct(); }
    CMemoryStream* Get() { return reinterpret_cast<CMemoryStream*>(bytes); }
};

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
        login.value = Owner()->unknownFC_;
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
            void* log = OstreamPutString(GameCout(), nullptr, TextClientToServer());
            log = OstreamPutUnsignedOriginal(log, nullptr, size);
            log = OstreamPutString(log, nullptr, TextSqueezedSize());
            log = OstreamPutUnsignedOriginal(log, nullptr, packedSize);
            log = OstreamPutChar(log, nullptr, '\n');
            OstreamManipulate(log, nullptr, FlushManipulator());
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
    void* variables = &Owner()->Party()->variables_;
    NamedTableGet(variables, nullptr, name);
    NamedTableSet(variables, nullptr, name, value);
    if (IsQuestVariable(name)) {
        QuestLogUpdateOriginal(QuestLog(), nullptr, name, value);
        ClientQuestUpdateOriginal(WorldClient(), nullptr, name, value);
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
    VariableRemove(&Owner()->Party()->variables_, nullptr, name);
    if (IsQuestVariable(name)) {
        QuestLogUpdateOriginal(QuestLog(), nullptr, name, 0);
        ClientQuestUpdateOriginal(WorldClient(), nullptr, name, 0);
    }
    return 1;
}

int CPlayerClient::OnZoneChange() {
    TextBuffer zone;
    ReadText(zone.bytes);
    int entrance;
    in_.Read(&entrance, 4);
    Flow()->RequestZoneChange(*zone.Get(), entrance);
    return 1;
}

int CPlayerClient::OnJingle() {
    TextBuffer name;
    in_.ReadString(name.bytes);
    Music()->PlayJingle(*name.Get());
    return 1;
}

int CPlayerClient::OnZone() {
    MemoryStream block;
    if (in_.ReadStream(block.Get()) == 0) return 0;
    PlayerClearOriginal(Owner());
    ClientLoadZone(WorldClient(), nullptr, block.Get());
    return 1;
}

int CPlayerClient::OnWorldBlock3() {
    MemoryStream block;
    if (in_.ReadStream(block.Get()) == 0) return 0;
    ClientWorldBlock3(WorldClient(), nullptr, block.Get());
    return 1;
}

int CPlayerClient::OnWorldBlock5() {
    MemoryStream block;
    if (in_.ReadStream(block.Get()) == 0) return 0;
    ClientWorldBlock5(WorldClient(), nullptr, block.Get());
    return 1;
}

int CPlayerClient::OnItemRemoved() {
    int id;
    in_.Read(&id, 4);
    PlayerRemoveItemOriginal(Owner(), nullptr, id);
    RefreshQuestLog();
    return 1;
}

int CPlayerClient::OnQuestItemsChanged() {
    PlayerQuestItemsOriginal(Owner());
    RefreshQuestLog();
    return 1;
}

int CPlayerClient::OnExperienceAll() {
    float amount;
    in_.Read(&amount, 4);
    CPlayer::ObjectMap& units = Owner()->units_;
    for (CPlayer::ObjectMap::Node* node = units.First(); node && node->value; node = units.After(node)) {
        auto* unit = static_cast<CUnit*>(node->value);
        ClientShowNumber(WorldClient(), nullptr, unit->id_, ScaledExperience(unit, amount), kNumberExperience);
    }
    return 1;
}

int CPlayerClient::OnExperience() {
    int id;
    float amount;
    in_.Read(&id, 4);
    in_.Read(&amount, 4);
    auto* unit = static_cast<CUnit*>(WorldFindUnit(WorldClient(), nullptr, id));
    if (unit != nullptr) ClientShowNumber(WorldClient(), nullptr, id, ScaledExperience(unit, amount), kNumberExperience);
    return 1;
}

EI_REPLACE_METHOD(0x00669350, CPlayerClient::OnQuestItemsChanged);
EI_REPLACE_METHOD(0x0066A480, CPlayerClient::OnExperienceAll);
EI_REPLACE_METHOD(0x0066A5A0, CPlayerClient::OnExperience);
EI_REPLACE_METHOD(0x00669010, CPlayerClient::OnZone);
EI_REPLACE_METHOD(0x006690F0, CPlayerClient::OnWorldBlock3);
EI_REPLACE_METHOD(0x006691C0, CPlayerClient::OnWorldBlock5);
EI_REPLACE_METHOD(0x00669290, CPlayerClient::OnItemRemoved);
EI_REPLACE_METHOD(0x00667D80, CPlayerClient::ReadText);
EI_REPLACE_METHOD(0x00669850, CPlayerClient::OnSetVariable);
EI_REPLACE_METHOD(0x00669960, CPlayerClient::OnDeleteVariable);
EI_REPLACE_METHOD(0x00669EB0, CPlayerClient::OnZoneChange);
EI_REPLACE_METHOD(0x00669F40, CPlayerClient::OnJingle);
EI_REPLACE_METHOD(0x00667680, CPlayerClient::Pump);
EI_REPLACE_METHOD(0x00667810, CPlayerClient::SendToServer);
EI_REPLACE_METHOD(0x00667970, CPlayerClient::DispatchMessages);
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
bool MarkersShown() { return *reinterpret_cast<const int*>(0x0079BE70) != 0; }
const char* TextPartyReceived() { return reinterpret_cast<const char*>(0x00794DF8); }   // "party received on client"

// 005652E0 (plain RET): the client world gives its objects the party's variables.
__declspec(naked) void __fastcall ClientApplyVariables(void* /*world*/) EI_ORIGINAL(0x005652E0)
// 0052EA40 (RET 8): a unit's motion speeds (float bits; only this handler calls it).
__declspec(naked) void __fastcall UnitSetMotionSpeeds(void* /*unit*/, void*, std::uint32_t, std::uint32_t)
    EI_ORIGINAL(0x0052EA40)
// 00535240 (RET 8): the unit's client controller keeps the two marker sources (+0x24, +0x28; the
// old ones stopped and released, the new ones referenced).
__declspec(naked) void __fastcall ControllerSetMarkers(void* /*controller*/, void*, void* /*path*/, void* /*marker*/)
    EI_ORIGINAL(0x00535240)

// The STLport table's inlined destructor: every node deleted (004616A0), the buckets cleared, the
// count zeroed, then the bucket vector destroyed (00609F50).
void DestroyTable(PartyVariableMap* table) {
    table->ClearWith([table](PartyVariableMap::Node* node) { FloatTableDeleteNodeOriginal(table, nullptr, node); });
    BucketVectorDestroyOriginal(table->BucketVector());
}

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
        auto* manager = static_cast<CBaseManager*>(WorldClient()->itemManager_);
        if (CEngineObject* item = manager->FindObject(static_cast<unsigned>(id))) items.AddHead(item);
    }
    if (result != 0) {
        PlayerGiveItemsOriginal(Owner(), nullptr, &items, flag);
        QuestLogRefresh(QuestLog(), nullptr, &items, money - before, flag);
    }
    items.RemoveAll();   // the list's inlined destructor
    return result;
}

int CPlayerClient::OnParty() {
    CParty* party = Owner()->Party();
    PartyVariableMap saved;
    FloatTableCopyConstructOriginal(&saved, nullptr, &party->variables_);
    {
        MemoryStream block;
        in_.ReadSized(block.Get());
        block.Get()->CMemoryStream::Seek(0);
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
            PartyVariableMap* variables = &party->variables_;
            FloatTableClearOriginal(variables);
            std::memcpy(variables->Functors(), saved.Functors(), 3);
            FloatTableCopyFromOriginal(variables, nullptr, &saved);
        }
        ClientApplyVariables(WorldClient());
        void* log = OstreamPutString(GameCout(), nullptr, TextPartyReceived());
        log = OstreamPutChar(log, nullptr, '\n');
        OstreamManipulate(log, nullptr, FlushManipulator());
        Flow()->ReturnLocalParty();
    }
    DestroyTable(&saved);
    return 1;
}

int CPlayerClient::OnPartyUpdate() {
    MemoryStream block;
    in_.ReadSized(block.Get());
    block.Get()->CMemoryStream::Seek(0);
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
        CEngineObject* walker = camera::ObjectManager()->CreateObject(kClassScratchUnit, -1);
        ++walker->refCount_;
        // The controller's slot 16 (00533DF0 for the client's): its path.
        AIPath* path = static_cast<CUnitClientSpecific*>(static_cast<CUnit*>(walker)->controller)->MotionPath();
        DeltaField* writing = field->BeginWriteB(0);
        PathWriteOriginal(path, nullptr, writing);
        writing->EndField();
        DeltaField* reading = field->BeginReadB(0, &in_);
        PathReadOriginal(path, nullptr, reading);
        reading->EndField();
        if (MarkersShown()) {
            UnitSetMotionSpeeds(walker, nullptr, kMarkerSpeedA, kMarkerSpeedB);
            auto* source = static_cast<CParticleSource*>(
                ParticleCreateSourceOriginal(ParticleManagerObject(), nullptr, kPathParticles));
            pathSource = reinterpret_cast<CEngineObject*>(source);
            source->Attach(walker);
            source->unknown_11C = value;
            if (kind == 0) source->alpha = 1.0f;
            else if (kind == 1) source->alpha = 0.0f;
            else if (kind == 2) source->alpha = 2.0f;
            pathSource->Slot6();   // one tick
            source->Attach(nullptr);
        }
        walker->Release();
    }
    if (MarkersShown()) {
        auto* marker = static_cast<CParticleSource*>(ParticleCreateSourceOriginal(
            ParticleManagerObject(), nullptr, flag != 0 ? kMarkerParticles + 1 : kMarkerParticles));
        marker->position = at;
        auto* owner = reinterpret_cast<CEngineObject*>(player_);
        CEngineObject* unit = WorldFindUnit(owner->ManagerContext(), nullptr, unitId);
        if (unit != nullptr) ControllerSetMarkers(static_cast<CUnit*>(unit)->controller, nullptr, pathSource, marker);
    }
    field->Destruct();
    return 1;
}

int CPlayerClient::OnStringEvent() {
    TextBuffer text;
    ReadText(text.bytes);
    InterfaceStringEventOriginal(QuestLog(), nullptr, *text.Get());
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
EI_REPLACE_METHOD(0x00669710, CPlayerClient::StartRecording);
EI_REPLACE_METHOD(0x00669750, CPlayerClient::StopRecording);

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
    std::uint32_t prototype;   // the object's unknown_18
    std::uint32_t flags;
};

// Model and part names the handler compares with (the exe's strings).
const char* TextCow1() { return reinterpret_cast<const char*>(0x00793FA8); }        // "unmoco1"
const char* TextCow2() { return reinterpret_cast<const char*>(0x0078A5C0); }        // "unmoco2"
const char* TextHuman() { return reinterpret_cast<const char*>(0x007908C8); }       // "unhuma"
const char* TextHumanFemale() { return reinterpret_cast<const char*>(0x00792EA4); } // "unhufe"
const char* TextHairPart() { return reinterpret_cast<const char*>(0x0078FB38); }    // "hr"
const char* TextHairFormat() { return reinterpret_cast<const char*>(0x0078FB0C); }  // "hr.%.2d"
const char* TextGoblin() { return reinterpret_cast<const char*>(0x00794DF0); }      // "unmogo"
const char* TextAxePart() { return reinterpret_cast<const char*>(0x00794DE4); }     // "rh3.axeth00"
const char* TextSwordPart() { return reinterpret_cast<const char*>(0x00794DD8); }   // "rh3.sword00"
const char* TextPikePart() { return reinterpret_cast<const char*>(0x00794DCC); }    // "rh3.pike00"
const char* TextSlingPart() { return reinterpret_cast<const char*>(0x00794DC4); }   // "sling"
const char* TextWisp() { return reinterpret_cast<const char*>(0x00794DBC); }        // "unmowi"
const char* TextSkinFrom() { return reinterpret_cast<const char*>(0x0078FB7C); }    // "mo%dp"
const char* TextSkinTo() { return reinterpret_cast<const char*>(0x0078FB44); }      // "mo%ds"
const char* TextNone() { return reinterpret_cast<const char*>(0x007846F0); }        // "none"

bool Same(const char* a, const char* b) { return std::strcmp(a, b) == 0; }

// 00455B50: the 1% check (rand) that runs 00455BA0 / 00458DA0 (ECX is the client, not read).
__declspec(naked) void __fastcall NewObjectCheckOriginal(void* /*client*/) EI_ORIGINAL(0x00455B50)
// 0050EAB0 (RET 8): the map object reads its full network state (its slot 49 between the
// state's begin / end). Nonzero when it was read.
__declspec(naked) int __fastcall MapObjectReadStateOriginal(void* /*object*/, void*, int /*second*/, BitStream* /*from*/)
    EI_ORIGINAL(0x0050EAB0)
// 005654F0 (RET 8): the client world's variable name for an object's second name, as a CString
// constructed at `out`; returns `out`.
__declspec(naked) mfc::CString* __fastcall ClientVariableNameOriginal(void* /*world*/, void*, void* /*out*/,
                                                                     const char* /*name*/) EI_ORIGINAL(0x005654F0)
// 00538DB0 (plain RET): a unit's client controller, for client sessions of kind 3.
__declspec(naked) void __fastcall ControllerClientSessionOriginal(void* /*controller*/) EI_ORIGINAL(0x00538DB0)

const UnitPrototypeRecord* PrototypeRecord(std::uint32_t index) {
    return reinterpret_cast<const UnitPrototypeRecord*>(unitdb::Prototypes().At(index, unitdb::kPrototypeSize));
}

// FindPart, then RemovePart when there is one (the original inlines both calls).
void RemovePartNamed(CObject3D* object, const char* name) {
    if (FigurePart* part = object->FindPart(name)) object->RemovePart(part);
}

} // namespace

int CPlayerClient::OnNewObject() {
    NewObjectCheckOriginal(this);
    console::CStringArray parts;
    mfc::CString scriptName;
    mfc::CString secondName;
    NewObjectHeader header;
    in_.Read(&header, sizeof(header));
    float scale[3];
    in_.Read(scale, sizeof(scale));
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
    CEngineObject* created = static_cast<CBaseManager*>(world->manager40_)->CreateObject(header.type, header.id);
    if (created == nullptr) return 0;
    auto* object = static_cast<CMapObject*>(created);
    auto* figure = static_cast<CObject3D*>(created);
    *reinterpret_cast<mfc::CString*>(&object->name) = model;
    bool isUnit = created->GetClassId() == kClassUnitObject;
    created->unknown_18 = header.prototype;
    figure->SetFigureScale(scale);
    std::memcpy(object->baseScale, scale, sizeof(scale));
    ObjectSlots(created)->ApplyBaseScale();
    *reinterpret_cast<mfc::CString*>(&object->name2) = static_cast<const char*>(secondName);   // 0050F150
    if (isUnit) {
        auto* unit = static_cast<CUnit*>(created);
        const UnitPrototypeRecord* prototype = PrototypeRecord(header.prototype);
        auto* modelRecord = reinterpret_cast<const UnitModelRecord*>(
            unitdb::Models().At(static_cast<std::uint32_t>(prototype->modelIndex), unitdb::kModelSize));
        unit->modelHeight[0] = 0.0f;
        unit->modelHeight[1] = modelRecord->height;
        unit->modelHeight[2] = 0.0f;
        std::memcpy(unit->resistances, modelRecord->unknown_2C, sizeof(unit->resistances));
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
    if (Object3DMakeFigureOriginal(figure, nullptr, model, &parts, 1) == 0) return 0;   // the object is left made
    if (Same(model, TextGoblin())) {
        RemovePartNamed(figure, TextAxePart());
        RemovePartNamed(figure, TextSwordPart());
        bool pike = static_cast<int>(PrototypeRecord(created->unknown_18)->naturalWeapon) == kWeaponPike;
        RemovePartNamed(figure, pike ? TextPikePart() : TextSlingPart());
    }
    if (Same(model, TextWisp())) {
        auto* source =
            static_cast<CParticleSource*>(ParticleCreateSourceOriginal(ParticleManagerObject(), nullptr, kWispParticles));
        source->Attach(created);
        source->ScaleSize(kWispSize / source->baseSize);
        source->attachPoint = 0;
    }
    if (!isUnit) {
        for (int i = 0; i < figure->parts.size; ++i) static_cast<FigurePart*>(figure->parts.data[i])->flags_ |= kPartFlagFigure;   // 00517910
        created->flags_ |= kFlagNoParts;
    }
    CEngineObject* attached = figure->attached;
    FigureLoadTexturesOriginal(attached);
    // (The client world's 00566310 and 00566320 around the texture swap are empty.)
    Object3DSetAnimationsOriginal(figure, nullptr, model, 0);
    Object3DRestartAnimationOriginal(figure);
    const char* to = textureTo;
    if (created->type_ == kTypeHumanoid) {
        gamecrt::sprintf(textureFrom, TextSkinFrom(), created->id_);
        gamecrt::sprintf(textureTo, TextSkinTo(), created->id_);
    } else if (Same(textureTo, TextNone())) {
        to = nullptr;
    }
    FigureSwapTextureOriginal(attached, nullptr, textureFrom, to);
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
    ObjectPrepareOriginal(created);
    if (MapObjectReadStateOriginal(created, nullptr, 0, &in_) == 0) return 0;
    WorldClient()->AddObject(created);
    if (object->name2[0] != 0) {
        TextBuffer variable;
        ClientVariableNameOriginal(WorldClient(), nullptr, variable.bytes, object->name2);
        object->SetVisibilityLevel(NamedTableGet(&player->Party()->variables_, nullptr, *variable.Get()));
    }
    if (player->sessionKind100_ == kSessionClient && created->GetClassId() == kClassUnitObject)
        ControllerClientSessionOriginal(static_cast<CUnit*>(created)->controller);
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
struct HandlerEntry {
    std::uint8_t type;
    std::uintptr_t address;
};
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
    array50D8_.SetSize(0, -1);
    in_.Resize(0);
    out_.Resize(0);
    unknown_84 = 0;
    sendParty_ = 0;
    compressor_.Reset();
    decompressor_.Reset();
    handlers_.SetSize(kClientHandlerCount);
    std::memset(static_cast<void*>(handlers_.data), 0, static_cast<std::size_t>(handlers_.size) * sizeof(Handler));
    for (const HandlerEntry& entry : kClientHandlers)
        handlers_[entry.type] = reinterpret_cast<Handler>(entry.address);
    return 1;
}
EI_REPLACE_METHOD(0x00667230, CPlayerClient::CreateClient);

void CPlayerClient::Clear() {
    state_ = 0;
    if (array50D8_.data != nullptr) {
        // Aligned storage: the block's own pointer is kept just before the data.
        gamecrt::operator_delete(reinterpret_cast<void**>(array50D8_.data)[-1]);
        array50D8_.data = nullptr;
    }
    array50D8_.capacity = 0;
    array50D8_.size = 0;
    in_.Resize(0);
    out_.Resize(0);
    unknown_84 = 0;
    sendParty_ = 0;
    compressor_.Reset();
    decompressor_.Reset();
}

EI_REPLACE_METHOD(0x006671A0, CoderState::Reset);
EI_REPLACE_METHOD(0x00667510, CPlayerClient::Clear);
EI_REPLACE_METHOD(0x00667600, CPlayerClient::Clear);
