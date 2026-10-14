// CPlayerServer's message writers and the party record lookup. See player_messages.h and
// docs/notes/objects.md.

#include "player_messages.h"
#include "party.h"
#include "party_record_lookup.h"
#include "random.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "originals_core.h"
#include "originals_engine.h"
#include "originals_gameplay.h"
#include "originals_ai.h"
#include "originals_script.h"
#include "../lib/crt.h"

#include <cstring>

namespace {

// The player fields the writers touch.
constexpr std::size_t kPlayerVariables = 0xC4;   // NamedFloatTable (object_tables.h)
constexpr std::size_t kPlayerValue100 = 0x100;
constexpr std::size_t kPlayerDirty = 0x130;      // bits the state messages clear once sent

// The variables table (object_tables.cpp / not rewritten), through the game addresses.
void __fastcall VariableSet(void* a0, void*, const char* a2, std::uint32_t a3) { NamedTableSet(a0, nullptr, a2, a3); }
double __fastcall VariableGet(void* a0, void*, const char* a2) { return (double)NamedTableGet(a0, nullptr, a2); }
// 0047DA80: writes a state value into the stream; 006D4570: an object writes itself into one.
__declspec(naked) void __fastcall StreamWriteState(BitStream* /*stream*/, void*, int /*value*/) EI_ORIGINAL(0x0047DA80)
__declspec(naked) void __fastcall WriteStateTo(void* /*source*/, void*, BitStream* /*stream*/) EI_ORIGINAL(0x006D4570)

void Write(BitStream* out, const void* data, std::uint32_t size) { ei::VCall<void>(out, 2, data, size); }
void WriteByte(BitStream* out, std::uint8_t value) { Write(out, &value, 1); }
void WriteDword(BitStream* out, std::uint32_t value) { Write(out, &value, 4); }
void WriteText(BitStream* out, const char* text) {
    Write(out, text, static_cast<std::uint32_t>(std::strlen(text) + 1));
}

std::uint32_t ObjectId(const void* object) { return *reinterpret_cast<const std::uint32_t*>(static_cast<const char*>(object) + 0x0C); }

// The float at +0x28 of an object, copied through FLD / FSTP as the original does.
std::uint32_t ObjectValue28(const void* object) {
    std::uint32_t bits;
    const void* from = static_cast<const char*>(object) + 0x28;
    __asm {
        mov eax, from
        fld dword ptr [eax]
        fstp dword ptr [bits]
    }
    return bits;
}

std::uint32_t& Dirty(std::uint8_t* player) { return *reinterpret_cast<std::uint32_t*>(player + kPlayerDirty); }

bool IsNaN(std::uint32_t bits) { return (bits & 0x7F800000u) == 0x7F800000u && (bits & 0x007FFFFFu) != 0; }

} // namespace

// --- Variables and state -------------------------------------------------------------------------

void CPlayerServer::SetVariable(const char* name, std::uint32_t value) {
    VariableSet(player_ + kPlayerVariables, nullptr, name, value);
    WriteByte(&out_, 0x81);
    WriteText(&out_, name);
    WriteDword(&out_, value);
}

void CPlayerServer::SetVariableMax(const char* name, float value) {
    // Rounded to float (FST) before any comparison.
    volatile float stored = static_cast<float>(VariableGet(player_ + kPlayerVariables, nullptr, name));
    float current = stored;
    std::uint32_t currentBits, valueBits;
    std::memcpy(&currentBits, &current, 4);
    std::memcpy(&valueBits, &value, 4);
    // The larger value; the current one when the new one is not above it (or either is NaN).
    std::uint32_t resultBits = !(value >= current) ? currentBits : valueBits;
    float result;
    std::memcpy(&result, &resultBits, 4);
    // Nothing to do when it compares equal (or unordered).
    if (IsNaN(resultBits) || IsNaN(currentBits) || result == current) return;
    VariableSet(player_ + kPlayerVariables, nullptr, name, resultBits);
    WriteByte(&out_, 0x81);
    WriteText(&out_, name);
    WriteDword(&out_, resultBits);
}

void CPlayerServer::DeleteVariable(const char* name) {
    VariableRemove(player_ + kPlayerVariables, nullptr, name);
    WriteByte(&out_, 0x82);
    WriteText(&out_, name);
}

void CPlayerServer::SendGameTime() {
    std::uint32_t value = *reinterpret_cast<const std::uint32_t*>(player_ + kPlayerValue100);
    WriteByte(&out_, 0x07);
    WriteDword(&out_, value);
    WriteDword(&out_, *reinterpret_cast<const std::uint32_t*>(0x007AFFC4));
    Dirty(player_) &= ~0x10u;
}

