// Checks for the plain figure part draw (figure_part_draw_plain.cpp).
#include "verify.h"

#include <windows.h>

#include <cstdint>

int __fastcall DrawFigurePartPlain(char* node, void*, char* device, int, int, int, int alpha, RECT* rect);

namespace {
__declspec(naked) int __fastcall ClipTestAt(void*, void*, const void*) {
    __asm mov eax, 0x004F16A0
    __asm jmp eax
}
std::uint32_t& Flags(char* mesh) { return *reinterpret_cast<std::uint32_t*>(mesh + 8); }
char* MeshOf(char* node) { return *reinterpret_cast<char**>(node + 0x1C); }
void* s_unused;
} // namespace

// 005A7640 draws: ours only. The clip test (pure) runs first. The result is 1; a hidden mesh keeps
// its flags, one outside the view is not marked drawn (0x40), the lit flag (0x10) is never
// changed, and with a rectangle the node's (+0x20) and the caller's are equal afterwards.
static int __fastcall DrawFigurePartPlainVerify(char* node, void*, char* device, int a, int b, int c, int alpha,
                                                RECT* rect) {
    char* mesh = MeshOf(node);
    std::uint32_t before = Flags(mesh);
    bool hidden = (before & 0x80) != 0;
    int visibility = hidden ? 2 : ClipTestAt(*reinterpret_cast<void**>(device + 0xAC), nullptr, mesh + 0x188);
    int result = DrawFigurePartPlain(node, nullptr, device, a, b, c, alpha, rect);
    std::uint32_t flags = Flags(MeshOf(node));
    bool ok = result == 1 && (flags & 0x10) == (before & 0x10);
    if (hidden) {
        ok = ok && flags == before;
    } else if (visibility == 2) {
        ok = ok && !(flags & 0x40);
    } else if (rect) {
        ok = ok && EqualRect(reinterpret_cast<RECT*>(node + 0x20), rect);
    }
    ei::verify::Check("DrawFigurePartPlain", true, ok, "visibility %d flags %x rect %d", visibility, flags,
                      rect != nullptr);
    return result;
}
EI_VERIFY(0x005A7640, DrawFigurePartPlainVerify, 0, s_unused);
