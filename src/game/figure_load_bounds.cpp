// 005AC010 (thiscall on the figure manager, RET 0x1C): a figure part's bounding volume. The part
// data comes from the cached model's mesh part (slot 5 with `name`, freed by slot 6) or from the
// file "<directory><name>.fig" (manager slot 19, freed by slot 20); missing data returns 0. A
// "FIG" header (the low three bytes of the first dword) sends the data past the 0x28-byte header
// to 005AAE60 with the full magic, any other to 005AB9A0 past 8 bytes, both with the morph
// weights. Returns 1. See figure_mesh.h and docs/notes/models.md.
#include "ei/hook.h"
#include "ei/vcall.h"

#include <cstdint>
#include <cstring>

namespace {

// The bounds morphs (figure_mesh.cpp) through their original addresses; ECX is not used.
__declspec(naked) void __fastcall MorphFig(void*, void*, void*, const void*, std::uint32_t, std::uint32_t,
                                           std::uint32_t, std::uint32_t) EI_ORIGINAL(0x005AAE60)
__declspec(naked) void __fastcall MorphPlain(void*, void*, void*, const void*, std::uint32_t, std::uint32_t,
                                             std::uint32_t) EI_ORIGINAL(0x005AB9A0)

constexpr int kSlotOpenResource = 19;
constexpr int kSlotFreeResource = 20;
constexpr int kSlotPartRead = 5;
constexpr int kSlotPartFree = 6;
constexpr std::uint32_t kFigMagic = 0x474946;   // "FIG"

} // namespace

int __fastcall LoadFigureBounds(void* manager, void*, void* out, void** model, const char* directory, const char* name,
                                std::uint32_t s, std::uint32_t t, std::uint32_t u) {
    std::uint8_t* data;
    if (model) {
        data = ei::VCall<std::uint8_t*>(model[0], kSlotPartRead, name, 0);
    } else {
        // The inlined strcpy / strcat into a 0x200-byte buffer (no bounds check, as the original).
        char path[0x200];
        std::size_t length = std::strlen(directory);
        std::memcpy(path, directory, length);
        std::size_t nameLength = std::strlen(name);
        std::memcpy(path + length, name, nameLength);
        std::memcpy(path + length + nameLength, reinterpret_cast<const char*>(0x0078EC78), 5);   // ".fig"
        data = ei::VCall<std::uint8_t*>(manager, kSlotOpenResource, static_cast<const char*>(path), 0);
    }
    if (!data) return 0;
    std::uint32_t magic;
    std::memcpy(&magic, data, 4);
    if ((magic & 0xFFFFFF) == kFigMagic) MorphFig(manager, nullptr, out, data + 0x28, s, t, u, magic);
    else MorphPlain(manager, nullptr, out, data + 8, s, t, u);
    if (model) ei::VCall<void>(model[0], kSlotPartFree, data);
    else ei::VCall<void>(manager, kSlotFreeResource, data);
    return 1;
}

EI_REPLACE(0x005AC010, LoadFigureBounds);
