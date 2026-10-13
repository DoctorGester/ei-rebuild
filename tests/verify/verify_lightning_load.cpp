// Check for lightning_load.cpp (A-E1). Main thread; vanilla launch only (Rebirth patches 00468A00).
// Side by side: the original fills the noise table, which is copied and put back as it was, then
// ours; the results and the two tables (bytes read from the file, no arithmetic) are compared.
// Reached once, from the first CLightningManager::Create (the flag 007A9694), by reading: when the
// first world's managers are created. Not run.
#include "verify.h"

#include "../../src/game/lightning.h"

#include <cstring>

namespace {
constexpr int kTableFloats = kLightningNoiseRows * kLightningNoiseRowLength;
float s_before[kTableFloats];
float s_expected[kTableFloats];

// 00468A00: PUSH -1 (2), PUSH imm32 (5).
int(__cdecl* s_load)();
int __cdecl LoadNoiseVerify() {
    float* table = LightningNoiseTable();
    std::memcpy(s_before, table, sizeof s_before);
    int expected = s_load();
    std::memcpy(s_expected, table, sizeof s_expected);
    std::memcpy(table, s_before, sizeof s_before);
    int actual = LoadLightningNoise();
    ei::verify::Check("LoadLightningNoise", expected, actual, "");
    ei::verify::Check("LoadLightningNoise table", true, std::memcmp(s_expected, table, sizeof s_expected) == 0, "");
    return actual;
}
} // namespace

EI_VERIFY(0x00468A00, LoadNoiseVerify, 7, s_load);
