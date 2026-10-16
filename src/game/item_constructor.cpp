// Item construction: CItemConstructor (vtable 00744020) and the CItem methods it drives.
//
// The item manager's CreateItem paths run BeginCreateItem (the manager creates the empty item
// object), SetPrototype / SetMaterial / SetSpellObject, then EndCreateItem, whose Build fills the
// item from a temporary database record: the prototype's values combined with the material's.
#include "item.h"
#include "spell.h"

#include "console.h"
#include "ei/hook.h"
#include "ei/rebirth.h"
#include "ei/vcall.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "lib/stlp_hash.h"
#include "originals_effects.h"
#include "originals_library.h"
#include "originals_engine.h"
#include "originals_gameplay.h"

#include <bit>
#include <cstring>
#include <new>

using namespace itemdb;

namespace {


// Item class ids (GetClassId).
constexpr int kClassWeapon = 0x11;
constexpr int kClassArmor = 0x12;
constexpr int kClassQuick = 0x13;
constexpr int kClassQuest = 0x14;    // QuestItems() (007C8758)
constexpr int kClassLoot = 0x15;     // Loot() (007C8778)
constexpr int kClassSpell = 0x16;    // spell container; quick item table

constexpr int kCategoryWand = 8;     // quick item category whose spell comes from the prototype
constexpr std::uint32_t kFlagConstructing = 8;

// "CItemConstructor::BeginCreateItem.\n", passed by its game address.
const char* const kCantCreateItem = reinterpret_cast<const char*>(0x00796638);

// The charge rate every item starts with (0x3A83126F).
constexpr std::uint32_t kDefaultChargeRate = 0x3A83126F;

// --- The temporary database record ---------------------------------------------------------------
//
// Build creates a record of the item's kind (item_db.h), fills only the fields the item's slot 13
// reads, and destroys it again. The prototype and material records are read without a null check
// where the original reads them without one (a missing record faults there, as in the original).

// A dword copied as a dword (mov), so float bit patterns pass unchanged.
template <typename T> void CopyBits(T& to, const T& from) {
    static_assert(sizeof(T) == 4);
    std::memcpy(&to, &from, sizeof(T));
}

// A record of kind R: the base record's construction (inline in Build; 006A63B0 as a function),
// then the kind's vtable. Null when the allocation fails.
template <typename R> R* NewRecordOf() {
    auto* record = static_cast<R*>(gamecrt::operator_new(sizeof(R)));
    if (!record) return nullptr;
    record->ConstructHead(ItemRecordHead::kBaseVtableAddress);
    ei::SetVtable(record, R::kVtableAddress);
    return record;
}

// Allocates and constructs the temporary record for an item of class `classId`; null for other
// classes (and when the allocation fails).
ItemRecordHead* NewRecord(int classId) {
    switch (classId) {
    case kClassWeapon: {
        auto* record = NewRecordOf<WeaponRecord>();
        if (!record) return nullptr;
        std::memset(record->field1B, 0, 0x83 - 0x78);   // +0x78..+0x82
        return record;
    }
    case kClassArmor:
        return NewRecordOf<ArmorRecord>();
    case kClassQuick:
    case kClassSpell: {
        auto* record = static_cast<QuickItemRecord*>(gamecrt::operator_new(sizeof(QuickItemRecord)));
        if (!record) return nullptr;
        record->ConstructHead(ItemRecordHead::kBaseVtableAddress);
        new (&record->text7C) mfc::CString();   // a CString the record's destructor would free
        ei::SetVtable(record, QuickItemRecord::kVtableAddress);
        std::memset(record->attributeBonus, 0, 0x8B - 0x80);   // +0x80..+0x8A
        return record;
    }
    case kClassQuest: {
        auto* record = static_cast<QuestItemRecord*>(gamecrt::operator_new(sizeof(QuestItemRecord)));
        if (!record) return nullptr;
        record->ConstructHead(ItemRecordHead::kBaseVtableAddress);
        std::memset(&record->zonesBegin, 0, 3 * sizeof(record->zonesBegin));   // an empty STLport vector (0055A3E0)
        ei::SetVtable(record, QuestItemRecord::kVtableAddress);
        return record;
    }
    case kClassLoot:
        return NewRecordOf<LootItemRecord>();
    default:
        return nullptr;
    }
}

// Destroys what every record has (the extra members of the quick, quest and loot records are
// not destroyed: the original leaks them, though they hold nothing here) and frees it.
void DeleteRecord(ItemRecordHead* record) {
    record->list50.~CStringArray();
    record->list20.~CStringArray();
    record->materialClass.~CString();
    record->text08.~CString();
    record->text04.~CString();
    gamecrt::operator_delete(record);
}

// Index of `record` in `table` (the pointer difference divided by the stride, signed).
std::uint32_t IndexIn(const RecordTable& table, const ItemRecordHead* record, int stride) {
    return static_cast<std::uint32_t>(static_cast<int>(reinterpret_cast<const char*>(record) - table.begin) / stride);
}

#pragma warning(disable : 4740) // naked forwarders

} // namespace

