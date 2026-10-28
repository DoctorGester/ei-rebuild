// A figure part's bounding volume. The part data comes from the cached model's mesh archive (GetFile with
// `name`, freed by CloseFile) or from the file "<directory><name>.fig" (the manager's GetFile / CloseFile);
// missing data returns 0. A "FIG" header (the low three bytes of the first dword) sends the data past the
// 0x28-byte header to the FIG bounds morph with the full magic, any other past 8 bytes to the older-format
// one, both with the morph weights. Returns 1. See figure_mesh.h and docs/notes/models.md.
#include "figure_manager.h"   // CFigureManager, FigureModel

#include <bit>
#include <cstdint>
#include <cstring>

namespace {


constexpr std::uint32_t kFigMagic = 0x474946;   // "FIG"

} // namespace

int CFigureManager::LoadFigureBounds(ClipBounds* out, FigureModel* model, std::string_view directory, std::string_view name, std::uint32_t s, std::uint32_t t, std::uint32_t u) {
    std::uint8_t* data;
    if (model) {
        data = static_cast<std::uint8_t*>(model->mesh->GetFile(name, nullptr));
    } else {
        // The inlined strcpy / strcat into a 0x200-byte buffer (no bounds check, like the original).
        char path[0x200];
        std::size_t length = directory.size();
        std::memcpy(path, directory.data(), length);
        std::size_t nameLength = name.size();
        std::memcpy(path + length, name.data(), nameLength);
        std::memcpy(path + length + nameLength, ".fig", 5);
        data = static_cast<std::uint8_t*>(GetFile(std::string_view(path, length + nameLength + 4), nullptr));
    }
    if (!data) return 0;
    std::uint32_t magic;
    std::memcpy(&magic, data, 4);
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
