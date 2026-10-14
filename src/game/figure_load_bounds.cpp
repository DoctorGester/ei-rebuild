// 005AC010 (thiscall on the figure manager, RET 0x1C): a figure part's bounding volume. The part
// data comes from the cached model's mesh archive (GetFile with `name`, freed by CloseFile) or from
// the file "<directory><name>.fig" (the manager's GetFile / CloseFile); missing data returns 0. A
// "FIG" header (the low three bytes of the first dword) sends the data past the 0x28-byte header
// to 005AAE60 with the full magic, any other to 005AB9A0 past 8 bytes, both with the morph
// weights. Returns 1. See figure_mesh.h and docs/notes/models.md.
#include "figure_manager.h"   // CFigureManager, FigureModel

#include "ei/hook.h"

#include <cstdint>
#include <cstring>

namespace {

// The bounds morphs (figure_mesh.cpp) through their original addresses; ECX is not used.
__declspec(naked) void __fastcall MorphFig(CFigureManager*, void*, ClipBounds*, const void*, std::uint32_t, std::uint32_t,
                                           std::uint32_t, std::uint32_t) EI_ORIGINAL(0x005AAE60)
__declspec(naked) void __fastcall MorphPlain(CFigureManager*, void*, ClipBounds*, const void*, std::uint32_t,
                                             std::uint32_t, std::uint32_t) EI_ORIGINAL(0x005AB9A0)

constexpr std::uint32_t kFigMagic = 0x474946;   // "FIG"

} // namespace

int CFigureManager::LoadFigureBounds(ClipBounds* out, FigureModel* model, const char* directory, const char* name, std::uint32_t s, std::uint32_t t, std::uint32_t u) {
    std::uint8_t* data;
    if (model) {
        data = static_cast<std::uint8_t*>(model->mesh->GetFile(name, nullptr));
    } else {
        // The inlined strcpy / strcat into a 0x200-byte buffer (no bounds check, as the original).
        char path[0x200];
        std::size_t length = std::strlen(directory);
        std::memcpy(path, directory, length);
        std::size_t nameLength = std::strlen(name);
        std::memcpy(path + length, name, nameLength);
        std::memcpy(path + length + nameLength, reinterpret_cast<const char*>(0x0078EC78), 5);   // ".fig"
        data = static_cast<std::uint8_t*>(GetFile(path, nullptr));
    }
    if (!data) return 0;
    std::uint32_t magic;
    std::memcpy(&magic, data, 4);
    if ((magic & 0xFFFFFF) == kFigMagic) MorphFig(this, nullptr, out, data + 0x28, s, t, u, magic);
    else MorphPlain(this, nullptr, out, data + 8, s, t, u);
    if (model) model->mesh->CloseFile(data);
    else CloseFile(data);
    return 1;
}

EI_REPLACE_METHOD(0x005AC010, CFigureManager::LoadFigureBounds);
