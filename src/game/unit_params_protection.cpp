// UnitParams::ProtectionBlock (005270E0): the protection figures the unit panel and the script
// addon show, from the packed parameters (CUnit + 0x330) and packed vitals (+0x17C). See unit.h.
#include "unit.h"

#include "logic_settings.h"
#include "ui.h"

#include "ei/hook.h"

#include <cstdint>

namespace {
constexpr int kDamageTypes = 7;
constexpr int kSlotKindWorn = 1;   // a slot counts when its kind is above this
// The protection factor by difficulty (GameDifficulty, ui.h): the setting "Absorption" (007C54A0).
inline const float* DifficultyFactors() { return GameLogicSettings().absorption; }
} // namespace

void UnitParams::ProtectionBlock(const UnitVitalsPacked* vitals, float* out, std::uint8_t keyed) {
    for (int k = 0; k < kDamageTypes; ++k) out[k] = 0.0f;
    double scale = 0.0;
    if (keyed) {
        double total = 0.0;   // summed on the FPU stack
        for (const UnitParamsSlot& slot : slots) {
            if (slot.kind <= kSlotKindWorn) continue;
            double weight = unitdb::kSlotWeights[slot.type];
            for (int k = 0; k < kDamageTypes; ++k)
                out[k] = static_cast<float>(static_cast<std::uint16_t>(slot.protection[k]) * weight + out[k]);
            total += weight;
        }
        if (total > 0.0) scale = 1.0 / total;
    }
    double factor = keyed ? 1.0 : DifficultyFactors()[GameDifficulty()];
    for (int k = 0; k < kDamageTypes; ++k)
        out[k] = static_cast<float>((scale * out[k] + static_cast<std::uint16_t>(vitals->protection[k])) * factor);
}
EI_REPLACE_METHOD(0x005270E0, UnitParams::ProtectionBlock);
