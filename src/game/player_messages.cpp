// PlayerServer's message writers and the party record lookup. See player_messages.h and
// docs/notes/objects.md.

#include "player_messages.h"

#include "ai_map.h"
#include "base_manager.h"
#include "game_singletons.h"
#include "item.h"
#include "item_manager.h"
#include "object_tables.h"
#include "party.h"
#include "random.h"
#include "script.h"
#include "unit.h"
#include "world.h"

#include "lib/bytes.h"
#include "lib/nan_audit.h"

#include <algorithm>
#include <bit>
#include <cstring>

namespace {

// The variables table (object_tables.cpp).
void VariableSet(NamedFloatTable* table, std::string_view name, std::uint32_t bits) { table->Set(name, std::bit_cast<float>(bits)); }

void Write(MemoryStream* out, const void* data, std::uint32_t size) { out->Write(data, size); }
void WriteByte(MemoryStream* out, std::uint8_t value) { Write(out, &value, 1); }
void WriteDword(MemoryStream* out, std::uint32_t value) { Write(out, &value, 4); }

std::uint32_t ObjectId(const EngineObject* object) { return static_cast<std::uint32_t>(object->id); }

// An item's charge (the float; the client's 0x0C / 0x14 handlers store it there), as its bits.
std::uint32_t ItemCharge(const EngineObject* object) {
    return std::bit_cast<std::uint32_t>(static_cast<const Item*>(object)->charge);
}

// The world player's party variables (a NamedFloatTable, object_tables.h).
NamedFloatTable* Variables(Player* player) { return &player->GetParty()->variables; }

// Looks an object up by id (base_manager.cpp), through its game address.

constexpr int kServerHandlerCount = 0x100;

// The server's message handlers by type.
constexpr HandlerEntry<PlayerServer> kServerHandlers[] = {
    {0x00, &PlayerServer::OnLogin}, {0x01, &PlayerServer::OnLeave}, {0x02, &PlayerServer::OnRefreshParty},
    {0x03, &PlayerServer::OnTakeExit}, {0x04, &PlayerServer::OnGameStateRequest}, {0x05, &PlayerServer::OnJoined},
    {0x06, &PlayerServer::OnLeaveToZone}, {0x07, &PlayerServer::OnBriefingDone}, {0x08, &PlayerServer::OnNewParty},
    {0x09, &PlayerServer::OnResume}, {0x10, &PlayerServer::OnPause}, {0x30, &PlayerServer::OnWalk},
    {0x31, &PlayerServer::OnAttack}, {0x32, &PlayerServer::OnCastAtPoint}, {0x33, &PlayerServer::OnCastAtObject},
    {0x34, &PlayerServer::OnWantedWeapon}, {0x35, &PlayerServer::OnMoveStyle}, {0x36, &PlayerServer::OnUseOrder},
    {0x37, &PlayerServer::OnMessage37}, {0x38, &PlayerServer::OnUnitLookup}, {0x39, &PlayerServer::OnFollow},
    {0x3A, &PlayerServer::OnGoTo}, {0x3B, &PlayerServer::OnFightAtWill}, {0x40, &PlayerServer::OnLastPosition},
    {0x50, &PlayerServer::OnPartyUpdate},
};

// The sent-object arrays emptied: every object released, the block freed.
void ReleaseAll(std::vector<EngineObject*>& objects) {
    // The data pointer is read once, the size after every release.
    EngineObject** element = objects.data();
    for (int i = 0; i < std::ssize(objects); ++i) (*element++)->Release();
    objects.clear();
}

constexpr std::uint8_t kPathMessage = 0x0E;
constexpr int kPathKindNoPath = 3;

} // namespace

// --- Variables and state -------------------------------------------------------------------------

void PlayerServer::SetVariable(std::string_view name, std::uint32_t value) {
    VariableSet(Variables(player), name, value);
    WriteByte(&out, 0x81);
    out.WriteString(name);
    WriteDword(&out, value);
}

