// Checks for the figure part bounds loader (src/game/figure_load_bounds.cpp). Ours only (it reads
// resources): a load that found data marks the centre and radius valid (flag 1).
#include "verify.h"

#include "../../src/game/figure_manager.h"

#include <cstdint>

int __fastcall LoadFigureBounds(CFigureManager* manager, void*, ClipBounds* out, FigureModel* model,
                                const char* directory, const char* name, std::uint32_t s, std::uint32_t t,
                                std::uint32_t u);

namespace {
void* s_unused;
}

// 005AC010.
static int __fastcall LoadFigureBoundsVerify(CFigureManager* manager, void*, ClipBounds* out, FigureModel* model,
                                             const char* directory, const char* name, std::uint32_t s, std::uint32_t t,
                                             std::uint32_t u) {
    int result = LoadFigureBounds(manager, nullptr, out, model, directory, name, s, t, u);
    std::uint32_t flags = out->flags;
    bool ok = result == 0 || (result == 1 && (flags & 1));
    ei::verify::Check("LoadFigureBounds", true, ok, "%s -> %d (flags %x)", name, result, flags);
    return result;
}
EI_VERIFY(0x005AC010, LoadFigureBoundsVerify, 0, s_unused);
