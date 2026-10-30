// The party: its constructor and destructor, Create / Destroy (the managers and constructors it makes) and
// Clear. See party.h and docs/notes/items_spells.md ("Party").
#include "party.h"

#include "chunk_archive.h"
#include "core_streams.h"
#include "game_singletons.h"
#include "game_strings.h"
#include "item.h"
#include "item_manager.h"
#include "net_link.h"
#include "object_tables.h"
#include "random.h"
#include "resource_manager.h"
#include "save_versions.h"
#include "spell.h"
#include "ui_screens.h"
#include "unit.h"
#include "unit_create.h"
#include "unit_db.h"
#include "world.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/nan_audit.h"
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
void Drop(T*& object) {
    if (object == nullptr) return;
    --object->refCount;
    delete object;
    object = nullptr;
}

void ReleaseItems(const std::vector<Item*>& items) {
    for (int i = 0; i < std::ssize(items); ++i) items[i]->Release();
}

} // namespace

Party::Party() = default;

Party::~Party() {
    Clear(1, 1);
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

    Clear(1, 0);
}

void Party::Destroy() {
    members.clear();
    ReleaseItems(items);
    items.clear();
    Drop(itemConstructor);
    Drop(spellConstructor);
    Drop(itemManager);
    Drop(spellManager);
}