int CPlayerServer::SendState02(int value) {
    Dirty(player_) &= ~0x1u;
    WriteByte(&out_, 0x02);
    StreamWriteState(&out_, nullptr, value);
    return 1;
}

int CPlayerServer::SendState70(std::uint8_t a, std::uint16_t b) {
    Dirty(player_) &= ~0x2u;
    std::uint8_t payload[3] = {a, static_cast<std::uint8_t>(b), static_cast<std::uint8_t>(b >> 8)};
    WriteByte(&out_, 0x70);
    Write(&out_, payload, 3);
    return 1;
}

int CPlayerServer::SendState71(std::uint32_t a, std::uint32_t b) {
    Dirty(player_) &= ~0x4u;
    std::uint32_t payload[2] = {a, b};
    WriteByte(&out_, 0x71);
    Write(&out_, payload, 8);
    return 1;
}

int CPlayerServer::SendState72(std::uint32_t x, std::uint32_t y, std::uint32_t z, std::uint32_t w) {
    Dirty(player_) &= ~0x8u;
    WriteByte(&out_, 0x72);
    std::uint32_t xyz[3] = {x, y, z};
    Write(&out_, xyz, 12);
    WriteDword(&out_, w);
    return 1;
}

void CPlayerServer::SendState73(void* source) {
    WriteByte(&out_, 0x73);
    WriteStateTo(source, nullptr, &out_);
    Dirty(player_) &= ~0x20u;
}

int CPlayerServer::SendPosition04(const Vector3* at, std::uint32_t a, std::uint32_t b) {
    WriteByte(&out_, 0x04);
    Write(&out_, at, 12);
    WriteDword(&out_, a);
    WriteDword(&out_, b);
    std::memcpy(&lastPosition_, at, sizeof(Vector3));
    return 1;
}

std::uint32_t CPlayerServer::SendPosition0F(const Vector3* at, const Vector3* other, std::uint8_t flag) {
    WriteByte(&out_, 0x0F);
    Write(&out_, at, 12);
    Write(&out_, other, 12);
    Write(&out_, &flag, 1);
    std::memcpy(&lastPosition_, at, sizeof(Vector3));
    // EAX is left holding the position's z.
    std::uint32_t z;
    std::memcpy(&z, &at->z, 4);
    return z;
}

// --- Simple messages -----------------------------------------------------------------------------

void CPlayerServer::Send17() { WriteByte(&out_, 0x17); }

void CPlayerServer::SendObject0A(const void* object) {
    WriteByte(&out_, 0x0A);
    WriteDword(&out_, ObjectId(object));
}

void CPlayerServer::SendPair16(std::uint32_t a, std::uint32_t b) {
    WriteByte(&out_, 0x16);
    WriteDword(&out_, a);
    WriteDword(&out_, b);
}

void CPlayerServer::SendObjects0C(const void* list) {
    WriteByte(&out_, 0x0C);
    // An MFC CPtrList: head at +4; nodes {next, prev, object}.
    auto* node = *reinterpret_cast<void* const*>(static_cast<const char*>(list) + 4);
    while (node != nullptr) {
        void* next = *static_cast<void**>(node);
        const void* object = static_cast<void**>(node)[2];
        WriteDword(&out_, ObjectId(object));
        WriteDword(&out_, ObjectValue28(object));
        node = next;
    }
    WriteDword(&out_, 0xFFFFFFFFu);
}

void CPlayerServer::SendObject14(const void* object) {
    WriteByte(&out_, 0x14);
    WriteDword(&out_, ObjectId(object));
    WriteDword(&out_, ObjectValue28(object));
}

void CPlayerServer::SendText0D(const char* text) {
    WriteByte(&out_, 0x0D);
    WriteText(&out_, text);
}

void CPlayerServer::SendText08(const char* text, std::uint32_t value) {
    WriteByte(&out_, 0x08);
    WriteText(&out_, text);
    WriteDword(&out_, value);
}

void CPlayerServer::SendValue15(std::uint32_t value) {
    WriteByte(&out_, 0x15);
    WriteDword(&out_, value);
}

void CPlayerServer::SendPair18(std::uint32_t a, std::uint32_t b) {
    WriteByte(&out_, 0x18);
    WriteDword(&out_, a);
    WriteDword(&out_, b);
}

void CPlayerServer::SendObject43(const void* object) {
    if (object == nullptr) return;
    WriteByte(&out_, 0x43);
    WriteDword(&out_, ObjectId(object));
}

