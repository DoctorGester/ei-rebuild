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

void* __fastcall FindModelAnimation(char* model, void*, const void* name);

namespace {
__declspec(naked) void __fastcall AnimFindAt(void*, void*, Iterator*, const void*) {
    __asm mov eax, 0x005AD520
    __asm jmp eax
}
void* CachedAnimation(char* model, const void* name) {
    Iterator it{};
    AnimFindAt(model + 0xC, nullptr, &it, name);
    return it.node ? *reinterpret_cast<void**>(static_cast<char*>(it.node) + 0x10) : nullptr;
}
} // namespace

// 005A9EF0: loads resources, so ours only; as above for the model's animation map.
static void* __fastcall FindAnimationVerify(char* model, void*, const void* name) {
    void* before = CachedAnimation(model, name);
    void* result = FindModelAnimation(model, nullptr, name);
    bool ok = CachedAnimation(model, name) == result && (!before || before == result);
    ei::verify::Check("FindModelAnimation", true, ok, "%p (cached %p)", result, before);
    return result;
}
EI_VERIFY(0x005A9EF0, FindAnimationVerify, 0, s_unused);

void* __fastcall ConstructFigureModel(unsigned char* model);

// 005A9A50: constructs in place, so ours only. Afterwards the map is empty with at least 100
// null buckets and the three parts exist.
static void* __fastcall ConstructModelVerify(unsigned char* model) {
    void* result = ConstructFigureModel(model);
    auto** begin = *reinterpret_cast<void***>(model + 0x10);
    auto** end = *reinterpret_cast<void***>(model + 0x14);
    bool ok = result == model && *reinterpret_cast<int*>(model + 0x1C) == 0 && end - begin >= 100;
    for (void** b = begin; ok && b != end; ++b) ok = *b == nullptr;
    for (int i = 0; ok && i < 3; ++i) ok = reinterpret_cast<void**>(model)[i] != nullptr;
    ei::verify::Check("ConstructFigureModel", true, ok, "%d buckets", static_cast<int>(end - begin));
    return result;
}
EI_VERIFY(0x005A9A50, ConstructModelVerify, 0, s_unused);
