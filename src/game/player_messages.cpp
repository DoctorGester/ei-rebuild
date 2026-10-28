// CPlayerServer's message writers and the party record lookup. See player_messages.h and
// docs/notes/objects.md.

#include "ai_map.h"
#include "base_manager.h"
#include "object_tables.h"
#include "player_messages.h"
#include "item.h"
#include "game_singletons.h"
#include "party.h"
#include "random.h"
#include "script.h"
#include "unit.h"
#include "world.h"

#include "ei/x87.h"
#include "lib/bytes.h"
#include "lib/static_object.h"
#include "lib/nan_audit.h"

#include <algorithm>
#include <bit>
#include <cstring>

namespace {

// The variables table (object_tables.cpp).
void VariableSet(NamedFloatTable* table, std::string_view name, std::uint32_t bits) { table->Set(name, std::bit_cast<float>(bits)); }
double VariableGet(NamedFloatTable* table, std::string_view name) { return table->Get(name); }

void Write(CMemoryStream* out, const void* data, std::uint32_t size) { out->Write(data, size); }
void WriteByte(CMemoryStream* out, std::uint8_t value) { Write(out, &value, 1); }
void WriteDword(CMemoryStream* out, std::uint32_t value) { Write(out, &value, 4); }

std::uint32_t ObjectId(const CEngineObject* object) { return static_cast<std::uint32_t>(object->id_); }

// An item's charge (the float; the client's 0x0C / 0x14 handlers store it there), as its bits.
std::uint32_t ItemCharge(const CEngineObject* object) {
    return std::bit_cast<std::uint32_t>(static_cast<const CItem*>(object)->charge_);
}

// The world player's party variables (a NamedFloatTable, object_tables.h).
NamedFloatTable* Variables(CPlayer* player) { return &player->Party()->variables_; }

// Looks an object up by id (base_manager.cpp), through its game address.

constexpr int kServerHandlerCount = 0x100;

// The server's message handlers by type.
constexpr HandlerEntry<CPlayerServer> kServerHandlers[] = {
    {0x00, &CPlayerServer::OnLogin}, {0x01, &CPlayerServer::OnLeave}, {0x02, &CPlayerServer::OnRefreshParty},
    {0x03, &CPlayerServer::OnTakeExit}, {0x04, &CPlayerServer::OnGameStateRequest}, {0x05, &CPlayerServer::OnJoined},
    {0x06, &CPlayerServer::OnLeaveToZone}, {0x07, &CPlayerServer::OnBriefingDone}, {0x08, &CPlayerServer::OnNewParty},
    {0x09, &CPlayerServer::OnResume}, {0x10, &CPlayerServer::OnPause}, {0x30, &CPlayerServer::OnWalk},
    {0x31, &CPlayerServer::OnAttack}, {0x32, &CPlayerServer::OnCastAtPoint}, {0x33, &CPlayerServer::OnCastAtObject},
    {0x34, &CPlayerServer::OnWantedWeapon}, {0x35, &CPlayerServer::OnMoveStyle}, {0x36, &CPlayerServer::OnUseOrder},
    {0x37, &CPlayerServer::OnMessage37}, {0x38, &CPlayerServer::OnUnitLookup}, {0x39, &CPlayerServer::OnFollow},
    {0x3A, &CPlayerServer::OnGoTo}, {0x3B, &CPlayerServer::OnFightAtWill}, {0x40, &CPlayerServer::OnLastPosition},
    {0x50, &CPlayerServer::OnPartyUpdate},
};

// The sent-object arrays emptied: every object released, the block freed.
void ReleaseAll(std::vector<CEngineObject*>& objects) {
    // The data pointer is read once, the size after every release.
    CEngineObject** element = objects.data();
    for (int i = 0; i < std::ssize(objects); ++i) (*element++)->Release();
    objects.clear();
}

constexpr std::uint8_t kPathMessage = 0x0E;
constexpr int kPathKindNoPath = 3;

} // namespace

// --- Variables and state -------------------------------------------------------------------------

void CPlayerServer::SetVariable(std::string_view name, std::uint32_t value) {
    VariableSet(Variables(player_), name, value);
    WriteByte(&out_, 0x81);
    out_.WriteString(name);
    WriteDword(&out_, value);
}

