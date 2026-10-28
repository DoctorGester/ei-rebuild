// The zone's unit skins and figures (the zone load): for every dressed unit (first list) two composite
// textures are built through the texture cache: the body skin ("<model>skin_NN.mmp" and the armour pieces'
// "<model><item>.mmp"), registered as "mo<n>p" / "prot<n>p", then the head and weapon textures ("mo<n>s" /
// "prot<n>s"); for every other unit (second list) the list of its textures goes to the figure maker. See
// docs/notes/world.md.
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
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include "lib/static_object.h"
#include "unit_looks.h"
#include "lib/str.h"

#include <memory>
#include <new>
#include <vector>

namespace {

// --- Tables --------------------------------------------------------------------------------------

// The unit prototypes, models and armours by index; an index past the end gives null.
const UnitPrototypeRecord* UnitPrototype(std::uint32_t index) {
    return unitdb::PrototypeRecord(index);
}
const UnitModelRecord* UnitModel(std::uint32_t index) {
    return unitdb::ModelRecord(index);
}
const itemdb::ArmorRecord* Armour(std::uint32_t index) {
    return itemdb::ArmorAt(index);
}
const char* ModelPrefix(const UnitModelRecord* model) { return model->figureName.c_str(); }

constexpr std::uint32_t kNone = 0xFFFFFFFFu;

using IdList = UnitIdList;

// --- Helpers -------------------------------------------------------------------------------------

// --- The dressed units ---------------------------------------------------------------------------

// What the texture cache composes a skin from: the texture records as its layer bytes, the texture file
// names, the composite's size as its flags.
using SkinComposition = SkinRequest;

// The vector UnitTextureRecords fills: 12-byte texture records (copied into the set as bytes): a kind (0x32
// the body, 0x3005 an armour piece, 0x3004 a weapon), the item id and the item record's type.
struct TextureRecord {
    std::uint32_t kind;
    std::uint32_t id;
    std::uint32_t itemType;   // the item record's type
};
using RecordList = std::vector<TextureRecord>;
} // namespace

void UnitTextureRecords(char body, const DressedUnitRecord* record, RecordList* list);

