// The zone's unit skins and figures (the zone load): for every dressed unit (first list) two composite
// textures are built through the texture cache: the body skin ("<model>skin_NN.mmp" and the armour pieces'
// "<model><item>.mmp"), registered as "mo<n>p" / "prot<n>p", then the head and weapon textures ("mo<n>s" /
// "prot<n>s"); for every other unit (second list) the list of its textures goes to the figure maker. See
// docs/notes/world.md.
#include "game/items/item.h"
#include "game/items/item_db.h"
#include "game/items/item_names.h"
#include "engine/models/link_loader.h"
#include "engine/models/texture.h"
#include "game/units/unit.h"
#include "game/units/unit_client_models.h"
#include "game/units/unit_db.h"
#include "game/units/unit_looks.h"
#include "game/world/world.h"

#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/str.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>
#include <format>

namespace {

// --- Tables --------------------------------------------------------------------------------------


// The texture records' kind and item type of an empty record.
constexpr std::uint32_t kNoRecordField = 0xFFFFFFFFu;

// --- Helpers -------------------------------------------------------------------------------------

// --- The dressed units ---------------------------------------------------------------------------

// The vector UnitTextureRecords fills: 12-byte texture records (copied into the set as bytes): a kind (0x32
// the body, ItemKind::Armor an armour piece, ItemKind::Weapon a weapon), the item id and the item record's type.
struct TextureRecord {
    std::uint32_t kind = 0;
    std::uint32_t id = 0;
    std::uint32_t itemType = 0;  // the item record's type
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

// "<model prefix><name>.mmp".
std::string TextureName(const UnitModelRecord* model, const std::string& name) {
    return model->figureName + name + ".mmp";
}

void AddItem(SkinRequest& set, const UnitModelRecord* model, std::uint32_t id) {
    set.textures.push_back(TextureName(model, ArmorModelName(PrototypeIndex(id), MaterialIndex(id))));
}

// The weapons (items 1 and 2): the one whose armour layer is lower first when both exist. An id whose record is
// missing (kNoItemId, or an id of another database) is no item.
void AddWeapons(SkinRequest& set, const UnitModelRecord* model, const std::uint32_t* items) {
    const itemdb::ArmorRecord* first = itemdb::ArmorAt(items[1] >> 16);
    const itemdb::ArmorRecord* second = itemdb::ArmorAt(items[2] >> 16);
    if (first && second) {
        if (second->layer < first->layer) {
            AddItem(set, model, items[2]);
            AddItem(set, model, items[1]);
        } else {
            AddItem(set, model, items[1]);
            AddItem(set, model, items[2]);
        }
        return;
    }
    if (first) AddItem(set, model, items[1]);
    else if (second) AddItem(set, model, items[2]);
}

// The extra weapon picked by the record's index byte in its list: weapon and material records named together.
void AddExtraWeapon(SkinRequest& set, const UnitModelRecord* model, DressedUnitRecord* record) {
    const std::uint32_t* list = record->extraWeapons.data();
    int count = static_cast<int>(record->extraWeapons.data() + record->extraWeapons.size() - list);
    int index = record->extraWeapon;
    if (!(static_cast<std::uint32_t>(count) > static_cast<std::uint32_t>(index))) return;
    std::uint32_t id = list[index];
    if (id == kNoItemId) return;
    set.textures.push_back(TextureName(model, WeaponModelName(PrototypeIndex(id), MaterialIndex(id))));
}

// The texture cache is the client world's texture manager.
void Compose(WorldClient* self, std::string& out, SkinRequest& set, const std::string& name, int size, int kind) {
    set.side = size;
    self->textureManager->Compose(&out, &set, name, name, 3, MmpFormat::A1R5G5B5);
    self->textureManager->FindOrLoad(kind, name);
}

void DressUnit(WorldClient* self, DressedUnitRecord* record) {
    const UnitPrototypeRecord* prototype = unitdb::PrototypeRecord(record->prototype);
    const UnitModelRecord* model = unitdb::ModelRecord(static_cast<std::uint32_t>(prototype->modelIndex));
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
            skin = str::Format("%sskin_%.2d.mmp", model->figureName, prototype->skin);
        } else {
            skin = std::format("{}skin_00.mmp", model->figureName);
        }
        set.textures.push_back(skin);
        for (int slot = 3; slot <= 6; ++slot) {
            if (items[slot] != kNoItemId) AddItem(set, model, items[slot]);
        }
        AddWeapons(set, model, items);