// --- CItem ---------------------------------------------------------------------------------------

void CItem::InitFromRecordImpl(const ItemRecordHead* head) {
    // The condition and the charge pass through the FPU in the original (FLD, FST, FSTP), which
    // quiets a signalling NaN; the other fields are copied as dwords.
    x87::CopyFloat(&conditionMax_, &head->condition);
    x87::CopyFloat(&condition_, &head->condition);
    std::memcpy(&chargeRate_, &kDefaultChargeRate, 4);
    x87::CopyFloat(&chargeMax_, &head->charge);
    x87::CopyFloat(&charge_, &head->charge);
    std::memcpy(&weight_, &head->value38, 4);
    std::memcpy(&price_, &head->price, 4);
    value3C_ = head->value3C;
    magicLevel_ = head->magicLevel;
}
EI_REPLACE_METHOD(0x006A0490, CItem::InitFromRecordImpl);

void CItem::AttachSpell(CSpell* spell) {
    CSpell* previous = spell_;
    if (previous) price_ = price_ - std::bit_cast<float>(previous->price_);
    spell_ = spell;
    if (spell) ++spell->refCount_;
    if (previous) previous->Release();
    if (spell_) price_ = std::bit_cast<float>(spell_->price_) + price_;
}
EI_REPLACE_METHOD(0x006A04D0, CItem::AttachSpell);

void CItem::SetSpell(CSpell* spell) {
    CSpell* previous = spell_;
    spell_ = spell;
    if (spell) ++spell->refCount_;
    if (previous) previous->Release();
}
EI_REPLACE_METHOD(0x006A0520, CItem::SetSpell);

// --- CItemConstructor ----------------------------------------------------------------------------

int CItemConstructor::Create() {
    CItemConstructor::Destroy();
    return 1;
}

void CItemConstructor::Destroy() {
    if (manager_) --manager_->refCount_;
    manager_ = nullptr;
    if (item_) item_->Release();
    item_ = nullptr;
    material_ = nullptr;
    prototype_ = nullptr;
    spell_ = nullptr;
}

namespace {
// Slots 2 and 3's entry points (rule 4).
int __fastcall ConstructorCreateHook(CItemConstructor* self) { return self->CItemConstructor::Create(); }
void __fastcall ConstructorDestroyHook(CItemConstructor* self) { self->CItemConstructor::Destroy(); }
} // namespace
EI_REPLACE(0x006A5B90, ConstructorCreateHook);
EI_REPLACE(0x006A5BD0, ConstructorDestroyHook);

void CItemConstructor::DestructBody() {
    ei::SetVtable(this, kVtableAddress);
    CItemConstructor::Destroy();
    DestructBase();
}
EI_REPLACE_METHOD(0x006A5B20, CItemConstructor::DestructBody);

CItemConstructor* CItemConstructor::DeletingDestruct(unsigned flags) {
    DestructBody();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x006A5B00, CItemConstructor::DeletingDestruct);

