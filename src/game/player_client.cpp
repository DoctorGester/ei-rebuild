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
        if (static_cast<int>(type) >= handlerCount_) return PlayerMessageErrorOriginal(TextUnknownType(), previous, type);
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
