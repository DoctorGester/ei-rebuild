// UnitParams::ProtectionBlock: the protection figures the unit panel and the script addon show, from the
// packed parameters and packed vitals. See unit.h.
#include "unit.h"

#include "logic_settings.h"
#include "ui.h"
#include "options.h"

#include <cstdint>

namespace {
constexpr int kDamageTypes = 7;
constexpr int kSlotKindWorn = 1;   // a slot counts when its kind is above this
// The protection factor by difficulty (GameDifficulty, ui.h): the setting "Absorption".
inline const float* DifficultyFactors() { return g_logicSettings.absorption; }
} // namespace

void UnitParams::ProtectionBlock(const UnitVitalsPacked* vitals, float* out, std::uint8_t keyed) {
    for (int k = 0; k < kDamageTypes; ++k) out[k] = 0.0f;
    float scale = 0.0f;
    if (keyed) {
        float total = 0.0f;
        for (const UnitParamsSlot& slot : slots) {
            if (slot.kind <= kSlotKindWorn) continue;
            float weight = unitdb::kSlotWeights[slot.type];
            for (int k = 0; k < kDamageTypes; ++k)
                out[k] = static_cast<std::uint16_t>(slot.protection[k]) * weight + out[k];
            total += weight;
        }
        if (total > 0.0f) scale = 1.0f / total;
    }
    float factor = keyed ? 1.0f : DifficultyFactors()[options::g_options.difficulty];
    for (int k = 0; k < kDamageTypes; ++k)
        out[k] = (scale * out[k] + static_cast<std::uint16_t>(vitals->protection[k])) * factor;

}