void CPlayerServer::SendObject0B(const void* object, std::uint32_t value) {
    std::uint32_t payload[2] = {ObjectId(object), value};
    WriteByte(&out_, 0x0B);
    Write(&out_, payload, 8);
}

void CPlayerServer::SendText13(std::uint32_t, const char* text) {
    WriteByte(&out_, 0x13);
    WriteText(&out_, text);
}

void CPlayerServer::SendText(std::uint8_t type, const char* text) {
    WriteByte(&out_, type);
    WriteText(&out_, text);
}

void CPlayerServer::WriteTo(void* target) { ei::VCall<void>(target, 7, &out_); }

void CPlayerServer::SetDisconnect() { state_ = 4; }

void CPlayerServer::SetFlag70(std::uint8_t value) { flag70_ = value; }

// --- Party records -------------------------------------------------------------------------------

std::uint8_t* PartyRecords::Find(int unitId) const {
    for (int i = 0; i < count; ++i)
        if (*reinterpret_cast<const int*>(records + i * 0x784 + 0x1C) == unitId) return records + i * 0x784;
    return nullptr;
}

// --- CPlayer ------------------------------------------------------------------------------------

namespace {
// 00403F40 (base_manager.cpp), through its game address.
} // namespace

void CPlayer::Tick() {
    auto* node = objects_.head;
    while (node != nullptr) {
        auto* current = node;
        node = node->next;
        int id = static_cast<int>(reinterpret_cast<std::intptr_t>(current->data));
        CEngineObject* object = ManagerFindObject(Party()->ItemManager(), nullptr, id);
        if (object == nullptr || ei::VCall<int>(object, 6) == 0) objects_.RemoveAt(current);
    }
    connection_->TickDone();
}

// --- 0066C9A0: the server's Create ---------------------------------------------------------------

namespace {

constexpr int kServerHandlerCount = 0x100;

// The server's message handlers (thiscall, RET 0) by type, as 0066C9A0 stores them.
constexpr HandlerEntry kServerHandlers[] = {
    {0x00, std::uintptr_t(0x0066F890)}, {0x01, std::uintptr_t(0x0066FA90)}, {0x02, std::uintptr_t(0x0066FAA0)},
    {0x03, std::uintptr_t(0x00670C20)}, {0x04, std::uintptr_t(0x00670CE0)}, {0x05, std::uintptr_t(0x00670DA0)},
    {0x06, std::uintptr_t(0x00670E00)}, {0x07, std::uintptr_t(0x00670EB0)}, {0x08, std::uintptr_t(0x00670F50)},
    {0x09, std::uintptr_t(0x00671060)}, {0x10, std::uintptr_t(0x0066FAC0)}, {0x30, std::uintptr_t(0x0066FFF0)},
    {0x31, std::uintptr_t(0x00670190)}, {0x32, std::uintptr_t(0x006708D0)}, {0x33, std::uintptr_t(0x00670820)},
    {0x34, std::uintptr_t(0x00670960)}, {0x35, std::uintptr_t(0x006709D0)}, {0x36, std::uintptr_t(0x00670310)},
    {0x37, std::uintptr_t(0x00670810)}, {0x38, std::uintptr_t(0x00670A40)}, {0x39, std::uintptr_t(0x00670A90)},
    {0x3A, std::uintptr_t(0x0066FBD0)}, {0x3B, std::uintptr_t(0x0066FB70)}, {0x40, std::uintptr_t(0x00670B20)},
    {0x50, std::uintptr_t(0x00670B60)},
};

// The sent-object arrays emptied as 0047B620 does: every object released (slot 4), the block freed.
void ReleaseAll(PlayerObjectArray& objects) {
    // The data pointer is read once, the size after every release.
    CEngineObject** element = objects.data;
    for (int i = 0; i < objects.size; ++i) (*element++)->Release();
    objects.RemoveAll();
}

} // namespace

void CPlayerServer::ClearState() {
    out_.Resize(0);
    ReleaseAll(sentObjects_);
    ReleaseAll(sentTimed_);
    state_ = 0;
    lastPosition_.x = 0.0f;
    lastPosition_.y = 0.0f;
    lastPosition_.z = 0.0f;
    unknown_44 = -1;
    group50F8_.SetSize(0, -1);
    partyUnits_.SetSize(0, -1);
    unknown24_ = 0xFFFFFFFFu;
    refreshPending_ = 0;
    flag70_ = 0;
    PlacementsSetSizeOriginal(&placements_, nullptr, 0, -1);
    compressor_.Reset();
    decompressor_.Reset();
    refreshTimer_ = static_cast<int>(IsaacRandom::Global().Next() % 0x385u + 0x384u);
}

