// The item description panel: what the camp, the multiplayer hero screen and the exchange window write about the item or
// statistic under the mouse (ui_item_info.h, docs/notes/ui.md, "Item descriptions"). The statistics functions'
// results (double) are truncated as they come.
#include "party.h"
#include "ui_screens.h"
#include "text_split.h"
#include "ui_item_info.h"

#include "console.h"
#include "item.h"
#include "item_db.h"
#include "text_manager.h"
#include "spell.h"
#include "unit.h"
#include "unit_db.h"

#include "ei/rebirth.h"
#include "lib/nan_audit.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "platform/clock.h"
#include <bit>
#include <cstdint>
#include <cstring>
#include <new>
#include <string>
#include "lib/text_case.h"

#include <concepts>
#include <vector>
#include "game_singletons.h"
#include "world.h"
#include "lib/str.h"
#include "platform/keys.h"
#include <format>

namespace {

constexpr float kPercentScale = 100.0f;
constexpr float kStatBase = 25.0f;        // an attribute's neutral value
constexpr float kIntelligenceGrowth = 1.04f;

// pow(base, exponent - subtrahend) * scale, truncated.
int PowFtol(float base, float exponent, float subtrahend, float scale) {
    const float power = std::pow(base, exponent - subtrahend);
    return static_cast<int>(power * scale);
}
// b + a truncated; ScaledFtol the same of a * scale.
int SumFtol(float a, float b) { return static_cast<int>(b + a); }
int SumFtolScaled(float a, float b, float scale) { return static_cast<int>((b + a) * scale); }
int ScaledFtol(float a, float scale) { return static_cast<int>(a * scale); }
int DifferenceFtol(float a, float subtrahend) { return static_cast<int>(a - subtrahend); }
// value * factor * scale in that order, stored as float.
float ArmourValue(float value, float factor, float scale) {
    return value * factor * scale;
}
// 0 + p[0] + p[0x12] + p[0x24] (three armour layers 0x48 bytes apart), stored as float.
float LayerSum(const float* p) { return ((0.0f + p[0]) + p[0x12]) + p[0x24]; }



constexpr std::uint32_t kWhite = 0xFFFFFF;
constexpr std::uint32_t kText = 0xA7D7E4;

class Writer {
public:
    explicit Writer(ItemInfoPanel* newPanel)
        : draw(newPanel->surfaces[0]), panel(newPanel), rect{15, 10, 185, 25} {}

    const std::string& T(int index) const { return panel->itemTexts[index]; }
    const std::string& Skill(int index) const { return panel->skillTexts[index]; }

