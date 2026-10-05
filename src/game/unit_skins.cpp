// The zone's unit skins and figures (00568800, thiscall on the zone's client object, called by the
// zone load 00567AB0): for every dressed unit (first list, 0x24-byte records) two composite
// textures are built through the texture cache (+0x18C, 00693180): the body skin ("<model>skin_NN.mmp"
// and the armour pieces' "<model><item>.mmp"), registered as "mo<n>p" / "prot<n>p", then the head
// and weapon textures ("mo<n>s" / "prot<n>s"); for every other unit (second list, 0x3C-byte records)
// the list of its textures goes to the figure maker (+0x34, slot 3). Names provisional; see
// docs/notes/world.md ("Unit skins").
#include "console.h"

#include "../lib/crt.h"
#include "../lib/mfc.h"
#include "ei/hook.h"
#include "originals_gameplay.h"
#include "originals_library.h"

#include <cstdint>
#include <cstring>
#include <new>
#include "originals_render.h"

namespace {

template <typename T>
T& At(const void* base, std::size_t offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(const_cast<void*>(base)) + offset);
}

const char* Text(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }

// --- Tables --------------------------------------------------------------------------------------

struct Table {
    const char* begin;
    const char* end;
};
// The unit prototypes (007AF358, 0x120 bytes; +8 the model) and models (007AF398, 0x168 bytes; +0xE8
// the texture name prefix), the armours (007C8718, 200 bytes; +0xB8 the layer), weapons (007C86F8)
// and materials (007C86D8). An index past the end gives null.
const char* RecordAt(std::uintptr_t table, std::uint32_t index, int size) {
    const Table& t = *reinterpret_cast<const Table*>(table);
    int count = static_cast<int>(t.end - t.begin) / size;
    return index < static_cast<std::uint32_t>(count) ? t.begin + index * size : nullptr;
}
const char* UnitPrototype(std::uint32_t index) { return RecordAt(0x007AF358, index, 0x120); }
const char* UnitModel(std::uint32_t index) { return RecordAt(0x007AF398, index, 0x168); }
const char* Armour(std::uint32_t index) { return RecordAt(0x007C8718, index, 200); }
const char* ModelPrefix(const char* model) { return At<const char*>(model, 0xE8); }

constexpr std::uint32_t kNone = 0xFFFFFFFFu;

// --- Forwarders ----------------------------------------------------------------------------------

// An item's texture name [006A3480: out, material index; armour index] and a weapon's [006A0BF0:
// out, weapon record; material record]; the weapon and material records [0056E440 on 007C86F8,
// 0056E400 on 007C86D8].
__declspec(naked) void __fastcall WeaponTextureName(mfc::CString*, const char*, const char*) EI_ORIGINAL(0x006A0BF0)
__declspec(naked) const char* __fastcall WeaponRecord(void*, void*, std::uint32_t) EI_ORIGINAL(0x0056E440)
__declspec(naked) const char* __fastcall MaterialRecord(void*, void*, std::uint32_t) EI_ORIGINAL(0x0056E400)
// The 12-byte record list of a unit's textures [005683D0: flag in CL, the unit record, out list],
// its constructor [0056F570, allocator] and destructor [0056F590].
__declspec(naked) void __fastcall TextureRecords(char /*body*/, const char*, void*) EI_ORIGINAL(0x005683D0)
__declspec(naked) void __fastcall RecordListConstruct(void*, void*, const void*) EI_ORIGINAL(0x0056F570)
__declspec(naked) void __fastcall RecordListDestroy(void*) EI_ORIGINAL(0x0056F590)
// The texture set's byte array: constructor [0056FCC0], destructor [0051E5B0], allocation [004ECB50].
__declspec(naked) void __fastcall BytesConstruct(void*) EI_ORIGINAL(0x0056FCC0)
__declspec(naked) void __fastcall BytesDestroy(void*) EI_ORIGINAL(0x0051E5B0)
char* __fastcall AllocBytes(int a0) { return AllocAligned32Original(a0); }
// CStringArray SetSize [006FE9B8], SetAtGrow of a CString [006FEBFA] and of a C string [006FEBCE].
void __fastcall NamesSetAtGrowString(console::CStringArray* a0, void*, int a2, const mfc::CString* a3) { StringArraySetAtGrowStringOriginal((void*)a0, nullptr, a2, a3); }
void __fastcall NamesSetAtGrowText(console::CStringArray* a0, void*, int a2, const char* a3) { StringArraySetAtGrowTextOriginal((void*)a0, nullptr, a2, a3); }
// The texture cache [this +0x18C]: build a composite texture [00693180: out name, set, name, name,
// 3, 0, 0x5551] and register it [00693F10: kind, name].
__declspec(naked) void* __fastcall CacheBuild(void*, void*, mfc::CString*, void*, const mfc::CString*, const mfc::CString*,
                                             int, int, int) EI_ORIGINAL(0x00693180)
