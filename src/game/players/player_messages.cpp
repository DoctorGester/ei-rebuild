// PlayerServer's message writers and the party record lookup. See player_messages.h and
// docs/notes/objects.md.

#include "game/players/player_messages.h"
#include "game/players/player.h"

#include "game/ai/ai_map.h"
#include "engine/core/base_manager.h"
#include "game/app/singletons.h"
#include "game/items/item.h"
#include "game/items/item_manager.h"
#include "engine/core/named_floats.h"
#include "game/players/party.h"
#include "engine/core/random.h"
#include "game/script/script.h"
#include "game/units/unit.h"
#include "game/world/world.h"
#include "game/effects/world_effect.h"

#include "lib/bytes.h"

#include <algorithm>
#include <bit>
#include <cstring>

namespace {

// The variables table (named_floats.h).
void VariableSet(NamedFloatTable* table, std::string_view name, std::uint32_t bits) { table->Set(name, std::bit_cast<float>(bits)); }

void Write(MemoryStream* out, const void* data, std::uint32_t size) { out->Write(data, size); }
void WriteDword(MemoryStream* out, std::uint32_t value) { Write(out, &value, 4); }


// The world player's party variables (a NamedFloatTable, named_floats.h).
NamedFloatTable* Variables(Player* player) { return &player->GetParty()->variables; }

// Looks an object up by id (base_manager.cpp), through its game address.

// The sent-object arrays emptied: every object released, the block freed.
template <class T>
void ReleaseAll(std::vector<T*>& objects) {
    // The data pointer is read once, the size after every release.
    T** element = objects.data();
    for (int i = 0; i < std::ssize(objects); ++i) (*element++)->Release();
    objects.clear();
}

constexpr int kPathKindNoPath = 3;

} // namespace

// --- Variables and state -------------------------------------------------------------------------

void PlayerServer::SetVariable(std::string_view name, float value) {
    Variables(player)->Set(name, value);
    WriteType(&out, ServerMessage::SetVariable);
    out.WriteString(name);
    WriteDword(&out, std::bit_cast<std::uint32_t>(value));
}

void PlayerServer::SetVariableMax(std::string_view name, float value) {
    float current = Variables(player)->Get(name);
    // The larger value; the current one when the new one is not above it (or either is NaN).
    float result = value >= current ? value : current;
    // Nothing to do when it compares equal.
    if (result == current) return;
    const std::uint32_t resultBits = std::bit_cast<std::uint32_t>(result);
    VariableSet(Variables(player), name, resultBits);
    WriteType(&out, ServerMessage::SetVariable);
    out.WriteString(name);
    WriteDword(&out, resultBits);
}

void PlayerServer::DeleteVariable(std::string_view name) {
    Variables(player)->Remove(name);
    WriteType(&out, ServerMessage::DeleteVariable);
    out.WriteString(name);
}

void PlayerServer::SendGameTime() {
    std::uint32_t value = static_cast<std::uint32_t>(player->sessionKind);
    WriteType(&out, ServerMessage::GameState);
    WriteDword(&out, value);
    WriteDword(&out, static_cast<std::uint32_t>(g_serverWorld.tick));
    player->dirty &= ~PlayerDirty::GameState;
}

void PlayerServer::SendZone(MemoryStream* zone) {
    player->dirty &= ~PlayerDirty::Zone;
    WriteType(&out, ServerMessage::Zone);
    out.WriteStream(zone);
}

void PlayerServer::SendWeather(std::uint8_t weather, std::uint16_t strength) {
    player->dirty &= ~PlayerDirty::Weather;
    std::uint8_t payload[3] = {weather, static_cast<std::uint8_t>(strength), static_cast<std::uint8_t>(strength >> 8)};
    WriteType(&out, ServerMessage::Weather);
    Write(&out, payload, 3);
}

void PlayerServer::SendTime(float hours, float speed) {
    player->dirty &= ~PlayerDirty::Time;
    float payload[2] = {hours, speed};
    WriteType(&out, ServerMessage::Time);
    Write(&out, payload, 8);
}

void PlayerServer::SendWind(float x, float y, float z, float strength) {
    player->dirty &= ~PlayerDirty::Wind;
    WriteType(&out, ServerMessage::Wind);
    const Vector3 direction{x, y, z};
    Write(&out, &direction, 12);
    Write(&out, &strength, 4);
}

void PlayerServer::SendCountdowns(const WaterTweens* source) {
    WriteType(&out, ServerMessage::Countdowns);
    source->Save(&out);
    player->dirty &= ~PlayerDirty::Countdowns;
}

void PlayerServer::SendCameraPlacement(const Vector3& at, std::uint32_t startValue, std::uint32_t sessionKind) {
    WriteType(&out, ServerMessage::CameraPlacement);
    Write(&out, &at, 12);
    WriteDword(&out, startValue);
    WriteDword(&out, sessionKind);
    lastPosition = at;
}

