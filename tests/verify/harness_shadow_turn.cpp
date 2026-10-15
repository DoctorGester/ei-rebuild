// The shadow's turn undone exactly in the original (harness_shadow_turn.h; ticket SV7b4a).
#include "harness_shadow_turn.h"

#include "../../src/game/originals_core.h"
#include "../../src/hook/log.h"
#include "ei/hook.h"
#include "lib/vecmath.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

namespace ei::verify::shadow_turn {

namespace {

constexpr std::uintptr_t kPreMultiply = 0x004018A0;
constexpr std::uintptr_t kTurnCall = 0x0051D479;   // DrawShadow: toward the light
constexpr std::uintptr_t kBackCall = 0x0051D4E0;   // DrawShadow: back, by the conjugate

// DrawShadow runs on the main thread only and does not nest.
Quaternion* g_turned = nullptr;
Quaternion g_kept{};

void __fastcall KeepAndTurn(Quaternion* q, void*, const Quaternion* r) {
    g_turned = q;
    g_kept = *q;
    PreMultiplyAt(q, nullptr, r);
}

void __fastcall PutBack(Quaternion* q, void*, const Quaternion* r) {
    if (q == g_turned) {
        *q = g_kept;
    } else {
        PreMultiplyAt(q, nullptr, r);   // not the turned object: the original's turn back
    }
    g_turned = nullptr;
}

bool CallsPreMultiply(std::uintptr_t call) {
    auto* p = reinterpret_cast<const unsigned char*>(call);
    std::int32_t rel = 0;
    std::memcpy(&rel, p + 1, 4);
    return p[0] == 0xE8 && call + 5 + static_cast<std::uintptr_t>(rel) == kPreMultiply;
}

void Redirect(std::uintptr_t call, const void* to) {
    auto* p = reinterpret_cast<unsigned char*>(call);
    auto rel = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(to) - (call + 5));
    ei::NoteHarnessPatch(call, 5, "the shadow's turn undone exactly, SV7b4a");
    DWORD old;
    VirtualProtect(p + 1, 4, PAGE_EXECUTE_READWRITE, &old);
    std::memcpy(p + 1, &rel, 4);
    VirtualProtect(p + 1, 4, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
}

} // namespace

void Install() {
    if (ei::Standalone()) {
        Log("harness: the shadow's turn: the rewrite keeps the orientation (no original code in the image)");
        return;
    }
    char value[8] = "";
    if (GetEnvironmentVariableA("EI_SHADOW_TURN", value, sizeof(value)) && value[0] == '0') {
        Log("harness: the shadow's turn: the original's round trip (EI_SHADOW_TURN=0)");
        return;
    }
    // Both or neither: a kept orientation without the put-back (or the reverse) would be wrong.
    if (!CallsPreMultiply(kTurnCall) || !CallsPreMultiply(kBackCall)) {
        Log("harness: %08X / %08X are not calls of %08X; the shadow's turn stays the original's", unsigned(kTurnCall),
            unsigned(kBackCall), unsigned(kPreMultiply));
        return;
    }
    Redirect(kTurnCall, reinterpret_cast<const void*>(&KeepAndTurn));
    Redirect(kBackCall, reinterpret_cast<const void*>(&PutBack));
    Log("harness: the shadow's turn undone exactly in the original (0051D479 / 0051D4E0)");
}

} // namespace ei::verify::shadow_turn