__declspec(naked) void __fastcall CacheRegister(void*, void*, int, const char*) EI_ORIGINAL(0x00693F10)
// The second list: the unit's texture names from its prototype [00535CB0: unit in ECX, record +4 in
// EDX; record +0x10, names, more names]; vector<string> members: constructor [0053BB80], copy into
// new storage [0053D7F0: count, first, last], element destructors [004DDC90 with 0, 0040C720],
// storage free [00426860: ECX block, EDX count; 0053BF40: block, count], push_back's copy
// [004F5DF0: ECX place, EDX string] and overflow [004DCE90: position, string, count], range insert
// [00575E50: position, first, last, tag]; the node allocator [00405950: ECX bytes].
__declspec(naked) void __fastcall PrototypeTextures(const char*, const char*, const char*, void*, void*) EI_ORIGINAL(0x00535CB0)
__declspec(naked) void __fastcall StringsConstruct(void*, void*, const void*) EI_ORIGINAL(0x0053BB80)
__declspec(naked) char* __fastcall StringsCopy(void*, void*, int, const char*, const char*) EI_ORIGINAL(0x0053D7F0)
__declspec(naked) void __fastcall StringDestroyFlag(void*, void*, int) EI_ORIGINAL(0x004DDC90)
__declspec(naked) void __fastcall StringsFree(char*, int) EI_ORIGINAL(0x00426860)
__declspec(naked) void __fastcall StringsDeallocate(void*, void*, char*, int) EI_ORIGINAL(0x0053BF40)
__declspec(naked) void __fastcall StringCopyConstruct(char*, const void*) EI_ORIGINAL(0x004F5DF0)
__declspec(naked) void __fastcall StringsInsertOverflow(void*, void*, char*, const void*, int) EI_ORIGINAL(0x004DCE90)
__declspec(naked) void __fastcall StringsInsertRange(void*, void*, char*, const char*, const char*, int) EI_ORIGINAL(0x00575E50)
__declspec(naked) char* __fastcall NodeAllocate(int) EI_ORIGINAL(0x00405950)
// STLport string members (as in figure_part_draw.cpp).
__declspec(naked) void __fastcall StringInitRange(void*, void*, const char*, const char*, int) EI_ORIGINAL(0x0042A3D0)
void __fastcall StringEndOfStorage(void* a0, void*, const void* a2, const void* a3) { StringEndOfStorageOriginal(a0, nullptr, a2, a3); }

const std::uint8_t kAllocator = 0;

template <typename R, typename... Args>
R ThisCall(void* object, int slot, Args... args) {
    void* fn = (*static_cast<void***>(object))[slot];
    return reinterpret_cast<R(__fastcall*)(void*, void*, Args...)>(fn)(object, nullptr, args...);
}

// --- The dressed units ---------------------------------------------------------------------------

// The set handed to the texture cache (0x28 bytes): the texture records as raw bytes (an aligned
// array), the texture file names, the composite's size.
struct TextureSet {
    char* bytes;
    int byteCount;
    int capacity;
    int growBy;
    console::CStringArray names;
    int size;
};
static_assert(sizeof(TextureSet) == 0x28 && offsetof(TextureSet, size) == 0x24);

