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
#include "unit_client_models.h"
#include "unit_db.h"
#include "unit_looks.h"
#include "world.h"

#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/static_object.h"
#include "lib/str.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <string>
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

// --- Helpers -------------------------------------------------------------------------------------

// --- The dressed units ---------------------------------------------------------------------------

// The vector UnitTextureRecords fills: 12-byte texture records (copied into the set as bytes): a kind (0x32
// the body, kItemArmor an armour piece, kItemWeapon a weapon), the item id and the item record's type.
struct TextureRecord {
    std::uint32_t kind;
    std::uint32_t id;
    std::uint32_t itemType;   // the item record's type
};
} // namespace

void UnitTextureRecords(char body, const DressedUnitRecord* record, std::vector<TextureRecord>* list);

// A second block: it uses UnitTextureRecords (defined above).
namespace {

// The texture records copied into the set's byte array (SetSize inlined: kept contents, 0 frees).
void CopyRecords(SkinRequest& set, const std::vector<TextureRecord>& list) {
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

void AddItem(SkinRequest& set, const UnitModelRecord* model, std::uint32_t id) {
    char buffer[0x100];
    {
        std::string name;
        ArmorModelName(&name, id >> 16, id & 0xFFFF);
        AppendName(model, name, buffer);
    }
    set.textures.push_back(buffer);
}

// The weapons (items 1 and 2): the one whose armour layer is lower first when both exist.
void AddWeapons(SkinRequest& set, const UnitModelRecord* model, const std::uint32_t* items) {
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
void AddExtraWeapon(SkinRequest& set, const UnitModelRecord* model, DressedUnitRecord* record) {
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
        AppendName(model, name, buffer);
    }
    set.textures.push_back(buffer);
}

// The texture cache is the client world's texture manager.
void Compose(CWorldClient* self, std::string& out, SkinRequest& set, const std::string& name, int size, int kind) {
    set.flags = size;
    self->textureManager_->Compose(&out, &set, &name, &name, 3, 0x5551);
    self->textureManager_->FindOrLoad(kind, name);
}

void DressUnit(CWorldClient* self, DressedUnitRecord* record) {
    const UnitPrototypeRecord* prototype = UnitPrototype(record->prototype);
    const UnitModelRecord* model = UnitModel(static_cast<std::uint32_t>(prototype->modelIndex));
    std::vector<TextureRecord> list;
    SkinRequest set{};

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

void FigureUnit(CWorldClient* self, FiguredUnitRecord* record) {
    const UnitPrototypeRecord* prototype = UnitPrototype(record->prototype);
    const UnitModelRecord* model = UnitModel(static_cast<std::uint32_t>(prototype->modelIndex));
    std::vector<std::string> first;
    std::vector<std::string> second;

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
// (0x32, the prototype), [1] empty (0, -1, -1), [2..7] the armour pieces of items[1..6] (kItemArmor, the
// armour's type; item i goes to record i + 1). The head list two: [0] the helmet row items[0] (kItemArmor) and
// [1] the shown extra weapon (kItemWeapon, the weapon's type) when its index is within the list. items[1..6] are
// read whatever the item list's size; an id past its table reads through a null record (it faults).
void UnitTextureRecords(char body, const DressedUnitRecord* record, std::vector<TextureRecord>* list) {
    constexpr std::uint32_t kBody = 0x32;
    list->clear();
    auto& items = const_cast<std::vector<std::uint32_t>&>(record->items);
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
            out[i + 1].kind = kItemArmor;
            out[i + 1].itemType = static_cast<std::uint32_t>(Armour(ids[i] >> 16)->type);
        }
        return;
    }
    list->resize(2, empty);
    TextureRecord* out = list->data();
    if (ids[0] != kNone) {
        out[0].id = ids[0];
        out[0].kind = kItemArmor;
        out[0].itemType = static_cast<std::uint32_t>(Armour(ids[0] >> 16)->type);
    }
    auto& extra = const_cast<std::vector<std::uint32_t>&>(record->extraWeapons);
    const auto count = static_cast<std::uint32_t>(extra.size());
    const auto index = static_cast<std::uint32_t>(static_cast<int>(record->extraWeapon));
    if (index >= count) return;
    const std::uint32_t weapon = extra[index];
    if (weapon == kNone) return;
    out[1].id = weapon;
    out[1].kind = kItemWeapon;
    out[1].itemType = static_cast<std::uint32_t>(
        itemdb::WeaponAt(weapon >> 16)->type);
}