void CItemConstructor::SetManager(CBaseManager* manager) {
    if (manager_) --manager_->refCount_;
    manager_ = manager;
    if (manager) ++manager->refCount_;
}
EI_REPLACE_METHOD(0x006A5C00, CItemConstructor::SetManager);

void CItemConstructor::SetItem(CItem* item) {
    if (item_) item_->Release();
    item_ = item;
    if (item) ++item->refCount_;
    material_ = nullptr;
    prototype_ = nullptr;
}
EI_REPLACE_METHOD(0x006A5C20, CItemConstructor::SetItem);

BOOL CItemConstructor::BeginCreateItem(int kind, int key) {
    CItem* item = static_cast<CItem*>(manager_->CreateObject(kind, key));
    if (!item) return game::FatalError(kCantCreateItem);
    if (item_) item_->Release();
    item_ = item;
    ++item->refCount_;
    material_ = nullptr;
    prototype_ = nullptr;
    count_ = 1;
    flags_ |= kFlagConstructing;
    return 1;
}
EI_REPLACE_METHOD(0x006A5C50, CItemConstructor::BeginCreateItem);

BOOL CItemConstructor::SetMaterial(char* material) {
    material_ = material;
    return 1;
}
EI_REPLACE_METHOD(0x006A5CC0, CItemConstructor::SetMaterial);

BOOL CItemConstructor::SetPrototype(char* prototype) {
    prototype_ = prototype;
    return 1;
}
EI_REPLACE_METHOD(0x006A5CD0, CItemConstructor::SetPrototype);

BOOL CItemConstructor::SetSpellObject(CSpell* spell) {
    spell_ = spell;
    return 1;
}
EI_REPLACE_METHOD(0x006A5CE0, CItemConstructor::SetSpellObject);

CItem* CItemConstructor::EndCreateItem() {
    if (!Build()) {
        item_->Release();
        item_ = nullptr;
        return nullptr;
    }
    flags_ &= ~kFlagConstructing;
    CItem* item = item_;
    --item->refCount_;
    item_ = nullptr;
    spell_ = nullptr;
    count_ = 0;
    return item;
}
EI_REPLACE_METHOD(0x006A5CF0, CItemConstructor::EndCreateItem);

BOOL CItemConstructor::Cancel() {
    if (item_) item_->Release();
    item_ = nullptr;
    spell_ = nullptr;
    material_ = nullptr;
    prototype_ = nullptr;
    return 1;
}
EI_REPLACE_METHOD(0x006A5D30, CItemConstructor::Cancel);