void CPlayerServer::SetVariableMax(std::string_view name, float value) {
    // Rounded to float before any comparison.
    float stored = static_cast<float>(VariableGet(Variables(player_), name));
    float current = stored;
    std::uint32_t currentBits, valueBits;
    currentBits = std::bit_cast<std::uint32_t>(current);
    valueBits = std::bit_cast<std::uint32_t>(value);
    // The larger value; the current one when the new one is not above it (or either is NaN).
    std::uint32_t resultBits = value >= current ? valueBits : currentBits;
    float result;
    result = std::bit_cast<float>(resultBits);
    // Nothing to do when it compares equal.
    EI_NAN_AUDIT(result, current);
    if (result == current) return;
    VariableSet(Variables(player_), name, resultBits);
    WriteByte(&out_, 0x81);
    out_.WriteString(name);
    WriteDword(&out_, resultBits);
}

void CPlayerServer::DeleteVariable(const char* name) {
    Variables(player_)->Remove(name);
    WriteByte(&out_, 0x82);
    out_.WriteString(name);
}

void CPlayerServer::SendGameTime() {
    std::uint32_t value = static_cast<std::uint32_t>(player_->sessionKind_);
    WriteByte(&out_, 0x07);
    WriteDword(&out_, value);
    WriteDword(&out_, static_cast<std::uint32_t>(g_serverWorld->tick_));
    player_->dirty_ &= ~0x10;
}

int CPlayerServer::SendState02(CMemoryStream* zone) {
    player_->dirty_ &= ~0x1;
    WriteByte(&out_, 0x02);
    out_.WriteStream(zone);
    return 1;
}

int CPlayerServer::SendState70(std::uint8_t a, std::uint16_t b) {
    player_->dirty_ &= ~0x2;
    std::uint8_t payload[3] = {a, static_cast<std::uint8_t>(b), static_cast<std::uint8_t>(b >> 8)};
    WriteByte(&out_, 0x70);
    Write(&out_, payload, 3);
    return 1;
}

int CPlayerServer::SendState71(std::uint32_t a, std::uint32_t b) {
    player_->dirty_ &= ~0x4;
    std::uint32_t payload[2] = {a, b};
    WriteByte(&out_, 0x71);
    Write(&out_, payload, 8);
    return 1;
}

int CPlayerServer::SendState72(std::uint32_t x, std::uint32_t y, std::uint32_t z, std::uint32_t w) {
    player_->dirty_ &= ~0x8;
    WriteByte(&out_, 0x72);
    std::uint32_t xyz[3] = {x, y, z};
    Write(&out_, xyz, 12);
    WriteDword(&out_, w);
    return 1;
}

void CPlayerServer::SendState73(const WaterTweens* source) {
    WriteByte(&out_, 0x73);
    source->Save(&out_);
    player_->dirty_ &= ~0x20;
}

int CPlayerServer::SendPosition04(const Vector3* at, std::uint32_t a, std::uint32_t b) {
    WriteByte(&out_, 0x04);
    Write(&out_, at, 12);
    WriteDword(&out_, a);
    WriteDword(&out_, b);
    lastPosition_ = *at;
    return 1;
}

std::uint32_t CPlayerServer::SendPosition0F(const Vector3* at, const Vector3* other, std::uint8_t flag) {
    WriteByte(&out_, 0x0F);
    Write(&out_, at, 12);
    Write(&out_, other, 12);
    Write(&out_, &flag, 1);
    lastPosition_ = *at;
    // EAX is left holding the position's z.
    std::uint32_t z;
    z = std::bit_cast<std::uint32_t>(at->z);
    return z;
}

// --- Simple messages -----------------------------------------------------------------------------

void CPlayerServer::Send17() { WriteByte(&out_, 0x17); }

void CPlayerServer::SendObject0A(const CEngineObject* object) {
    WriteByte(&out_, 0x0A);
    WriteDword(&out_, ObjectId(object));
}

void CPlayerServer::SendPair16(std::uint32_t a, std::uint32_t b) {
    WriteByte(&out_, 0x16);
    WriteDword(&out_, a);
    WriteDword(&out_, b);
}

void CPlayerServer::SendObject14(const CEngineObject* object) {
    WriteByte(&out_, 0x14);
    WriteDword(&out_, ObjectId(object));
    WriteDword(&out_, ItemCharge(object));
}

