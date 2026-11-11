// The item description panel: what the camp, the multiplayer hero screen and the exchange window write about the item or
// statistic under the mouse (ui_item_info.h, docs/notes/ui.md, "Item descriptions").
#include "game/players/party.h"
#include "game/ui/ui_screens.h"
#include "engine/core/text_split.h"
#include "game/ui/ui_item_info.h"

#include "game/console/console.h"
#include "game/items/item.h"
#include "game/items/item_db.h"
#include "game/spells/spell_db.h"
#include "engine/data/text_manager.h"
#include "game/spells/spell.h"
#include "game/units/unit.h"
#include "game/units/unit_db.h"

#include "lib/maths.h"
#include "lib/crt.h"
#include "lib/rect.h"
#include "platform/platform.h"
#include <bit>
#include <cstdint>
#include <cstring>
#include <new>
#include <string>

#include <concepts>
#include <vector>
#include "game/app/singletons.h"
#include "game/world/world.h"
#include "lib/str.h"
#include <format>

namespace {

constexpr float kPercentScale = 100.0f;
constexpr float kStatBase = 25.0f;        // an attribute's neutral value
constexpr float kIntelligenceGrowth = 1.04f;

// pow(base, exponent - subtrahend) * scale, truncated.
int PowFtol(float base, float exponent, float subtrahend, float scale) {
    const float power = lib::Pow(base, exponent - subtrahend);
    return static_cast<int>(power * scale);
}
// b + a truncated; ScaledFtol the same of a * scale.
int SumFtol(float a, float b) { return static_cast<int>(b + a); }
int SumFtolScaled(float a, float b, float scale) { return static_cast<int>((b + a) * scale); }
int ScaledFtol(float value, float scale) { return static_cast<int>(value * scale); }
int DifferenceFtol(float minuend, float subtrahend) { return static_cast<int>(minuend - subtrahend); }
// value * factor * scale in that order, stored as float.
float ArmourValue(float value, float factor, float scale) {
    return value * factor * scale;
}
// 0 + p[0] + p[0x12] + p[0x24] (three armour layers 0x48 bytes apart), stored as float.
float LayerSum(const float* p) { return ((0.0f + p[0]) + p[0x12]) + p[0x24]; }



constexpr std::uint32_t kWhite = 0xFFFFFF;

class Writer {
public:
    explicit Writer(ItemInfoPanel* newPanel)
        : panel(newPanel), rect{15, 10, 185, 25} {}

    const std::string& T(int index) const { return panel->itemTexts[index]; }
    const std::string& Skill(int index) const { return panel->skillTexts[index]; }

    void Down() { rect.Offset(0, 15); }
    // A label of the panel's surface in font 0 at `at` (wrapped with maxLines).
    void Add(std::string_view text, const Rect& at, std::uint32_t colour, render::text::Format format, int maxLines = 0) {
        panel->labels.push_back({std::string(text), at, {0, colour, format}, maxLines});
    }
    void Text(std::string_view text) { Add(text, rect, kUiTextColour, render::text::Format::Left); }
    void Value(std::string_view text) {
        Rect shifted = rect;
        shifted.Offset(-10, 0);
        Add(text, shifted, kUiTextColour, render::text::Format::Right);
    }
    void Title(std::string_view text) { Add(text, rect, kWhite, render::text::Format::WordBreak | render::text::Format::Center, 2); }
    void Wrapped(std::string_view text, int maxLines) { Add(text, rect, kUiTextColour, render::text::Format::WordBreak | render::text::Format::Center, maxLines); }

    void Head(const std::string& name, const std::string& kind) {
        Title(name);
        Down();
        Down();
        line = std::format("{} {}", T(37), kind);
        Text(line);
        Down();
    }
    void Number(int label, int value) {
        line = std::format("{} {}", T(label), value);
        Text(line);
        Down();
    }
    void Joined(int label, std::string_view text) {
        line = T(label);
        line += " ";
        line += text;
        Text(line);
        Down();
    }
    // The lists of 7 damage values: "<label> <threshold>" and, when some kinds lie below it,
    // "<infoitem_24> <those kinds>".
    void DamageLines(const float* values, int label) {
        const DamageLists lists = ItemDamageLists(values);
        Joined(label, lists.threshold);
        if (!lists.below.empty()) Joined(24, lists.below);
    }
    void School(int school) {
        std::string key = std::format("school_{}", school);
        line = g_clientWorld.texts->GetString(key);
        std::string text = line;
        Joined(18, text);
    }

