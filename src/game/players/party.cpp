// The party: its constructor and destructor, Create / Destroy (the managers and constructors it makes) and
// Clear. See party.h and docs/notes/items_spells.md ("Party").
#include "game/players/party.h"

#include "engine/data/chunk_archive.h"
#include "engine/core/core_streams.h"
#include "game/app/game_flow.h"
#include "game/app/singletons.h"
#include "game/items/item.h"
#include "game/items/item_manager.h"
#include "game/net/net_link.h"
#include "engine/core/named_floats.h"
#include "game/map/map_database.h"
#include "engine/data/text_manager.h"
#include "engine/core/random.h"
#include "engine/data/resource_manager.h"
#include "game/saves/save_file.h"
#include "game/saves/save_versions.h"
#include "game/players/saved_parties.h"
#include "game/spells/spell.h"
#include "game/ui/ui_screens.h"
#include "game/units/unit.h"
#include "game/units/unit_create.h"
#include "game/units/unit_db.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/str.h"
#include "lib/text_case.h"
#include "platform/platform.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

// A counted object the party owns: its count dropped and the object deleted, whatever the count, and the
// pointer cleared.
template <typename T>
void DeleteCounted(T*& object) {
    if (object == nullptr) return;
    --object->refCount;
    delete object;
    object = nullptr;
}

} // namespace

void ReleaseItems(std::vector<Item*>& list) {
    for (Item* item : list) item->Release();
    list.clear();
}

Party::Party() = default;

Party::~Party() {
    Clear(true, true);
    Party::Destroy();
}

const char* Party::ClassName() { return "CParty"; }
ClassId Party::GetClassId() { return ClassId::Party; }

ItemConstructor* Party::GetItemConstructor() { return itemConstructor; }
SpellConstructor* Party::GetSpellConstructor() { return spellConstructor; }
ItemManager* Party::ItemFactory() { return itemManager; }
SpellManager* Party::GetSpellManager() { return spellManager; }

void Party::Create() {
    itemManager = new ItemManager();
    itemManager->AddRef();    // not tested for null
    itemManager->Init(this);

    spellManager = new SpellManager();
    spellManager->AddRef();
    spellManager->Init(this);

    itemConstructor = new ItemConstructor();
    itemConstructor->AddRef();
    itemConstructor->Create();

    spellConstructor = new SpellConstructor();
    spellConstructor->AddRef();
    spellConstructor->Create();

    Clear(true, false);
}

void Party::Destroy() {
    members.clear();
    ReleaseItems(items);
    DeleteCounted(itemConstructor);
    DeleteCounted(spellConstructor);
    DeleteCounted(itemManager);
    DeleteCounted(spellManager);
}

void Party::Clear(bool clearVariables, bool keepSpells) {
    ReleaseItems(items);
    if (itemManager != nullptr) itemManager->DestroyAll();
    if (spellManager != nullptr) {
        spellManager->Destroy();
        if (!keepSpells) spellManager->Init(this);
    }
    members.clear();
    if (clearVariables) variables.clear();
    zone.clear();
    entrance = 0;
    money = 0;
}

PartyUnitRecord* Party::Find(int unitId) {
    for (int i = 0; i < std::ssize(members); ++i)
        if (members[i].unitId == static_cast<std::uint32_t>(unitId)) return &members[i];
    return nullptr;
}

// --- File --------------------------------------------------------------------------------------------

// The multiplayer heroes' files (<folder>\<n>.mp): one party per file, saved (with its variables)
// into a memory stream and written as a save file (magic kMagicParty). See party.h, ui_screens.h
// (MpHeroRecord) and docs/notes/items_spells.md ("Party records and the party object").

namespace {

constexpr const char* kTempFileName = "temp.mp";

std::string InFolder(const std::string& folder, const std::string& name) { return folder + name; }
std::string TempFile(const std::string& folder) { return folder + kTempFileName; }

// The file stream and the memory stream the originals build on the stack (the memory stream torn down first).
struct LocalStreams {
    FileStream file;
    MemoryStream data;
};

// Whether a file name is "<digits>.<anything>": a '.' and only digits before the last one.
bool Numbered(const std::string& name) {
    int dot = str::ReverseFind(name, '.');
    if (dot == -1) return false;
    for (int i = 0; i < dot; ++i)
        if (!text::IsDigit(static_cast<signed char>(name[i]))) return false;
    return true;
}

} // namespace