void PlayerServer::SendFreeCamera(const Vector3& at, const Vector3& other, std::uint8_t flag) {
    WriteType(&out, ServerMessage::FreeCamera);
    Write(&out, &at, 12);
    Write(&out, &other, 12);
    Write(&out, &flag, 1);
    lastPosition = at;
}

// --- Simple messages -----------------------------------------------------------------------------

void PlayerServer::ItemsTaken() { WriteType(&out, ServerMessage::QuestItemsChanged); }

void PlayerServer::ItemTaken(EngineObject* object) {
    WriteType(&out, ServerMessage::ItemRemoved);
    out.WriteId(object);
}

void PlayerServer::SendWeaponRemoved(std::uint32_t unitId, std::uint32_t index) {
    WriteType(&out, ServerMessage::WeaponRemoved);
    WriteDword(&out, unitId);
    WriteDword(&out, index);
}

// The item's charge goes as its float's bits (the client's 0x0C / 0x14 handlers store it there).
void PlayerServer::ItemUsed(Item* item) {
    WriteType(&out, ServerMessage::ItemValue);
    out.WriteId(item);
    WriteDword(&out, std::bit_cast<std::uint32_t>(item->charge));
}

void PlayerServer::SendJingle(std::string_view text) {
    WriteType(&out, ServerMessage::Jingle);
    out.WriteString(text);
}

void PlayerServer::SendExperienceAll(std::uint32_t value) {
    WriteType(&out, ServerMessage::ExperienceAll);
    WriteDword(&out, value);
}

void PlayerServer::SendExperience(std::uint32_t unitId, std::uint32_t value) {
    WriteType(&out, ServerMessage::Experience);
    WriteDword(&out, unitId);
    WriteDword(&out, value);
}

void PlayerServer::SendUnitDied(const EngineObject* object) {
    if (object == nullptr) return;
    WriteType(&out, ServerMessage::UnitDied);
    out.WriteId(object);
}

void PlayerServer::SendUnitSpeech(const EngineObject* object, AckKind kind) {
    WriteType(&out, ServerMessage::UnitSpeech);
    out.WriteId(object);
    out.WriteValue(static_cast<std::uint32_t>(kind));
}

void PlayerServer::SendWorldValue(std::uint32_t value) {
    std::uint8_t payload[1 + sizeof(value)] = {};
    lib::Store(payload + 1, value);
    WriteType(&out, ServerMessage::WorldValue);
    Write(&out, payload, sizeof(payload));
}

void PlayerServer::SendStringEvent(std::string_view text) {
    WriteType(&out, ServerMessage::StringEvent);
    out.WriteString(text);
}

void PlayerServer::WriteTo(ByteStream* target) { target->CopyFrom(&out); }

void PlayerServer::SetDisconnect() { state = State::Disconnected; }

void PlayerServer::SetPartyDeployed(std::uint8_t value) { partyDeployed = value; }

// --- Party records -------------------------------------------------------------------------------

// --- The server's Create ---------------------------------------------------------------

void PlayerServer::ResetState() {
    out.Resize(0);
    ReleaseAll(sentObjects);
    ReleaseAll(sentTimed);
    state = State::NotLoggedIn;
    lastPosition = {};
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

// --- The server's lifetime and virtuals ------------------------------------------------------

PlayerServer::PlayerServer() {
    ResetState();
}

PlayerServer::~PlayerServer() {
    ResetState();
}

const char* PlayerServer::ClassName() { return "CPlayerServer"; }
ClassId PlayerServer::GetClassId() { return ClassId::PlayerServer; }

void PlayerServer::ForgetSentObjects() {
    ReleaseAll(sentObjects);
    ReleaseAll(sentTimed);
    possessedGroup.Clear();
    partyUnits.Clear();
}

void PlayerServer::TickDone() { possessedGroup.Clear(); }

void PlayerServer::SendPathMessage(EngineObject* unit, PathOrder* path, const Vector3& point, int kind, int flag,
                                    float limit) {
    if (!unit) return;
    WriteType(&out, ServerMessage::PathMarker);
    out.WriteValue(kind);
    out.WriteValue(flag);
    out.WriteValue(XY(point));
    out.WriteId(unit);
    out.WriteValue(limit);
    if (kind == kPathKindNoPath) return;
    DeltaField field;
    DeltaWriter writer(field, DeltaWrite::Full);
    path->WriteState(writer);
    writer.End();
    out.EndBits();
    MemoryStream& written = field.out[0];
    written.Seek(0);
    std::uint8_t* data = written.data.data();
    out.Write(data, written.Size());
}