    std::string line;

private:
    ItemInfoPanel* panel = nullptr;

public:
    Rect rect;
};

// Whether a 7-entry damage weight array has a weight that compares unequal (and ordered) to 0: returns the
// index, or 7.
unsigned FirstDamageKind(const std::array<float, kDamageTypeCount>& weights) {
    unsigned i = 0;
    for (; i < 7; ++i) {
        if (weights[i] != 0.0f) break;
    }
    return i;
}

void SpellLevelLine(Writer& w, bool known, int level) {
    if (!known)
        w.line = std::format("{} {}", w.T(8), w.T(28));
    else
        w.line = std::format("{} {}", w.T(8), level);
    w.Text(w.line);
    w.Down();
}
// The duration: "<infoitem_12> <infoitem_21>" under 2, else in seconds (the unsigned value times 0.0666667f)
// with infoitem_27.
void DurationLine(Writer& w, std::uint32_t ticks) {
    if (ticks < 2)
        w.line = std::format("{} {}", w.T(12), w.T(21));
    else
        w.line = str::Format("%s %.1f%s", w.T(12),
                      ticks * 0.06666667f, w.T(27));
    w.Text(w.line);
    w.Down();
}

void AttributeLines(Writer& w, UnitStats* stats, int index) {
    auto line = [&](const char* format, int label, int value) {
        w.line = str::Format(format, w.Skill(label), value);
        w.Text(w.line);
        w.Down();
    };
    switch (index) {
    case 0:
        line("%s %d%%", 0, static_cast<int>(stats->StrengthRatio() * kPercentScale));
        line("%s %d", 1, static_cast<int>(stats->ScaledStrength()));
        w.Down();
        break;
    case 1:
        line("%s %d%%", 2, static_cast<int>(stats->DexterityRatio() * kPercentScale));
        line("%s %d", 3, static_cast<int>(stats->RateValue()));
        w.Down();
        break;
    case 2:
        // 1.04 ^ (intelligence - 25) * 100.
        line("%s %d%%", 4, PowFtol(kIntelligenceGrowth, stats->intelligence, kStatBase, kPercentScale));
        line("%s %d%%", 24, static_cast<int>(stats->Period20() * kPercentScale));
        w.Down();
        break;
    default:
        break;
    }
}

void PerkLines(Writer& w, const ItemHoverInfo& info, std::string& title) {
    TextManager* texts = g_clientWorld.texts.get();
    std::string name = info.text;
    str::TrimRight(name, "0");
    name += "1";
    int group = g_skillDatabase.IndexOf(name) / 3;
    for (int i = 0; i < 3; ++i) {
        const SkillLevelRecord* level = unitdb::SkillLevelAt(static_cast<std::uint32_t>(group * 3 + i));
        if (!level) continue;
        std::string key("perk ");
        key += level->name;
        if (texts->Open(key)) title = texts->ReadLine(true);
        w.Text(title);
        if (info.index > i) w.Add(w.Skill(21), w.rect, kUiTextColour, render::text::Format::Right);    // learned
        w.Down();
        int value = static_cast<int>(level->values[7]);
        const char* percent = "     %s %d%%";
        const char* plain = "     %s %d";
        bool drawn = true;
        switch (group) {
        case 0: case 1: case 2: case 3: case 4: case 5: case 6:
            w.line = str::Format(plain, w.Skill(10), value);
            break;
        case 7: case 8: case 9: case 10: case 11: case 12: case 13: case 14:
            w.line = str::Format(plain, w.Skill(11), value);
            break;
        case 15:
            w.line = str::Format(percent, w.Skill(12), static_cast<std::uint32_t>(value + 100) >> 1);
            break;
        case 16: w.line = str::Format(percent, w.Skill(14), value + 100); break;
        case 17: w.line = str::Format(percent, w.Skill(15), value + 100); break;
        case 18: w.line = str::Format(percent, w.Skill(16), value + 100); break;
        case 19: w.line = str::Format(percent, w.Skill(17), value + 100); break;
        case 20: w.line = str::Format(percent, w.Skill(3), value + 100); break;
        case 21: w.line = str::Format(percent, w.Skill(1), value + 100); break;
        case 22: w.line = str::Format(percent, w.Skill(22), value + 300); break;
        case 23: case 24: case 25:
            w.line = str::Format(plain, w.Skill(23), value);
            break;
        default:
            drawn = false;
            break;
        }
        if (drawn) w.Text(w.line);
        w.Down();
        // The cost (negative: 0x7FFFFFFF), on a line 15 pixels wider.
        w.rect.right += 15;
        int cost = info.stats->SkillPrice(level);
        if (cost < 0) cost = 0x7FFFFFFF;
        w.line = str::Format(plain, w.Skill(18), cost);
        w.Text(w.line);
        w.rect.right -= 15;
        w.Down();
    }
}

// HoverKind::Weapon, the weapon's attack or defence: its four parts and their sum (or, for the defence of a
// ranged weapon, infoitem_34).
void WeaponLines(Writer& w, const ItemHoverInfo& info) {
    ItemWeapon* weapon = info.weapon;
    UnitStats* stats = info.stats;
    int type = static_cast<int>(weapon->GetType());
    if (str::Compare(info.text, "defense") == 0 && weapon->IsRanged()) {
        w.Wrapped(w.T(34), 2);
        w.Down();
        return;
    }
    auto part = [&](int label, int value) {
        w.line = std::format("     {}", w.T(label));
        w.Text(w.line);
        w.line = std::format("{}", value);
        w.Value(w.line);
    };
    int base = DifferenceFtol(stats->dexterity, kStatBase);   // dexterity - 25
    part(29, base);
    int sum = base;
    w.Down();
    int skill = static_cast<int>(stats->WeaponSkill(type));
    part(30, skill);
    sum += skill;
    w.Down();
    int level = static_cast<int>(stats->SkillLevelValue(type));
    part(31, level);
    sum += level;
    w.Down();
    w.line = std::format("     {}", w.T(32));
    w.Text(w.line);
    int own = static_cast<int>(WeaponPrototypeSkillBonus(weapon->prototypeId));
    if (str::Compare(info.text, "defense") == 0)
        own += static_cast<int>(WeaponDefenceBonus(weapon->prototypeId));
    else
        own += static_cast<int>(WeaponAttackBonus(weapon->prototypeId));
    w.line = std::format("{}", own);
    w.Value(w.line);
    sum += own;
    w.Down();
    w.Down();
    part(33, sum);
}

} // namespace