BOOL CPlayerServer::CreateServer() {
    ClearState();
    handlers_.SetSize(kServerHandlerCount);
    std::memset(static_cast<void*>(handlers_.data), 0, static_cast<std::size_t>(handlers_.size) * sizeof(Handler));
    for (const HandlerEntry& entry : kServerHandlers)
        handlers_[entry.type] = reinterpret_cast<Handler>(entry.address);
    return 1;
}

// --- A-H15: the server's lifetime and slots --------------------------------------------------------

namespace {

constexpr std::uintptr_t kConnectionVtable = std::uintptr_t(0x007431E0);   // CPlayerConnection's own
constexpr std::uintptr_t kServerVtable = std::uintptr_t(0x00743280);
constexpr std::uintptr_t kPlacementsVtable = std::uintptr_t(0x0073D2C8);
constexpr std::uintptr_t kClassNameAddress = std::uintptr_t(0x00794E40);   // "CPlayerServer"
constexpr int kClassId = 0x3B;

void ConstructGroup(ScriptGroup& group) {
    group.data = nullptr;
    group.size = 0;
    group.maxSize = 0;
    group.growBy = 0;
    group.vtable = scriptvtable::kRefs;
}

// The engine object part (CPlayerServer does not derive from CEngineObject in C++).
CEngineObject* EngineObject(CPlayerServer* server) { return reinterpret_cast<CEngineObject*>(server); }

// A delta field on the stack (four memory streams), as 0066D490 builds and destroys it inline.
struct LocalDeltaField {
    alignas(4) unsigned char bytes[sizeof(DeltaField)];
    LocalDeltaField() { Get()->Construct(); }
    ~LocalDeltaField() { Get()->Destruct(); }
    DeltaField* Get() { return reinterpret_cast<DeltaField*>(bytes); }
};
constexpr std::uint8_t kPathMessage = 0x0E;
constexpr int kPathKindNoPath = 3;

} // namespace

CPlayerServer* CPlayerServer::Construct() {
    EngineObject(this)->ClearFields();
    vtable_ = reinterpret_cast<void*>(kConnectionVtable);
    player_ = nullptr;
    handlers_.Construct();
    out_.ConstructQueueStream();
    placements_.vtable = reinterpret_cast<void*>(kPlacementsVtable);
    placements_.data = nullptr;
    placements_.size = 0;
    placements_.maxSize = 0;
    placements_.growBy = 0;
    compressor_.Construct();
    decompressor_.Construct();
    sentObjects_.Construct();
    sentTimed_.Construct();
    ConstructGroup(group50F8_);
    ScriptGroupConstructOriginal(&partyUnits_);
    partyUnits_.vtable = scriptvtable::kRefs;
    vtable_ = reinterpret_cast<void*>(kServerVtable);
    ClearState();
    return this;
}

void CPlayerServer::DestructBody() {
    vtable_ = reinterpret_cast<void*>(kServerVtable);
    ClearState();
    ScriptGroupDestructOriginal(&partyUnits_);
    ScriptGroupDestructOriginal(&group50F8_);
    sentTimed_.FreeData();
    sentObjects_.FreeData();
    decompressor_.out.End();
    compressor_.out.End();
    placements_.vtable = reinterpret_cast<void*>(kPlacementsVtable);
    if (placements_.data) gamecrt::operator_delete(placements_.data);
    out_.Destruct();
    handlers_.FreeData();
    vtable_ = reinterpret_cast<void*>(kConnectionVtable);
    player_ = nullptr;
    EngineObject(this)->DestructBase();
}

CPlayerServer* CPlayerServer::DeletingDestruct(unsigned flags) {
    DestructBody();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}

const char* __fastcall PlayerServerClassName(CPlayerServer*) { return reinterpret_cast<const char*>(kClassNameAddress); }
int __fastcall PlayerServerClassId(CPlayerServer*) { return kClassId; }

void CPlayerServer::ForgetSentObjects() {
    ReleaseAll(sentObjects_);
    ReleaseAll(sentTimed_);
    group50F8_.SetSize(0, -1);
    partyUnits_.Resize(0);
}

void CPlayerServer::ClearGroup50F8() { group50F8_.Resize(0); }

