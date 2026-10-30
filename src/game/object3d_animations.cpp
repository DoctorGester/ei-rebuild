// Random animations of a 3D object's figure (Object3D::RandomAnimation and RandomAnimationOfKind; units'
// FindAnimation picks idle, attack and death animations with them). See map_object_figures.h.
#include "map_object.h"
#include "map_object_figures.h"
#include "random.h"

#include "lib/containers.h"
#include "lib/crt.h"

#include <vector>

namespace {
constexpr std::uint32_t kKeyKindBits = 0xFFFF8000;     // must match exactly

// A draw below `n` (> 0) from `random`'s generator.
int RandomBelow(AnimationRandom random, int n) {
    return random == AnimationRandom::Client ? ClientRandBelow(n) : libcrt::RandBelow(n);
}

} // namespace

FigureAnimation* Object3D::RandomAnimation(std::uint32_t key, AnimationRandom random) {
    AnimationDatabase* database = Animations();
    if (!database) return nullptr;
    std::vector<FigureAnimation*> candidates{};
    std::uint32_t wanted = key & kAnimationVariantBits;
    for (int i = 0; i < static_cast<int>(animations->count); ++i) {
        FigureAnimation* entry = &animations->animations[i];
        std::uint32_t entryKey = entry->key;
        bool variant =
            wanted == 0 ? (entryKey & kAnimationVariantBits) == 0 : (~entryKey & key & kAnimationVariantBits) == 0;
        if (!variant || ((entryKey ^ key) & kKeyKindBits) != 0) continue;
        if (AcceptsAnimation(entry)) candidates.push_back(entry);
    }
    FigureAnimation** list = candidates.data();
    int count = lib::Count(candidates);
    if (count < 2) {
        FigureAnimation* only = count == 1 ? list[0] : nullptr;
        return only;
    }
    std::uint32_t percent = static_cast<std::uint32_t>(RandomBelow(random, 100));
    FigureAnimation* picked = list[RandomBelow(random, count)];
    for (int i = 0; i < count; ++i) {
        if (list[i]->chance < percent) {
            for (int k = i; k < count - 1; ++k) list[k] = list[k + 1];
            --count;
            --i;
        }
    }
    if (count > 0) picked = list[RandomBelow(random, count)];
    return picked;
}

FigureAnimation* Object3D::RandomAnimationOfKind(std::uint32_t key, AnimationRandom random) {
    AnimationDatabase* database = Animations();
    if (!database) return nullptr;
    std::vector<FigureAnimation*> candidates{};
    for (int i = 0; i < static_cast<int>(animations->count); ++i) {
        FigureAnimation* entry = &animations->animations[i];
        if (((entry->key ^ key) & kKeyKindBits) != 0) continue;
        if (!AcceptsAnimation(entry)) continue;
        int index = lib::Count(candidates);
        candidates.resize(index + 1);
        candidates[index] = entry;
    }
    if (candidates.empty()) {
        return nullptr;
    }
    FigureAnimation* picked = candidates[RandomBelow(random, lib::Count(candidates))];
    return picked;
}