void CPlayerServer::SendText0D(const char* text) {
    WriteByte(&out_, 0x0D);
    out_.WriteString(text);
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

void CPlayerServer::SendObject43(const CEngineObject* object) {
    if (object == nullptr) return;
    WriteByte(&out_, 0x43);
    WriteDword(&out_, ObjectId(object));
}

void CPlayerServer::SendObject0B(const CEngineObject* object, std::uint32_t value) {
    std::uint32_t payload[2] = {ObjectId(object), value};
    WriteByte(&out_, 0x0B);
    Write(&out_, payload, 8);
}

void CPlayerServer::SendValue12(std::uint32_t value) {
    std::uint8_t payload[1 + sizeof(value)] = {};
    lib::StoreBytes(payload + 1, value);
    WriteByte(&out_, 0x12);
    Write(&out_, payload, sizeof(payload));
}

void CPlayerServer::SendText13(std::string_view text) {
    WriteByte(&out_, 0x13);
    out_.WriteString(text);
}

void CPlayerServer::WriteTo(CStream* target) { target->CopyFrom(&out_); }

void CPlayerServer::SetDisconnect() { state_ = 4; }

void CPlayerServer::SetPartyDeployed(std::uint8_t value) { partyDeployed_ = value; }

// --- Party records -------------------------------------------------------------------------------

PartyUnitRecord* CParty::Find(int unitId) {
    for (int i = 0; i < std::ssize(members_); ++i)
        if (members_[i].unitId == static_cast<std::uint32_t>(unitId)) return &members_[i];
    return nullptr;
}

void CPlayer::Tick() {
    auto node = recharging_.begin();
    while (node != recharging_.end()) {
        auto current = node;
        ++node;
        int id = static_cast<int>(*current);
        CEngineObject* object = Party()->ItemFactory()->FindObject(id);
        if (object == nullptr || object->RunTick() == 0) recharging_.erase(current);
    }
    connection_->TickDone();
}

// --- The server's Create ---------------------------------------------------------------

void CPlayerServer::ClearServerState() {
    out_.Resize(0);
    ReleaseAll(sentObjects_);
    ReleaseAll(sentTimed_);
    state_ = 0;
    lastPosition_.x = 0.0f;
    lastPosition_.y = 0.0f;
    lastPosition_.z = 0.0f;
    spare_ = -1;
    possessedGroup_.clear();
    partyUnits_.clear();
    possessedId_ = 0xFFFFFFFFu;
    refreshPending_ = 0;
    partyDeployed_ = 0;
    placements_.clear();
    compressor_.Reset();
    decompressor_.Reset();
    refreshTimer_ = static_cast<int>(g_random.Next() % 0x385u + 0x384u);
}

BOOL CPlayerServer::CreateServer() {
    ClearServerState();
    handlers_.resize(kServerHandlerCount);
    std::fill_n(handlers_.data(), std::ssize(handlers_), Handler{});
    for (const auto& entry : kServerHandlers)
        handlers_[entry.type] = entry.handler;
    return 1;
}

int CPlayerServer::OnMessage37() { return 0; }

// --- The server's lifetime and virtuals ------------------------------------------------------

// The stream is a member (constructed before the body; the original builds it after the handler table).
CPlayerServer::CPlayerServer() {
    compressor_.Construct();
    decompressor_.Construct();
    ClearServerState();
}

// The stream is a member, destroyed after the body (the original frees it before the handler table).
CPlayerServer::~CPlayerServer() {
    ClearServerState();
    decompressor_.out.End();
    compressor_.out.End();
}

const char* CPlayerServer::GetClassName() { return "CPlayerServer"; }
int CPlayerServer::GetClassId() { return 0x3B; }

void CPlayerServer::ReleaseSentObjects() {
    ReleaseAll(sentObjects_);
    ReleaseAll(sentTimed_);
    possessedGroup_.clear();
    partyUnits_.clear();
}

void CPlayerServer::ClearPossessedGroup() { possessedGroup_.clear(); }

void CPlayerServer::SendPathMessage(CEngineObject* unit, PathOrder* path, const float* point, int kind, int flag,
                                    std::uint32_t limitBits) {
    if (!unit) return;
    std::uint8_t type = kPathMessage;
    out_.WriteValue(type);
    out_.WriteValue(kind);
    out_.WriteValue(flag);
    std::uint32_t word;
    word = std::bit_cast<std::uint32_t>(point[0]);
    out_.WriteValue(word);
    word = std::bit_cast<std::uint32_t>(point[1]);
    out_.WriteValue(word);
    int id = unit->id_;
    out_.WriteValue(id);
    out_.WriteValue(limitBits);
    if (kind == kPathKindNoPath) return;
    DeltaField field;
    DeltaField* delta = field.BeginWriteB(0);
    path->WriteState(delta);
    delta->EndField();
    out_.EndBits();
    CMemoryStream& written = field.out[0];
    written.Seek(0);
    std::uint8_t* data = written.data_;
    out_.Write(data, written.Size());
}
