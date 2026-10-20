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

namespace {

using ModelMap = stlp::hash_map<stlp::string, FigureModel*, stlp::StringHash>;
using AnimationMap = FigureModel::AnimationMap;

// The model map's find (RET 8: out iterator, key) and operator[] (RET 4: the pair, key then value).
__declspec(naked) void __fastcall MapFind(ModelMap*, void*, ModelMap::Iterator*, const stlp::string*) EI_ORIGINAL(0x004FB0F0)
__declspec(naked) ModelMap::Pair* __fastcall MapIndex(ModelMap*, void*, const stlp::string*) EI_ORIGINAL(0x004FBBD0)

// The model's animation map: find (RET 8) and operator[] (RET 4). The animations are
// CMMIOResources (0x6C bytes, CMMIOResource::Construct).
__declspec(naked) void __fastcall AnimFind(AnimationMap*, void*, AnimationMap::Iterator*, const stlp::string*) EI_ORIGINAL(0x005AD520)
__declspec(naked) AnimationMap::Pair* __fastcall AnimIndex(AnimationMap*, void*, const stlp::string*) EI_ORIGINAL(0x005ADC10)

// The STLport hashtable's inlined constructor: the buckets vector's end-of-storage member from an
// allocator and a value (RET 8), lower_bound over the prime list (ECX first, EDX last; RET 8),
// reserve (RET 4) and insert(pos, n, value) (RET 0xC).
using BucketVector = stlp::vector<AnimationMap::Node*>;
__declspec(naked) void __fastcall BucketsEndOfStorage(AnimationMap::Node***, void*, const void*, const void*) EI_ORIGINAL(0x005AD640)
__declspec(naked) void __fastcall BucketsReserve(BucketVector*, void*, std::uint32_t) EI_ORIGINAL(0x005AD880)
__declspec(naked) void __fastcall BucketsInsert(BucketVector*, void*, AnimationMap::Node**, std::uint32_t, const void*) EI_ORIGINAL(0x005ADAD0)

// The data of "<name><suffix>" (the manager's GetFile), or null.
void* OpenPart(CFigureManager* manager, const stlp::string* name, std::uintptr_t suffix) {
    char path[0x200];
    // The inlined strcpy and strcat (no bounds check, as in the original).
    std::size_t length = std::strlen(name->data());
    std::memcpy(path, name->data(), length);
    const char* tail = reinterpret_cast<const char*>(suffix);
    std::memcpy(path + length, tail, std::strlen(tail) + 1);
    return manager->GetFile(path, nullptr);
}

// operator[] with a copy of the name as its key.
FigureModel** ModelSlot(ModelMap* map, const stlp::string* name) {
    stlp::string key(*name);
    return &MapIndex(map, nullptr, &key)->value;
}
CMappedResource** AnimationSlot(AnimationMap* map, const stlp::string* name) {
    stlp::string key(*name);
    return &AnimIndex(map, nullptr, &key)->value;
}

} // namespace

FigureModel* CFigureManager::FindModel(const stlp::string* name) {
    ModelMap* map = &models_;
    ModelMap::Iterator found;
    MapFind(map, nullptr, &found, name);
    if (found.node) return found.node->value;

    void* mesh = OpenPart(this, name, 0x0078EC88);        // ".mod"
    void* bones = OpenPart(this, name, 0x00784640);       // ".bon"
    void* animation = OpenPart(this, name, 0x0078EC80);   // ".anm"
    if (!mesh) return nullptr;

    void* memory = gamecrt::operator_new(sizeof(FigureModel));
    auto* model = static_cast<FigureModel*>(memory ? ConstructFigureModel(static_cast<FigureModel*>(memory)) : nullptr);
    *ModelSlot(map, name) = model;
    FigureModel* parts = *ModelSlot(map, name);
    parts->mesh->OpenFromMemory(mesh);
    if (bones) parts->bones->OpenFromMemory(bones);
    if (animation) parts->animations->OpenFromMemory(animation);
    MapFind(map, nullptr, &found, name);
    return found.node ? found.node->value : nullptr;
}

EI_REPLACE_METHOD(0x005AA5F0, CFigureManager::FindModel);

// 005A9EF0 (thiscall on a model, RET 4): the model's animation of a name, loaded on first use from
// its animation archive (GetFile, slot 5, with the name) into a new archive over that data
// (OpenFromMemory, slot 2); without data nothing is created. The cached entry is looked up again
// either way.
CMappedResource* FigureModel::FindAnimation(const stlp::string* name) {
    AnimationMap* map = &animationCache;
    AnimationMap::Iterator found;
    AnimFind(map, nullptr, &found, name);
    if (found.node) return found.node->value;
    void* data = animations->GetFile(name->data(), nullptr);
    if (data) {
        void* memory = gamecrt::operator_new(0x6C);
        CMappedResource* animation = memory ? static_cast<CMMIOResource*>(memory)->Construct() : nullptr;
        *AnimationSlot(map, name) = animation;
        (*AnimationSlot(map, name))->OpenFromMemory(data);
    }
    AnimFind(map, nullptr, &found, name);
    return found.node ? found.node->value : nullptr;
}
EI_REPLACE_METHOD(0x005A9EF0, FigureModel::FindAnimation);

// 005A9A50 (thiscall, returns this): the model's constructor. The animation map is built as
// STLport's hashtable constructor inlines it: empty functors (the original copies an
// uninitialised stack byte into them; ours 0), no elements, buckets for the first prime of the
// list 0073B914..0073B984 not below 100, all null. Then the three archives are new 0x6C-byte
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
    char allocatorTag = 0;
    std::uint32_t zero = 0;
    BucketsEndOfStorage(&buckets->EndOfStorage(), nullptr, &allocatorTag, &zero);
    *count = 0;
    const auto* first = reinterpret_cast<const std::uint32_t*>(0x0073B914);
    const auto* last = reinterpret_cast<const std::uint32_t*>(0x0073B984);
    std::uint32_t hint = 100;
    const std::uint32_t* prime = stlp::LowerBoundDwords(first, last, &hint, 0);
    std::uint32_t n = prime == last ? *reinterpret_cast<const std::uint32_t*>(0x0073B980) : *prime;
    BucketsReserve(buckets, nullptr, n);
    void* none = nullptr;
    BucketsInsert(buckets, nullptr, buckets->Finish(), n, &none);
    *count = 0;
    static constexpr CMappedResource* FigureModel::*kArchives[3] = {&FigureModel::mesh, &FigureModel::bones,
                                                                    &FigureModel::animations};
    for (CMappedResource* FigureModel::*archive : kArchives) {
        void* memory = gamecrt::operator_new(0x6C);
        model->*archive = memory ? static_cast<CMMIOResource*>(memory)->Construct() : nullptr;
    }
    return model;
}
EI_REPLACE(0x005A9A50, ConstructFigureModel);