void MpHeroRecord::WriteFile(const std::string& folder) {
    if (!Numbered(file)) {
        platform::RemoveFile(InFolder(folder, file));
        return;
    }
    platform::RenameFile(InFolder(folder, file), TempFile(folder));
    {
        LocalStreams streams;
        streams.file.Open(InFolder(folder, file), platform::FileMode::Create);
        GetParty()->SaveParty(&streams.data, true);
        SaveFileWrite(&streams.file, kMagicParty, &streams.data);
    }
    platform::RemoveFile(TempFile(folder));
}

bool MpHeroRecord::ReadFile(const std::string& folder) {
    LocalStreams streams;
    streams.file.Open(InFolder(folder, file), platform::FileMode::Read);
    if (!SaveFileRead(&streams.data, kMagicParty, &streams.file)) return false;
    GetParty()->LoadParty(&streams.data, true);
    RequireEveryRecord(GetParty());
    return streams.data.ok;
}

void SavedParties::SaveAll() {
    if (folder.empty()) return;
    ClearSaveFolder(folder);
    // The count is read again after each write.
    for (unsigned i = 0; i < static_cast<unsigned>(entries.size()); ++i) entries[i].WriteFile(folder);
}

bool SavedParties::InUse(const MpHeroRecord* hero) const {
    if (hero->file == outsideGame.file) return true;
    for (const std::string& file : companions)
        if (file == hero->file) return true;
    return false;
}

bool SavedParties::ReturnCompanions(Party* party) {
    const std::vector<std::string>& files = companions;
    std::vector<bool> taken(files.size(), false);
    for (int i = 1; i < std::ssize(party->members); ++i) {
        const PartyUnitRecord& record = party->members[i];
        for (int c = 0; c < std::ssize(files); ++c) {
            if (taken[c]) continue;
            MpHeroRecord* hero = nullptr;
            for (MpHeroRecord& entry : entries)
                if (entry.file == files[c]) hero = &entry;
            if (!hero || hero->party.members.size() != 1) continue;
            PartyUnitRecord& saved = hero->party.members[0];
            if (saved.scriptName != record.scriptName || saved.prototype != record.prototype) continue;
            taken[c] = true;
            saved.CopyFrom(record, &hero->party);
            hero->WriteFile(folder);
            break;
        }
    }
    if (std::ssize(party->members) < 2) return false;
    party->members.erase(party->members.begin() + 1, party->members.end());
    return true;
}

void SavedParties::ReserveOne() {
    if (entries.size() == entries.capacity()) entries.reserve(entries.empty() ? 1 : 2 * entries.size());
}

// The saved parties.
SavedParties g_savedParties;

// --- Items -------------------------------------------------------------------------------------------

// Item lists: the party's item list and the party unit records' four item lists. See party.h, item.h, unit.h and
// docs/notes/items_spells.md ("Party item lists").

namespace {

Item* AddEntry(std::vector<Item*>& list, Item* item) {
    Item* entry = item->Clone(item->GetManagerContext()->GetSpellManager());
    entry->AddRef();
    list.push_back(entry);
    return entry;
}

void RemoveEntry(std::vector<Item*>& list, Item* entry) {
    for (int i = 0; i < std::ssize(list); ++i) {
        if (list[i] != entry) continue;
        entry->Release();
        list.erase(list.begin() + i);
        return;
    }
}

// One running sum over the lists, in this order.
void AddListWeight(float& weight, const std::vector<Item*>& list) {
    for (int i = 0; i < std::ssize(list); ++i) {
        if (list[i]) weight += list[i]->weight;
    }
}

} // namespace

// --- The party's items --------------------------------------------------------------------------

Item* Party::AddItem(Item* item, bool keepCount, bool merge) {
    if (!item) return nullptr;
    int count = keepCount ? static_cast<int>(item->count) : 1;
    if (merge) {
        for (int i = 0; i < std::ssize(items); ++i) {
            Item* existing = items[i];
            if (!existing->IsSameAs(item)) continue;
            existing->count += static_cast<std::uint32_t>(count);
            return existing;
        }
    }
    Item* copy = CopyItem(item);
    copy->count = static_cast<std::uint32_t>(count);
    items.push_back(copy);
    copy->AddRef();
    return copy;
}

Item* Party::RemoveItem(Item* item, bool all) {
    for (int i = 0; i < std::ssize(items); ++i) {
        if (items[i] != item) continue;
        Item* entry = items[i];
        if (static_cast<int>(entry->count) > 1 && !all) {
            --entry->count;
            return entry;
        }
        entry->Release();
        items.erase(items.begin() + i);
        return nullptr;
    }
    return nullptr;
}

void Party::RemoveQuestItems() {
    for (int i = lib::Count(items) - 1; i >= 0; --i) {
        Item* item = items[i];
        if (item->GetClassId() == ClassId::ItemQuest) RemoveItem(item, true);
    }
}

