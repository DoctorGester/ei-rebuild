// Checks for the figure manager's model lookup (src/game/figure_models.cpp). Ours only (it loads
// resources): a model that was cached is returned unchanged, and afterwards the cache holds
// exactly what was returned (nothing when null was returned and the name was new).
#include "verify.h"

#include <cstdint>

void* __fastcall FindFigureModel(char* manager, void*, const void* name);

namespace {
struct Iterator {
    void* node;
    void* map;
};
__declspec(naked) void __fastcall MapFindAt(void*, void*, Iterator*, const void*) {
    __asm mov eax, 0x004FB0F0
    __asm jmp eax
}
void* Cached(char* manager, const void* name) {
    Iterator it{};
    MapFindAt(manager + 0xBC, nullptr, &it, name);
    return it.node ? *reinterpret_cast<void**>(static_cast<char*>(it.node) + 0x10) : nullptr;
}
void* s_unused;
} // namespace

// 005AA5F0.
static void* __fastcall FindModelVerify(char* manager, void*, const void* name) {
    void* before = Cached(manager, name);
    void* result = FindFigureModel(manager, nullptr, name);
    bool ok = Cached(manager, name) == result && (!before || before == result);
    ei::verify::Check("FindFigureModel", true, ok, "%p (cached %p)", result, before);
    return result;
}
EI_VERIFY(0x005AA5F0, FindModelVerify, 0, s_unused);
