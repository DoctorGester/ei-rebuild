// The zone's unit skins and figures (00568800, thiscall on the zone's client object, called by the
// zone load 00567AB0): for every dressed unit (first list, 0x24-byte records) two composite
// textures are built through the texture cache (+0x18C, 00693180): the body skin ("<model>skin_NN.mmp"
// and the armour pieces' "<model><item>.mmp"), registered as "mo<n>p" / "prot<n>p", then the head
// and weapon textures ("mo<n>s" / "prot<n>s"); for every other unit (second list, 0x3C-byte records)
// the list of its textures goes to the figure maker (+0x34, slot 3). Names provisional; see
// docs/notes/world.md.
#include "console.h"
#include "game_strings.h"
#include "item.h"
#include "item_db.h"
#include "model.h"
#include "texture.h"
#include "unit.h"
#include "unit_db.h"
#include "world.h"

#include "../lib/containers.h"
#include "../lib/crt.h"
#include "unit_client_models.h"
#include "../lib/mfc.h"
#include "../lib/stlp.h"
#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/stlp_string.h"
#include "originals_units.h"
#include "originals_gameplay.h"
#include "unit_looks.h"
#include "originals_library.h"

#include <cstdint>
#include <cstring>
#include <new>
#include "originals_render.h"
#include "originals_world.h"

// 005683D0 (__fastcall: CL `body`, EDX the dressed unit record; RET 4 the record list). Defined below;
// DressUnit calls it through its address so that its verify wrapper sees the calls.
void __fastcall UnitTextureRecords(char body, const void* record, void* list);