// --- The party unit records' item lists ---------------------------------------------------------

Item* PartyUnitRecord::AddWeapon(Item* item) { return AddEntry(weapons, item); }
Item* PartyUnitRecord::AddQuickItem(Item* item) { return AddEntry(quickItems, item); }
Item* PartyUnitRecord::AddArmor(Item* item) { return AddEntry(armor, item); }
Item* PartyUnitRecord::AddSpell(Item* item) { return AddEntry(spells, item); }

void PartyUnitRecord::RemoveWeapon(Item* entry) { RemoveEntry(weapons, entry); }
void PartyUnitRecord::RemoveQuickItem(Item* entry) { RemoveEntry(quickItems, entry); }
void PartyUnitRecord::RemoveArmor(Item* entry) { RemoveEntry(armor, entry); }
void PartyUnitRecord::RemoveSpell(Item* entry) { RemoveEntry(spells, entry); }

// --- The weight a unit record carries ---------------------------------------------

float PartyUnitRecord::CarriedWeight() const {
    float weight = 0.0f;
    AddListWeight(weight, weapons);
    AddListWeight(weight, quickItems);
    AddListWeight(weight, spells);
    AddListWeight(weight, armor);
    return weight;
}

// --- Members -----------------------------------------------------------------------------------------

// A party's unit records: adding one from a prototype (its texts, statistics and starting items),
// removing one, the record's assignment and the copy of another record's items. See party.h, unit.h and docs/notes/items_spells.md ("Party").

namespace {

// A counted copy of `item` (Item::Clone with its context's spell manager), null for none (a name the
// item database lacks); `item` is held meanwhile and released after.
Item* CountedClone(Item* item) {
    if (!item) return nullptr;
    item->AddRef();
    Item* copy = item->Clone(item->GetManagerContext()->GetSpellManager());
    copy->AddRef();
    return copy;
}

// Appends a counted copy of `item` to `list` (nothing for a null item), then releases `item`.
void AppendCopy(std::vector<Item*>& list, Item* item) {
    if (Item* copy = CountedClone(item)) list.push_back(copy);
    if (item) item->Release();
}

// A weapon name that gives a weapon: not empty and not "none" (_mbsicmp).
bool Named(const std::string& name) {
    return name.size() != 0 &&
           text::CompareIgnoringCase(name,
                           "none") != 0;
}

// The party's copies of `source`'s items, each with the source's count and a reference.
void CopyList(std::vector<Item*>& list, const std::vector<Item*>& source, Party* party) {
    list.resize(std::ssize(source));
    for (int i = 0; i < std::ssize(source); ++i) {
        Item* item = source[i];
        list[i] = party->CopyItem(item);
        Item* copy = list[i];
        copy->count = item->count;
        copy->AddRef();
    }
}

constexpr const char* kZoneVariableFormat = "z.%s";
constexpr int kZoneKindGame = 0;
constexpr int kZoneKindEdge = 1;
constexpr float kOpened = 1.0f;

} // namespace

void PartyUnitRecord::CopyItemsFrom(const PartyUnitRecord* source, Party* party) {
    ClearItems();
    CopyList(weapons, source->weapons, party);
    CopyList(quickItems, source->quickItems, party);
    CopyList(armor, source->armor, party);
    CopyList(spells, source->spells, party);
}

void PartyUnitRecord::CopyFrom(const PartyUnitRecord& source, Party* party) {
    CopyItemsFrom(&source, party);
    scriptName = source.scriptName;
    name = source.name;
    figureName = source.figureName;
    parentIname = source.parentIname;
    skinTexture = source.skinTexture;
    prototype = source.prototype;
    secondPrototype = source.secondPrototype;
    unitId = source.unitId;
    stats = source.stats;
    scaleX = source.scaleX;
    scaleY = source.scaleY;
    height = source.height;
    moveMode2 = source.moveMode2;
    weaponIndex = source.weaponIndex;
    selected = source.selected;
    leaderId = source.leaderId;
    fightAtWill = source.fightAtWill;
}

PartyUnitRecord* Party::AddMember(std::string_view name, std::string_view prototype, std::string_view extra) {
    if (!itemManager) Create();
    if (!g_unitDatabase.prototypes.FindByName(prototype)) {   // a script's unit of a prototype the database lacks
        game::Warning("Can't find prototype \"%s\" for unit \"%s\": no unit", std::string(prototype),
                      std::string(name.empty() ? extra : name));
        return nullptr;
    }
    // The game assigned a constructed stack record to the new entry. Ours is value-initialised: the record's
    // defaults leave the statistics, which the game took from its stack and saved with the party (a
    // determinism fix).
    PartyUnitRecord* record = &members.emplace_back();
    record->name = name;
    record->unitId = name.empty() ? 0xFFFFFFFFu : static_cast<std::uint32_t>(TextKeyFromName(name));
    record->prototype = prototype;
    record->scriptName = extra;
    FillFromPrototype(record);
    return record;
}

