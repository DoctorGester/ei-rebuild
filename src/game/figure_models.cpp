// 005AA5F0 (thiscall on the figure manager, RET 4): the model named `name`, loaded on first use.
// The models are cached in CFigureManager::models_ (nodes {next, key, value}; the key hashed
// h = h * 5 + c). A missing model opens "<name>.mod", ".bon" and ".anm" through the manager's
// GetFile (slot 19); without the .mod nothing is created and null is returned. Otherwise a
// FigureModel (constructor 005A9A50) is stored under the name, its mesh, bones and animation
// archives load from the files (OpenFromMemory, slot 2; the .bon and .anm only when they opened),
// and the cached entry is looked up again. See docs/notes/models.md.
#include "figure_manager.h"   // CFigureManager, FigureModel

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/stlp.h"

#include <cstdint>
#include <cstring>
#include "originals_engine.h"
#include "originals_library.h"
#include "originals_world.h"
#include "game_data.h"

namespace {

using ModelMap = stlp::hash_map<stlp::string, FigureModel*, stlp::StringHash>;
using AnimationMap = FigureModel::AnimationMap;

// The model map's and the model's animation map's find (004FB0F0 / 005AD520) and find_or_insert
// (004FBBD0 / 005ADC10, through operator[]). The animations are CMMIOResources (0x6C bytes,
// CMMIOResource::Construct).

// The STLport hashtable's inlined constructor: the buckets vector's end-of-storage member from an
// allocator and a value (RET 8), lower_bound over the prime list (ECX first, EDX last; RET 8) and
// insert(pos, n, value) (RET 0xC); reserve is the vector's own.
using BucketVector = stlp::vector<AnimationMap::Node*>;

// The data of "<name><suffix>" (the manager's GetFile), or null.
void* OpenPart(CFigureManager* manager, const stlp::string* name, const char* suffix) {
    char path[0x200];
    // The inlined strcpy and strcat (no bounds check, as in the original).
    std::size_t length = std::strlen(name->data());
    std::memcpy(path, name->data(), length);
    const char* tail = suffix;
    std::memcpy(path + length, tail, std::strlen(tail) + 1);
    return manager->GetFile(path, nullptr);
}

// operator[]: a {name, null} pair for find_or_insert.
FigureModel** ModelSlot(ModelMap* map, const stlp::string* name) { return &map->Subscript(*name); }
CMappedResource** AnimationSlot(AnimationMap* map, const stlp::string* name) { return &map->Subscript(*name); }

} // namespace

FigureModel* CFigureManager::FindModel(const stlp::string* name) {
    ModelMap* map = &models_;
    if (const ModelMap::Node* found = map->Find(*name)) return found->value;

    void* mesh = OpenPart(this, name, ".mod");
    void* bones = OpenPart(this, name, ".bon");
    void* animation = OpenPart(this, name, ".anm");
    if (!mesh) return nullptr;

    FigureModel* memory = gamecrt::operator_new_as<FigureModel>();
    auto* model = memory ? ConstructFigureModel(memory) : nullptr;
    *ModelSlot(map, name) = model;
    FigureModel* parts = *ModelSlot(map, name);
    parts->mesh->OpenFromMemory(mesh);
    if (bones) parts->bones->OpenFromMemory(bones);
    if (animation) parts->animations->OpenFromMemory(animation);
    const ModelMap::Node* found = map->Find(*name);
    return found ? found->value : nullptr;
}

EI_REWRITTEN(0x005AA5F0, CFigureManager::FindModel);
// 005A9EF0 (thiscall on a model, RET 4): the model's animation of a name, loaded on first use from
// its animation archive (GetFile, slot 5, with the name) into a new archive over that data
// (OpenFromMemory, slot 2); without data nothing is created. The cached entry is looked up again
// either way.
CMappedResource* FigureModel::FindAnimation(const stlp::string* name) {
    AnimationMap* map = &animationCache;
    if (const AnimationMap::Node* found = map->Find(*name)) return found->value;
    void* data = animations->GetFile(name->data(), nullptr);
    if (data) {
        void* memory = gamecrt::operator_new(0x6C);
        CMappedResource* animation = memory ? static_cast<CMMIOResource*>(memory)->Construct() : nullptr;
        *AnimationSlot(map, name) = animation;
        (*AnimationSlot(map, name))->OpenFromMemory(data);
    }
    const AnimationMap::Node* found = map->Find(*name);
    return found ? found->value : nullptr;
}
EI_REWRITTEN(0x005A9EF0, FigureModel::FindAnimation);

// The library's hash table sizes.
constexpr std::uint32_t kPrimeCount = 28;
constexpr std::uint32_t kPrimes[kPrimeCount] = /*0073B914*/ {
    53,        97,        193,       389,       769,       1543,       3079,       6151,       12289,      24593,
    49157,     98317,     196613,    393241,    786433,    1572869,    3145739,    6291469,    12582917,   25165843,
    50331653,  100663319, 201326611, 402653189, 805306457, 1610612741, 3221225473, 4294967291};

// 005A9A50 (thiscall, returns this): the model's constructor. The animation map is built as
// STLport's hashtable constructor inlines it: empty functors (the original copies an
// uninitialised stack byte into them; ours 0), no elements, buckets for the first prime of the
// list (kPrimes) not below 100, all null. Then the three archives are new 0x6C-byte
// objects (006721D0), null when the allocation fails.
FigureModel* __fastcall ConstructFigureModel(FigureModel* model) {
    AnimationMap& table = model->animationCache;
    std::uint8_t* functors = table.Functors();
    functors[0] = 0;
    functors[1] = 0;
    functors[2] = 0;
    auto* buckets = static_cast<BucketVector*>(table.BucketVector());
    auto* count = static_cast<std::uint32_t*>(table.CountProxy());
    buckets->Start() = nullptr;
    buckets->Finish() = nullptr;
    buckets->EndOfStorage() = nullptr;   // the allocator proxy (005AD640)
    *count = 0;
    const std::uint32_t* first = kPrimes;
    const std::uint32_t* last = kPrimes + kPrimeCount;
    std::uint32_t hint = 100;
    const std::uint32_t* prime = stlp::LowerBoundDwords(first, last, &hint, 0);
    std::uint32_t n = prime == last ? kPrimes[kPrimeCount - 1] : *prime;
    buckets->Reserve<stlp::PlainElement<AnimationMap::Node*>>(n);
    buckets->FillInsert<stlp::PlainElement<AnimationMap::Node*>>(buckets->Finish(), n, nullptr);   // 005ADAD0
    *count = 0;
    static constexpr CMappedResource* FigureModel::*kArchives[3] = {&FigureModel::mesh, &FigureModel::bones,
                                                                    &FigureModel::animations};
    for (CMappedResource* FigureModel::*archive : kArchives) {
        void* memory = gamecrt::operator_new(0x6C);
        model->*archive = memory ? static_cast<CMMIOResource*>(memory)->Construct() : nullptr;
    }
    return model;
}
EI_REWRITTEN(0x005A9A50, ConstructFigureModel);