namespace {

// Build's common part: price, values and charge of the record, from the prototype alone or
// scaled by the material.
void FillCommon(ItemRecordHead* record, const ItemRecordHead* prototype, const MaterialRecordHead* material) {
    if (!material) {
        CopyBits(record->price, prototype->price);
        CopyBits(record->value38, prototype->value38);
        CopyBits(record->value3C, prototype->value3C);
        CopyBits(record->charge, prototype->charge);
        CopyBits(record->condition, prototype->condition);
        CopyBits(record->magicLevel, prototype->magicLevel);
        CopyBits(record->materialUnits, prototype->materialUnits);
        return;
    }
    // Price: the prototype's material units (unsigned, FILD qword) times the material's unit
    // price, plus the prototype's price.
    double units = x87::FromUnsigned(prototype->materialUnits);
    float unitPrice = material->unitPrice, price = prototype->price;
    record->price = static_cast<float>(units * unitPrice + price);
    float value38 = prototype->value38, factor38 = material->factor38;
    record->value38 = value38 * factor38;
    CopyBits(record->value3C, prototype->value3C);
    float charge = prototype->charge, extraCharge = material->magicPower;
    record->charge = charge + extraCharge;
    float condition = prototype->condition, conditionFactor = material->conditionFactor;
    record->condition = condition * conditionFactor;
    record->magicLevel = prototype->magicLevel + material->magicLevel;
    CopyBits(record->materialUnits, prototype->materialUnits);
}

// Build's per-class part; returns the prototype's index in its table.
std::uint32_t FillClass(int classId, ItemRecordHead* record, const ItemRecordHead* prototype,
                        const MaterialRecordHead* material) {
    switch (classId) {
    case kClassWeapon: {
        auto* to = static_cast<WeaponRecord*>(record);
        auto* from = static_cast<const WeaponRecord*>(prototype);
        CopyBits(to->reach, from->reach);
        float damage = from->damage, factor = material->damageFactor;
        to->damage = damage * factor;
        float spread = from->damageSpread, factor2 = material->damageFactor;
        to->damageSpread = spread * factor2;
        return IndexIn(Weapons(), prototype, kWeaponSize);
    }
    case kClassArmor: {
        auto* to = static_cast<ArmorRecord*>(record);
        auto* from = static_cast<const ArmorRecord*>(prototype);
        to->firstFlags = from->firstFlags | material->armourFlags;
        to->othersFlags = from->othersFlags | material->armourFlags;
        // Seven absorption values in two rows, each scaled by the material's factor for its
        // damage type and by the row's factor.
        for (int k = 0; k < 7; ++k) {
            float a = from->absorbFirst[k], b = material->absorbFactors[k], c = from->FirstFactor();
            to->absorbFirst[k] = a * b * c;
            float d = from->absorbOthers[k], e = material->absorbFactors[k], f = from->othersFactor;
            to->absorbOthers[k] = d * e * f;
        }
        return IndexIn(Armors(), prototype, kArmorSize);
    }
    case kClassQuick:
    case kClassSpell:
        CopyBits(static_cast<QuickItemRecord*>(record)->value70, static_cast<const QuickItemRecord*>(prototype)->value70);
        return IndexIn(QuickItems(), prototype, kQuickItemSize);
    case kClassQuest:
        CopyBits(record->category, prototype->category);
        return IndexIn(QuestItems(), prototype, kQuestItemSize);
    case kClassLoot: {
        auto* to = static_cast<LootItemRecord*>(record);
        auto* from = static_cast<const LootItemRecord*>(prototype);
        CopyBits(to->category, from->category);
        CopyBits(to->recordType, from->recordType);
        // Kind 1 (a material) takes the material's index when one is given.
        if (from->category == 1 && material)
            CopyBits(to->index, material->number);
        else
            CopyBits(to->index, from->index);
        return IndexIn(Loot(), prototype, kLootSize);
    }
    default:
        return 0;   // not reached: the record of an unknown class is null and faulted above
    }
}

} // namespace

BOOL CItemConstructor::Build() {
    int classId = item_->GetClassId();
    if ((classId == kClassWeapon || item_->GetClassId() == kClassArmor) && !material_) return 0;

    auto* prototype = reinterpret_cast<const ItemRecordHead*>(prototype_);
    auto* material = reinterpret_cast<const MaterialRecordHead*>(material_);
    ItemRecordHead* record = NewRecord(item_->GetClassId());
    FillCommon(record, prototype, material);
    std::uint32_t index = FillClass(item_->GetClassId(), record, prototype, material);

    item_->InitFromRecord(record);
    if (item_->GetClassId() == kClassQuick && static_cast<int>(prototype->category) == kCategoryWand) {
        CSpellManager* spellManager = static_cast<CManagerContext*>(manager_->context_)->SpellManager();
        CSpell* spell = spellManager->CreateSpellFromText(static_cast<const char*>(static_cast<const QuickItemRecord*>(prototype)->text7C));
        spell_ = spell;
        item_->SetSpell(spell);
    } else if (spell_) {
        item_->AttachSpell(spell_);
    }
    if (record) DeleteRecord(record);

    if (material)
        item_->prototypeId_ = (material->number & 0xFFFF) | index << 16;
    else
        item_->prototypeId_ = index << 16;
    item_->count_ = static_cast<std::uint32_t>(count_);
    return 1;
}
EI_REPLACE_METHOD(0x006A5D60, CItemConstructor::Build);

// --- The item manager's CreateItem (006AC970) ----------------------------------------------------