std::uint32_t ItemCacheTime() { return platform::FrameClock(); }

DamageLists ItemDamageLists(const float* values) {
    TextManager* texts = g_clientWorld.texts.get();
    DamageLists lists;
    std::string key, text;
    float threshold = values[6];
    lists.threshold = str::Format("%.1f", threshold);
    for (int i = 0; i < 6; ++i) {
        std::string* list;
        if (values[i] > threshold)
            list = &lists.above;
        else if (values[i] == threshold)
            continue;
        else
            list = &lists.below;   // also NaN
        key = std::format("dmg_{}", i);
        text = texts->GetString(key);
        *list += text;
        *list += ", ";
    }
    str::TrimRight(lists.above, ", ");
    str::TrimRight(lists.below, ", ");
    return lists;
}

void ItemInfoPanel::DescribeItem() {
    Item* item = FirstItem();
    labels.clear();
    if (!item) return;

    // A loot item standing for a material or a record, when the record exists (the predicates test it; one
    // without is described as plain loot).
    if (IsMaterial(item)) {
        DescribeMaterial();
        return;
    }
    if (IsPart(item)) {
        DescribeBlueprint();
        return;
    }
    if (IsSpell(item)) {
        DescribeSpellRecord();
        return;
    }
    if (IsModifier(item)) {
        DescribeModifierRecord();
        return;
    }
    ItemKind kind = item->Kind();
    if (kind == ItemKind::Quick && item->GetCategory() == QuickItemCategory::Potion) {
        DescribeQuickItem();
        return;
    }
    if ((g_interface.ctrlDown && item->spell) || kind == ItemKind::Scroll) {
        DescribeCarriedSpell();
        return;
    }

    TextManager* texts = g_clientWorld.texts.get();
    // The material's name ("matshort <NAME>"): the record's name with '_' for ' ', lower case; empty without the
    // record (an item of another database: "Unknown").
    const itemdb::MaterialRecordHead* materialRecord = itemdb::MaterialAt(MaterialIndex(item->prototypeId));
    std::string material = materialRecord ? materialRecord->key : std::string();
    text::ReplaceChar(&material, ' ', '_');
    str::MakeLower(material);
    auto [name, description, kindText] = texts->GetItemTexts(item);

    Writer w(this);
    std::string text;
    w.Head(name, kindText);

    // The material and count, unless a quick item whose prototype's field is "none"; then the price.
    bool materialLine;
    if (kind == ItemKind::Quick) {
        const itemdb::ItemRecordHead* prototype = item->GetPrototype();
        materialLine = !prototype || prototype->HasMaterial();
    } else {
        materialLine = kind != ItemKind::Loot && kind != ItemKind::Quest;
    }
    if (materialLine) {
        text = "Unknown";
        std::string key("matshort ");
        key += material;
        if (texts->Open(key)) text = texts->ReadLine(true);
        w.line = std::format("{} {} x {}", w.T(1), text,
                      static_cast<int>(item->GetPrototype()->materialUnits));
        w.Text(w.line);
        w.Down();
    }
    if (kind != ItemKind::Quest) w.Number(0, static_cast<int>(item->weight));

    if (kind == ItemKind::Armor || kind == ItemKind::Weapon || (kind == ItemKind::Quick && item->GetCategory() == QuickItemCategory::Wand)) {
        Spell* spell = item->spell;
        int spellValue = spell ? static_cast<int>(spell->skillNeeded) : 0;
        w.Number(2, static_cast<int>(item->chargeMax));
        w.line = std::format("{} {}/{}", w.T(14), spellValue, static_cast<int>(item->magicLevel));
        w.Text(w.line);
        w.Down();
    }

    if (kind == ItemKind::Armor || kind == ItemKind::Weapon) {
        // The condition (current only when positive).
        float current = item->condition;
        int condition = current > 0.0f ? static_cast<int>(current) : 0;
        int maximum = static_cast<int>(item->conditionMax);
        w.line = std::format("{} {}/{}", w.T(3), condition, maximum);
        w.Text(w.line);
        w.Down();
        if (kind == ItemKind::Weapon) {
            ItemWeapon* weapon = item->AsWeapon();
            // Damage: min-max and the first damage kind with a weight ("damage<n>"; the text
            // keeps the material line's otherwise).
            unsigned first = FirstDamageKind(weapon->DamageTypes());
            if (first < 7) {
                w.line = std::format("damage_{}", first);
                text = texts->GetString(w.line);
            }
            float low = weapon->damage;
            float spread = weapon->damageSpread;
            int high = SumFtol(low, spread);
            int lowValue = static_cast<int>(low);
            w.line = std::format("{} {}-{} {}", w.T(15), lowValue, high,
                          text);
            w.Text(w.line);
            w.Down();
            w.Number(20, static_cast<int>(item->GetPrototype()->category));
            w.Number(38, static_cast<int>(WeaponAttackBonus(item->prototypeId)));
            if (weapon->IsRanged())
                w.Number(10, static_cast<int>(weapon->reach));
            else
                w.Number(39, static_cast<int>(WeaponDefenceBonus(item->prototypeId)));
        }
    }
    if (kind == ItemKind::Armor) {
        w.DamageLines(item->AsArmor()->absorb[0].data(), 25);
    }

    w.Down();
    if (kind == ItemKind::Loot) {
        text = texts->GetString("loot_item");
        description += text;
        w.Wrapped(description, 7);
    } else {
        w.Wrapped(description, 4);
        w.Down();
        w.Down();
        w.Down();
        w.Down();
        if (kind != ItemKind::Weapon) w.Down();
        Spell* spell;
        if ((kind == ItemKind::Armor || kind == ItemKind::Weapon || (kind == ItemKind::Quick && item->GetCategory() == QuickItemCategory::Wand)) &&
            (spell = item->spell) != nullptr) {
            text = "Unknown";
            std::string key("spell ");
            key += spell->GetPrototype()->code;
            if (texts->Open(key)) text = texts->ReadLine(true);
            w.line = std::format("{} {}", w.T(5), text);
            w.Text(w.line);
            w.Down();
            w.Text(w.T(6));
        }
    }
}