void Party::Clear(int clearVariables, int keepSpells) {
    ReleaseItems(items);
    items.clear();
    if (itemManager != nullptr) itemManager->DestroyAll();
    if (spellManager != nullptr) {
        spellManager->Destroy();
        if (keepSpells == 0) spellManager->Init(this);
    }
    members.clear();
    if (clearVariables != 0) variables.clear();
    zone = game::kEmptyText;
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
// into a memory stream and written as a save file (magic 0x114). See party.h, ui_screens.h
// (MpHeroRecord) and docs/notes/items_spells.md ("Party records and the party object").

namespace {

constexpr unsigned kPartyFileMagic = 0x114;

const char* TempFileName() { return "temp.mp"; }

std::string InFolder(const std::string& folder, const std::string& name) { return folder + name; }
std::string TempFile(const std::string& folder) { return folder + TempFileName(); }

// The file stream and the memory stream the originals build on the stack (the memory stream torn down first).
struct LocalStreams {
    netlink::FileStream file;
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
        platform::RemoveFile(InFolder(folder, file).c_str());
        return;
    }
    {
        std::string temp = TempFile(folder);
        platform::RenameFile(InFolder(folder, file).c_str(), temp.c_str());
    }
    {
        LocalStreams streams;
        streams.file.Open(InFolder(folder, file), platform::FileMode::Create, 0);
        GetParty()->SaveParty(&streams.data, 1);
        SaveFileWrite(&streams.file, kPartyFileMagic, &streams.data);
    }
    platform::RemoveFile(TempFile(folder).c_str());
}

bool MpHeroRecord::ReadFile(const std::string& folder) {
    LocalStreams streams;
    streams.file.Open(InFolder(folder, file), platform::FileMode::Read, 1);
    if (!SaveFileRead(&streams.data, kPartyFileMagic, &streams.file)) return false;
    GetParty()->LoadParty(&streams.data, 1);
    return streams.data.ok != 0;
}

void SavedParties::SaveAll() {
    if (str::Compare(folder, game::kEmptyText) == 0)
        return;
    ClearSaveFolder(folder);
    // The count is read again after each write.
    for (unsigned i = 0; i < static_cast<unsigned>(entries.size()); ++i) entries[i].WriteFile(folder);
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

// A flag argument read as a byte.
bool ByteFlag(int flag) { return static_cast<std::uint8_t>(flag) != 0; }

// Appends `item` to `list` (SetSize(count + 1) and store, inlined).
void Append(std::vector<Item*>& list, Item* item) {
    int index = lib::Count(list);
    list.resize(index + 1);
    list[index] = item;
}

// Removes the element at `index`, closing the gap.
void RemoveAt(std::vector<Item*>& list, int index) { list.erase(list.begin() + index); }

Item* AddEntry(std::vector<Item*>& list, Item* item) {
    Item* entry = item->Clone(item->WorldContext()->GetSpellManager());
    entry->AddRef();
    Append(list, entry);
    return entry;
}

void RemoveEntry(std::vector<Item*>& list, Item* entry) {
    for (int i = 0; i < std::ssize(list); ++i) {
        if (list[i] != entry) continue;
        entry->Release();
        RemoveAt(list, i);
        return;
    }
}

// One running sum over the lists, in this order.
void AddListWeight(float& weight, const std::vector<Item*>& list) {
    for (int i = 0; i < std::ssize(list); ++i) {
        if (list[i]) weight += list[i]->Weight();
    }
}

} // namespace

// --- The party's items --------------------------------------------------------------------------

Item* Party::AddItem(Item* item, int keepCount, int merge) {
    if (!item) return nullptr;
    int count = ByteFlag(keepCount) ? static_cast<int>(item->count) : 1;
    if (ByteFlag(merge)) {
        for (int i = 0; i < std::ssize(items); ++i) {
            Item* existing = items[i];
            if (!existing->IsSameAs(item)) continue;
            existing->count += static_cast<std::uint32_t>(count);
            return existing;
        }
    }
    Item* copy = CopyItem(item);
    copy->count = static_cast<std::uint32_t>(count);
    Append(items, copy);
    copy->AddRef();
    return copy;
}

Item* Party::RemoveItem(Item* item, int all) {
    for (int i = 0; i < std::ssize(items); ++i) {
        if (items[i] != item) continue;
        Item* entry = items[i];
        if (static_cast<int>(entry->count) > 1 && !ByteFlag(all)) {
            --entry->count;
            return entry;
        }
        entry->Release();
        RemoveAt(items, i);
        return nullptr;
    }
    return nullptr;
}

void Party::RemoveQuestItems() {
    for (int i = lib::Count(items) - 1; i >= 0; --i) {
        Item* item = items[i];
        if (item->GetClassId() == ClassId::ItemQuest) RemoveItem(item, 1);
    }
}

void Party::ReleaseAllItems() {
    for (int i = 0; i < std::ssize(items); ++i) items[i]->Release();
    items.clear();
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
// removing one, the record's assignment and the copy of another record's items; the record lists' copies of
// item arrays' SetAtGrow. See party.h, unit.h and docs/notes/items_spells.md ("Party").

namespace {

// A counted copy of `item` (Item::Clone with its context's spell manager); `item` is held
// meanwhile and released after.
Item* CountedClone(Item* item) {
    if (item) item->AddRef();
    Item* copy = item->Clone(item->WorldContext()->GetSpellManager());
    copy->AddRef();
    return copy;
}

void Drop(Item* item) {
    if (item) item->Release();
}

void ReleaseAll(const std::vector<Item*>& list) {
    for (int i = 0; i < std::ssize(list); ++i) list[i]->Release();
}

// A weapon name that gives a weapon: not empty and not "none" (_mbsicmp).
bool Named(const std::string& name) {
    return name.size() != 0 &&
           text::CompareIgnoringCase(name,
                           game::NoneText()) != 0;
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

const char* ZoneVariableFormat() { return "z.%s"; }
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

PartyUnitRecord* Party::AddMember(std::string_view name, std::string_view prototype, std::string_view extra) {
    if (!itemManager) Create();
    // The game assigned a constructed stack record to the new entry. Ours is value-initialised: the record's
    // defaults leave the statistics, and BuildNpcStats' reset leaves some of theirs (the layers' spare values, ...),
    // which the game took from its stack and saved with the party (a determinism fix; the harness zeroes the
    // game's too, harness_leftovers.h).
    PartyUnitRecord* record = &members.emplace_back();
    record->texts[1] = name;
    record->unitId = name.empty() ? 0xFFFFFFFFu : static_cast<std::uint32_t>(TextKeyFromName(name));
    record->texts[5] = prototype;
    record->texts[0] = extra;
    FillFromPrototype(record);
    return record;
}

void Party::FillFromPrototype(PartyUnitRecord* record) {
    auto* prototype = g_unitDatabase.prototypes.FindByName(record->texts[5]);
    auto* model = unitdb::ModelRecord(static_cast<std::uint32_t>(prototype->modelIndex));
    record->texts[2] = model->figureName;
    BuildNpcStats(&record->stats, record->texts[5]);
    record->scaleY = prototype->scale.y;
    record->scaleX = prototype->scale.x;
    record->height = prototype->scale.z;

    // What the prototype lookup copies out of the prototype: armour names, the two weapon names, spell names.
    const std::vector<std::string>& armour = prototype->armourNames;
    for (int i = 0; i < static_cast<int>(armour.size()); ++i) {
        Item* item = ItemFactory()->CreateItem(armour.begin()[i]);
        Item* copy = CountedClone(item);
        lib::SetAtGrow(record->armor, lib::Count(record->armor), copy);
        Drop(item);
    }
    for (const auto* weapon : {&prototype->weaponName, &prototype->secondWeaponName}) {
        if (!Named((*weapon))) continue;
        Item* item = ItemFactory()->CreateItem(*weapon);
        Item* copy = CountedClone(item);
        lib::SetAtGrow(record->weapons, lib::Count(record->weapons), copy);
        Drop(item);
    }
    const std::vector<std::string>& spellNames = prototype->spellNames;
    for (int i = 0; i < static_cast<int>(spellNames.size()); ++i) {
        SpellManager* spellOwner = GetSpellManager();
        auto* spell = ItemFactory()->CreateSpellItem(spellNames.begin()[i], spellOwner);
        Item* copy = CountedClone(spell);
        std::vector<Item*>& list = record->spells;
        int index = lib::Count(list);
        list.resize(index + 1);
        list[index] = copy;
        Drop(spell);
    }
    record->texts[6] = record->texts[5];
}

bool Party::EquipFromPrototype(PartyUnitRecord* record) {
    // Each list emptied from the front (the first entry is looked up and found at 0).
    for (std::vector<Item*>* list : {&record->spells, &record->weapons}) {
        while (!list->empty()) {
            (*list)[0]->Release();
            list->erase(list->begin() + 0, list->begin() + (0 + 1));
        }
    }
    auto* npc = g_unitDatabase.npcs.byName[std::string(record->texts[5])];
    if (!npc) return false;

    const std::vector<std::string>& spellNames = npc->startSpells;
    for (unsigned i = 0; i < static_cast<unsigned>(spellNames.size()); ++i) {
        SpellManager* spellOwner = GetSpellManager();
        auto* spell = ItemFactory()->CreateSpellItem(spellNames.begin()[i], spellOwner);
        record->spells.push_back(CountedClone(spell));
        Drop(spell);
    }
    const std::vector<std::string>& quickNames = npc->startQuickItems;
    for (unsigned i = 0; i < static_cast<unsigned>(quickNames.size()); ++i) {
        Item* item = ItemFactory()->CreateItem(quickNames.begin()[i]);
        Item* copy = CountedClone(item);
        lib::SetAtGrow(record->quickItems, lib::Count(record->quickItems), copy);
        Drop(item);
    }
    const std::vector<std::string>& weaponNames = npc->startWeapons;
    for (unsigned i = 0; i < static_cast<unsigned>(weaponNames.size()); ++i) {
        Item* item = ItemFactory()->CreateItem(weaponNames.begin()[i]);
        Item* copy = CountedClone(item);
        lib::SetAtGrow(record->weapons, lib::Count(record->weapons), copy);
        Drop(item);
    }
    money += npc->startMoney;
    record->stats.AddExperience(npc->experienceFloor);
    record->texts[6] = record->texts[5];
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
        variable = str::Format(ZoneVariableFormat(), target->name);
        float value = currentVariables.Get(variable);
        EI_NAN_AUDIT(value);
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
    ReleaseAll(weapons);
    ReleaseAll(quickItems);
    ReleaseAll(armor);
    ReleaseAll(spells);
    weapons.clear();
    quickItems.clear();
    armor.clear();
    spells.clear();
}

// --- Save --------------------------------------------------------------------------------------------

// Saving and loading parties and their unit records, and the party copies made through a memory stream
// (operator= and the copy constructor). See party.h and docs/notes/items_spells.md ("Party").

namespace {

// A list's count, then each item's id (-1 for an empty entry).
void WriteItemIds(ByteStream* stream, const std::vector<Item*>& list) {
    stream->WriteValue(static_cast<std::int32_t>(std::ssize(list)));
    for (int i = 0; i < std::ssize(list); ++i) {
        const Item* item = list[i];
        stream->WriteValue(item != nullptr ? item->id : -1);
    }
}

// Releases what the list holds, then reads a count and that many ids, each resolved in `manager` and counted.
void ReadItemIds(ByteStream* stream, std::vector<Item*>& list, BaseManager* manager) {
    for (int i = 0; i < std::ssize(list); ++i) list[i]->Release();
    int count;
    stream->ReadValue(count);
    list.resize(count);
    for (int i = 0; i < std::ssize(list); ++i) {
        unsigned id;
        stream->ReadValue(id);
        Item* item = FindItem(manager, id);
        list[i] = item;
        if (item != nullptr) item->AddRef();
    }
}

// Versions of the save format that added fields (compared signed).
constexpr int kVersionRecord77C = 0x5B;
constexpr int kVersionRecord780 = 0x61;
constexpr int kVersionRecordText6 = 0x6B;

void CopyThrough(Party* target, Party* source) {
    // Through an empty memory stream, freed afterwards.
    MemoryStream copy;
    source->SaveParty(&copy, 1);
    copy.MemoryStream::Seek(0);
    target->LoadParty(&copy, 1);
}

} // namespace

void PartyUnitRecord::Load(ByteStream* stream) {
    ClearItems();
    weaponIndex = 0;
    moveMode2 = 2;
    leaderId = -1;
    selected = 0;
    fightAtWill = 1;
    texts[0] = stream->ReadString();
    texts[2] = stream->ReadString();
    texts[3] = stream->ReadString();
    texts[4] = stream->ReadString();
    texts[5] = stream->ReadString();
    texts[6] = texts[5];
    stream->ReadValue(unitId);
    stats.LoadFrom(stream);
    stream->Read(&scaleX, 0xC);   // +0x728 and the height
    ReadItemIds(stream, weapons, g_loadingParty->ItemFactory());
    ReadItemIds(stream, quickItems, g_loadingParty->ItemFactory());
    ReadItemIds(stream, armor, g_loadingParty->ItemFactory());
    ReadItemIds(stream, spells, g_loadingParty->ItemFactory());
    texts[1] = stream->ReadString();
    if (stream->Version() < kVersionRecordFlags) return;
    stream->ReadValue(moveMode2);
    stream->ReadValue(weaponIndex);
    stream->ReadValue(selected);
    if (stream->Version() < kVersionRecord77C) return;
    stream->ReadValue(leaderId);
    if (stream->Version() < kVersionRecord780) return;
    stream->ReadValue(fightAtWill);
    if (stream->Version() < kVersionRecordText6) return;
    texts[6] = stream->ReadString();
}

void PartyUnitRecord::Save(ByteStream* stream) {
    stream->WriteString(texts[0]);
    stream->WriteString(texts[2]);
    stream->WriteString(texts[3]);
    stream->WriteString(texts[4]);
    stream->WriteString(texts[5]);
    stream->WriteValue(unitId);
    stats.SaveTo(stream);
    stream->Write(&scaleX, 0xC);
    WriteItemIds(stream, weapons);
    WriteItemIds(stream, quickItems);
    WriteItemIds(stream, armor);
    WriteItemIds(stream, spells);
    stream->WriteString(texts[1]);
    stream->WriteValue(moveMode2);
    stream->WriteValue(weaponIndex);
    stream->WriteValue(selected);
    stream->WriteValue(leaderId);
    stream->WriteValue(fightAtWill);
    stream->WriteString(texts[6]);
}

void Party::LoadParty(ByteStream* stream, int loadVariables) {
    if (itemManager == nullptr) Create();
    g_loadingParty = this;
    Clear(loadVariables, 0);
    zone = stream->ReadString();
    stream->ReadValue(entrance);
    itemManager->Load(stream);
    spellManager->Load(stream);
    itemManager->LoadObjects(stream);
    spellManager->LoadObjects(stream);
    ReadItemIds(stream, items, ItemFactory());
    int records;
    stream->ReadValue(records);
    members.resize(records);
    for (int i = 0; i < records; ++i) members[i].Load(stream);
    if (loadVariables != 0) variables.Load(stream);
    g_loadingParty = nullptr;
    money = 0;
    stream->ReadValue(money);
    if (stream->Version() > kVersionPartyKey) {
        std::uint32_t key;
        stream->ReadValue(key);
        money ^= key;
    }
    itemManager->ReleaseUnused();
    spellManager->ReleaseUnused();
}

void Party::SaveParty(ByteStream* stream, int saveVariables) {
    if (itemManager == nullptr) Create();
    stream->WriteString(zone);
    stream->WriteValue(entrance);
    itemManager->Save(stream);
    spellManager->Save(stream);
    itemManager->SaveObjects(stream);
    spellManager->SaveObjects(stream);
    WriteItemIds(stream, items);
    int records = lib::Count(members);
    stream->WriteValue(records);
    for (int i = 0; i < records; ++i) members[i].Save(stream);
    if (saveVariables != 0) variables.Save(stream);
    std::uint32_t key = g_random.Next();
    stream->WriteValue(money ^ key);
    stream->WriteValue(key);
}

Party* Party::Assign(Party* source) {
    Clear(1, 0);
    CopyThrough(this, source);
    return this;
}

Party::Party(Party& source) : Party() { CopyThrough(this, &source); }

// --- The party as a stream field of an archive --------------------------------------

void Party::ArchiveParty(ChunkArchive* ar) {
    constexpr int kStreamField = 1;
    MemoryStream held;
    if (ar->loading) {
        ar->StreamField(kStreamField, &held);
        held.version = static_cast<std::uint32_t>(ar->version);
        LoadParty(&held, 1);
    } else {
        SaveParty(&held, 1);
        ar->StreamField(kStreamField, &held);
    }
}
