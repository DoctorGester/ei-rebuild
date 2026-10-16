// The unit record applied to a new unit (00477B00). See unit_record.h.
#include "unit.h"
#include "unit_record.h"

#include "ei/hook.h"
#include "originals_library.h"
#include "originals_units.h"
#include "originals_gameplay.h"
#include "ei/x87.h"

#include <cstring>

#pragma warning(disable : 4740) // naked forwarders

namespace unitrecord {
namespace {

// The unit's methods used here, through their original addresses (006AC970 is patched by Rebirth;
// the others may be rewritten elsewhere).
struct Unit {
    int AddQuickItem(void* item);           // 00531180
    void AddCarried(void* item);            // 00531220
    int AddSpell(void* spell);              // 00531820
    void* EquipArmour(void* item);          // 00530730
    int AddWeapon(void* item);              // 00531070
};
int Unit::AddQuickItem(void* a0) { return reinterpret_cast<CUnit*>(this)->AddQuickItem(static_cast<CEngineObject*>(a0)); }
void Unit::AddCarried(void* a0) { reinterpret_cast<CUnit*>(this)->AddCarried(static_cast<CEngineObject*>(a0)); }
int Unit::AddSpell(void* a0) { return reinterpret_cast<CUnit*>(this)->AddSpell(static_cast<CEngineObject*>(a0)); }
void* Unit::EquipArmour(void* a0) { return reinterpret_cast<CUnit*>(this)->EquipArmour(static_cast<CEngineObject*>(a0)); }
int Unit::AddWeapon(void* a0) { return reinterpret_cast<CUnit*>(this)->AddWeapon(static_cast<CEngineObject*>(a0)); }

// The unit's body slots (controller +0x1E0, unit.h UnitStats).
struct Slots {
    void SetSlotMaxima(std::uint32_t maxHitPointsBits);   // 00525A00 (a float, pushed as bits)
    void* RefillSlots();                                   // 00525AF0
};
__declspec(naked) void Slots::SetSlotMaxima(std::uint32_t) EI_ORIGINAL(0x00525A00)
void* Slots::RefillSlots() { return UnitStatsRefillSlotsOriginal(this); }

// The item and spell factory (global pointer at 007AFFB4).
struct Factory {
    void* CreateItem(std::uint32_t id) {                        // 006AC970
        return ItemManagerCreateItemOriginal(this, nullptr, reinterpret_cast<const char*>(id));
    }
    void* CreateSpell(std::uint32_t id, std::uint32_t owner);   // 006AD1E0
};
void* Factory::CreateSpell(std::uint32_t a0, std::uint32_t a1) { return FactoryCreateSpellOriginal((void*)this, nullptr, (const char*)a0, (void*)a1); }
Factory* TheFactory() { return *reinterpret_cast<Factory**>(0x007AFFB4); }
std::uint32_t SpellOwner() { return *reinterpret_cast<const std::uint32_t*>(0x007AFFAC); }


// *a = *b = float(value * *factor), the product rounded by the FPU's precision.
void StoreScaledTwice(void* a, void* b, int value, const void* factor) {
    const float scaled = static_cast<float>(static_cast<double>(value) * *static_cast<const float*>(factor));
    std::memcpy(a, &scaled, sizeof scaled);
    std::memcpy(b, &scaled, sizeof scaled);
}

std::uint32_t Dword(const char* p, std::size_t offset) {
    std::uint32_t v;
    std::memcpy(&v, p + offset, 4);
    return v;
}
void SetDword(char* p, std::size_t offset, std::uint32_t v) { std::memcpy(p + offset, &v, 4); }
void SetFloat(char* p, std::size_t offset, float v) { std::memcpy(p + offset, &v, 4); }

// A dword array of the record ({data, size} at `offset`).
const std::uint32_t* Ids(const char* r, std::size_t offset) { return *reinterpret_cast<const std::uint32_t* const*>(r + offset); }
int Count(const char* r, std::size_t offset) { return static_cast<int>(Dword(r, offset + 4)); }

// Equal or unordered: the original's FCOMP; TEST AH,0x40.
bool EqualOrNaN(float a, float b) { return !(a < b) && !(a > b); }

// The unit's fields (CUnit, ai_map.h owns the layout).
constexpr std::size_t kUnitStats = 0x170;
constexpr std::size_t kUnitController = 0x240;

void AddItems(const char* r, Unit* unit, Factory* factory) {
    for (int i = 0; i < Count(r, field::kQuickItems); ++i) unit->AddQuickItem(factory->CreateItem(Ids(r, field::kQuickItems)[i]));
    for (int i = 0; i < Count(r, field::kCarried); ++i) unit->AddCarried(factory->CreateItem(Ids(r, field::kCarried)[i]));
}

// The controller fields taken from the record (record offset, controller offset).
constexpr std::uint16_t kControllerCopies[][2] = {
    {0x10, 0x208}, {0x14, 0x210}, {0x18, 0x214}, {0x1C, 0x218}, {0x20, 0x21C}, {0x24, 0x220},
    {0x28, 0x224}, {0x2C, 0x228}, {0x30, 0x22C}, {0x34, 0x244},
};
constexpr std::uint16_t kControllerCopies2[][2] = {
    {0x3C, 0x250}, {0x40, 0x254}, {0x44, 0x25C}, {0x48, 0x260}, {0x4C, 0x264}, {0x50, 0x268},
};

} // namespace

void UnitRecord::ApplyTo(CUnit* target) {
    const char* r = reinterpret_cast<const char*>(this);
    Factory* factory = TheFactory();
    Unit* unit = reinterpret_cast<Unit*>(target);
    float k0;
    std::memcpy(&k0, reinterpret_cast<const void*>(0x0073B858), 4);   // 0.0
    float f10, f14;
    std::memcpy(&f10, r + field::kNoStats0, 4);
    std::memcpy(&f14, r + field::kNoStats2, 4);
    bool noStats = EqualOrNaN(f10, k0) && Dword(r, field::kNoStats1) == 0 && EqualOrNaN(f14, k0);
    if (noStats || Dword(r, field::kApplyStats) == 0) {
        AddItems(r, unit, factory);
        return;
    }

    target->ReleaseAllItems();
    char* u = reinterpret_cast<char*>(target);
    char* stats = *reinterpret_cast<char**>(u + kUnitStats);
    char* controller = *reinterpret_cast<char**>(u + kUnitController);
    SetFloat(stats, 0x28, static_cast<float>(static_cast<int>(Dword(r, 0x0))));
    SetFloat(stats, 0x2C, static_cast<float>(static_cast<int>(Dword(r, 0x4))));
    SetFloat(stats, 0x34, static_cast<float>(static_cast<int>(Dword(r, 0x8))));
    SetFloat(stats, 0x38, static_cast<float>(static_cast<int>(Dword(r, 0xC))));
    Slots* slots = reinterpret_cast<Slots*>(controller + 0x1E0);
    slots->SetSlotMaxima(Dword(stats, 0x2C));
    slots->RefillSlots();
    SetDword(controller, 0x8DC, Dword(stats, 0x2C));
    for (const auto& c : kControllerCopies) SetDword(controller, c[1], Dword(r, c[0]));
    std::uint16_t b38 = static_cast<std::uint8_t>(r[0x38]);
    std::uint16_t b39 = static_cast<std::uint8_t>(r[0x39]);
    std::memcpy(controller + 0x248, &b38, 2);
    std::memcpy(controller + 0x24C, &b39, 2);
    std::memcpy(controller + 0x24E, &b39, 2);
    for (const auto& c : kControllerCopies2) SetDword(controller, c[1], Dword(r, c[0]));
    GameMemmove(controller + 0x270, r + field::kBytes54, 7);

    // Seven model values scaled by the record's byte, each stored twice.
    for (std::size_t k = 0; k < 7; ++k) {
        char* model = target->GetPrototypeModel();
        StoreScaledTwice(stats + 0x40 + k * 8, stats + 0x44 + k * 8, static_cast<std::uint8_t>(r[field::kModelScale]),
                         model + 0x6C + k * 4);
    }
    for (std::size_t i = 0; i < 6; ++i) {
        std::uint32_t v = x87::LoadStoreBits(r + field::kPairsA + i * 4);
        SetDword(controller, 0x290 + i * 8, v);
        SetDword(controller, 0x294 + i * 8, v);
    }
    for (std::size_t i = 0; i < 6; ++i) {
        std::uint32_t v = x87::LoadStoreBits(r + field::kPairsB + i * 4);
        SetDword(controller, 0x2C0 + i * 8, v);
        SetDword(controller, 0x2C4 + i * 8, v);
    }
    GameMemmove(controller + 0x8A8, r + field::kBytesA4, 3);

    for (int i = 0; i < Count(r, field::kSpells); ++i) unit->AddSpell(factory->CreateSpell(Ids(r, field::kSpells)[i], SpellOwner()));
    AddItems(r, unit, factory);
    for (int i = 0; i < Count(r, field::kArmour); ++i) unit->EquipArmour(factory->CreateItem(Ids(r, field::kArmour)[i]));
    for (int i = 0; i < Count(r, field::kWeapons); ++i) unit->AddWeapon(factory->CreateItem(Ids(r, field::kWeapons)[i]));
}
EI_REPLACE_METHOD(0x00477B00, UnitRecord::ApplyTo);

} // namespace unitrecord