void ItemInfoPanel::DescribeMaterial() {
    Item* item = FirstItem();
    TextManager* texts = g_clientWorld.texts.get();
    const itemdb::MaterialRecordHead* material = itemdb::MaterialAt(item->AsLoot()->index);
    auto [name, description, kindText] = texts->GetItemTexts(item);
    Writer w(this);
    w.Head(name, kindText);
    std::string text = material->soundName;
    str::MakeLower(text);
    w.line = texts->GetString("mattype_" + text);
    const std::string typeText = w.line;
    w.Joined(17, typeText);
    w.Number(0, static_cast<int>(material->weightFactor));
    w.Number(2, static_cast<int>(material->magicPower));
    w.Number(14, static_cast<int>(material->magicLevel));
    w.Number(3, static_cast<int>(material->conditionFactor));
    w.line = str::Format("%s %.1f", w.T(15), material->damageFactor);
    w.Text(w.line);
    w.Down();
    float values[7];
    std::memcpy(values, material->absorbFactors, sizeof(values));
    w.DamageLines(values, 4);
    w.Down();
    w.Wrapped(description, 4);
}

void ItemInfoPanel::DescribeBlueprint() {
    Item* item = FirstItem();
    TextManager* texts = g_clientWorld.texts.get();
    // The record by the blueprint's kind: weapons, armour, quick items (the dispatcher's IsPart: one of them, and
    // the record exists).
    int sort = 3;
    const itemdb::ItemRecordHead* head = nullptr;
    std::uint32_t index = item->AsLoot()->index;
    switch (item->AsLoot()->recordType) {
    case LootRecordType::Weapon: sort = 0; head = itemdb::WeaponAt(index); break;
    case LootRecordType::Armor: sort = 1; head = itemdb::ArmorAt(index); break;
    case LootRecordType::Quick: sort = 2; head = itemdb::QuickItemAt(index); break;
    default: break;
    }
    auto [name, description, kindText] = texts->GetItemTexts(item);
    Writer w(this);
    w.Head(name, kindText);
    // The material type ("mattype_<NAME>" of the record) and the count. (The weapon and armour views
    // are read only for their own sort.)
    const auto* weapon = itemdb::RecordAs<itemdb::WeaponRecord>(head);
    const auto* armor = itemdb::RecordAs<itemdb::ArmorRecord>(head);
    std::string text = head->materialClass;
    str::MakeLower(text);
    w.line = texts->GetString("mattype_" + text);
    text = w.line;
    w.line = std::format("{} {} x {}", w.T(1), text,
                  static_cast<int>(head->materialUnits));
    w.Text(w.line);
    w.Down();
    w.Number(0, static_cast<int>(head->weight));
    if (sort <= 2) {
        w.Number(2, static_cast<int>(head->charge));
        w.Number(14, static_cast<int>(head->magicLevel));
    }
    if (sort <= 1) w.Number(3, static_cast<int>(head->condition));
    if (sort == 0) {
        unsigned first = FirstDamageKind(weapon->damageTypes);
        if (first < 7) {
            w.line = std::format("damage_{}", first);
            text = texts->GetString(w.line);
        }
        int high = SumFtolScaled(weapon->damage, weapon->damageSpread, 10.0f);
        int low = ScaledFtol(weapon->damage, 10.0f);
        w.line = std::format("{} {}-{} {}", w.T(15), low, high,
                      text);
        w.Text(w.line);
        w.Down();
        w.Number(20, static_cast<int>(head->category));
        w.Number(38, static_cast<int>(weapon->attackBonus));
        int headType = head->type;
        if (headType == 5 || headType == 6)
            w.Number(10, static_cast<int>(weapon->reach));
        else
            w.Number(39, static_cast<int>(weapon->defenceBonus));
    } else if (sort == 1) {
        float values[7];
        for (int i = 0; i < 7; ++i) values[i] = ArmourValue(armor->absorbFirst[i], armor->firstFactor, 10.0f);
        w.DamageLines(values, 4);
    }
    w.Down();
    w.Wrapped(description, 4);
}

