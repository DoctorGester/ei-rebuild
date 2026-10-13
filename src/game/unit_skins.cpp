// The zone's unit skins and figures (00568800, thiscall on the zone's client object, called by the
// zone load 00567AB0): for every dressed unit (first list, 0x24-byte records) two composite
// textures are built through the texture cache (+0x18C, 00693180): the body skin ("<model>skin_NN.mmp"
// and the armour pieces' "<model><item>.mmp"), registered as "mo<n>p" / "prot<n>p", then the head
// and weapon textures ("mo<n>s" / "prot<n>s"); for every other unit (second list, 0x3C-byte records)
// the list of its textures goes to the figure maker (+0x34, slot 3). Names provisional; see
// docs/notes/world.md.
#include "console.h"
#include "item_db.h"
#include "model.h"
#include "unit.h"
#include "unit_db.h"
#include "world.h"

#include "../lib/containers.h"
#include "../lib/crt.h"
#include "../lib/mfc.h"
#include "../lib/stlp.h"
#include "ei/hook.h"
#include "originals_gameplay.h"
#include "originals_library.h"

#include <cstdint>
#include <cstring>
#include <new>
#include "originals_render.h"
#include "originals_world.h"

// 005683D0 (__fastcall: CL `body`, EDX the dressed unit record; RET 4 the record list). Defined below;
// DressUnit calls it through its address so that its verify wrapper sees the calls.
void __fastcall UnitTextureRecords(char body, const void* record, void* list);
__declspec(naked) void __fastcall UnitTextureRecordsAt(char /*body*/, const void* /*record*/, void* /*list*/)
    EI_ORIGINAL(0x005683D0)

