// The mirror of the old device's state, fed by the game code (render_state.h), and the pass state
// built from it with the old device as the fallback (render_internal.h, PassValues).
#include "render_state.h"
#include "render_internal.h"

#include "../hook/log.h"

#include <cstdio>
#include <cstring>

namespace render::state {

namespace {

constexpr int kRenderStates = 256;
constexpr int kStages = 2;
constexpr int kStageStates = 32;

std::uint32_t g_render[kRenderStates];
bool g_renderKnown[kRenderStates];
std::uint32_t g_stage[kStages][kStageStates];
bool g_stageKnown[kStages][kStageStates];

// The four state blocks the device records at its creation (004E6392..004E67BD): stage 0 colour
// and alpha setups. Handles in 007AEDDC / 007AEDD8 / 007AEDD4 / 007AEDD0; per block the values of
// COLOROP (1), COLORARG1 (2), COLORARG2 (3), ALPHAOP (4), ALPHAARG1 (5), ALPHAARG2 (6).
struct KnownBlock {
    std::uintptr_t handleAddress;
    std::uint32_t values[6];
};
constexpr KnownBlock kBlocks[] = {
    {0x007AEDDC, {4, 2, 1, 2, 2, 1}},   // texture x current; alpha texture
    {0x007AEDD8, {4, 2, 0, 4, 2, 0}},   // texture x diffuse; alpha texture x diffuse
    {0x007AEDD4, {4, 2, 0, 2, 2, 1}},   // texture x diffuse; alpha texture
    {0x007AEDD0, {4, 2, 0, 2, 0, 1}},   // texture x diffuse; alpha diffuse
};

} // namespace

void Render(std::uint32_t state, std::uint32_t value) {
    if (state >= kRenderStates) return;
    g_render[state] = value;
    g_renderKnown[state] = true;
}

void Stage(std::uint32_t stage, std::uint32_t state, std::uint32_t value) {
    if (stage >= kStages || state >= kStageStates) return;
    g_stage[stage][state] = value;
    g_stageKnown[stage][state] = true;
}

void Block(std::uint32_t handle) {
    for (const KnownBlock& b : kBlocks) {
        if (*reinterpret_cast<const std::uint32_t*>(b.handleAddress) != handle) continue;
        for (int i = 0; i < 6; ++i) Stage(0, static_cast<std::uint32_t>(i + 1), b.values[i]);
        return;
    }
    // Not one of the four: the stage states may have changed.
    std::memset(g_stageKnown, 0, sizeof(g_stageKnown));
}

void Reset() {
    std::memset(g_renderKnown, 0, sizeof(g_renderKnown));
    std::memset(g_stageKnown, 0, sizeof(g_stageKnown));
}

} // namespace render::state

namespace render::detail {

namespace {

std::uint32_t g_passes = 0;
std::uint32_t g_fallbackPasses = 0;
std::uint32_t g_renderFallbacks[256];
std::uint32_t g_stageFallbacks[2][32];
std::uint32_t g_renderMismatches[256];
std::uint32_t g_stageMismatches[2][32];

void Note(std::uint32_t& counter, const char* what, std::uint32_t stage, std::uint32_t state, std::uint32_t fed,
          std::uint32_t device, bool mismatch) {
    if (counter++ != 0) return;
    char where[32] = "";
    if (stage != ~0u) std::snprintf(where, sizeof(where), " of stage %u", stage);
    if (mismatch)
        ei::Log("render: pass state: %s %u%s fed %u but the old device has %u (a writer not converted yet)", what, state,
                where, fed, device);
    else
        ei::Log("render: pass state: %s %u%s not fed by the game code yet; read from the old device", what, state, where);
}

bool g_passFallback = false;

} // namespace

// The mirror's values, else the old device's. The verify build reads the device for every state
// and logs disagreements (writers the conversion missed); the device's value is used then.
void RenderValues(const std::uint32_t* states, std::uint32_t* values, int count) {
    using namespace render::state;
    std::uint32_t read[64], readIndex[64];
    int reads = 0;
    for (int i = 0; i < count && i < 64; ++i) {
        std::uint32_t s = states[i];
        bool known = s < 256 && g_renderKnown[s];
        values[i] = known ? g_render[s] : 0;
#if defined(EI_RENDER_VALIDATION)
        bool check = true;
#else
        bool check = !known;
#endif
        if (check) {
            read[reads] = s;
            readIndex[reads++] = static_cast<std::uint32_t>(i);
        }
    }
    if (!reads) return;
    std::uint32_t device[64];
    OldRenderStates(read, device, reads);
    for (int k = 0; k < reads; ++k) {
        std::uint32_t i = readIndex[k], s = read[k];
        bool known = s < 256 && g_renderKnown[s];
        if (!known) {
            g_passFallback = true;
            if (s < 256) Note(g_renderFallbacks[s], "render state", ~0u, s, 0, device[k], false);
        } else if (device[k] != values[i]) {
            if (s < 256) Note(g_renderMismatches[s], "render state", ~0u, s, values[i], device[k], true);
        }
        values[i] = device[k];
    }
}

void StageValues(int stage, const std::uint32_t* states, std::uint32_t* values, int count) {
    using namespace render::state;
    if (stage < 0 || stage > 1) return;
    std::uint32_t read[32], readIndex[32];
    int reads = 0;
    for (int i = 0; i < count && i < 32; ++i) {
        std::uint32_t s = states[i];
        bool known = s < 32 && g_stageKnown[stage][s];
        values[i] = known ? g_stage[stage][s] : 0;
#if defined(EI_RENDER_VALIDATION)
        bool check = true;
#else
        bool check = !known;
#endif
        if (check) {
            read[reads] = s;
            readIndex[reads++] = static_cast<std::uint32_t>(i);
        }
    }
    if (!reads) return;
    std::uint32_t device[32];
    OldStageStates(stage, read, device, reads);
    auto st = static_cast<std::uint32_t>(stage);
    for (int k = 0; k < reads; ++k) {
        std::uint32_t i = readIndex[k], s = read[k];
        bool known = s < 32 && g_stageKnown[stage][s];
        if (!known) {
            g_passFallback = true;
            if (s < 32) Note(g_stageFallbacks[stage][s], "stage state", st, s, 0, device[k], false);
        } else if (device[k] != values[i]) {
            if (s < 32) Note(g_stageMismatches[stage][s], "stage state", st, s, values[i], device[k], true);
        }
        values[i] = device[k];
    }
}

void PassDone() {
    ++g_passes;
    if (g_passFallback) ++g_fallbackPasses;
    g_passFallback = false;
    if (g_passes % 20000 == 0) {
        ei::Log("render: pass state: %u of the last 20000 passes still read something from the old device",
                g_fallbackPasses);
        g_fallbackPasses = 0;
    }
}

} // namespace render::detail