void ItemInfoPanel::DescribeSpellRecord() {
    Item* item = FirstItem();
    TextManager* texts = g_clientWorld.texts.get();
    const itemdb::SpellRecord* record = itemdb::SpellAt(item->AsLoot()->index);
    auto [name, description, kindText] = texts->GetItemTexts(item);
    Writer w(this);
    w.Head(name, kindText);
    w.Number(7, static_cast<int>(record->mana));
    w.Number(14, record->skill);
    w.School(record->castType);
    bool known = record->allowedModifiers[3] != 0;
    SpellLevelLine(w, known, known ? static_cast<int>(record->damage) : 0);
    float power = record->speed;
    if (power < 1.0f)
        w.line = std::format("{} {}", w.T(9), w.T(21));
    else
        w.line = std::format("{} {}", w.T(9), static_cast<int>(power));
    w.Text(w.line);
    w.Down();
    w.line = std::format("{} {}{}", w.T(10), static_cast<int>(record->range),
                  w.T(26));
    w.Text(w.line);
    w.Down();
    float radius = record->area;
    if (radius < 1.0f)
        w.line = std::format("{} {}", w.T(11), w.T(22));
    else
        w.line = str::Format("%s %.1f%s", w.T(11), radius,
                      w.T(26));
    w.Text(w.line);
    w.Down();
    DurationLine(w, record->duration);
    w.Number(13, static_cast<int>(record->targetCount));
    w.Down();
    w.Wrapped(description, 4);
}

