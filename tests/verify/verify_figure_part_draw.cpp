// Checks for the figure part draw (figure_part_draw.cpp).
#include "verify.h"

#include <windows.h>

#include <cstdint>

struct FigureMeshDrawer;
struct FigureLights;
class CMaterial;
class CTextureDevice;
class CTexture;
int __fastcall DrawFigurePart(FigureMeshDrawer* node, void*, CTextureDevice* device, const FigureLights* lights,
                              CMaterial* material, int alpha, RECT* rect, CTexture* textureA, CTexture* textureB);

namespace {
__declspec(naked) int __fastcall ClipTestAt(void*, void*, const void*) {
    __asm mov eax, 0x004F16A0
    __asm jmp eax
}
std::uint32_t& Flags(char* mesh) { return *reinterpret_cast<std::uint32_t*>(mesh + 8); }
char* MeshOf(char* node) { return *reinterpret_cast<char**>(node + 0x1C); }
} // namespace

// 005A7A70 draws: ours only. The clip test (pure) runs first. The result is 1; a hidden mesh or one
// outside the view is not marked drawn (0x40); otherwise a mesh that is not flat (0x100) ends up
// lit (0x10).
static void* s_part;
static int __fastcall DrawFigurePartVerify(char* node, void*, char* device, const void* lights, char* material,
                                           int alpha, RECT* rect, char* textureA, char* textureB) {
    char* mesh = MeshOf(node);
    bool hidden = (Flags(mesh) & 0x80) != 0;
    int visibility = hidden ? 2 : ClipTestAt(*reinterpret_cast<void**>(device + 0xAC), nullptr, mesh + 0x188);
    int result = DrawFigurePart(reinterpret_cast<FigureMeshDrawer*>(node), nullptr, reinterpret_cast<CTextureDevice*>(device),
                                static_cast<const FigureLights*>(lights), reinterpret_cast<CMaterial*>(material), alpha,
                                rect, reinterpret_cast<CTexture*>(textureA), reinterpret_cast<CTexture*>(textureB));
    std::uint32_t flags = Flags(MeshOf(node));
    bool ok = result == 1;
    if (visibility == 2) {
        ok = ok && !(flags & 0x40);
    } else if (!(flags & 0x100)) {
        ok = ok && (flags & 0x10);
    }
    ei::verify::Check("DrawFigurePart", true, ok, "visibility %d flags %x rect %d", visibility, flags, rect != nullptr);
    return result;
}
EI_VERIFY(0x005A7A70, DrawFigurePartVerify, 0, s_part);
