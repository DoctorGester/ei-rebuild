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
__declspec(naked) void __fastcall StringDestroyA(RawString*) EI_ORIGINAL(0x0040C770)
__declspec(naked) void __fastcall StringDestroyB(RawString*) EI_ORIGINAL(0x0040C720)
// 005A9A50: the model's constructor; returns this.
__declspec(naked) void* __fastcall ConstructModel(void*) EI_ORIGINAL(0x005A9A50)

// The model's animation map (+0xC): find (RET 8) and operator[] (RET 4); 006721D0 the
// animation's constructor (0x6C bytes, returns this).
__declspec(naked) void __fastcall AnimFind(void*, void*, Iterator*, const RawString*) EI_ORIGINAL(0x005AD520)
__declspec(naked) char* __fastcall AnimIndex(void*, void*, const RawString*) EI_ORIGINAL(0x005ADC10)
__declspec(naked) void* __fastcall ConstructAnimation(void*) EI_ORIGINAL(0x006721D0)

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

void* ModelSlot(void* map, const RawString* name, void (__fastcall* destroy)(RawString*)) {
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
    *static_cast<void**>(ModelSlot(map, name, &StringDestroyA)) = model;
    void** parts = *static_cast<void***>(ModelSlot(map, name, &StringDestroyB));
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
        StringDestroyA(&key);
        *slot = animation;
        StringCopy(&key, nullptr, name);
        slot = reinterpret_cast<void**>(AnimIndex(map, nullptr, &key) + 0xC);
        StringDestroyA(&key);
        ei::VCall<void>(*slot, kSlotLoad, data);
    }
    AnimFind(map, nullptr, &found, name);
    return found.node ? found.node->value : nullptr;
}
EI_REPLACE(0x005A9EF0, FindModelAnimation);