void PlayerServer::SetVariableMax(std::string_view name, float value) {
    float stored = Variables(player)->Get(name);
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
    VariableSet(Variables(player), name, resultBits);
    WriteByte(&out, 0x81);
    out.WriteString(name);
    WriteDword(&out, resultBits);
}

void PlayerServer::DeleteVariable(const char* name) {
    Variables(player)->Remove(name);
    WriteByte(&out, 0x82);
    out.WriteString(name);
}

void PlayerServer::SendGameTime() {
    std::uint32_t value = static_cast<std::uint32_t>(player->sessionKind);
    WriteByte(&out, 0x07);
    WriteDword(&out, value);
    WriteDword(&out, static_cast<std::uint32_t>(g_serverWorld.tick));
    player->dirty &= ~PlayerDirty::GameState;
}

int PlayerServer::SendState02(MemoryStream* zone) {
    player->dirty &= ~PlayerDirty::Zone;
    WriteByte(&out, 0x02);
    out.WriteStream(zone);
    return 1;
}

int PlayerServer::SendState70(std::uint8_t weather, std::uint16_t strength) {
    player->dirty &= ~PlayerDirty::Weather;
    std::uint8_t payload[3] = {weather, static_cast<std::uint8_t>(strength), static_cast<std::uint8_t>(strength >> 8)};
    WriteByte(&out, 0x70);
    Write(&out, payload, 3);
    return 1;
}

int PlayerServer::SendState71(std::uint32_t time, std::uint32_t speed) {
    player->dirty &= ~PlayerDirty::Time;
    std::uint32_t payload[2] = {time, speed};
    WriteByte(&out, 0x71);
    Write(&out, payload, 8);
    return 1;
}

int PlayerServer::SendState72(std::uint32_t x, std::uint32_t y, std::uint32_t z, std::uint32_t w) {
    player->dirty &= ~PlayerDirty::Wind;
    WriteByte(&out, 0x72);
    std::uint32_t xyz[3] = {x, y, z};
    Write(&out, xyz, 12);
    WriteDword(&out, w);
    return 1;
}

void PlayerServer::SendState73(const WaterTweens* source) {
    WriteByte(&out, 0x73);
    source->Save(&out);
    player->dirty &= ~PlayerDirty::Countdowns;
}

int PlayerServer::SendPosition04(const Vector3& at, std::uint32_t startValue, std::uint32_t sessionKind) {
    WriteByte(&out, 0x04);
    Write(&out, &at, 12);
    WriteDword(&out, startValue);
    WriteDword(&out, sessionKind);
    lastPosition = at;
    return 1;
}

std::uint32_t PlayerServer::SendPosition0F(const Vector3& at, const Vector3& other, std::uint8_t flag) {
    WriteByte(&out, 0x0F);
    Write(&out, &at, 12);
    Write(&out, &other, 12);
    Write(&out, &flag, 1);
    lastPosition = at;
    // EAX is left holding the position's z.
    std::uint32_t z;
    z = std::bit_cast<std::uint32_t>(at.z);
    return z;
}

// --- Simple messages -----------------------------------------------------------------------------

void PlayerServer::Send17() { WriteByte(&out, 0x17); }

void PlayerServer::SendObject0A(const EngineObject* object) {
    WriteByte(&out, 0x0A);
    WriteDword(&out, ObjectId(object));
}

void PlayerServer::SendPair16(std::uint32_t unitId, std::uint32_t index) {
    WriteByte(&out, 0x16);
    WriteDword(&out, unitId);
    WriteDword(&out, index);
}

void PlayerServer::SendObject14(const EngineObject* object) {
    WriteByte(&out, 0x14);
    WriteDword(&out, ObjectId(object));
    WriteDword(&out, ItemCharge(object));
}

void PlayerServer::SendText0D(const char* text) {
    WriteByte(&out, 0x0D);
    out.WriteString(text);
}

void PlayerServer::SendValue15(std::uint32_t value) {
    WriteByte(&out, 0x15);
    WriteDword(&out, value);
}

void PlayerServer::SendPair18(std::uint32_t unitId, std::uint32_t value) {
    WriteByte(&out, 0x18);
    WriteDword(&out, unitId);
    WriteDword(&out, value);
}

