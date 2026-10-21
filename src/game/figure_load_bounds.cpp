// 005AC010 (thiscall on the figure manager, RET 0x1C): a figure part's bounding volume. The part
// data comes from the cached model's mesh archive (GetFile with `name`, freed by CloseFile) or from
// the file "<directory><name>.fig" (the manager's GetFile / CloseFile); missing data returns 0. A
// "FIG" header (the low three bytes of the first dword) sends the data past the 0x28-byte header
// to 005AAE60 with the full magic, any other to 005AB9A0 past 8 bytes, both with the morph
// weights. Returns 1. See figure_mesh.h and docs/notes/models.md.
#include "figure_manager.h"   // CFigureManager, FigureModel

#include "ei/hook.h"

#include <bit>
#include <cstdint>
#include <cstring>

namespace {


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
        std::memcpy(path + length + nameLength, ".fig", 5);
        data = static_cast<std::uint8_t*>(GetFile(path, nullptr));
    }
    if (!data) return 0;
    std::uint32_t magic;
    std::memcpy(&magic, data, 4);
    // The bounds morphs (figure_mesh.cpp; 005AAE60 / 005AB9A0, thiscall on the manager, not read).
    if ((magic & 0xFFFFFF) == kFigMagic)
        MorphFigureBounds(out, reinterpret_cast<const float*>(data + 0x28), std::bit_cast<float>(s), std::bit_cast<float>(t),
                          std::bit_cast<float>(u), magic);
    else
        MorphFigureBounds6(out, reinterpret_cast<const float*>(data + 8), std::bit_cast<float>(s), std::bit_cast<float>(t),
                           std::bit_cast<float>(u));
    if (model) model->mesh->CloseFile(data);
    else CloseFile(data);
    return 1;
}