void ItemInfoPanel::DescribeModifierRecord() {
    Item* item = FirstItem();
    TextManager* texts = g_clientWorld.texts.get();
    auto [name, description, kindText] = texts->GetItemTexts(item);
    const itemdb::SpellModifierRecord* record = itemdb::SpellModifierAt(item->AsLoot()->index);
    Writer w(this);
    w.Head(name, kindText);
    w.Number(7, static_cast<int>(record->mana));
    w.Number(14, record->skill);
    w.Down();
    w.Wrapped(description, 8);
}

void ItemInfoPanel::DescribeQuickItem() {
    Item* item = FirstItem();
    TextManager* texts = g_clientWorld.texts.get();
    auto [name, description, kindText] = texts->GetItemTexts(item);
    Writer w(this);
    w.Head(name, kindText);
    if (Spell* spell = item->spell) {
        bool known = spell->GetPrototype()->allowedModifiers[3] != 0;
        SpellLevelLine(w, known, known ? static_cast<int>(spell->damage) : 0);
        DurationLine(w, spell->duration);
        w.Down();
    }
    w.Wrapped(description, 8);
}

void ItemInfoPanel::DescribeCarriedSpell() {
    Item* item = FirstItem();
    Spell* spell = item ? item->spell.Get() : nullptr;
    if (!spell) {
        return;
    }
    TextManager* texts = g_clientWorld.texts.get();
    std::string title, description, kindText;
    {
        std::string key("spell ");
        key += spell->GetPrototype()->code;
        if (texts->Open(key)) {
            title = texts->ReadLine(true);
            description = texts->ReadToHash();
        }
    }
    kindText = texts->GetString("item_spell");
    Writer w(this);
    std::string text;
    w.Head(title, kindText);
    w.Number(7, static_cast<int>(spell->manaCost));
    w.Number(14, static_cast<int>(spell->skillNeeded));
    w.School(spell->GetPrototype()->castType);
    bool known = spell->GetPrototype()->allowedModifiers[3] != 0;
    SpellLevelLine(w, known, known ? static_cast<int>(spell->damage) : 0);
    float power = spell->speed;
    if (power < 1.0f)
        w.line = std::format("{} {}", w.T(9), w.T(21));
    else
        w.line = std::format("{} {}", w.T(9), static_cast<int>(power));
    w.Text(w.line);
    w.Down();
    w.line = std::format("{} {}{}", w.T(10), static_cast<int>(spell->range),
                  w.T(26));
    w.Text(w.line);
    w.Down();
    if (spell->targetFlags & SpellTargetFlag::AtUnits) {
        w.line = std::format("{} {}", w.T(11), w.T(22));
    } else {
        // The area: pi * r * r in that order.
        float radius = spell->radius;
        float area = 3.1415927f * radius * radius;
        w.line = str::Format("%s %.1f%s", w.T(11), area, w.T(26));
    }
    w.Text(w.line);
    w.Down();
    DurationLine(w, spell->duration);
    w.Number(13, static_cast<int>(spell->targetCount));
    w.Down();
    w.Wrapped(description, 4);
    w.Down();
    w.Down();
    w.Down();
    w.Down();
    if (spell->GetModifierCount() > 0) {
        // "<infoitem_19> name, name, ...": each modifier's third text line ("modifier <code>"),
        // "?" without one.
        w.line = w.T(19);
        for (int i = 0; i < spell->GetModifierCount(); ++i) {
            w.line += " ";
            text = "?";
            std::uint32_t index = static_cast<std::uint32_t>(spell->modifiers[i] - 1);
            const itemdb::SpellModifierRecord* modifier = itemdb::SpellModifierAt(index);
            if (texts->Open(std::string("modifier ") + modifier->code)) {
                texts->NextLine();
                texts->NextLine();
                text = texts->ReadLine(true);
            }
            std::string entry = text;
            entry += ",";
            w.line += entry;
        }
        str::TrimRight(w.line, ",");
        w.line += ".";
        w.Add(w.line, w.rect, kUiTextColour, render::text::Format::WordBreak, 3);
    }
}