    void Down() { gameapi::OffsetRect(&rect, 0, 15); }
    void Text(std::string_view text) { draw->DrawTextIn(text, &rect, 0, kText, 0, 1); }
    void Value(std::string_view text) {
        RECT shifted = rect;
        gameapi::OffsetRect(&shifted, -10, 0);
        draw->DrawTextIn(text, &shifted, 0, kText, 2, 1);
    }
    void Title(std::string_view text) { draw->DrawWrappedText(text, &rect, 2, 0, kWhite, 0x11, 1); }
    void Wrapped(std::string_view text, int maxLines) {
        draw->DrawWrappedText(text, &rect, maxLines, 0, kText, 0x11, 1);
    }

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
        line = T(label).c_str();
        line += " ";
        line += text;
        Text(line);
        Down();
    }
    // The lists of 7 damage values: "<label> <threshold>" and, when some kinds lie below it,
    // "<infoitem_24> <those kinds>".
    void DamageLines(const float* values, int label) {
        const DamageLists lists = ItemDamageLists(values, false);
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

    UiSurface* draw;   // the panel's first surface

private:
    ItemInfoPanel* panel;

public:
    RECT rect;
};

// Whether a 7-entry damage weight array has a weight that compares unequal (and ordered) to 0: returns the
// index, or 7.
unsigned FirstDamageKind(const float* weights) {
    unsigned i = 0;
    for (; i < 7; ++i) {
        EI_NAN_AUDIT(weights[i]);
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
    TextManager* texts = g_clientWorld.texts;
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
        if (info.index > i) w.draw->DrawTextIn(w.Skill(21), &w.rect, 0, kText, 2, 1);    // learned
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
// weapon of type 5 or 6, infoitem_34).
void WeaponLines(Writer& w, const ItemHoverInfo& info) {
    ItemWeapon* weapon = info.weapon;
    UnitStats* stats = info.stats;
    int type = weapon->GetType();
    if (str::Compare(info.text, "defense") == 0 && (type == 5 || type == 6)) {
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

std::uint32_t ItemCacheTime() { return platform::Milliseconds(); }

DamageLists ItemDamageLists(const float* values, bool integer) {
    TextManager* texts = g_clientWorld.texts;
    DamageLists lists;
    std::string key, text;
    float threshold = values[6];
    // EI_Plugin's Fixes: the jump over the integer format is always taken.
    if (integer && !ei::rebirth::Enabled(ei::rebirth::Patch::EiPluginFixes))
        lists.threshold = std::format("{}", static_cast<int>(threshold));
    else
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
    UiSurface* draw = surfaces[0];
    draw->Clear();
    if (!item) return;

    ItemKind kind = item->Kind();
    if (kind == ItemKind::Loot) {
        LootKind sort = AsLoot(item)->kind;
        LootRecordType sub = AsLoot(item)->recordType;
        if (sort == LootKind::Material) {
            DescribeMaterial();
            return;
        }
        if (sort == LootKind::Record && (sub == LootRecordType::Weapon || sub == LootRecordType::Armor || sub == LootRecordType::Quick)) {
            DescribeBlueprint();
            return;
        }
        if (sort == LootKind::Record && sub == LootRecordType::Spell) {
            DescribeSpellRecord();
            return;
        }
        if (sort == LootKind::Record && sub == LootRecordType::Modifier) {
            DescribeModifierRecord();
            return;
        }
    }
    if (kind == ItemKind::Quick && item->GetCategory() == QuickItemCategory::Potion) {
        DescribeQuickItem();
        return;
    }
    if ((ui->ctrlDown && item->spell) || kind == ItemKind::Scroll) {
        DescribeCarriedSpell();
        return;
    }

    TextManager* texts = g_clientWorld.texts;
    // The material's name ("matshort <NAME>"): the record's name with '_' for ' ', lower case.
    std::string material = itemdb::MaterialAt(MaterialIndex(item->prototypeId))->key;
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
        materialLine = !prototype || text::CompareIgnoringCase(prototype->materialClass, "none") != 0;
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
    if (kind != ItemKind::Quest) w.Number(0, static_cast<int>(item->Weight()));

    if (kind == ItemKind::Armor || kind == ItemKind::Weapon || (kind == ItemKind::Quick && item->GetCategory() == QuickItemCategory::Wand)) {
        CSpell* spell = item->spell;
        int spellValue = spell ? static_cast<int>(spell->skillNeeded) : 0;
        w.Number(2, static_cast<int>(item->chargeMax));
        w.line = std::format("{} {}/{}", w.T(14), spellValue, static_cast<int>(item->magicLevel));
        w.Text(w.line);
        w.Down();
    } else if (kind == ItemKind::Quick) {
        goto tail;
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
            ItemWeapon* weapon = AsWeapon(item);
            // Damage: min-max and the first damage kind with a weight ("damage<n>"; the text
            // keeps the material line's otherwise).
            float weights[7];
            weapon->GetDamageTypes(weights);
            unsigned first = FirstDamageKind(weights);
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
            if (weapon->GetType() == 5 || weapon->GetType() == 6)
                w.Number(10, static_cast<int>(weapon->reach));
            else
                w.Number(39, static_cast<int>(WeaponDefenceBonus(item->prototypeId)));
            goto tail;
        }
    }
    if (kind == ItemKind::Armor) {
        float values[7];
        std::memcpy(values, AsArmor(item)->absorb[0], sizeof(values));
        w.DamageLines(values, 25);
    }

tail:
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
        CSpell* spell;
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
    TextManager* texts = g_clientWorld.texts;
    const itemdb::MaterialRecordHead* material = itemdb::MaterialAt(AsLoot(item)->index);
    if (!material) return;
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
    TextManager* texts = g_clientWorld.texts;
    // The record by the blueprint's kind: weapons, armour, quick items. The dispatcher calls this only
    // for kinds 0-2; the original leaves the record and kind uninitialised otherwise (here: none, and it
    // faults reading the record like a missing one).
    int sort = 3;
    const itemdb::ItemRecordHead* head = nullptr;
    std::uint32_t index = AsLoot(item)->index;
    switch (AsLoot(item)->recordType) {
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
        for (int i = 0; i < 7; ++i) values[i] = ArmourValue(armor->absorbFirst[i], armor->FirstFactor(), 10.0f);
        w.DamageLines(values, 4);
    }
    w.Down();
    w.Wrapped(description, 4);
}

void ItemInfoPanel::DescribeSpellRecord() {
    Item* item = FirstItem();
    TextManager* texts = g_clientWorld.texts;
    const itemdb::SpellRecord* record = itemdb::SpellAt(AsLoot(item)->index);
    auto [name, description, kindText] = texts->GetItemTexts(item);
    Writer w(this);
    w.Head(name, kindText);
    w.Number(7, static_cast<int>(record->mana));
    w.Number(14, record->skill);
    w.School(record->castType);
    bool known = record->allowedModifiers[3] != 0;
    SpellLevelLine(w, known, known ? static_cast<int>(record->damage) : 0);
    float power = record->speed;
    EI_NAN_AUDIT(power);
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
    EI_NAN_AUDIT(radius);
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
    TextManager* texts = g_clientWorld.texts;
    auto [name, description, kindText] = texts->GetItemTexts(item);
    const itemdb::SpellModifierRecord* record = itemdb::SpellModifierAt(AsLoot(item)->index);
    Writer w(this);
    w.Head(name, kindText);
    w.Number(7, static_cast<int>(record->mana));
    w.Number(14, record->skill);
    w.Down();
    w.Wrapped(description, 8);
}

void ItemInfoPanel::DescribeQuickItem() {
    Item* item = FirstItem();
    TextManager* texts = g_clientWorld.texts;
    auto [name, description, kindText] = texts->GetItemTexts(item);
    Writer w(this);
    w.Head(name, kindText);
    if (CSpell* spell = item->spell) {
        bool known = spell->GetPrototype()->allowedModifiers[3] != 0;
        SpellLevelLine(w, known, known ? static_cast<int>(spell->damage) : 0);
        DurationLine(w, spell->duration);
        w.Down();
    }
    w.Wrapped(description, 8);
}

void ItemInfoPanel::DescribeCarriedSpell() {
    Item* item = FirstItem();
    CSpell* spell = item ? item->spell.Get() : nullptr;
    if (!spell) {
        return;
    }
    TextManager* texts = g_clientWorld.texts;
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
    EI_NAN_AUDIT(power);
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
            std::string key("modifier ");
            key += modifier->code;
            if (texts->Open(key)) {
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
        w.draw->DrawWrappedText(w.line, &w.rect, 3, 0, kText, 0x10, 1);
    }
}

// --- The statistic under the mouse ----------------------------------------------------------

void ItemInfoPanel::DescribeStats(const ItemHoverInfo& info) {
    itemParty.Clear(1, 0);
    DescribeItem();
    if (info.kind == HoverKind::None) return;
    TextManager* texts = g_clientWorld.texts;
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
            w.line = std::format("{}", static_cast<int>(stats->SpellSchoolSkill(school)));
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
    itemParty.Clear(1, 0);
    DescribeItem();
    TextManager* texts = g_clientWorld.texts;
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
    itemParty.Clear(1, 0);
    if (item) itemParty.AddItem(item, 0, 1);
    DescribeItem();
}

// --- Input side: the input virtuals (mouse, key down, key up) ------------------

void ItemInfoPanel::KeyDown(int key, int, int modifiers) {
    if (key == platform::kKeyControl && !(modifiers & 0x4000) && FirstItem()) DescribeItem();
}

void ItemInfoPanel::KeyUp(int key, int, int) {
    if (key == platform::kKeyControl && FirstItem()) DescribeItem();
}

void ItemInfoPanel::Build(int atX, int atY) {
    x = atX;
    y = atY;
    UiSurface* surface = new UiSurface(ui, x, y, x + 200, y + 300, SurfaceBlend::Keyed, true);
    AddSurface(surface);
    TextManager* texts = g_clientWorld.texts;
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