namespace {

// The texture records copied into the set's byte array (SetSize inlined: kept contents, 0 frees).
void CopyRecords(SkinComposition& set, const RecordList& list) {
    int size = static_cast<int>(list.size()) * static_cast<int>(sizeof(TextureRecord));
    set.layers.resize(size);
    std::memcpy(set.layers.data(), list.data(), static_cast<std::size_t>(size));
}

// Inlined strcpy / strcat: the bytes up to and including the terminator.
void CopyText(char* to, const char* from) { std::memcpy(to, from, std::strlen(from) + 1); }
void AppendText(char* to, const char* from) { CopyText(to + std::strlen(to), from); }

// "<model prefix><name>.mmp" into `buffer`.
void AppendName(const UnitModelRecord* model, const std::string& name, char* buffer) {
    CopyText(buffer, ModelPrefix(model));
    AppendText(buffer, name.c_str());
    AppendText(buffer, ".mmp");
}

void AddItem(SkinComposition& set, const UnitModelRecord* model, std::uint32_t id) {
    char buffer[0x100];
    {
        std::string name;
        ArmorModelName(&name, id >> 16, id & 0xFFFF);
        AppendName(model, name.c_str(), buffer);
    }
    set.textures.push_back(buffer);
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

// The extra weapon picked by the record's index byte in its list: weapon and material records named together.
void AddExtraWeapon(SkinComposition& set, const UnitModelRecord* model, DressedUnitRecord* record) {
    const std::uint32_t* list = record->extraWeapons.data();
    int count = static_cast<int>(record->extraWeapons.data() + record->extraWeapons.size() - list);
    int index = record->extraWeapon;
    if (!(static_cast<std::uint32_t>(count) > static_cast<std::uint32_t>(index))) return;
    std::uint32_t id = list[index];
    if (id == kNone) return;
    char buffer[0x100];
    {
        const itemdb::WeaponRecord* weapon = itemdb::WeaponAt(id >> 16);
        std::string name;
        const itemdb::MaterialRecordHead* material = itemdb::MaterialAt(id & 0xFFFF);
        WeaponRecordModelName(&name, weapon, material);
        AppendName(model, name.c_str(), buffer);
    }
    set.textures.push_back(buffer);
}

// The texture cache is the client world's texture manager.
void Compose(CWorldClient* self, std::string& out, SkinComposition& set, const std::string& name, int size, int kind) {
    set.flags = size;
    self->textureManager_->Compose(&out, &set, &name, &name, 3, 0, 0x5551);
    self->textureManager_->FindOrLoad(kind, name.c_str());
}

void DressUnit(CWorldClient* self, DressedUnitRecord* record) {
    const UnitPrototypeRecord* prototype = UnitPrototype(record->prototype);
    const UnitModelRecord* model = UnitModel(static_cast<std::uint32_t>(prototype->modelIndex));
    RecordList list;
    SkinComposition set{};

    const std::uint32_t* items = record->items.data();
    if (!record->items.empty()) {
        std::string built;
        UnitTextureRecords(1, record, &list);
        CopyRecords(set, list);
        std::string skin;
        set.textures.clear();
        if (prototype) {
            skin = str::Format("%sskin_%.2d.mmp", ModelPrefix(model), prototype->skin);
        } else {
            skin = str::Format("%sskin_00.mmp", ModelPrefix(model));
        }
        set.textures.push_back(skin);
        for (int slot = 3; slot <= 6; ++slot) {
            if (items[slot] != kNone) AddItem(set, model, items[slot]);
        }
        AddWeapons(set, model, items);

        std::string name;
        if (record->modelNumber != -1) {
            name = str::Format("mo%dp", record->modelNumber);
        } else {
            name = str::Format("prot%dp", static_cast<int>(record->prototype));
        }
        Compose(self, built, set, name, 0x100, 1);

        UnitTextureRecords(0, record, &list);
        CopyRecords(set, list);
        set.textures.clear();
        if (items[0] != kNone) AddItem(set, model, items[0]);
        AddExtraWeapon(set, model, record);
        if (record->modelNumber != -1) {
            name = str::Format("mo%ds", record->modelNumber);
        } else {
            name = str::Format("prot%ds", static_cast<int>(record->prototype));
        }
        Compose(self, built, set, name, 0x80, 2);
    }

    lib::FreeStorage(set.layers);
}

// --- The other units -----------------------------------------------------------------------------

using StringList = std::vector<std::string>;

void FigureUnit(CWorldClient* self, FiguredUnitRecord* record) {
    const UnitPrototypeRecord* prototype = UnitPrototype(record->prototype);
    const UnitModelRecord* model = UnitModel(static_cast<std::uint32_t>(prototype->modelIndex));
    StringList first;
    StringList second;

    if (record->names.empty()) {
        CollectArmourPartNames(prototype, &record->items, &record->extraWeapons, &first,
                               &second);
    } else {
        first.reserve(first.size() + record->names.size());
        for (const std::string& name : record->names) first.push_back(name);
    }
    first.insert(first.end(), second.data(), second.data() + second.size());

    {
        std::string name(ModelPrefix(model));
        // The figure loader's LoadFigure (not fresh).
        self->figureLoader_->LoadFigure(&name, &first, record->morphS, record->morphT, record->morphU, false);
    }
}
} // namespace

// `others` and `dressed` are vectors of unit records; the dressed units (the second argument) come first.
void CWorldClient::BuildUnitSkins(std::vector<FiguredUnitRecord>* others, std::vector<DressedUnitRecord>* dressed) {
    for (DressedUnitRecord& r : *dressed) DressUnit(this, &r);
    for (FiguredUnitRecord& r : *others) FigureUnit(this, &r);
}

// The list is emptied; nothing more for a unit without items. The body list has eight records: [0] the body
// (0x32, the prototype), [1] empty (0, -1, -1), [2..7] the armour pieces of items[1..6] (0x3005, the
// armour's type; item i goes to record i + 1). The head list two: [0] the helmet row items[0] (0x3005) and
// [1] the shown extra weapon (0x3004, the weapon's type) when its index is within the list. items[1..6] are
// read whatever the item list's size; an id past its table reads through a null record (it faults).
void UnitTextureRecords(char body, const DressedUnitRecord* record, RecordList* list) {
    constexpr std::uint32_t kBody = 0x32, kArmourPiece = 0x3005, kWeapon = 0x3004;
    list->clear();
    auto& items = const_cast<IdList&>(record->items);
    if (items.empty()) return;
    const TextureRecord empty = {0, kNone, kNone};
    const std::uint32_t* ids = items.data();
    if (body) {
        list->resize(8, empty);
        TextureRecord* out = list->data();
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
    list->resize(2, empty);
    TextureRecord* out = list->data();
    if (ids[0] != kNone) {
        out[0].id = ids[0];
        out[0].kind = kArmourPiece;
        out[0].itemType = static_cast<std::uint32_t>(Armour(ids[0] >> 16)->type);
    }
    auto& extra = const_cast<IdList&>(record->extraWeapons);
    const auto count = static_cast<std::uint32_t>(extra.size());
    const auto index = static_cast<std::uint32_t>(static_cast<int>(record->extraWeapon));
    if (index >= count) return;
    const std::uint32_t weapon = extra[index];
    if (weapon == kNone) return;
    out[1].id = weapon;
    out[1].kind = kWeapon;
    out[1].itemType = static_cast<std::uint32_t>(
        itemdb::WeaponAt(weapon >> 16)->type);
}
