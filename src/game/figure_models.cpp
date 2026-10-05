// 005AA5F0 (thiscall on the figure manager, RET 4): the model named `name`, loaded on first use.
// The models are cached in a hash_map<string, model*> at +0xBC (nodes {next, key, value}; the key
// hashed h = h * 5 + c). A missing model opens "<name>.mod", ".bon" and ".anm" through the
// manager's slot 19 (open a resource file); without the .mod nothing is created and null is
// returned. Otherwise a model (0x20 bytes, constructor 005A9A50) is stored under the name, its
// mesh, bones and animation parts (+0, +4, +8) load from the files (slot 2, the .bon and .anm
// only when they opened), and the cached entry is looked up again. See docs/notes/models.md.
#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"

#include <cstdint>
#include <cstring>
#include "originals_library.h"

namespace {

struct RawString {
    char* start;
    char* finish;
    char* eos;
};
struct Node {
    Node* next;
    RawString key;
    void* value;
};
struct Iterator {
    Node* node;
    void* map;
};

// The hash map's find (RET 8: out iterator, key) and operator[] (RET 4: the pair, key then value).
__declspec(naked) void __fastcall MapFind(void*, void*, Iterator*, const RawString*) EI_ORIGINAL(0x004FB0F0)
__declspec(naked) char* __fastcall MapIndex(void*, void*, const RawString*) EI_ORIGINAL(0x004FBBD0)
// STLport string copy constructor (RET 4) and two destructor instantiations.
__declspec(naked) void __fastcall StringCopy(RawString*, void*, const RawString*) EI_ORIGINAL(0x004A8930)
// 005A9A50: the model's constructor; returns this.
__declspec(naked) void* __fastcall ConstructModel(void*) EI_ORIGINAL(0x005A9A50)

// The model's animation map (+0xC): find (RET 8) and operator[] (RET 4); 006721D0 the
// animation's constructor (0x6C bytes, returns this).
__declspec(naked) void __fastcall AnimFind(void*, void*, Iterator*, const RawString*) EI_ORIGINAL(0x005AD520)
__declspec(naked) char* __fastcall AnimIndex(void*, void*, const RawString*) EI_ORIGINAL(0x005ADC10)
__declspec(naked) void* __fastcall ConstructAnimation(void*) EI_ORIGINAL(0x006721D0)

// The STLport hashtable's inlined constructor: the buckets vector's end-of-storage member from an
// allocator and a value (RET 8), lower_bound over the prime list (ECX first, EDX last; RET 8),
// reserve (RET 4) and insert(pos, n, value) (RET 0xC).
__declspec(naked) void __fastcall BucketsEndOfStorage(void*, void*, const void*, const void*) EI_ORIGINAL(0x005AD640)
__declspec(naked) const std::uint32_t* __fastcall PrimeLowerBound(const std::uint32_t*, const std::uint32_t*,
                                                                  const std::uint32_t*, int) EI_ORIGINAL(0x00408A50)
__declspec(naked) void __fastcall BucketsReserve(void*, void*, std::uint32_t) EI_ORIGINAL(0x005AD880)
__declspec(naked) void __fastcall BucketsInsert(void*, void*, void*, std::uint32_t, const void*) EI_ORIGINAL(0x005ADAD0)

constexpr int kModels = 0xBC;
constexpr int kSlotOpenResource = 19;
constexpr int kSlotLoad = 2;
constexpr int kSlotPartRead = 5;

void* OpenPart(void* manager, const RawString* name, std::uintptr_t suffix) {
    char path[0x200];
    // The inlined strcpy and strcat (no bounds check, as in the original).
    std::size_t length = std::strlen(name->start);
    std::memcpy(path, name->start, length);
    const char* tail = reinterpret_cast<const char*>(suffix);
    std::memcpy(path + length, tail, std::strlen(tail) + 1);
    return ei::VCall<void*>(manager, kSlotOpenResource, static_cast<const char*>(path), 0);
}

void* ModelSlot(void* map, const RawString* name, void (__fastcall* destroy)(void*)) {
    RawString key;
    StringCopy(&key, nullptr, name);
    char* pair = MapIndex(map, nullptr, &key);
    destroy(&key);
    return pair + 0xC;
}

} // namespace