namespace {

constexpr int kKindSpellContainer = 0x3008;
constexpr int kLootKindTable = 2;             // a loot item (class 0x15) whose +0x50 names a table

const char* const kSpellContainer = reinterpret_cast<const char*>(0x007938A0);   // "spell container"
const char* const kMaterialType = reinterpret_cast<const char*>(0x007938C4);     // "material"
const char* const kNoPrototype = reinterpret_cast<const char*>(0x007966BC);      // "Can't find prototype for item \"%s\".\n"
const char* const kCantCreate = reinterpret_cast<const char*>(0x00796684);       // "Can't create item \"%s\".\nCheck shape and material"

// CString members used here (MFC; the original implementations).
struct ItemText : mfc::CString {
    int Find(char c) const;               // 00700E91
    void TrimSpaceLeft() { CStringTrimLeftOriginal(this); }     // 006FE216 TrimLeft() (moves the text, memmove)
    void TrimSpaceRight() { CStringTrimRightOriginal(this); }   // 006FE11F TrimRight()
};
int ItemText::Find(char c) const { return CStringFindCharOriginal(this, nullptr, c); }
ItemText& AsText(mfc::CString& s) { return static_cast<ItemText&>(s); }

// 006E80D8 atoi.

// 004DBE40 (ECX text, EDX array, separator on the stack). Splits `text` at `separator`.

// Prototype lookups by name: weapons 004AB720 (007C86F8), armors 004AB820 (007C8718), quick items
// 00613820 (007C8738), the class-0x14 table 006AD580 (007C8758), the class-0x15 table 0069D380
// (007C8778); materials 0069D290 (007C86D8). Each returns the record or null.
void* const kWeaponTable = reinterpret_cast<void*>(0x007C86F8);
void* const kArmorTable = reinterpret_cast<void*>(0x007C8718);
void* const kQuickTable = reinterpret_cast<void*>(0x007C8738);
void* const kTable14 = reinterpret_cast<void*>(0x007C8758);
void* const kTable15 = reinterpret_cast<void*>(0x007C8778);
void* const kMaterialTable = reinterpret_cast<void*>(0x007C86D8);

// 006AD000 (RET 0x10). The item manager makes an item (kind, record index, a, b).
CItem* __fastcall ManagerMakeItem(CItemManager* a0, void*, int a2, int a3, int a4, int a5) { return (CItem*)a0->MakeItem(a2, a3, a4, a5); }

// The string-keyed maps the loot item's table index comes from (STLport strings handled through
// the original members; see class_templates.cpp): operator[] of the weapon (007C8704, 006AD670),
// armor (007C8724, 006A75E0), quick item (007C8744, 004ABBD0) and spell (007C851C, 004ABD00) maps,
// and the modifier map's find_or_insert (007C853C, 004ACC10) of a (name, null) pair (00689950).
struct NameKey {
    char* start;
    char* finish;
    char* eos;
};
struct NamePair {
    NameKey name;
    void* record;
};
static NameKey* KeyFromChars(NameKey* s, void*, const char* text, const void* alloc) { return static_cast<NameKey*>(::StringInitFromChars(s, nullptr, text, alloc)); }
__declspec(naked) void __fastcall PairConstruct(NamePair*, void*, const NameKey*, void* const*) EI_ORIGINAL(0x00689950)
__declspec(naked) char** __fastcall WeaponNameAt(void*, void*, const NameKey*) EI_ORIGINAL(0x006AD670)
__declspec(naked) char** __fastcall ArmorNameAt(void*, void*, const NameKey*) EI_ORIGINAL(0x006A75E0)

// Index of `record` in `table` (signed division of the byte offset), -1 for no record.
int TableIndex(const RecordTable& table, const char* record, int stride) {
    return record ? static_cast<int>(record - table.begin) / stride : -1;
}

// The table index of a loot item's second name part (`which`: its +0x50).
int LootTableIndex(int which, const char* name, const void* allocator) {
    NameKey key;
    char* record;
    switch (which) {
    case 0:
        KeyFromChars(&key, nullptr, name, allocator);
        record = *WeaponNameAt(reinterpret_cast<void*>(0x007C8704), nullptr, &key);
        StlStringDestroy(&key);
        return TableIndex(Weapons(), record, kWeaponSize);
    case 1:
        KeyFromChars(&key, nullptr, name, allocator);
        record = *ArmorNameAt(reinterpret_cast<void*>(0x007C8724), nullptr, &key);
        StlStringDestroy(&key);
        return TableIndex(Armors(), record, kArmorSize);
    case 2:
        KeyFromChars(&key, nullptr, name, allocator);
        record = *(char**)QuickItemMapAtOriginal(reinterpret_cast<void*>(0x007C8744), nullptr, (const void*)&key);
        StlStringDestroy(&key);
        return TableIndex(QuickItems(), record, kQuickItemSize);
    case 3:
        StringInitWithAllocator(&key, nullptr, allocator);
        StringAssignSpan(&key, nullptr, name, name + std::strlen(name));
        record = *(char**)SpellMapAtOriginal(reinterpret_cast<void*>(0x007C851C), nullptr, (const void*)&key);
        StlStringDestroy(&key);
        return TableIndex(Spells(), record, kSpellSize);
    case 4: {
        StringInitWithAllocator(&key, nullptr, allocator);
        StringAssignSpan(&key, nullptr, name, name + std::strlen(name));
        void* none = nullptr;
        NamePair pair;
        PairConstruct(&pair, nullptr, &key, &none);
        void** slot = &((NamePair*)ModifierMapFindOrInsertOriginal(reinterpret_cast<void*>(0x007C853C), nullptr, (const void*)&pair))->record;
        StlStringDestroy(&pair.name);
        record = static_cast<char*>(*slot);
        StringRepDestroy(&key);
        return TableIndex(SpellModifiers(), record, kSpellModifierSize);
    }
    default:
        return 0;
    }
}

} // namespace