// --- The statistic under the mouse ----------------------------------------------------------

void ItemInfoPanel::DescribeStats(const ItemHoverInfo& info) {
    itemParty.Clear(true, false);
    DescribeItem();
    if (info.kind == HoverKind::None) return;
    TextManager* texts = g_clientWorld.texts.get();
    Writer w(this);
    std::string text, title, description;
    if (info.kind <= HoverKind::Perk) {
        std::string key("perk ");
        key += info.text;
        if (texts->Open(key)) {
            title = texts->ReadLine(true);
            description = texts->ReadLine(true);
        }
    } else if (info.kind <= HoverKind::Text) {
        title = texts->GetString(info.text);
        std::string key = info.text;
        key += "_desc";
        description = texts->GetString(key);
    }
    w.Title(title);
    w.Down();
    w.Down();
    UnitStats* stats = info.stats;
    switch (info.kind) {
    case HoverKind::Attribute:
        AttributeLines(w, stats, info.index);
        w.Wrapped(description, 13);
        break;
    case HoverKind::Skill: {
        int index = info.index;
        w.line = std::format("{} {}", w.Skill(5), static_cast<int>(stats->SkillBase(index)));
        w.Text(w.line);
        w.Down();
        if (index >= 0) {
            bool drawn = true;
            if (index <= 1)
                w.line = std::format("{} {}", w.Skill(6),
                              static_cast<int>(stats->SkillWithBonus(static_cast<std::uint32_t>(index))));
            else if (index == 2)
                w.line = std::format("{} {}%", w.Skill(7),
                              static_cast<int>(stats->BackstabFactor() * kPercentScale));
            else
                drawn = false;
            if (drawn) {
                w.Text(w.line);
                w.Down();
            }
        }
        w.Down();
        w.Wrapped(description, 13);
        break;
    }
    case HoverKind::Science:
        w.line = std::format("{} {}", w.Skill(5),
                      static_cast<int>(stats->AttributeBase(info.index)));
        w.Text(w.line);
        w.Down();
        if (info.index == 0) {
            w.line = std::format("{} {}", w.Skill(9), static_cast<int>(stats->AttributeWithBonus(0)));
            w.Text(w.line);
            w.Down();
        }
        w.Down();
        w.Wrapped(description, 13);
        break;
    case HoverKind::Perk:
        PerkLines(w, info, title);
        w.Down();
        w.Wrapped(description, 6);
        break;
    case HoverKind::Armour: {
        w.Text(w.T(25));
        w.Down();
        float sums[7];
        const UnitStatSlot& slot = stats->slots[info.part];
        for (int i = 0; i < 7; ++i) sums[i] = LayerSum(&slot.layers[0].absorb[i]);
        for (int i = 0; i < 6; ++i) {
            w.line = std::format("damage_{}", i);
            text = texts->GetString(w.line);
            w.line = std::format("     {}:", text);
            w.Text(w.line);
            w.line = str::Format("%.1f", sums[i]);
            w.Value(w.line);
            w.Down();
        }
        w.Down();
        w.Wrapped(description, 8);
        break;
    }
    case HoverKind::Weapon:
        WeaponLines(w, info);
        w.Down();
        w.Down();
        w.Wrapped(description, 9);
        break;
    case HoverKind::Magic: {
        int label = 40;
        for (int school = 0; school < 8; ++school) {
            if (school == 4 || school == 6 || school == 7) continue;
            w.line = std::format("     {}", w.T(label++));
            w.Text(w.line);
            w.line = std::format("{}", static_cast<int>(stats->SpellSchoolSkill(static_cast<SpellSchool>(school))));
            w.Value(w.line);
            w.Down();
        }
        w.Down();
        w.Wrapped(description, 9);
        break;
    }
    case HoverKind::Text:
        w.Wrapped(description, 16);
        break;
    default:
        break;
    }
}

