// Random animations of a 3D object's figure (CObject3D::RandomAnimation 005190C0 and
// RandomAnimationOfKind 00519380; units' FindAnimation 005301B0 picks idle, attack and death
// animations with them). See map_object_figures.h.
#include "map_object_figures.h"

#include "map_object.h"

#include "lib/crt.h"

#include "ei/hook.h"

namespace {
constexpr std::uint32_t kKeyLowBits = 0x7FFF;          // variant bits a key may ask for
constexpr std::uint32_t kKeyKindBits = 0xFFFF8000;     // must match exactly
constexpr int kRandMax = 0x7FFF;

// The two growing helpers of the original's local candidate list: 0051A010 (SetAtGrow) and
// 0051A190 (SetSize), copies of AlignedPtrArray's 00519E80 / 00519D70 (registered with the same
// members below); through their addresses so their wrappers run.
__declspec(naked) void __fastcall CandidatesSetAtGrow(AlignedPtrArray*, void*, int, void*) EI_ORIGINAL(0x0051A010)
__declspec(naked) void __fastcall CandidatesSetSize(AlignedPtrArray*, void*, int, int) EI_ORIGINAL(0x0051A190)

bool Accepts(CObject3D* object, const FigureAnimation* animation) {
    return ObjectSlots(object)->AcceptsAnimation(animation);
}

// The local list's storage (32-byte aligned; the block from operator new kept before it).
void FreeCandidates(AlignedPtrArray& list) {
    if (list.data) gamecrt::operator_delete(list.data[-1]);
}

// rand() scaled to 0..count-1 as the original does.
int RandomIndex(int count) { return (gamecrt::rand() * count / kRandMax) % count; }
} // namespace

FigureAnimation* CObject3D::RandomAnimation(std::uint32_t key) {
    AnimationDatabase* database = Animations();
    if (!database) return nullptr;
    AlignedPtrArray candidates{};
    std::uint32_t wanted = key & kKeyLowBits;
    for (int i = 0; i < static_cast<int>(animations->count); ++i) {
        FigureAnimation* entry = &animations->animations[i];
        std::uint32_t entryKey = entry->key;
        bool variant = wanted == 0 ? (entryKey & kKeyLowBits) == 0 : (~entryKey & key & kKeyLowBits) == 0;
        if (!variant || ((entryKey ^ key) & kKeyKindBits) != 0) continue;
        if (Accepts(this, entry)) CandidatesSetAtGrow(&candidates, nullptr, candidates.size, entry);
    }
    auto** list = reinterpret_cast<FigureAnimation**>(candidates.data);
    int count = candidates.size;
    if (count < 2) {
        FigureAnimation* only = count == 1 ? list[0] : nullptr;
        FreeCandidates(candidates);
        return only;
    }
    std::uint32_t percent = static_cast<std::uint32_t>((gamecrt::rand() * 100 / kRandMax) % 100);
    FigureAnimation* picked = list[RandomIndex(count)];
    for (int i = 0; i < count; ++i) {
        if (list[i]->chance < percent) {
            for (int k = i; k < count - 1; ++k) list[k] = list[k + 1];
            --count;
            --i;
        }
    }
    if (count > 0) picked = list[RandomIndex(count)];
    FreeCandidates(candidates);
    return picked;
}
EI_REPLACE_METHOD(0x005190C0, CObject3D::RandomAnimation);

FigureAnimation* CObject3D::RandomAnimationOfKind(std::uint32_t key) {
    AnimationDatabase* database = Animations();
    if (!database) return nullptr;
    AlignedPtrArray candidates{};
    for (int i = 0; i < static_cast<int>(animations->count); ++i) {
        FigureAnimation* entry = &animations->animations[i];
        if (((entry->key ^ key) & kKeyKindBits) != 0) continue;
        if (!Accepts(this, entry)) continue;
        int index = candidates.size;
        CandidatesSetSize(&candidates, nullptr, index + 1, -1);
        candidates.data[index] = entry;
    }
    if (candidates.size == 0) {
        FreeCandidates(candidates);
        return nullptr;
    }
    auto* picked = static_cast<FigureAnimation*>(candidates.data[RandomIndex(candidates.size)]);
    FreeCandidates(candidates);
    return picked;
}
EI_REPLACE_METHOD(0x00519380, CObject3D::RandomAnimationOfKind);

// The candidate lists' helpers are the same code as AlignedPtrArray's.
EI_REPLACE_METHOD(0x0051A010, AlignedPtrArray::SetAtGrow);
EI_REPLACE_METHOD(0x0051A190, AlignedPtrArray::SetSize);