CItem* CItemManager::CreateItem(const char* name) {
    auto* context = static_cast<CManagerContext*>(context_);
    CItemConstructor* constructor = context->ItemConstructor();
    mfc::CString extra;               // the part in brackets: "name[extra]"
    mfc::CString text(name);
    console::CStringArray parts;      // the name split at '.'
    int bracket = AsText(text).Find('[');
    if (bracket != -1) {
        extra = AsText(text).Right(console::Length(text) - bracket - 1);
        console::Ops(extra).TrimLeft('[');
        console::Ops(extra).TrimRight(']');
        AsText(extra).TrimSpaceLeft();
        AsText(extra).TrimSpaceRight();
        text = text.Left(bracket);
        AsText(text).TrimSpaceRight();
    }
    StringSplit(text, &parts, '.');
    for (int i = 0; i < parts.size; ++i) parts.data[i].MakeLower();

    // "spell container[spell text]": a scroll holding that spell.
    if (gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(static_cast<const char*>(parts.data[0])),
                        reinterpret_cast<const unsigned char*>(kSpellContainer)) == 0) {
        const char* spellText = extra;
        CSpell* spell = context->SpellManager()->CreateSpellFromText(spellText);
        CItem* item = nullptr;
        if (spell) {
            char* record = (char*)stlp::RecordNamed(static_cast<stlp::RecordsByName*>(kQuickTable), nullptr, kSpellContainer);
            int index = static_cast<int>(record - QuickItems().begin) / kQuickItemSize;
            item = ManagerMakeItem(this, nullptr, kKindSpellContainer, index, 0, -1);
            if (item) item->AttachSpell(spell);
        }
        return item;
    }

    // "prototype[.material]".
    const char* prototypeName = parts.data[0];
    char* prototype = (char*)stlp::RecordNamed(static_cast<stlp::RecordsByName*>(kWeaponTable), nullptr, prototypeName);
    if (!prototype) prototype = (char*)stlp::RecordNamed(static_cast<stlp::RecordsByName*>(kArmorTable), nullptr, prototypeName);
    if (!prototype) prototype = (char*)stlp::RecordNamed(static_cast<stlp::RecordsByName*>(kQuickTable), nullptr, prototypeName);
    if (!prototype) prototype = static_cast<char*>(stlp::RecordNamed(static_cast<stlp::RecordsByName*>(kTable14), nullptr, prototypeName));
    if (!prototype) prototype = static_cast<char*>(stlp::RecordNamed(static_cast<stlp::RecordsByName*>(kTable15), nullptr, prototypeName));
    if (!prototype) {
        // Rebirth's FixCreateItem (context hook at 006ACBBD) logs instead of the fatal error.
        if (!ei::rebirth::Enabled(ei::rebirth::Patch::FixCreateItem))
            game::FatalError(kNoPrototype, static_cast<const char*>(parts.data[0]));
        return nullptr;
    }
    constructor->SetManager(this);
    constructor->BeginCreateItem(RecordSlots(reinterpret_cast<ItemRecordHead*>(prototype))->Kind(), -1);
    char* material = nullptr;
    if (parts.size > 1) material = static_cast<char*>(stlp::RecordNamed(static_cast<stlp::RecordsByName*>(kMaterialTable), nullptr, parts.data[1]));
    constructor->SetPrototype(prototype);
    constructor->SetMaterial(material);
    // The bracket part: a spell for most prototypes, a number (+0x30, the count) for materials.
    auto* type = reinterpret_cast<const unsigned char*>(static_cast<const char*>(reinterpret_cast<ItemRecordHead*>(prototype)->text04));
    if (console::Length(extra) != 0 && gamecrt::mbscmp(type, reinterpret_cast<const unsigned char*>(kMaterialType)) != 0) {
        CSpellManager* spells = context->SpellManager();
        if (CSpell* spell = spells->CreateSpellFromText(extra)) constructor->SetSpellObject(spell);
    } else if (gamecrt::mbscmp(type, reinterpret_cast<const unsigned char*>(kMaterialType)) == 0 &&
               console::Length(extra) != 0) {
        constructor->count_ = GameAtoi(extra);
    }
    CItem* item = constructor->EndCreateItem();
    if (!item) {
        // Rebirth's FixCreateItem (context hook at 006ACCD0): logged, and no item. The original
        // reports the error and goes on with the null item (and crashes).
        if (ei::rebirth::Enabled(ei::rebirth::Patch::FixCreateItem)) return nullptr;
        game::FatalError(kCantCreate, name);
    }
    constructor->SetManager(nullptr);
    auto* loot = static_cast<CItemLoot*>(item);
    if (item->GetClassId() == kClassLoot && static_cast<int>(loot->kind_) == kLootKindTable && parts.size > 1) {
        int which = static_cast<int>(loot->recordType_);
        loot->index_ = static_cast<std::uint32_t>(LootTableIndex(which, parts.data[1], &name));
    }
    return item;
}
EI_REPLACE_METHOD(0x006AC970, CItemManager::CreateItem);