void CPlayerServer::SendPathMessage(CEngineObject* unit, void* path, const float* point, int kind, int flag,
                                    std::uint32_t limitBits) {
    if (!unit) return;
    std::uint8_t type = kPathMessage;
    out_.Write(&type, 1);
    out_.Write(&kind, 4);
    out_.Write(&flag, 4);
    std::uint32_t word;
    std::memcpy(&word, &point[0], 4);
    out_.Write(&word, 4);
    std::memcpy(&word, &point[1], 4);
    out_.Write(&word, 4);
    int id = unit->id_;
    out_.Write(&id, 4);
    out_.Write(&limitBits, 4);
    if (kind == kPathKindNoPath) return;
    LocalDeltaField field;
    DeltaField* delta = field.Get()->BeginWriteB(0);
    PathWriteOriginal(path, nullptr, delta);
    delta->EndField();
    out_.EndBits();
    BitStream& written = field.Get()->out[0];
    written.Seek(0);
    std::uint8_t* data = written.data_;
    out_.Write(data, written.Size());
}

// --- Hooks ---------------------------------------------------------------------------------------

EI_REPLACE_METHOD(0x0066C540, CPlayerServer::Construct);
EI_REPLACE_METHOD(0x0066C770, CPlayerServer::DestructBody);
EI_REPLACE_METHOD(0x0066C750, CPlayerServer::DeletingDestruct);
EI_REPLACE(0x0066C730, PlayerServerClassName);
EI_REPLACE(0x0066C740, PlayerServerClassId);
EI_REPLACE_METHOD(0x0066CC50, CPlayerServer::ClearState);
EI_REPLACE_METHOD(0x0066CF20, CPlayerServer::ClearState);
EI_REPLACE_METHOD(0x0066CE50, CPlayerServer::ForgetSentObjects);
EI_REPLACE_METHOD(0x0066D090, CPlayerServer::ClearGroup50F8);
EI_REPLACE_METHOD(0x0066D490, CPlayerServer::SendPathMessage);

EI_REPLACE_METHOD(0x0066C9A0, CPlayerServer::CreateServer);

EI_REPLACE_METHOD(0x0066EE10, CPlayerServer::SetVariable);
EI_REPLACE_METHOD(0x0066EE80, CPlayerServer::SetVariableMax);
EI_REPLACE_METHOD(0x0066EF30, CPlayerServer::DeleteVariable);
EI_REPLACE_METHOD(0x0066F180, CPlayerServer::SendGameTime);
EI_REPLACE_METHOD(0x0066EBD0, CPlayerServer::SendState02);
EI_REPLACE_METHOD(0x0066EC10, CPlayerServer::SendState70);
EI_REPLACE_METHOD(0x0066EC70, CPlayerServer::SendState71);
EI_REPLACE_METHOD(0x0066ECD0, CPlayerServer::SendState72);
EI_REPLACE_METHOD(0x0066F240, CPlayerServer::SendState73);
EI_REPLACE_METHOD(0x0066ED30, CPlayerServer::SendPosition04);
EI_REPLACE_METHOD(0x0066EDA0, CPlayerServer::SendPosition0F);
EI_REPLACE_METHOD(0x0066EA10, CPlayerServer::Send17);
EI_REPLACE_METHOD(0x0066EA30, CPlayerServer::SendObject0A);
EI_REPLACE_METHOD(0x0066EA70, CPlayerServer::SendPair16);
EI_REPLACE_METHOD(0x0066EAC0, CPlayerServer::SendObjects0C);
EI_REPLACE_METHOD(0x0066EB40, CPlayerServer::SendObject14);
EI_REPLACE_METHOD(0x0066EB90, CPlayerServer::SendText0D);
EI_REPLACE_METHOD(0x0066F1F0, CPlayerServer::SendText08);
EI_REPLACE_METHOD(0x0066F280, CPlayerServer::SendValue15);
EI_REPLACE_METHOD(0x0066F2C0, CPlayerServer::SendPair18);
EI_REPLACE_METHOD(0x0066D6A0, CPlayerServer::SendObject43);
EI_REPLACE_METHOD(0x0066D6E0, CPlayerServer::SendObject0B);
EI_REPLACE_METHOD(0x0066D770, CPlayerServer::SendText13);
EI_REPLACE_METHOD(0x0066D7B0, CPlayerServer::SendText);
EI_REPLACE_METHOD(0x0066D230, CPlayerServer::WriteTo);
EI_REPLACE_METHOD(0x0066D250, CPlayerServer::SetDisconnect);
EI_REPLACE_METHOD(0x0066D260, CPlayerServer::SetFlag70);
EI_REPLACE_METHOD(0x006621B0, PartyRecords::Find);
EI_REPLACE_METHOD(0x00665DE0, CPlayer::Tick);