        std::string name;
        if (record->modelNumber != -1) {
            name = std::format("mo{}p", record->modelNumber);
        } else {
            name = std::format("prot{}p", static_cast<int>(record->prototype));
        }
        Compose(self, built, set, name, 0x100, 1);

        UnitTextureRecords(0, record, &list);
        CopyRecords(set, list);
        set.textures.clear();
        if (items[0] != kNoItemId) AddItem(set, model, items[0]);
        AddExtraWeapon(set, model, record);
        if (record->modelNumber != -1) {
            name = std::format("mo{}s", record->modelNumber);
        } else {
            name = std::format("prot{}s", static_cast<int>(record->prototype));
        }
        Compose(self, built, set, name, 0x80, 2);
    }

    lib::FreeStorage(set.layers);
}

// --- The other units -----------------------------------------------------------------------------

void FigureUnit(WorldClient* self, FiguredUnitRecord* record) {
    const UnitPrototypeRecord* prototype = unitdb::PrototypeRecord(record->prototype);
    const UnitModelRecord* model = unitdb::ModelRecord(static_cast<std::uint32_t>(prototype->modelIndex));
    std::vector<std::string> first = record->names;
    if (first.empty()) {
        ArmourPartNames parts = CollectArmourPartNames(prototype, record->items, record->extraWeapons);
        first = std::move(parts.worn);
        first.insert(first.end(), parts.plain.begin(), parts.plain.end());
    }

    self->figureLoader->LoadFigure(model->figureName, first,
                                      {record->morphS, record->morphT, record->morphU});
}
} // namespace

// `others` and `dressed` are vectors of unit records; the dressed units (the second argument) come first.
void WorldClient::BuildUnitSkins(std::vector<FiguredUnitRecord>* others, std::vector<DressedUnitRecord>* dressed) {
    for (DressedUnitRecord& r : *dressed) DressUnit(this, &r);
    for (FiguredUnitRecord& r : *others) FigureUnit(this, &r);
}

// The list is emptied; nothing more for a unit without items. The body list has eight records: [0] the body
// (0x32, the prototype), [1] empty (0, -1, -1), [2..7] the armour pieces of items[1..6] (ItemKind::Armor, the
// armour's type; item i goes to record i + 1). The head list two: [0] the helmet row items[0] (ItemKind::Armor) and
// [1] the shown extra weapon (ItemKind::Weapon, the weapon's type) when its index is within the list. items[1..6] are
// read whatever the item list's size; an id whose record is missing (kNoItemId, or an id of another database) is
// no item.
void UnitTextureRecords(char body, const DressedUnitRecord* record, std::vector<TextureRecord>* list) {
    constexpr auto kBody = static_cast<std::uint32_t>(MapObjectType::Hero);
    list->clear();
    auto& items = const_cast<std::vector<std::uint32_t>&>(record->items);
    if (items.empty()) return;
    const TextureRecord empty = {0, kNoRecordField, kNoRecordField};
    const std::uint32_t* ids = items.data();
    if (body) {
        list->resize(8, empty);
        TextureRecord* out = list->data();
        out[0].id = record->prototype;
        out[0].kind = kBody;
        out[0].itemType = kNoRecordField;
        for (int i = 1; i < 7; ++i) {
            const itemdb::ArmorRecord* armour = itemdb::ArmorAt(ids[i] >> 16);
            if (!armour) continue;
            out[i + 1].id = ids[i];
            out[i + 1].kind = static_cast<std::uint32_t>(ItemKind::Armor);
            out[i + 1].itemType = static_cast<std::uint32_t>(armour->type);
        }
        return;
    }
    list->resize(2, empty);
    TextureRecord* out = list->data();
    if (const itemdb::ArmorRecord* helmet = itemdb::ArmorAt(ids[0] >> 16)) {
        out[0].id = ids[0];
        out[0].kind = static_cast<std::uint32_t>(ItemKind::Armor);
        out[0].itemType = static_cast<std::uint32_t>(helmet->type);
    }
    auto& extra = const_cast<std::vector<std::uint32_t>&>(record->extraWeapons);
    const auto count = static_cast<std::uint32_t>(extra.size());
    const auto index = static_cast<std::uint32_t>(static_cast<int>(record->extraWeapon));
    if (index >= count) return;
    const std::uint32_t weapon = extra[index];
    const itemdb::WeaponRecord* shown = itemdb::WeaponAt(weapon >> 16);
    if (!shown) return;
    out[1].id = weapon;
    out[1].kind = static_cast<std::uint32_t>(ItemKind::Weapon);
    out[1].itemType = static_cast<std::uint32_t>(shown->type);
}