// --- The item manager's MakeItem (006AD000, A-H20) -------------------------------------------------

CItem* CItemManager::MakeItem(int kind, std::uint32_t index, std::uint32_t material, int key) {
    CItemConstructor* constructor = static_cast<CManagerContext*>(context_)->ItemConstructor();
    if (static_cast<std::int16_t>(index) == -1) index = 0;
    constructor->SetManager(this);
    constructor->BeginCreateItem(kind, key);
    char* prototype = nullptr;
    char* materialRecord = nullptr;
    switch (kind) {
    case kItemWeapon:
        prototype = Weapons().RecordAt(index, kWeaponSize);
        materialRecord = Materials().RecordAt(material, kMaterialSize);
        break;
    case kItemArmor:
        prototype = Armors().RecordAt(index, kArmorSize);
        materialRecord = Materials().RecordAt(material, kMaterialSize);
        break;
    case kItemQuick:
    case kItemSpell:
        prototype = QuickItems().RecordAt(index, kQuickItemSize);
        break;
    case kItemLoot:
        prototype = Loot().RecordAt(index, kLootSize);
        break;
    case kItemQuest:
        prototype = QuestItems().RecordAt(index, kQuestItemSize);
        break;
    default:
        break;
    }
    constructor->SetPrototype(prototype);
    constructor->SetMaterial(materialRecord);
    CItem* item = constructor->EndCreateItem();
    constructor->SetManager(nullptr);
    return item;
}
EI_REPLACE_METHOD(0x006AD000, CItemManager::MakeItem);