void Party::FillFromPrototype(PartyUnitRecord* record) {
    auto* prototype = g_unitDatabase.prototypes.FindByName(record->prototype);
    auto* model = unitdb::ModelRecord(static_cast<std::uint32_t>(prototype->modelIndex));
    record->figureName = model->figureName;
    BuildNpcStats(&record->stats, record->prototype);
    record->scaleY = prototype->scale.y;
    record->scaleX = prototype->scale.x;
    record->height = prototype->scale.z;

    // What the prototype lookup copies out of the prototype: armour names, the two weapon names, spell names.
    for (const std::string& name : prototype->armourNames) AppendCopy(record->armor, ItemFactory()->CreateItem(name));
    for (const auto* weapon : {&prototype->weaponName, &prototype->secondWeaponName}) {
        if (Named(*weapon)) AppendCopy(record->weapons, ItemFactory()->CreateItem(*weapon));
    }
    for (const std::string& name : prototype->spellNames)
        AppendCopy(record->spells, ItemFactory()->CreateSpellItem(name, GetSpellManager()));
    record->secondPrototype = record->prototype;
}

bool Party::EquipFromPrototype(PartyUnitRecord* record) {
    ReleaseItems(record->spells);
    ReleaseItems(record->weapons);
    auto* npc = g_unitDatabase.npcs.FindByName(record->prototype);
    if (!npc) return false;

    for (const std::string& name : npc->startSpells)
        AppendCopy(record->spells, ItemFactory()->CreateSpellItem(name, GetSpellManager()));
    for (const std::string& name : npc->startQuickItems) AppendCopy(record->quickItems, ItemFactory()->CreateItem(name));
    for (const std::string& name : npc->startWeapons) AppendCopy(record->weapons, ItemFactory()->CreateItem(name));
    money += npc->startMoney;
    record->stats.AddExperience(npc->experienceFloor);
    record->secondPrototype = record->prototype;
    return true;
}

bool Party::RemoveMember(int key) {
    for (int i = 0; i < std::ssize(members); ++i) {
        if (static_cast<int>(members[i].unitId) != key) continue;
        members.erase(members.begin() + i);
        return true;
    }
    return false;
}

void Party::OpenEdgeExits() {
    std::string variable;
    MapRecord* newZone = g_mapDatabase.FindMap(zone);
    if (newZone == nullptr || newZone->kind != kZoneKindEdge) return;
    NamedFloatTable& currentVariables = variables;
    for (int i = 0; i < std::ssize(newZone->exits); ++i) {
        MapRecord* target = g_mapDatabase.FindMap(newZone->exits[i].target);
        if (target == nullptr || target->kind != kZoneKindGame) continue;
        variable = str::Format(kZoneVariableFormat, target->name);
        float value = currentVariables.Get(variable);
        if (value == 0.0f) currentVariables.Set(variable, kOpened);
    }
}

// --- The record's move assignment and item clear ---------------------------------------------------

PartyUnitRecord& PartyUnitRecord::operator=(PartyUnitRecord&& from) noexcept {
    if (this != &from) {
        std::destroy_at(this);
        std::construct_at(this, std::move(from));
    }
    return *this;
}

void PartyUnitRecord::ClearItems() {
    ReleaseItems(weapons);
    ReleaseItems(quickItems);
    ReleaseItems(armor);
    ReleaseItems(spells);
}

// --- Save --------------------------------------------------------------------------------------------

// Saving and loading parties and their unit records, and the party copies made through a memory stream
// (operator= and the copy constructor). See party.h and docs/notes/items_spells.md ("Party").

namespace {

// A list's count, then each item's id (-1 for an empty entry).
void WriteItemIds(ByteStream* stream, const std::vector<Item*>& list) {
    stream->WriteCount(list.size());
    for (const Item* item : list) stream->WriteId(item);
}

// Releases what the list holds, then reads a count and that many ids, each resolved in `manager` and counted.
void ReadItemIds(ByteStream* stream, std::vector<Item*>& list, BaseManager* manager) {
    ReleaseItems(list);
    list.resize(stream->ReadCount());
    for (Item*& item : list) {
        item = manager->FindObjectAs<Item>(static_cast<unsigned>(stream->ReadId()));
        if (item != nullptr) item->AddRef();
    }
}


} // namespace

