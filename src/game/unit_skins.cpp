// The zone's unit skins and figures (thiscall on the zone's client object, called by the zone load): for
// every dressed unit (first list, 0x24-byte records) two composite textures are built through the texture
// cache: the body skin ("<model>skin_NN.mmp" and the armour pieces' "<model><item>.mmp"),
// registered as "mo<n>p" / "prot<n>p", then the head and weapon textures ("mo<n>s" / "prot<n>s"); for every
// other unit (second list, 0x3C-byte records) the list of its textures goes to the figure maker.
// Names provisional; see docs/notes/world.md.
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
#include "lib/static_object.h"
#include "lib/crt.h"
#include "lib/stlp_string.h"
#include "unit_looks.h"
#include "lib/str.h"

#include <cstdint>
#include <cstring>
#include <new>
#include <string>

namespace {

// --- Tables --------------------------------------------------------------------------------------

// The unit prototypes (+8 the model) and models (+0xE8 the texture name prefix), the armours (+0xB8
// the layer). An index past the end gives null.
const UnitPrototypeRecord* UnitPrototype(std::uint32_t index) {
    return unitdb::PrototypeRecord(index);
}
const UnitModelRecord* UnitModel(std::uint32_t index) {
    return unitdb::ModelRecord(index);
}
const itemdb::ArmorRecord* Armour(std::uint32_t index) {
    return itemdb::ArmorAt(index);
}
const char* ModelPrefix(const UnitModelRecord* model) { return model->figureName; }

constexpr std::uint32_t kNone = 0xFFFFFFFFu;

using IdList = UnitIdList;

// The byte distance between a vector's ends, as tested in the game (the count's bits, times 4).
template <typename T>
std::uintptr_t ByteSpan(stlp::vector<T>& list) {
    return static_cast<std::uintptr_t>(list.Finish() - list.Start()) * sizeof(T);
}

// --- Helpers -------------------------------------------------------------------------------------

// An item's texture name (an armour's: out, material index, armour index; a weapon's: out, weapon record, material record);
// the 12-byte record list of a unit's textures and its allocator; the texture set's byte array; the texture cache: build
// a composite texture (CTextureManager::Compose) and register it; the unit's texture names from its prototype; the
// vector<string> members the texture lists use.
void __fastcall StringsFree(stlp::string* a0, int a1) { stlp::deallocate(a0, static_cast<std::size_t>(a1) * 12); }

// --- The dressed units ---------------------------------------------------------------------------

// What the texture cache composes a skin from: SkinRequest (unit_client_models.h), the texture
// records as its layer bytes, the texture file names, the composite's size as its flags.
using SkinComposition = SkinRequest;

// The vector UnitTextureRecords fills: 12-byte texture records (copied into the set as bytes): a kind (0x32
// the body, 0x3005 an armour piece, 0x3004 a weapon), the item id and the item record's type.
struct TextureRecord {
    std::uint32_t kind;
    std::uint32_t id;
    std::uint32_t itemType;   // the item record's type
};
static_assert(sizeof(TextureRecord) == 12);
using RecordList = stlp::vector<TextureRecord>;

} // namespace

// UnitTextureRecords (`body`, the dressed unit record; returns the record list). Defined below.
void __fastcall UnitTextureRecords(char body, const DressedUnitRecord* record, RecordList* list);

namespace {

// The texture records copied into the set's byte array (SetSize inlined: kept contents, 0 frees).
void CopyRecords(SkinComposition& set, const RecordList& list) {
    int size = static_cast<int>(list.size()) * static_cast<int>(sizeof(TextureRecord));
    set.layers.SetSize(size);
    std::memcpy(set.layers.data, list.begin(), static_cast<std::size_t>(size));
}

// Inlined strcpy / strcat: the bytes up to and including the terminator.
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
    set.textures.SetAtGrow(set.textures.GetSize(), buffer);
}

// The weapons (items 1 and 2): the one whose armour layer is lower first when both exist.
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

// The extra weapon picked by the record's index byte in its list: weapon and
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
        const itemdb::WeaponRecord* weapon = itemdb::WeaponAt(id >> 16);
        mfc::CString name;
        const itemdb::MaterialRecordHead* material = itemdb::MaterialAt(id & 0xFFFF);
        WeaponRecordModelName(&name, weapon, material);
        AppendName(model, name, buffer);
    }
    set.textures.SetAtGrow(set.textures.GetSize(), buffer);
}

// The texture cache is the client world's texture manager.
void Compose(CWorldClient* self, mfc::CString& out, SkinComposition& set, const mfc::CString& name, int size, int kind) {
    set.flags = size;
    self->textureManager_->Compose(&out, &set, &name, &name, 3, 0, 0x5551);
    self->textureManager_->FindOrLoad(kind, name);
}