// --- A-H25: the scroll of a spell text, the name lists, the constructor's magic capacity ----------

// The "spell container" record is looked up by name each time; without one the index is that of a
// null record (kept: MakeItem then finds no prototype).
CItem* CItemManager::CreateSpellItem(const char* spellText, CSpellManager* spells) {
    CSpell* spell = static_cast<CSpellManager*>(spells)->CreateSpellFromText(spellText);
    if (!spell) return nullptr;
    char* record = (char*)stlp::RecordNamed(static_cast<stlp::RecordsByName*>(kQuickTable), nullptr, kSpellContainer);
    int index = static_cast<int>(record - QuickItems().begin) / kQuickItemSize;
    CItem* item = ManagerMakeItem(this, nullptr, kKindSpellContainer, index, 0, -1);
    if (item) item->AttachSpell(spell);
    return item;
}
EI_REPLACE_METHOD(0x006AD1E0, CItemManager::CreateSpellItem);

namespace {

// The lists' loop: the array takes the table's count, then every element the name of the record
// with its index (the count is read again for each; a record out of range would be read at null).
template <typename Record, typename NameOf>
void ListRecordNames(const RecordTable& table, mfc::CStringArray* names, NameOf nameOf) {
    names->SetSize(static_cast<int>(table.end - table.begin) / static_cast<int>(sizeof(Record)), -1);
    for (int i = 0; i < names->size; ++i) {
        auto* record = reinterpret_cast<const Record*>(table.RecordAt(static_cast<std::uint32_t>(i), sizeof(Record)));
        names->data[i] = *nameOf(record);
    }
}

const mfc::CString* HeadName(const ItemRecordHead* record) { return &record->text04; }

} // namespace

void CItemManager::ListMaterialNames(mfc::CStringArray* names) {
    // The key is a CString (MaterialRecordHead declares its characters).
    ListRecordNames<MaterialRecordHead>(Materials(), names, [](const MaterialRecordHead* record) {
        return reinterpret_cast<const mfc::CString*>(&record->key);
    });
}
void CItemManager::ListWeaponNames(mfc::CStringArray* names) { ListRecordNames<WeaponRecord>(Weapons(), names, HeadName); }
void CItemManager::ListArmorNames(mfc::CStringArray* names) { ListRecordNames<ArmorRecord>(Armors(), names, HeadName); }
void CItemManager::ListQuickItemNames(mfc::CStringArray* names) {
    ListRecordNames<QuickItemRecord>(QuickItems(), names, HeadName);
}
EI_REPLACE_METHOD(0x006AD260, CItemManager::ListMaterialNames);
EI_REPLACE_METHOD(0x006AD2F0, CItemManager::ListWeaponNames);
EI_REPLACE_METHOD(0x006AD380, CItemManager::ListArmorNames);
EI_REPLACE_METHOD(0x006AD410, CItemManager::ListQuickItemNames);

bool CItemConstructor::MagicCapacity(float* level, float* power) {
    if ((item_->GetClassId() == kClassWeapon || item_->GetClassId() == kClassArmor) && !material_) return false;
    auto* prototype = reinterpret_cast<const ItemRecordHead*>(prototype_);
    auto* material = reinterpret_cast<const MaterialRecordHead*>(material_);
    if (material) {
        *level = static_cast<float>(x87::FromUnsigned(prototype->magicLevel + material->magicLevel));
        *power = prototype->charge + material->magicPower;
    } else {
        *level = static_cast<float>(x87::FromUnsigned(prototype->magicLevel));
        *power = prototype->charge;
    }
    return true;
}
EI_REPLACE_METHOD(0x006A6480, CItemConstructor::MagicCapacity);