// The list from 005683D0 (begin, end, end of storage): 12-byte records.
struct RecordList {
    char* begin;
    char* end;
    char* storageEnd;
};

// The texture records copied into the set's byte array (SetSize inlined: kept contents, 0 frees).
void CopyRecords(TextureSet& set, const RecordList& list) {
    int size = static_cast<int>(list.end - list.begin) / 12 * 12;
    if (size == 0) {
        if (set.bytes) {
            gamecrt::operator_delete(reinterpret_cast<void**>(set.bytes)[-1]);
            set.bytes = nullptr;
        }
        set.capacity = 0;
        set.byteCount = 0;
    } else if (!set.bytes) {
        set.bytes = AllocBytes(size);
        set.capacity = size;
        set.byteCount = size;
    } else if (size <= set.capacity) {
        set.byteCount = size;
    } else {
        int step = set.growBy;
        if (step == 0) {
            step = set.byteCount / 8;
            if (step < 4) step = 4;
            else if (step > 0x400) step = 0x400;
        }
        int capacity = size < step + set.capacity ? step + set.capacity : size;
        char* bytes = AllocBytes(capacity);
        std::memcpy(bytes, set.bytes, static_cast<std::size_t>(set.byteCount));
        gamecrt::operator_delete(reinterpret_cast<void**>(set.bytes)[-1]);
        set.bytes = bytes;
        set.byteCount = size;
        set.capacity = capacity;
    }
    std::memcpy(set.bytes, list.begin, static_cast<std::size_t>(size));
}

// The original's inlined strcpy / strcat: the bytes up to and including the terminator.
void CopyText(char* to, const char* from) { std::memcpy(to, from, std::strlen(from) + 1); }
void AppendText(char* to, const char* from) { CopyText(to + std::strlen(to), from); }

// "<model prefix><name>.mmp" into `buffer`.
void AppendName(const char* model, const mfc::CString& name, char* buffer) {
    CopyText(buffer, ModelPrefix(model));
    AppendText(buffer, name);
    AppendText(buffer, Text(0x0078FB4C));   // ".mmp"
}

void AddItem(TextureSet& set, const char* model, std::uint32_t id) {
    char buffer[0x100];
    {
        mfc::CString name;
        ArmourTexture(&name, id >> 16, id & 0xFFFF);
        AppendName(model, name, buffer);
    }
    NamesSetAtGrowText(&set.names, nullptr, set.names.GetSize(), buffer);
}

// The weapons (items 1 and 2): the one whose armour layer (+0xB8) is lower first when both exist.
void AddWeapons(TextureSet& set, const char* model, const std::uint32_t* items) {
    if (items[1] != kNone && items[2] != kNone) {
        const char* second = Armour(items[2] >> 16);
        const char* first = Armour(items[1] >> 16);
        if (At<std::uint32_t>(second, 0xB8) < At<std::uint32_t>(first, 0xB8)) {
            AddItem(set, model, items[2]);
            AddItem(set, model, items[1]);
        } else {
            AddItem(set, model, items[1]);
            AddItem(set, model, items[2]);
        }
        return;
    }
    std::uint32_t id = items[1] == kNone ? items[2] : items[1];
    if (id != kNone) AddItem(set, model, id);
}

// The extra weapon picked by the record's index byte (+0x1C) in its list (+0x10): weapon and
// material records named together.
void AddExtraWeapon(TextureSet& set, const char* model, const char* record) {
    const std::uint32_t* list = At<const std::uint32_t*>(record, 0x10);
    int count = static_cast<int>(At<const std::uint32_t*>(record, 0x14) - list);
    int index = At<signed char>(record, 0x1C);
    if (!(static_cast<std::uint32_t>(count) > static_cast<std::uint32_t>(index))) return;
    std::uint32_t id = list[index];
    if (id == kNone) return;
    char buffer[0x100];
    {
        const char* weapon = WeaponRecord(reinterpret_cast<void*>(0x007C86F8), nullptr, id >> 16);
        mfc::CString name;
        const char* material = MaterialRecord(reinterpret_cast<void*>(0x007C86D8), nullptr, id & 0xFFFF);
        WeaponTextureName(&name, weapon, material);
        AppendName(model, name, buffer);
    }
    NamesSetAtGrowText(&set.names, nullptr, set.names.GetSize(), buffer);
}