namespace {

const char* Text(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }

// --- Tables --------------------------------------------------------------------------------------

// The unit prototypes (+8 the model) and models (+0xE8 the texture name prefix), the armours (+0xB8
// the layer). An index past the end gives null.
const UnitPrototypeRecord* UnitPrototype(std::uint32_t index) {
    return reinterpret_cast<const UnitPrototypeRecord*>(unitdb::Prototypes().At(index, unitdb::kPrototypeSize));
}
const UnitModelRecord* UnitModel(std::uint32_t index) {
    return reinterpret_cast<const UnitModelRecord*>(unitdb::Models().At(index, unitdb::kModelSize));
}
const itemdb::ArmorRecord* Armour(std::uint32_t index) {
    return reinterpret_cast<const itemdb::ArmorRecord*>(itemdb::Armors().At(index, itemdb::kArmorSize));
}
const char* ModelPrefix(const UnitModelRecord* model) { return model->unknown_E8; }

constexpr std::uint32_t kNone = 0xFFFFFFFFu;

using IdList = stlp::vector<std::uint32_t>;

// A dressed unit of the zone (the first list, 0x24 bytes): its prototype, the item ids (armour
// type << 16 | material; kNone for none: [0] the helmet's row, [1] / [2] the weapons, [3..6] the
// armour) and the extra weapons, one of which (`extraWeapon`) is shown.
struct DressedUnitRecord {
    std::uint16_t prototype;        // +0x00
    std::uint16_t unknown_02;
    IdList items;                   // +0x04
    IdList extraWeapons;            // +0x10 weapon index << 16 | material index
    std::int8_t extraWeapon;        // +0x1C
    std::uint8_t unknown_1D[3];
    int modelNumber;                // +0x20 -1: the textures are named after the prototype
};
static_assert(offsetof(DressedUnitRecord, extraWeapons) == 0x10);
static_assert(sizeof(DressedUnitRecord) == 0x24);

// Another unit of the zone (the second list, 0x3C bytes): its prototype, armour and weapon ids, the
// figure's morph coefficients and the texture names (none: from the prototype, 00535CB0).
struct FiguredUnitRecord {
    std::uint16_t prototype;        // +0x00
    std::uint16_t unknown_02;
    IdList armourIds;               // +0x04
    IdList weaponIds;               // +0x10
    std::uint8_t unknown_1C[8];
    float morph[3];                 // +0x24
    stlp::vector<const char*> names;    // +0x30
};
static_assert(offsetof(FiguredUnitRecord, morph) == 0x24);
static_assert(sizeof(FiguredUnitRecord) == 0x3C);

// The byte distance between a vector's ends, as the original tests it (the count's bits, times 4).
template <typename T>
std::uintptr_t ByteSpan(stlp::vector<T>& list) {
    return reinterpret_cast<std::uintptr_t>(list.Finish()) - reinterpret_cast<std::uintptr_t>(list.Start());
}

// --- Forwarders ----------------------------------------------------------------------------------

// An item's texture name [006A3480: out, material index; armour index] and a weapon's [006A0BF0:
// out, weapon record; material record]; the weapon and material records [0056E440 on 007C86F8,
// 0056E400 on 007C86D8].
__declspec(naked) void __fastcall WeaponTextureName(mfc::CString*, const char*, const char*) EI_ORIGINAL(0x006A0BF0)
__declspec(naked) const char* __fastcall WeaponRecord(const RecordTable*, void*, std::uint32_t) EI_ORIGINAL(0x0056E440)
__declspec(naked) const char* __fastcall MaterialRecord(const RecordTable*, void*, std::uint32_t) EI_ORIGINAL(0x0056E400)
// The 12-byte record list of a unit's textures (filled by UnitTextureRecords, 005683D0): its
// constructor [0056F570, allocator] and destructor [0056F590].
__declspec(naked) void __fastcall RecordListConstruct(void*, void*, const void*) EI_ORIGINAL(0x0056F570)
__declspec(naked) void __fastcall RecordListDestroy(void*) EI_ORIGINAL(0x0056F590)
// The texture set's byte array: constructor [0056FCC0], destructor [0051E5B0], allocation [004ECB50].
__declspec(naked) void __fastcall BytesConstruct(void*) EI_ORIGINAL(0x0056FCC0)
__declspec(naked) void __fastcall BytesDestroy(void*) EI_ORIGINAL(0x0051E5B0)
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
__declspec(naked) void __fastcall PrototypeTextures(const UnitPrototypeRecord*, const IdList*, const IdList*, void*, void*)
    EI_ORIGINAL(0x00535CB0)
__declspec(naked) void __fastcall StringsConstruct(void*, void*, const void*) EI_ORIGINAL(0x0053BB80)
__declspec(naked) stlp::string* __fastcall StringsCopy(void*, void*, int, const stlp::string*, const stlp::string*)
    EI_ORIGINAL(0x0053D7F0)
__declspec(naked) void __fastcall StringDestroyFlag(void*, void*, int) EI_ORIGINAL(0x004DDC90)
void __fastcall StringsFree(stlp::string* a0, int a1) { NodeFree12Original(a0, static_cast<std::size_t>(a1)); }
__declspec(naked) void __fastcall StringsDeallocate(void*, void*, stlp::string*, int) EI_ORIGINAL(0x0053BF40)
__declspec(naked) void __fastcall StringCopyConstruct(stlp::string*, const void*) EI_ORIGINAL(0x004F5DF0)
void __fastcall StringsInsertOverflow(void* a0, void*, stlp::string* a2, const void* a3, int a4) { StringVectorInsertOverflowOriginal(a0, nullptr, a2, a3, static_cast<unsigned>(a4)); }
__declspec(naked) void __fastcall StringsInsertRange(void*, void*, stlp::string*, const stlp::string*, const stlp::string*, int)
    EI_ORIGINAL(0x00575E50)
inline char* __fastcall NodeAllocate(int a0) { return (char*)NodeAllocateOriginal((std::size_t)a0); }
// STLport string members (as in figure_part_draw.cpp).
inline void __fastcall StringInitRange(void* a0, void*, const char* a2, const char* a3, int a4) { StringInitRangeOriginal((void*)a0, nullptr, a2, a3, (const void*)a4); }
void __fastcall StringEndOfStorage(void* a0, void*, const void* a2, const void* a3) { StringEndOfStorageOriginal(a0, nullptr, a2, a3); }

const std::uint8_t kAllocator = 0;

// --- The dressed units ---------------------------------------------------------------------------

// The set handed to the texture cache (0x28 bytes): the texture records as raw bytes (an aligned
// array), the texture file names, the composite's size.
struct TextureSet {
    lib::AlignedArray<char> bytes;
    console::CStringArray names;
    int size;
};
static_assert(sizeof(TextureSet) == 0x28 && offsetof(TextureSet, size) == 0x24);

// The vector 005683D0 fills: 12-byte texture records (copied into the set as bytes): a kind (0x32
// the body, 0x3005 an armour piece, 0x3004 a weapon), the item id and the item record's type.
struct TextureRecord {
    std::uint32_t kind;
    std::uint32_t id;
    std::uint32_t itemType;   // the item record's type (+0x0C)
};
static_assert(sizeof(TextureRecord) == 12);
using RecordList = stlp::vector<TextureRecord>;

// The texture records copied into the set's byte array (SetSize inlined: kept contents, 0 frees).
void CopyRecords(TextureSet& set, const RecordList& list) {
    int size = static_cast<int>(list.size()) * static_cast<int>(sizeof(TextureRecord));
    set.bytes.SetSize(size);
    std::memcpy(set.bytes.data, list.begin(), static_cast<std::size_t>(size));
}

// The original's inlined strcpy / strcat: the bytes up to and including the terminator.
void CopyText(char* to, const char* from) { std::memcpy(to, from, std::strlen(from) + 1); }
void AppendText(char* to, const char* from) { CopyText(to + std::strlen(to), from); }

// "<model prefix><name>.mmp" into `buffer`.
void AppendName(const UnitModelRecord* model, const mfc::CString& name, char* buffer) {
    CopyText(buffer, ModelPrefix(model));
    AppendText(buffer, name);
    AppendText(buffer, Text(0x0078FB4C));   // ".mmp"
}

void AddItem(TextureSet& set, const UnitModelRecord* model, std::uint32_t id) {
    char buffer[0x100];
    {
        mfc::CString name;
        ArmourTexture(&name, id >> 16, id & 0xFFFF);
        AppendName(model, name, buffer);
    }
    NamesSetAtGrowText(&set.names, nullptr, set.names.GetSize(), buffer);
}

// The weapons (items 1 and 2): the one whose armour layer (+0xB8) is lower first when both exist.
void AddWeapons(TextureSet& set, const UnitModelRecord* model, const std::uint32_t* items) {
    if (items[1] != kNone && items[2] != kNone) {
        const itemdb::ArmorRecord* second = Armour(items[2] >> 16);
        const itemdb::ArmorRecord* first = Armour(items[1] >> 16);
        if (second->layer < first->layer) {
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
void AddExtraWeapon(TextureSet& set, const UnitModelRecord* model, DressedUnitRecord* record) {
    const std::uint32_t* list = record->extraWeapons.Start();
    int count = static_cast<int>(record->extraWeapons.Finish() - list);
    int index = record->extraWeapon;
    if (!(static_cast<std::uint32_t>(count) > static_cast<std::uint32_t>(index))) return;
    std::uint32_t id = list[index];
    if (id == kNone) return;
    char buffer[0x100];
    {
        const char* weapon = WeaponRecord(&itemdb::Weapons(), nullptr, id >> 16);
        mfc::CString name;
        const char* material = MaterialRecord(&itemdb::Materials(), nullptr, id & 0xFFFF);
        WeaponTextureName(&name, weapon, material);
        AppendName(model, name, buffer);
    }
    NamesSetAtGrowText(&set.names, nullptr, set.names.GetSize(), buffer);
}

// The texture cache is the client world's texture manager (+0x18C).
void Compose(CWorldClient* self, mfc::CString& out, TextureSet& set, const mfc::CString& name, int size, int kind) {
    set.size = size;
    CacheBuild(self->textureManager_, nullptr, &out, &set, &name, &name, 3, 0, 0x5551);
    CacheRegister(self->textureManager_, nullptr, kind, name);
}

void DressUnit(CWorldClient* self, DressedUnitRecord* record) {
    const UnitPrototypeRecord* prototype = UnitPrototype(record->prototype);
    const UnitModelRecord* model = UnitModel(static_cast<std::uint32_t>(prototype->modelIndex));
    alignas(4) char listStorage[sizeof(RecordList)];
    RecordListConstruct(listStorage, nullptr, &kAllocator);
    RecordList& list = *reinterpret_cast<RecordList*>(listStorage);
    alignas(4) char setStorage[sizeof(TextureSet)];
    TextureSet& set = *reinterpret_cast<TextureSet*>(setStorage);
    BytesConstruct(&set);
    new (&set.names) console::CStringArray;

    const std::uint32_t* items = record->items.Start();
    if (ByteSpan(record->items) & ~std::uintptr_t{3}) {
        mfc::CString built;
        UnitTextureRecordsAt(1, record, &list);
        CopyRecords(set, list);
        mfc::CString skin;
        StringArraySetSize(&set.names, nullptr, 0, -1);
        if (prototype) {
            skin.Format(Text(0x0078FB64), ModelPrefix(model), prototype->skin);   // "%sskin_%.2d.mmp"
        } else {
            skin.Format(Text(0x0078FB54), ModelPrefix(model));                             // "%sskin_00.mmp"
        }
        NamesSetAtGrowString(&set.names, nullptr, set.names.GetSize(), &skin);
        for (int slot = 3; slot <= 6; ++slot) {
            if (items[slot] != kNone) AddItem(set, model, items[slot]);
        }
        AddWeapons(set, model, items);

        mfc::CString name;
        if (record->modelNumber != -1) {
            name.Format(Text(0x0078FB7C), record->modelNumber);                     // "mo%dp"
        } else {
            name.Format(Text(0x0078FB74), static_cast<int>(record->prototype));     // "prot%dp"
        }
        Compose(self, built, set, name, 0x100, 1);

        UnitTextureRecordsAt(0, record, &list);
        CopyRecords(set, list);
        StringArraySetSize(&set.names, nullptr, 0, -1);
        if (items[0] != kNone) AddItem(set, model, items[0]);
        AddExtraWeapon(set, model, record);
        if (record->modelNumber != -1) {
            name.Format(Text(0x0078FB44), record->modelNumber);                     // "mo%ds"
        } else {
            name.Format(Text(0x0078FB3C), static_cast<int>(record->prototype));     // "prot%ds"
        }
        Compose(self, built, set, name, 0x80, 2);
    }

    set.names.~CStringArray();
    BytesDestroy(&set);
    RecordListDestroy(listStorage);
}

// --- The other units -----------------------------------------------------------------------------

using StringList = stlp::vector<stlp::string>;
int Capacity(StringList& list) { return static_cast<int>(list.EndOfStorage() - list.Start()); }

// Room for `needed` strings as inlined (reserve): a copy into new storage, the old strings destroyed
// and freed.
void Reserve(StringList& list, int needed) {
    int size = static_cast<int>(list.size());
    if (!(static_cast<std::uint32_t>(Capacity(list)) < static_cast<std::uint32_t>(needed))) return;
    stlp::string* storage;
    if (list.Start()) {
        storage = StringsCopy(&list, nullptr, needed, list.Start(), list.Finish());
        for (stlp::string* s = list.Start(); s != list.Finish(); ++s) StringDestroyFlag(s, nullptr, 0);
        StringsFree(list.Start(), Capacity(list));
    } else {
        storage = needed ? reinterpret_cast<stlp::string*>(NodeAllocate(needed * static_cast<int>(sizeof(stlp::string)))) : nullptr;
    }
    list.Start() = storage;
    list.Finish() = storage + size;
    list.EndOfStorage() = storage + needed;
}

void FigureUnit(CWorldClient* self, FiguredUnitRecord* record) {
    const UnitPrototypeRecord* prototype = UnitPrototype(record->prototype);
    const UnitModelRecord* model = UnitModel(static_cast<std::uint32_t>(prototype->modelIndex));
    alignas(4) char firstStorage[sizeof(StringList)];
    alignas(4) char secondStorage[sizeof(StringList)];
    StringsConstruct(firstStorage, nullptr, &kAllocator);
    StringsConstruct(secondStorage, nullptr, &kAllocator);
    StringList& first = *reinterpret_cast<StringList*>(firstStorage);
    StringList& second = *reinterpret_cast<StringList*>(secondStorage);

    const char* const* names = record->names.Start();
    if (!(ByteSpan(record->names) & ~std::uintptr_t{3})) {
        PrototypeTextures(prototype, &record->armourIds, &record->weaponIds, &first, &second);
    } else {
        int count = static_cast<int>(record->names.Finish() - names);
        Reserve(first, static_cast<int>(first.size()) + count);
        for (const char* const* p = record->names.Start(); p != record->names.Finish(); ++p) {
            const char* text = *p;
            alignas(4) char string[12];
            StringInitWithAllocator(string, nullptr, &kAllocator);
            StringInitRange(string, nullptr, text, text + std::strlen(text), 0);
            if (first.Finish() != first.EndOfStorage()) {
                StringCopyConstruct(first.Finish(), string);
                ++first.Finish();
            } else {
                StringsInsertOverflow(&first, nullptr, first.Finish(), string, 1);
            }
            StringBufferFree(string);
        }
    }
    StringsInsertRange(&first, nullptr, first.Finish(), second.Start(), second.Finish(), 0);

    alignas(4) char name[12];
    std::memset(name, 0, 2 * sizeof(char*));   // start and finish
    int zero = 0;
    StringEndOfStorage(name + 8, nullptr, &kAllocator, &zero);
    const char* prefix = ModelPrefix(model);
    StringAssignSpan(name, nullptr, prefix, prefix + std::strlen(prefix));
    // The figure loader's slot 3 (LoadFigure; not fresh).
    self->figureLoader_->LoadFigure(reinterpret_cast<const stlp::string*>(name), &first, record->morph[0], record->morph[1],
                                    record->morph[2], false);
    StringBufferFree(name);

    for (stlp::string* s = second.Start(); s != second.Finish(); ++s) StlStringDestroy(s);
    StringsDeallocate(&second.EndOfStorage(), nullptr, second.Start(), Capacity(second));
    for (stlp::string* s = first.Start(); s != first.Finish(); ++s) StringDestroyFlag(s, nullptr, 0);
    StringsDeallocate(&first.EndOfStorage(), nullptr, first.Start(), Capacity(first));
}

} // namespace

// 00568800 (thiscall, RET 8). `others` and `dressed` are vectors of unit records (0x3C / 0x24
// bytes); the dressed units (the second argument) come first.
void __fastcall BuildUnitSkins(CWorldClient* self, void*, const void* othersVector, const void* dressedVector) {
    auto* dressed = static_cast<stlp::vector<DressedUnitRecord>*>(const_cast<void*>(dressedVector));
    for (DressedUnitRecord* r = dressed->Start(); r != dressed->Finish(); ++r) DressUnit(self, r);
    auto* others = static_cast<stlp::vector<FiguredUnitRecord>*>(const_cast<void*>(othersVector));
    for (FiguredUnitRecord* r = others->Start(); r != others->Finish(); ++r) FigureUnit(self, r);
}
EI_REPLACE(0x00568800, BuildUnitSkins);

// The list is emptied; nothing more for a unit without items. The body list has eight records:
// [0] the body (0x32, the prototype), [1] empty (0, -1, -1), [2..7] the armour pieces of
// items[1..6] (0x3005, the armour's type; item i goes to record i + 1, 00568441). The head list two: [0] the helmet row items[0] (0x3005)
// and [1] the shown extra weapon (0x3004, the weapon's type) when its index is within the list.
// items[1..6] are read whatever the item list's size; an id past its table reads through a null
// record as the original does (it faults).
void __fastcall UnitTextureRecords(char body, const void* recordPointer, void* listPointer) {
    constexpr std::uint32_t kBody = 0x32, kArmourPiece = 0x3005, kWeapon = 0x3004;
    auto* record = static_cast<const DressedUnitRecord*>(recordPointer);
    auto* list = static_cast<RecordList*>(listPointer);
    list->clear();   // 0056F2F0 erase(begin, end)
    auto& items = const_cast<IdList&>(record->items);
    if ((ByteSpan(items) & ~std::uintptr_t{3}) == 0) return;
    const TextureRecord empty = {0, kNone, kNone};
    const std::uint32_t* ids = items.Start();
    if (body) {
        TextureRecordsResizeOriginal(list, nullptr, 8, &empty);
        TextureRecord* out = list->Start();
        out[0].id = record->prototype;
        out[0].kind = kBody;
        out[0].itemType = kNone;
        for (int i = 1; i < 7; ++i) {
            if (ids[i] == kNone) continue;
            out[i + 1].id = ids[i];
            out[i + 1].kind = kArmourPiece;
            out[i + 1].itemType = static_cast<std::uint32_t>(Armour(ids[i] >> 16)->type);
        }
        return;
    }
    TextureRecordsResizeOriginal(list, nullptr, 2, &empty);
    TextureRecord* out = list->Start();
    if (ids[0] != kNone) {
        out[0].id = ids[0];
        out[0].kind = kArmourPiece;
        out[0].itemType = static_cast<std::uint32_t>(Armour(ids[0] >> 16)->type);
    }
    auto& extra = const_cast<IdList&>(record->extraWeapons);
    const auto count = static_cast<std::uint32_t>(ByteSpan(extra) >> 2);
    const auto index = static_cast<std::uint32_t>(static_cast<int>(record->extraWeapon));
    if (index >= count) return;
    const std::uint32_t weapon = extra.Start()[index];
    if (weapon == kNone) return;
    out[1].id = weapon;
    out[1].kind = kWeapon;
    out[1].itemType = static_cast<std::uint32_t>(
        reinterpret_cast<const itemdb::WeaponRecord*>(itemdb::Weapons().At(weapon >> 16, itemdb::kWeaponSize))->type);
}
EI_REPLACE(0x005683D0, UnitTextureRecords);

// 005685C0 (A-W14). The constructor of a texture record: no kind, no id, no type (`empty` above,
// which the original builds through it). Returns the record.
void* __fastcall TextureRecordConstruct(void* self) {
    *static_cast<TextureRecord*>(self) = {0, kNone, kNone};
    return self;
}
EI_REPLACE(0x005685C0, TextureRecordConstruct);