void PlayerServer::SendObject43(const EngineObject* object) {
    if (object == nullptr) return;
    WriteByte(&out, 0x43);
    WriteDword(&out, ObjectId(object));
}

void PlayerServer::SendObject0B(const EngineObject* object, AckKind kind) {
    std::uint32_t payload[2] = {ObjectId(object), static_cast<std::uint32_t>(kind)};
    WriteByte(&out, 0x0B);
    Write(&out, payload, 8);
}

void PlayerServer::SendValue12(std::uint32_t value) {
    std::uint8_t payload[1 + sizeof(value)] = {};
    lib::StoreBytes(payload + 1, value);
    WriteByte(&out, 0x12);
    Write(&out, payload, sizeof(payload));
}

void PlayerServer::SendText13(std::string_view text) {
    WriteByte(&out, 0x13);
    out.WriteString(text);
}

void PlayerServer::WriteTo(ByteStream* target) { target->CopyFrom(&out); }

void PlayerServer::SetDisconnect() { state = 4; }

void PlayerServer::SetPartyDeployed(std::uint8_t value) { partyDeployed = value; }

// --- Party records -------------------------------------------------------------------------------

// --- The server's Create ---------------------------------------------------------------

void PlayerServer::ClearServerState() {
    out.Resize(0);
    ReleaseAll(sentObjects);
    ReleaseAll(sentTimed);
    state = 0;
    lastPosition.x = 0.0f;
    lastPosition.y = 0.0f;
    lastPosition.z = 0.0f;
    spare = -1;
    possessedGroup.Clear();
    partyUnits.Clear();
    possessedId = 0xFFFFFFFFu;
    danger = PartyDanger::Safe;
    partyDeployed = 0;
    placements.clear();
    compressor.Reset();
    decompressor.Reset();
    refreshTimer = static_cast<int>(g_random.Next() % 0x385u + 0x384u);
}

void PlayerServer::CreateServer() {
    ClearServerState();
    handlers.resize(kServerHandlerCount);
    std::fill_n(handlers.data(), std::ssize(handlers), Handler{});
    for (const auto& entry : kServerHandlers)
        handlers[entry.type] = entry.handler;
}

int PlayerServer::OnMessage37() { return 0; }

// --- The server's lifetime and virtuals ------------------------------------------------------

// The stream and the coders are members (constructed before the body; the original builds them after the handler
// table).
PlayerServer::PlayerServer() {
    ClearServerState();
}

// The stream and the coders are members, destroyed after the body (the original frees them before the handler table).
PlayerServer::~PlayerServer() {
    ClearServerState();
}

const char* PlayerServer::ClassName() { return "CPlayerServer"; }
ClassId PlayerServer::GetClassId() { return ClassId::PlayerServer; }

void PlayerServer::ReleaseSentObjects() {
    ReleaseAll(sentObjects);
    ReleaseAll(sentTimed);
    possessedGroup.Clear();
    partyUnits.Clear();
}

void PlayerServer::ClearPossessedGroup() { possessedGroup.Clear(); }

void PlayerServer::SendPathMessage(EngineObject* unit, PathOrder* path, const float* point, int kind, int flag,
                                    std::uint32_t limitBits) {
    if (!unit) return;
    std::uint8_t messageType = kPathMessage;
    out.WriteValue(messageType);
    out.WriteValue(kind);
    out.WriteValue(flag);
    std::uint32_t word;
    word = std::bit_cast<std::uint32_t>(point[0]);
    out.WriteValue(word);
    word = std::bit_cast<std::uint32_t>(point[1]);
    out.WriteValue(word);
    int unitId = unit->id;
    out.WriteValue(unitId);
    out.WriteValue(limitBits);
    if (kind == kPathKindNoPath) return;
    DeltaField field;
    DeltaField* delta = field.BeginWriteB(0);
    path->WriteState(delta);
    delta->EndField();
    out.EndBits();
    MemoryStream& written = field.out[0];
    written.Seek(0);
    std::uint8_t* data = written.data.data();
    out.Write(data, written.Size());
}