void Compose(char* self, mfc::CString& out, TextureSet& set, const mfc::CString& name, int size, int kind) {
    set.size = size;
    CacheBuild(At<void*>(self, 0x18C), nullptr, &out, &set, &name, &name, 3, 0, 0x5551);
    CacheRegister(At<void*>(self, 0x18C), nullptr, kind, name);
}

void DressUnit(char* self, const char* record) {
    const char* prototype = UnitPrototype(At<std::uint16_t>(record, 0));
    const char* model = UnitModel(At<std::uint32_t>(prototype, 8));
    alignas(4) char listStorage[sizeof(RecordList)];
    RecordListConstruct(listStorage, nullptr, &kAllocator);
    RecordList& list = *reinterpret_cast<RecordList*>(listStorage);
    alignas(4) char setStorage[sizeof(TextureSet)];
    TextureSet& set = *reinterpret_cast<TextureSet*>(setStorage);
    BytesConstruct(&set);
    new (&set.names) console::CStringArray;

    const std::uint32_t* const& items = At<const std::uint32_t*>(record, 4);
    if ((reinterpret_cast<std::uintptr_t>(At<const std::uint32_t*>(record, 8)) -
         reinterpret_cast<std::uintptr_t>(items)) & ~std::uintptr_t{3}) {
        mfc::CString built;
        TextureRecords(1, record, &list);
        CopyRecords(set, list);
        mfc::CString skin;
        StringArraySetSize(&set.names, nullptr, 0, -1);
        if (prototype) {
            skin.Format(Text(0x0078FB64), ModelPrefix(model), At<int>(prototype, 0xC));   // "%sskin_%.2d.mmp"
        } else {
            skin.Format(Text(0x0078FB54), ModelPrefix(model));                             // "%sskin_00.mmp"
        }
        NamesSetAtGrowString(&set.names, nullptr, set.names.GetSize(), &skin);
        for (int slot = 3; slot <= 6; ++slot) {
            if (items[slot] != kNone) AddItem(set, model, items[slot]);
        }
        AddWeapons(set, model, items);

        mfc::CString name;
        if (At<int>(record, 0x20) != -1) {
            name.Format(Text(0x0078FB7C), At<int>(record, 0x20));                   // "mo%dp"
        } else {
            name.Format(Text(0x0078FB74), static_cast<int>(At<std::uint16_t>(record, 0)));   // "prot%dp"
        }
        Compose(self, built, set, name, 0x100, 1);

        TextureRecords(0, record, &list);
        CopyRecords(set, list);
        StringArraySetSize(&set.names, nullptr, 0, -1);
        if (items[0] != kNone) AddItem(set, model, items[0]);
        AddExtraWeapon(set, model, record);
        if (At<int>(record, 0x20) != -1) {
            name.Format(Text(0x0078FB44), At<int>(record, 0x20));                   // "mo%ds"
        } else {
            name.Format(Text(0x0078FB3C), static_cast<int>(At<std::uint16_t>(record, 0)));   // "prot%ds"
        }
        Compose(self, built, set, name, 0x80, 2);
    }

    set.names.~CStringArray();
    BytesDestroy(&set);
    RecordListDestroy(listStorage);
}

// --- The other units -----------------------------------------------------------------------------

struct StringList {   // vector<string>: 12-byte strings
    char* begin;
    char* end;
    char* storageEnd;
};
int Count12(const char* from, const char* to) { return static_cast<int>(to - from) / 12; }

// Room for `needed` strings as inlined (reserve): a copy into new storage, the old strings destroyed
// and freed.
void Reserve(StringList& list, int needed) {
    int size = Count12(list.begin, list.end);
    if (!(static_cast<std::uint32_t>(Count12(list.begin, list.storageEnd)) < static_cast<std::uint32_t>(needed))) return;
    char* storage;
    if (list.begin) {
        storage = StringsCopy(&list, nullptr, needed, list.begin, list.end);
        for (char* s = list.begin; s != list.end; s += 12) StringDestroyFlag(s, nullptr, 0);
        StringsFree(list.begin, Count12(list.begin, list.storageEnd));
    } else {
        storage = needed ? NodeAllocate(needed * 12) : nullptr;
    }
    list.begin = storage;
    list.end = storage + size * 12;
    list.storageEnd = storage + needed * 12;
}