namespace {


// --- Tables --------------------------------------------------------------------------------------

// The unit prototypes (+8 the model) and models (+0xE8 the texture name prefix), the armours (+0xB8
// the layer). An index past the end gives null.
const UnitPrototypeRecord* UnitPrototype(std::uint32_t index) {
    return reinterpret_cast<const UnitPrototypeRecord*>(unitdb::Prototypes().RecordAt(index, unitdb::kPrototypeSize));
}
const UnitModelRecord* UnitModel(std::uint32_t index) {
    return reinterpret_cast<const UnitModelRecord*>(unitdb::Models().RecordAt(index, unitdb::kModelSize));
}
const itemdb::ArmorRecord* Armour(std::uint32_t index) {
    return reinterpret_cast<const itemdb::ArmorRecord*>(itemdb::Armors().RecordAt(index, itemdb::kArmorSize));
}
const char* ModelPrefix(const UnitModelRecord* model) { return model->figureName; }

constexpr std::uint32_t kNone = 0xFFFFFFFFu;

using IdList = UnitIdList;

// The byte distance between a vector's ends, as the original tests it (the count's bits, times 4).
template <typename T>
std::uintptr_t ByteSpan(stlp::vector<T>& list) {
    return reinterpret_cast<std::uintptr_t>(list.Finish()) - reinterpret_cast<std::uintptr_t>(list.Start());
}

// --- Forwarders ----------------------------------------------------------------------------------

// An item's texture name [006A3480: out, material index; armour index] and a weapon's [006A0BF0:
// out, weapon record; material record]; the weapon and material records are RecordAtB4 (0056E440,
// on 007C86F8) and RecordAt50 (0056E400, on 007C86D8; world.h), on the tables' vectors.
const RecordVector* Records(const RecordTable* table) { return reinterpret_cast<const RecordVector*>(table); }
// The 12-byte record list of a unit's textures (filled by UnitTextureRecords, 005683D0): its
// constructor [0056F570, allocator] and destructor [0056F590].
// The texture set's byte array: destructor [0051E5B0], allocation [004ECB50].
// The texture cache [this +0x18C]: build a composite texture [00693180 ComposeTexture: out name, set,
// name, name, 3, 0, 0x5551] and register it [00693F10: kind, name].
// The second list: the unit's texture names from its prototype [00535CB0: unit in ECX, record +4 in
// EDX; record +0x10, names, more names]; vector<string> members: constructor [0053BB80], copy into
// new storage [0053D7F0: count, first, last], element destructors [004DDC90 with 0, 0040C720],
// storage free [00426860: ECX block, EDX count; 0053BF40: block, count], push_back's copy
// [004F5DF0: ECX place, EDX string] and overflow [004DCE90: position, string, count], range insert
// [00575E50: position, first, last, tag]; the node allocator [00405950: ECX bytes].
void __fastcall StringsFree(stlp::string* a0, int a1) { stlp::deallocate(a0, static_cast<std::size_t>(a1) * 12); }
inline char* __fastcall NodeAllocate(int a0) { return (char*)libcrt::NodeAllocate((std::size_t)a0); }


// --- The dressed units ---------------------------------------------------------------------------

// What the texture cache composes a skin from (0x28 bytes): the texture records as raw bytes
// (an aligned array), the texture file names, the composite's size.
struct SkinComposition {
    lib::AlignedArray<char> bytes;
    console::CStringArray names;
    int size;
};
static_assert(sizeof(SkinComposition) == 0x28 && offsetof(SkinComposition, size) == 0x24);

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
void CopyRecords(SkinComposition& set, const RecordList& list) {
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
    AppendText(buffer, ".mmp");
}

void AddItem(SkinComposition& set, const UnitModelRecord* model, std::uint32_t id) {
    char buffer[0x100];
    {
        mfc::CString name;
        ArmorModelName(&name, id >> 16, id & 0xFFFF);
        AppendName(model, name, buffer);
    }
    set.names.SetAtGrow(set.names.GetSize(), buffer);
}

// The weapons (items 1 and 2): the one whose armour layer (+0xB8) is lower first when both exist.
void AddWeapons(SkinComposition& set, const UnitModelRecord* model, const std::uint32_t* items) {
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
void AddExtraWeapon(SkinComposition& set, const UnitModelRecord* model, DressedUnitRecord* record) {
    const std::uint32_t* list = record->extraWeapons.Start();
    int count = static_cast<int>(record->extraWeapons.Finish() - list);
    int index = record->extraWeapon;
    if (!(static_cast<std::uint32_t>(count) > static_cast<std::uint32_t>(index))) return;
    std::uint32_t id = list[index];
    if (id == kNone) return;
    char buffer[0x100];
    {
        const std::uint8_t* weapon = Records(&itemdb::Weapons())->AtB4(id >> 16);
        mfc::CString name;
        const std::uint8_t* material = Records(&itemdb::Materials())->At50(id & 0xFFFF);
        WeaponRecordModelName(&name, reinterpret_cast<const itemdb::WeaponRecord*>(weapon),   // 006A0BF0
                              reinterpret_cast<const itemdb::MaterialRecordHead*>(material));
        AppendName(model, name, buffer);
    }
    set.names.SetAtGrow(set.names.GetSize(), buffer);
}

// The texture cache is the client world's texture manager (+0x18C).
void Compose(CWorldClient* self, mfc::CString& out, SkinComposition& set, const mfc::CString& name, int size, int kind) {
    set.size = size;
    // (The set has the request's layout: the bytes as an aligned array, the names, the size.)
    ComposeTexture(self->textureManager_, nullptr, &out, reinterpret_cast<const SkinRequest*>(&set), &name, &name, 3, 0, 0x5551);
    self->textureManager_->FindOrLoad(kind, name);
}

void DressUnit(CWorldClient* self, DressedUnitRecord* record) {
    const UnitPrototypeRecord* prototype = UnitPrototype(record->prototype);
    const UnitModelRecord* model = UnitModel(static_cast<std::uint32_t>(prototype->modelIndex));
    alignas(4) char listStorage[sizeof(RecordList)];
    RecordList& list = *reinterpret_cast<RecordList*>(listStorage);
    list.ConstructEmpty();   // 0056F570
    alignas(4) char setStorage[sizeof(SkinComposition)];
    SkinComposition& set = *reinterpret_cast<SkinComposition*>(setStorage);
    set.bytes.Construct();
    new (&set.names) console::CStringArray;

    const std::uint32_t* items = record->items.Start();
    if (ByteSpan(record->items) & ~std::uintptr_t{3}) {
        mfc::CString built;
        UnitTextureRecords(1, record, &list);
        CopyRecords(set, list);
        mfc::CString skin;
        set.names.SetSize(0, -1);
        if (prototype) {
            skin.Format("%sskin_%.2d.mmp", ModelPrefix(model), prototype->skin);
        } else {
            skin.Format("%sskin_00.mmp", ModelPrefix(model));
        }
        set.names.SetAtGrow(set.names.GetSize(), skin);
        for (int slot = 3; slot <= 6; ++slot) {
            if (items[slot] != kNone) AddItem(set, model, items[slot]);
        }
        AddWeapons(set, model, items);

        mfc::CString name;
        if (record->modelNumber != -1) {
            name.Format("mo%dp", record->modelNumber);
        } else {
            name.Format("prot%dp", static_cast<int>(record->prototype));
        }
        Compose(self, built, set, name, 0x100, 1);

        UnitTextureRecords(0, record, &list);
        CopyRecords(set, list);
        set.names.SetSize(0, -1);
        if (items[0] != kNone) AddItem(set, model, items[0]);
        AddExtraWeapon(set, model, record);
        if (record->modelNumber != -1) {
            name.Format("mo%ds", record->modelNumber);
        } else {
            name.Format("prot%ds", static_cast<int>(record->prototype));
        }
        Compose(self, built, set, name, 0x80, 2);
    }

    set.names.~CStringArray();
    set.bytes.FreeData();   // 0051E5B0
    list.FreeStorage();   // 0056F590
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
        storage = StringList::AllocateAndCopy<stlp::StringElement>(static_cast<std::size_t>(needed), list.Start(), list.Finish());
        for (stlp::string* s = list.Start(); s != list.Finish(); ++s) stlp::StringAbi::DeletingDestructor(s, nullptr, 0);
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
    StringList& first = *reinterpret_cast<StringList*>(firstStorage);
    StringList& second = *reinterpret_cast<StringList*>(secondStorage);
    first.ConstructEmpty();   // 0053BB80
    second.ConstructEmpty();

    const char* const* names = record->names.Start();
    if (!(ByteSpan(record->names) & ~std::uintptr_t{3})) {
        CollectArmourPartNames(reinterpret_cast<const char*>(prototype), &record->armourIds, &record->weaponIds, &first,
                               &second);   // 00535CB0
    } else {
        int count = static_cast<int>(record->names.Finish() - names);
        Reserve(first, static_cast<int>(first.size()) + count);
        for (const char* const* p = record->names.Start(); p != record->names.Finish(); ++p) {
            stlp::string string(*p);
            if (first.Finish() != first.EndOfStorage()) {
                stlp::StringAbi::ConstructAt(first.Finish(), &string);
                ++first.Finish();
            } else {
                first.InsertOverflow<stlp::StringElement>(first.Finish(), string, 1);
            }
        }
    }
    first.RangeInsert<stlp::StringElement>(first.Finish(), second.Start(), second.Finish());

    {
        stlp::string name(ModelPrefix(model));
        // The figure loader's slot 3 (LoadFigure; not fresh).
        self->figureLoader_->LoadFigure(&name, &first, record->morphS, record->morphT, record->morphU, false);
    }

    for (stlp::string* s = second.Start(); s != second.Finish(); ++s) s->~string();
    stlp::deallocate(second.Start(), static_cast<std::size_t>(Capacity(second)) * 12);   // 0053BF40
    for (stlp::string* s = first.Start(); s != first.Finish(); ++s) stlp::StringAbi::DeletingDestructor(s, nullptr, 0);
    stlp::deallocate(first.Start(), static_cast<std::size_t>(Capacity(first)) * 12);   // 0053BF40
}

} // namespace

// 00568800 (thiscall, RET 8). `others` and `dressed` are vectors of unit records (0x3C / 0x24
// bytes); the dressed units (the second argument) come first.
void CWorldClient::BuildUnitSkins(const void* othersVector, const void* dressedVector) {
    auto* dressed = static_cast<stlp::vector<DressedUnitRecord>*>(const_cast<void*>(dressedVector));
    for (DressedUnitRecord* r = dressed->Start(); r != dressed->Finish(); ++r) DressUnit(this, r);
    auto* others = static_cast<stlp::vector<FiguredUnitRecord>*>(const_cast<void*>(othersVector));
    for (FiguredUnitRecord* r = others->Start(); r != others->Finish(); ++r) FigureUnit(this, r);
}
EI_REPLACE_METHOD(0x00568800, CWorldClient::BuildUnitSkins);

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
        list->Resize<stlp::PlainElement<TextureRecord>>(8, empty);   // 0056F360
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
    list->Resize<stlp::PlainElement<TextureRecord>>(2, empty);
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
        reinterpret_cast<const itemdb::WeaponRecord*>(itemdb::Weapons().RecordAt(weapon >> 16, itemdb::kWeaponSize))->type);
}
EI_REPLACE(0x005683D0, UnitTextureRecords);

// 005685C0 (A-W14). The constructor of a texture record: no kind, no id, no type (`empty` above,
// which the original builds through it). Returns the record.
void* __fastcall TextureRecordConstruct(void* self) {
    *static_cast<TextureRecord*>(self) = {0, kNone, kNone};
    return self;
}
EI_REPLACE(0x005685C0, TextureRecordConstruct);
