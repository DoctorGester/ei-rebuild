// Checks for the figure manager's model lookup (src/game/figure_models.cpp). Ours only (it loads
// resources): a model that was cached is returned unchanged, and afterwards the cache holds
// exactly what was returned (nothing when null was returned and the name was new).
#include "verify.h"

#include "../../src/game/figure_manager.h"

#include <cstdint>

FigureModel* __fastcall FindFigureModel(CFigureManager* manager, void*, const stlp::string* name);

namespace {
using ModelMap = stlp::hash_map<stlp::string, FigureModel*, stlp::StringHash>;
__declspec(naked) void __fastcall MapFindAt(ModelMap*, void*, ModelMap::Iterator*, const stlp::string*) {
    __asm mov eax, 0x004FB0F0
    __asm jmp eax
}
FigureModel* Cached(CFigureManager* manager, const stlp::string* name) {
    ModelMap::Iterator it{};
    MapFindAt(&manager->models_, nullptr, &it, name);
    return it.node ? it.node->value : nullptr;
}
void* s_unused;
} // namespace

// 005AA5F0.
static FigureModel* __fastcall FindModelVerify(CFigureManager* manager, void*, const stlp::string* name) {
    FigureModel* before = Cached(manager, name);
    FigureModel* result = FindFigureModel(manager, nullptr, name);
    bool ok = Cached(manager, name) == result && (!before || before == result);
    ei::verify::Check("FindFigureModel", true, ok, "%p (cached %p)", result, before);
    return result;
}
EI_VERIFY(0x005AA5F0, FindModelVerify, 0, s_unused);

CMappedResource* __fastcall FindModelAnimation(FigureModel* model, void*, const stlp::string* name);

namespace {
using AnimationMap = FigureModel::AnimationMap;
__declspec(naked) void __fastcall AnimFindAt(AnimationMap*, void*, AnimationMap::Iterator*, const stlp::string*) {
    __asm mov eax, 0x005AD520
    __asm jmp eax
}
CMappedResource* CachedAnimation(FigureModel* model, const stlp::string* name) {
    AnimationMap::Iterator it{};
    AnimFindAt(&model->animationCache, nullptr, &it, name);
    return it.node ? it.node->value : nullptr;
}
} // namespace

// 005A9EF0: loads resources, so ours only; as above for the model's animation map.
static CMappedResource* __fastcall FindAnimationVerify(FigureModel* model, void*, const stlp::string* name) {
    CMappedResource* before = CachedAnimation(model, name);
    CMappedResource* result = FindModelAnimation(model, nullptr, name);
    bool ok = CachedAnimation(model, name) == result && (!before || before == result);
    ei::verify::Check("FindModelAnimation", true, ok, "%p (cached %p)", result, before);
    return result;
}
EI_VERIFY(0x005A9EF0, FindAnimationVerify, 0, s_unused);

FigureModel* __fastcall ConstructFigureModel(FigureModel* model);

// 005A9A50: constructs in place, so ours only. Afterwards the map is empty with at least 100
// null buckets and the three archives exist.
static FigureModel* __fastcall ConstructModelVerify(FigureModel* model) {
    FigureModel* result = ConstructFigureModel(model);
    AnimationMap& table = model->animationCache;
    bool ok = result == model && table.Size() == 0 && table.BucketCount() >= 100;
    for (std::size_t b = 0; ok && b < table.BucketCount(); ++b) ok = table.Bucket(b) == nullptr;
    ok = ok && model->mesh && model->bones && model->animations;
    ei::verify::Check("ConstructFigureModel", true, ok, "%d buckets", static_cast<int>(table.BucketCount()));
    return result;
}
EI_VERIFY(0x005A9A50, ConstructModelVerify, 0, s_unused);