void FigureUnit(char* self, const char* record) {
    const char* prototype = UnitPrototype(At<std::uint16_t>(record, 0));
    const char* model = UnitModel(At<std::uint32_t>(prototype, 8));
    alignas(4) char firstStorage[sizeof(StringList)];
    alignas(4) char secondStorage[sizeof(StringList)];
    StringsConstruct(firstStorage, nullptr, &kAllocator);
    StringsConstruct(secondStorage, nullptr, &kAllocator);
    StringList& first = *reinterpret_cast<StringList*>(firstStorage);
    StringList& second = *reinterpret_cast<StringList*>(secondStorage);

    const char* const* names = At<const char* const*>(record, 0x30);
    if (!((reinterpret_cast<std::uintptr_t>(At<const char* const*>(record, 0x34)) - reinterpret_cast<std::uintptr_t>(names)) &
          ~std::uintptr_t{3})) {
        PrototypeTextures(prototype, record + 4, record + 0x10, &first, &second);
    } else {
        int count = static_cast<int>(At<const char* const*>(record, 0x34) - names);
        Reserve(first, Count12(first.begin, first.end) + count);
        for (const char* const* p = At<const char* const*>(record, 0x30); p != At<const char* const*>(record, 0x34); ++p) {
            const char* text = *p;
            alignas(4) char string[12];
            StringInitWithAllocator(string, nullptr, &kAllocator);
            StringInitRange(string, nullptr, text, text + std::strlen(text), 0);
            if (first.end != first.storageEnd) {
                StringCopyConstruct(first.end, string);
                first.end += 12;
            } else {
                StringsInsertOverflow(&first, nullptr, first.end, string, 1);
            }
            StringBufferFree(string);
        }
    }
    StringsInsertRange(&first, nullptr, first.end, second.begin, second.end, 0);

    alignas(4) char name[12];
    At<char*>(name, 0) = nullptr;
    At<char*>(name, 4) = nullptr;
    int zero = 0;
    StringEndOfStorage(name + 8, nullptr, &kAllocator, &zero);
    const char* prefix = ModelPrefix(model);
    StringAssignSpan(name, nullptr, prefix, prefix + std::strlen(prefix));
    ThisCall<void>(At<void*>(self, 0x34), 3, static_cast<void*>(name), static_cast<void*>(&first), At<std::uint32_t>(record, 0x24),
                   At<std::uint32_t>(record, 0x28), At<std::uint32_t>(record, 0x2C), 0);
    StringBufferFree(name);

    for (char* s = second.begin; s != second.end; s += 12) StlStringDestroy(s);
    StringsDeallocate(&second.storageEnd, nullptr, second.begin, Count12(second.begin, second.storageEnd));
    for (char* s = first.begin; s != first.end; s += 12) StringDestroyFlag(s, nullptr, 0);
    StringsDeallocate(&first.storageEnd, nullptr, first.begin, Count12(first.begin, first.storageEnd));
}

struct RecordVector {
    char* begin;
    char* end;
};

} // namespace

// 00568800 (thiscall, RET 8). `others` and `dressed` are vectors of unit records (0x3C / 0x24
// bytes); the dressed units (the second argument) come first.
void __fastcall BuildUnitSkins(char* self, void*, const void* othersVector, const void* dressedVector) {
    const auto* dressed = static_cast<const RecordVector*>(dressedVector);
    for (const char* r = dressed->begin; r != dressed->end; r += 0x24) DressUnit(self, r);
    const auto* others = static_cast<const RecordVector*>(othersVector);
    for (const char* r = others->begin; r != others->end; r += 0x3C) FigureUnit(self, r);
}
EI_REPLACE(0x00568800, BuildUnitSkins);