void ItemInfoPanel::ShowHelp(int topic) {
    itemParty.Clear(true, false);
    DescribeItem();
    TextManager* texts = g_clientWorld.texts.get();
    Writer w(this);
    std::string title, description, key;
    key = std::format("camphelp {}", topic);
    if (texts->Open(key)) {
        title = texts->ReadLine(true);
        description = texts->ReadLine(true);
    }
    w.Title(title);
    w.Down();
    w.Down();
    w.Wrapped(description, 16);
}

void ItemInfoPanel::Describe(Item* item) {
    if (FirstItem() && item) {
        if (FirstItem()->IsSameAs(item)) return;
    }
    itemParty.Clear(true, false);
    if (item) itemParty.AddItem(item, false, true);
    DescribeItem();
}

// --- Input: key down and key up --------------------------------------------------

void ItemInfoPanel::OnEvent(UiEvent& event) {
    switch (event.kind) {
    case UiEventKind::KeyDown: KeyDown(event); break;
    case UiEventKind::KeyUp: KeyUp(event); break;
    default: UiItemPanel::OnEvent(event); break;
    }
}

void ItemInfoPanel::KeyDown(UiEvent& event) {
    if (event.key == platform::Key::Control && !event.repeat && FirstItem()) DescribeItem();
}

void ItemInfoPanel::KeyUp(UiEvent& event) {
    if (event.key == platform::Key::Control && FirstItem()) DescribeItem();
}

void ItemInfoPanel::Build(int atX, int atY) {
    x = atX;
    y = atY;
    AddSurface(std::make_unique<UiSurface>(Rect::Sized({x, y}, {200, 300}), SurfaceBlend::Keyed, true))->PaintWith([this](UiSurface& out) { PaintLabels(out, labels); });
    TextManager* texts = g_clientWorld.texts.get();
    std::string text, key;
    for (int i = 0; i < 45; ++i) {
        key = std::format("infoitem_{}", i);
        text = texts->GetString(key);
        itemTexts.push_back(text);
    }
    for (int i = 0; i < 25; ++i) {
        key = std::format("infoskill_{}", i);
        text = texts->GetString(key);
        skillTexts.push_back(text);
    }
    itemParty.Create();
}