void PartyUnitRecord::Load(ByteStream* stream, Party* party) {
    ClearItems();
    weaponIndex = 0;
    moveMode2 = MoveMode::Walk;
    leaderId = -1;
    selected = 0;
    fightAtWill = kFightAtWill;
    scriptName = stream->ReadString();
    figureName = stream->ReadString();
    parentIname = stream->ReadString();
    skinTexture = stream->ReadString();
    prototype = stream->ReadString();
    secondPrototype = prototype;
    stream->ReadValue(unitId);
    stats.Load(stream);
    stream->Read(&scaleX, 0xC);   // +0x728 and the height
    ReadItemIds(stream, weapons, party->ItemFactory());
    ReadItemIds(stream, quickItems, party->ItemFactory());
    ReadItemIds(stream, armor, party->ItemFactory());
    ReadItemIds(stream, spells, party->ItemFactory());
    name = stream->ReadString();
    if (stream->version < kVersionRecordFlags) return;
    stream->ReadValue(moveMode2);
    stream->ReadValue(weaponIndex);
    stream->ReadValue(selected);
    if (stream->version < kVersionRecordLeader) return;
    stream->ReadValue(leaderId);
    if (stream->version < kVersionRecordFightAtWill) return;
    stream->ReadValue(fightAtWill);
    if (stream->version < kVersionRecordSixthText) return;
    secondPrototype = stream->ReadString();
}

void PartyUnitRecord::Save(ByteStream* stream) {
    stream->WriteString(scriptName);
    stream->WriteString(figureName);
    stream->WriteString(parentIname);
    stream->WriteString(skinTexture);
    stream->WriteString(prototype);
    stream->WriteValue(unitId);
    stats.Save(stream);
    stream->Write(&scaleX, 0xC);
    WriteItemIds(stream, weapons);
    WriteItemIds(stream, quickItems);
    WriteItemIds(stream, armor);
    WriteItemIds(stream, spells);
    stream->WriteString(name);
    stream->WriteValue(moveMode2);
    stream->WriteValue(weaponIndex);
    stream->WriteValue(selected);
    stream->WriteValue(leaderId);
    stream->WriteValue(fightAtWill);
    stream->WriteString(secondPrototype);
}

void Party::LoadParty(ByteStream* stream, bool loadVariables) {
    if (itemManager == nullptr) Create();
    Clear(loadVariables, 0);
    zone = stream->ReadString();
    stream->ReadValue(entrance);
    itemManager->Load(stream);
    spellManager->Load(stream);
    itemManager->LoadObjects(stream);
    spellManager->LoadObjects(stream);
    ReadItemIds(stream, items, ItemFactory());
    members.resize(stream->ReadCount());
    for (PartyUnitRecord& member : members) member.Load(stream, this);
    if (loadVariables) variables.Load(stream);
    money = 0;
    stream->ReadValue(money);
    if (stream->version >= kVersionPartyKey) {
        std::uint32_t key;
        stream->ReadValue(key);
        money ^= key;
    }
    itemManager->ReleaseUnused();
    spellManager->ReleaseUnused();
}

void Party::SaveParty(ByteStream* stream, bool saveVariables) {
    if (itemManager == nullptr) Create();
    stream->WriteString(zone);
    stream->WriteValue(entrance);
    itemManager->Save(stream);
    spellManager->Save(stream);
    itemManager->SaveObjects(stream);
    spellManager->SaveObjects(stream);
    WriteItemIds(stream, items);
    stream->WriteCount(members.size());
    for (PartyUnitRecord& member : members) member.Save(stream);
    if (saveVariables) variables.Save(stream);
    std::uint32_t key = g_random.Next();
    stream->WriteValue(money ^ key);
    stream->WriteValue(key);
}

void Party::CopyFrom(Party* source) {
    MemoryStream copy;
    source->SaveParty(&copy, true);
    copy.MemoryStream::Seek(0);
    LoadParty(&copy, true);
}

Party* Party::Assign(Party* source) {
    Clear(true, false);
    CopyFrom(source);
    return this;
}

Party::Party(Party& source) : Party() { CopyFrom(&source); }

// --- The party as a stream field of an archive --------------------------------------

void Party::ArchiveParty(ChunkArchive* ar, int field) {
    ar->StreamFieldOf(
        field, [this](MemoryStream* stream) { SaveParty(stream, true); },
        [this](MemoryStream* stream) {
            LoadParty(stream, true);
            RequireEveryRecord(this);
        });
}
