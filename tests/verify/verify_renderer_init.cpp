// Checks for the renderer's creation (renderer_init.cpp).
#include "verify.h"

#include <cstdint>

void __fastcall RendererCreate(char* renderer, void*, void* window, char* settings);

// 00500B70 creates the Direct3D device and sets its state once at start-up: ours only. Checked: the
// device holder (+0x20) and the helper (+0x24) are set, the projection stack (pipeline +0x44 / +0x48)
// has an entry, and flag 2 of +4 is set.
static void* s_rendererCreate;
static void __fastcall RendererCreateVerify(char* renderer, void*, void* window, char* settings) {
    RendererCreate(renderer, nullptr, window, settings);
    auto ptr = [](const void* p, int offset) { return *reinterpret_cast<void* const*>(static_cast<const char*>(p) + offset); };
    auto dword = [](const void* p, int offset) { return *reinterpret_cast<const std::uint32_t*>(static_cast<const char*>(p) + offset); };
    void* pipeline = ptr(renderer, 8);
    bool ok = ptr(renderer, 0x20) && ptr(renderer, 0x24) && pipeline && dword(pipeline, 0x48) >= 1 &&
              (dword(renderer, 4) & 2) != 0;
    ei::verify::Check("RendererCreate", true, ok, "device %p helper %p", ptr(renderer, 0x20), ptr(renderer, 0x24));
}
EI_VERIFY(0x00500B70, RendererCreateVerify, 0, s_rendererCreate);