void DressUnit(CWorldClient* self, DressedUnitRecord* record) {
    const UnitPrototypeRecord* prototype = UnitPrototype(record->prototype);
    const UnitModelRecord* model = UnitModel(static_cast<std::uint32_t>(prototype->modelIndex));
    lib::InPlace<RecordList> listStorage;
    RecordList& list = *listStorage.Get();
    list.ConstructEmpty();
    lib::InPlace<SkinComposition> setStorage;
    SkinComposition& set = *setStorage.Get();
    set.layers.Construct();
    new (&set.textures) console::CStringArray;

    const std::uint32_t* items = record->items.Start();
    if (ByteSpan(record->items) & ~std::uintptr_t{3}) {
        mfc::CString built;
        UnitTextureRecords(1, record, &list);
        CopyRecords(set, list);
        std::string skin;
        set.textures.SetSize(0, -1);
        if (prototype) {
            skin = str::Format("%sskin_%.2d.mmp", ModelPrefix(model), prototype->skin);
        } else {
            skin = str::Format("%sskin_00.mmp", ModelPrefix(model));
        }
        set.textures.SetAtGrow(set.textures.GetSize(), skin);
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
        set.textures.SetSize(0, -1);
        if (items[0] != kNone) AddItem(set, model, items[0]);
        AddExtraWeapon(set, model, record);
        if (record->modelNumber != -1) {
            name.Format("mo%ds", record->modelNumber);
        } else {
            name.Format("prot%ds", static_cast<int>(record->prototype));
        }
        Compose(self, built, set, name, 0x80, 2);
    }

    set.textures.~CStringArray();
    set.layers.FreeData();
    list.FreeStorage();
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
        for (stlp::string* s = list.Start(); s != list.Finish(); ++s) s->~string();
        StringsFree(list.Start(), Capacity(list));
    } else {
        storage = needed ? stlp::allocate_as<stlp::string>(static_cast<std::size_t>(needed)) : nullptr;
    }
    list.Start() = storage;
    list.Finish() = storage + size;
    list.EndOfStorage() = storage + needed;
}

void FigureUnit(CWorldClient* self, FiguredUnitRecord* record) {
    const UnitPrototypeRecord* prototype = UnitPrototype(record->prototype);
    const UnitModelRecord* model = UnitModel(static_cast<std::uint32_t>(prototype->modelIndex));
    lib::InPlace<StringList> firstStorage;
    lib::InPlace<StringList> secondStorage;
    StringList& first = *firstStorage.Get();
    StringList& second = *secondStorage.Get();
    first.ConstructEmpty();
    second.ConstructEmpty();

    const mfc::CString* names = record->names.Start();
    if (!(ByteSpan(record->names) & ~std::uintptr_t{3})) {
        CollectArmourPartNames(prototype, &record->items, &record->extraWeapons, &first,
                               &second);
    } else {
        int count = static_cast<int>(record->names.Finish() - names);
        Reserve(first, static_cast<int>(first.size()) + count);
        for (const mfc::CString* p = record->names.Start(); p != record->names.Finish(); ++p) {
            stlp::string string(p->GetString());
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
        // The figure loader's LoadFigure (not fresh).
        self->figureLoader_->LoadFigure(&name, &first, record->morphS, record->morphT, record->morphU, false);
    }

    for (stlp::string* s = second.Start(); s != second.Finish(); ++s) s->~string();
    stlp::deallocate(second.Start(), static_cast<std::size_t>(Capacity(second)) * 12);
    for (stlp::string* s = first.Start(); s != first.Finish(); ++s) s->~string();
    stlp::deallocate(first.Start(), static_cast<std::size_t>(Capacity(first)) * 12);
}

} // namespace

// `others` and `dressed` are vectors of unit records (0x3C / 0x24 bytes); the dressed units (the second
// argument) come first.
void CWorldClient::BuildUnitSkins(stlp::vector<FiguredUnitRecord>* others, stlp::vector<DressedUnitRecord>* dressed) {
    for (DressedUnitRecord* r = dressed->Start(); r != dressed->Finish(); ++r) DressUnit(this, r);
    for (FiguredUnitRecord* r = others->Start(); r != others->Finish(); ++r) FigureUnit(this, r);
}
EI_REWRITTEN(0x00568800, CWorldClient::BuildUnitSkins);

// The list is emptied; nothing more for a unit without items. The body list has eight records: [0] the body
// (0x32, the prototype), [1] empty (0, -1, -1), [2..7] the armour pieces of items[1..6] (0x3005, the
// armour's type; item i goes to record i + 1). The head list two: [0] the helmet row items[0] (0x3005) and
// [1] the shown extra weapon (0x3004, the weapon's type) when its index is within the list. items[1..6] are
// read whatever the item list's size; an id past its table reads through a null record (it faults).
void __fastcall UnitTextureRecords(char body, const DressedUnitRecord* record, RecordList* list) {
    constexpr std::uint32_t kBody = 0x32, kArmourPiece = 0x3005, kWeapon = 0x3004;
    list->clear();   // erase(begin, end)
    auto& items = const_cast<IdList&>(record->items);
    if ((ByteSpan(items) & ~std::uintptr_t{3}) == 0) return;
    const TextureRecord empty = {0, kNone, kNone};
    const std::uint32_t* ids = items.Start();
    if (body) {
        list->Resize<stlp::PlainElement<TextureRecord>>(8, empty);
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
        itemdb::WeaponAt(weapon >> 16)->type);
}
EI_REWRITTEN(0x005683D0, UnitTextureRecords);

