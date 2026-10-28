// Random animations of a 3D object's figure (CObject3D::RandomAnimation and RandomAnimationOfKind; units'
// FindAnimation picks idle, attack and death animations with them). See map_object_figures.h.
#include "map_object_figures.h"

#include "map_object.h"

#include "lib/crt.h"

#include <vector>

namespace {
constexpr std::uint32_t kKeyLowBits = 0x7FFF;          // variant bits a key may ask for
constexpr std::uint32_t kKeyKindBits = 0xFFFF8000;     // must match exactly
constexpr int kRandMax = 0x7FFF;

bool Accepts(CObject3D* object, const FigureAnimation* animation) {
    return object->AcceptsAnimation(animation);
}

// rand() scaled to 0..count-1 as the game does.
int RandomIndex(int count) { return (libcrt::Rand() * count / kRandMax) % count; }
} // namespace

FigureAnimation* CObject3D::RandomAnimation(std::uint32_t key) {
    AnimationDatabase* database = Animations();
    if (!database) return nullptr;
    std::vector<FigureAnimation*> candidates{};
    std::uint32_t wanted = key & kKeyLowBits;
    for (int i = 0; i < static_cast<int>(animations->count); ++i) {
        FigureAnimation* entry = &animations->animations[i];
        std::uint32_t entryKey = entry->key;
        bool variant = wanted == 0 ? (entryKey & kKeyLowBits) == 0 : (~entryKey & key & kKeyLowBits) == 0;
        if (!variant || ((entryKey ^ key) & kKeyKindBits) != 0) continue;
        if (Accepts(this, entry)) candidates.push_back(entry);
    }
    FigureAnimation** list = candidates.data();
    int count = std::ssize(candidates);
    if (count < 2) {
        FigureAnimation* only = count == 1 ? list[0] : nullptr;
        return only;
    }
    std::uint32_t percent = static_cast<std::uint32_t>((libcrt::Rand() * 100 / kRandMax) % 100);
    FigureAnimation* picked = list[RandomIndex(count)];
    for (int i = 0; i < count; ++i) {
        if (list[i]->chance < percent) {
            for (int k = i; k < count - 1; ++k) list[k] = list[k + 1];
            --count;
            --i;
        }
    }
    if (count > 0) picked = list[RandomIndex(count)];
    return picked;
}

FigureAnimation* CObject3D::RandomAnimationOfKind(std::uint32_t key) {
    AnimationDatabase* database = Animations();
    if (!database) return nullptr;
    std::vector<FigureAnimation*> candidates{};
    for (int i = 0; i < static_cast<int>(animations->count); ++i) {
        FigureAnimation* entry = &animations->animations[i];
        if (((entry->key ^ key) & kKeyKindBits) != 0) continue;
        if (!Accepts(this, entry)) continue;
        int index = std::ssize(candidates);
        candidates.resize(index + 1);
        candidates[index] = entry;
    }
    if (candidates.empty()) {
        return nullptr;
    }
    FigureAnimation* picked = candidates[RandomIndex(std::ssize(candidates))];
    return picked;
}