void* __fastcall FindFigureModel(char* manager, void*, const void* nameIn) {
    const auto* name = static_cast<const RawString*>(nameIn);
    void* map = manager + kModels;
    Iterator found;
    MapFind(map, nullptr, &found, name);
    if (found.node) return found.node->value;

    void* mesh = OpenPart(manager, name, 0x0078EC88);        // ".mod"
    void* bones = OpenPart(manager, name, 0x00784640);       // ".bon"
    void* animation = OpenPart(manager, name, 0x0078EC80);   // ".anm"
    if (!mesh) return nullptr;

    void* model = gamecrt::operator_new(0x20);
    if (model) model = ConstructModel(model);
    *static_cast<void**>(ModelSlot(map, name, &StringRepDestroy)) = model;
    void** parts = *static_cast<void***>(ModelSlot(map, name, &StlStringDestroy));
    ei::VCall<void>(parts[0], kSlotLoad, mesh);
    if (bones) ei::VCall<void>(parts[1], kSlotLoad, bones);
    if (animation) ei::VCall<void>(parts[2], kSlotLoad, animation);
    MapFind(map, nullptr, &found, name);
    return found.node ? found.node->value : nullptr;
}

EI_REPLACE(0x005AA5F0, FindFigureModel);

// 005A9EF0 (thiscall on a model, RET 4): the model's animation of a name, loaded on first use from
// its animation part (+8, slot 5 with the name) into a new animation object (slot 2 loads it);
// without data nothing is created. The cached entry is looked up again either way.
void* __fastcall FindModelAnimation(char* model, void*, const void* nameIn) {
    const auto* name = static_cast<const RawString*>(nameIn);
    void* map = model + 0xC;
    Iterator found;
    AnimFind(map, nullptr, &found, name);
    if (found.node) return found.node->value;
    void* part = *reinterpret_cast<void**>(model + 8);
    void* data = ei::VCall<void*>(part, kSlotPartRead, static_cast<const char*>(name->start), 0);
    if (data) {
        void* animation = gamecrt::operator_new(0x6C);
        if (animation) animation = ConstructAnimation(animation);
        RawString key;
        StringCopy(&key, nullptr, name);
        void** slot = reinterpret_cast<void**>(AnimIndex(map, nullptr, &key) + 0xC);
        StringRepDestroy(&key);
        *slot = animation;
        StringCopy(&key, nullptr, name);
        slot = reinterpret_cast<void**>(AnimIndex(map, nullptr, &key) + 0xC);
        StringRepDestroy(&key);
        ei::VCall<void>(*slot, kSlotLoad, data);
    }
    AnimFind(map, nullptr, &found, name);
    return found.node ? found.node->value : nullptr;
}
EI_REPLACE(0x005A9EF0, FindModelAnimation);

// 005A9A50 (thiscall, returns this): the model's constructor. The animation map (+0xC) is built
// as STLport's hashtable constructor inlines it: empty functors (the original copies an
// uninitialised stack byte into them; ours 0), no elements, buckets for the first prime of the
// list 0073B914..0073B984 not below 100, all null. Then the three parts (+0, +4, +8) are new
// 0x6C-byte objects (006721D0), null when the allocation fails.
void* __fastcall ConstructFigureModel(std::uint8_t* model) {
    std::uint8_t* table = model + 0xC;
    table[0] = 0;
    table[1] = 0;
    table[2] = 0;
    void* buckets = table + 4;
    *reinterpret_cast<std::uint32_t*>(table + 4) = 0;
    *reinterpret_cast<std::uint32_t*>(table + 8) = 0;
    char allocatorTag = 0;
    std::uint32_t zero = 0;
    BucketsEndOfStorage(table + 0xC, nullptr, &allocatorTag, &zero);
    *reinterpret_cast<std::uint32_t*>(table + 0x10) = 0;
    const auto* first = reinterpret_cast<const std::uint32_t*>(0x0073B914);
    const auto* last = reinterpret_cast<const std::uint32_t*>(0x0073B984);
    std::uint32_t hint = 100;
    const std::uint32_t* prime = PrimeLowerBound(first, last, &hint, 0);
    std::uint32_t count = prime == last ? *reinterpret_cast<const std::uint32_t*>(0x0073B980) : *prime;
    BucketsReserve(buckets, nullptr, count);
    void* none = nullptr;
    BucketsInsert(buckets, nullptr, *reinterpret_cast<void**>(table + 8), count, &none);
    *reinterpret_cast<std::uint32_t*>(table + 0x10) = 0;
    for (int i = 0; i < 3; ++i) {
        void* part = gamecrt::operator_new(0x6C);
        reinterpret_cast<void**>(model)[i] = part ? ConstructAnimation(part) : nullptr;
    }
    return model;
}
EI_REPLACE(0x005A9A50, ConstructFigureModel);
