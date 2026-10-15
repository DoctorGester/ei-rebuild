// Checks for the plain figure part draw (figure_part_draw_plain.cpp).
#include "verify.h"

#include "../../src/game/link_figure.h"
#include "../../src/game/texture.h"

#include <windows.h>

#include <cstdint>

namespace {
__declspec(naked) int __fastcall ClipTestAt(void*, void*, const void*) {
    __asm mov eax, 0x004F16A0
    __asm jmp eax
}
void* s_unused;
} // namespace

// 005A7640 draws: ours only. The clip test (pure) runs first. The result is 1; a hidden mesh keeps
// its flags, one outside the view is not marked drawn (0x40), the lit flag (0x10) is never
// changed, and with a rectangle the node's (+0x20) and the caller's are equal afterwards.
static int __fastcall DrawFigurePartPlainVerify(FigureMeshDrawer* node, void*, CTextureDevice* device, int a, int b, int c,
                                                int alpha, RECT* rect) {
    CFigureMesh* mesh = node->mesh_;
    std::uint32_t before = mesh->flags_;
    bool hidden = (before & 0x80) != 0;
    int visibility = hidden ? 2 : ClipTestAt(device->pipeline_, nullptr, &mesh->bounds_);
    int result = node->DrawPlain(device, a, b, c, alpha, rect);
    std::uint32_t flags = node->mesh_->flags_;
    bool ok = result == 1 && (flags & 0x10) == (before & 0x10);
    if (hidden) {
        ok = ok && flags == before;
    } else if (visibility == 2) {
        ok = ok && !(flags & 0x40);
    } else if (rect) {
        ok = ok && EqualRect(&node->screen_, rect);
    }
    ei::verify::Check("DrawFigurePartPlain", true, ok, "visibility %d flags %x rect %d", visibility, flags,
                      rect != nullptr);
    return result;
}
EI_VERIFY(0x005A7640, DrawFigurePartPlainVerify, 0, s_unused);
