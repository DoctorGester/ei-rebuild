// Sorting predicates used by the world code and the game interface glue functions. See world.h.
#include "world.h"

#include "base_manager.h"
#include "core_maths.h"

#include "ei/hook.h"
#include "ei/vcall.h"

#include <new>

namespace {

constexpr int kSlotUnitIsDead = 55;   // units: +0xDC, non-zero when dead

template <typename T>
__forceinline T Field(const void* object, int offset) {
    return *reinterpret_cast<const T*>(static_cast<const std::uint8_t*>(object) + offset);
}

// Named float table (player variables, +0xC4 of a player).
struct NamedFloatTable {
    float Get(const char* name);   // 00460AF0
};
__declspec(naked) float NamedFloatTable::Get(const char*) EI_ORIGINAL(0x00460AF0)

NamedFloatTable* Variables(const std::uint8_t* player) {
    return reinterpret_cast<NamedFloatTable*>(const_cast<std::uint8_t*>(player) + 0xC4);
}

// The local player record through the client's local view (no null checks, as the original).
std::uint8_t* LocalPlayer() { return Field<std::uint8_t*>(WorldClient()->LocalView(), 0x1C); }

} // namespace

// ---- Sorting predicates --------------------------------------------------------------------------

// /fp:fast would regroup the depth sums as (z + y) + (x + offset); the original adds left to right.
#pragma float_control(precise, on, push)
bool DepthLess::Less(const int* a, const int* b) {
    auto* registry = static_cast<CBaseManager*>(manager);
    CEngineObject* first = registry->FindObject(static_cast<unsigned>(a[1]));
    CEngineObject* second = registry->FindObject(static_cast<unsigned>(b[1]));
    if (!first || !second) return false;
    const auto* view = reinterpret_cast<const std::uint8_t*>(plane);
    float da = StoreFloat(Field<float>(first, 0x24) * Field<float>(view, 0x28) + Field<float>(first, 0x20) * Field<float>(view, 0x24) +
                          Field<float>(first, 0x1C) * Field<float>(view, 0x20) + Field<float>(view, 0x2C));
    float db = StoreFloat(Field<float>(second, 0x24) * Field<float>(view, 0x28) + Field<float>(second, 0x20) * Field<float>(view, 0x24) +
                          Field<float>(second, 0x1C) * Field<float>(view, 0x20) + Field<float>(view, 0x2C));
    // Living objects come before dead ones; within each kind, by depth. The original asks each
    // object again at every step, so the calls are repeated here in the same order.
    if ((ei::VCall<int>(first, kSlotUnitIsDead) == 0 && ei::VCall<int>(second, kSlotUnitIsDead) == 0) ||
        (ei::VCall<int>(first, kSlotUnitIsDead) != 0 && ei::VCall<int>(second, kSlotUnitIsDead) != 0))
        return !(da >= db);
    return ei::VCall<int>(first, kSlotUnitIsDead) == 0;
}
#pragma float_control(pop)

bool UnitOrderLess::Less(CEngineObject* a, CEngineObject* b) {
    if (!a || !b) return false;
    auto* groupA = Field<std::uint8_t*>(a, 0x94);
    auto* groupB = Field<std::uint8_t*>(b, 0x94);
    if (!groupA || !groupB) return false;
    auto* rankA = Field<std::uint32_t*>(groupA, 0x20);
    auto* rankB = Field<std::uint32_t*>(groupB, 0x20);
    std::uint32_t ra = rankA ? *rankA : 0;
    std::uint32_t rb = rankB ? *rankB : 0;
    if (rankB && ra < rb) return true;
    if (ra != rb) return false;
    std::uint32_t sa = Field<std::uint32_t>(a, 0xF0), sb = Field<std::uint32_t>(b, 0xF0);
    if (sa < sb) return true;
    if (sa != sb) return false;
    return !(Field<float>(a, 0x30) >= Field<float>(b, 0x30));
}

BOOL EntryIdLess::Less(const std::uint8_t* a, const std::uint8_t* b) {
    return Field<std::uint32_t>(Field<std::uint8_t*>(a, 8), 0x1C) < Field<std::uint32_t>(Field<std::uint8_t*>(b, 8), 0x1C);
}

// ---- Game interface glue -------------------------------------------------------------------------

BOOL IsMultiplayerClient() {
    auto* client = *reinterpret_cast<std::uint8_t**>(0x0079B5D8);
    if (client && !(Field<std::uint32_t>(client, 4) & 0x80000000u)) return 1;
    return 0;
}

float GameTime() {
    return static_cast<float>(*reinterpret_cast<const int*>(0x007AF61C)) + *reinterpret_cast<const float*>(0x007AFCA8);
}

std::uint8_t* LocalPlayerData() { return LocalPlayer() + 0x70; }

mfc::CString* __stdcall LocalPlayerName(mfc::CString* out) {
    return new (out) mfc::CString(*reinterpret_cast<const mfc::CString*>(LocalPlayer() + 0xD8));
}

float __stdcall LocalPlayerVariable(const char* name) { return Variables(LocalPlayer())->Get(name); }

float __stdcall PlayerZeroVariable(const char* name) {
    if (IsMultiplayerClient()) return Variables(LocalPlayer())->Get(name);
    WorldPlayer* player = WorldServer()->GetPlayer(0);
    if (!player) return 0.0f;
    return Variables(reinterpret_cast<std::uint8_t*>(player))->Get(name);
}

EI_REPLACE_METHOD(0x00573A60, DepthLess::Less);
EI_REPLACE_METHOD(0x00574300, UnitOrderLess::Less);
EI_REPLACE_METHOD(0x00589580, EntryIdLess::Less);
EI_REPLACE(0x0057F440, IsMultiplayerClient);
EI_REPLACE(0x00578B40, GameTime);
EI_REPLACE(0x00578BA0, LocalPlayerData);
EI_REPLACE(0x00578A90, LocalPlayerName);
EI_REPLACE(0x00578B50, LocalPlayerVariable);
EI_REPLACE(0x00578BE0, PlayerZeroVariable);
