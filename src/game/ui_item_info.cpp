// The item description panel: what the camp, the multiplayer hero screen and the exchange window write about the item or
// statistic under the mouse (ui_item_info.h, docs/notes/ui.md, "Item descriptions"). Values the game truncates with its
// _ftol are converted the same way; the statistics functions' results that the original leaves unrounded are declared
// double, so no rounding happens in between. Compiled with precise floating point so that products keep the original's order.
#include "party.h"
#include "ui_float.h"
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
#include "ei/x87.h"
#include "lib/nan_audit.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include <bit>
#include <cstdint>
#include <cstring>
#include <new>
#include <string>
#include "lib/text_case.h"

#include <concepts>
#include <vector>
#include "game_data.h"
#include "game_singletons.h"
#include "lib/str.h"
#include <format>

namespace {



int FtolOf(float value) { return static_cast<int>(libcrt::Ftol(D(value))); }

// _ftol of a value kept unrounded (a statistic's result).
int Ftol(double value) { return static_cast<int>(libcrt::Ftol(value)); }

constexpr float kPercentScale = 100.0f;
constexpr float kStatBase = 25.0f;        // an attribute's neutral value
constexpr double kIntelligenceGrowth = 1.04;

// _ftol(_CIpow(base, exponent - subtrahend) * scale), all unrounded.
int PowFtol(double base, float exponent, float subtrahend, float scale) {
    const double power = libcrt::CiPow(base, static_cast<double>(exponent) - subtrahend);
    return Ftol(power * scale);
}
// _ftol of b + a (in that order); ScaledFtol the same of a * scale.
int SumFtol(float a, float b) { return Ftol(static_cast<double>(b) + a); }
int SumFtolScaled(float a, float b, float scale) { return Ftol((static_cast<double>(b) + a) * scale); }
int ScaledFtol(float a, float scale) { return Ftol(static_cast<double>(a) * scale); }
int DifferenceFtol(float a, float subtrahend) { return Ftol(static_cast<double>(a) - subtrahend); }
// value * factor * scale in that order, stored as float.
float ArmourValue(float value, float factor, float scale) {
    return static_cast<float>(static_cast<double>(value) * factor * scale);
}
// 0 + p[0] + p[0x12] + p[0x24] (three armour layers 0x48 bytes apart), stored as float.
float LayerSum(const float* p) { return static_cast<float>(((0.0 + p[0]) + p[0x12]) + p[0x24]); }



constexpr std::uint32_t kWhite = 0xFFFFFF;
constexpr std::uint32_t kText = 0xA7D7E4;

class Writer {
public:
    explicit Writer(CItemInfoPanel* panel)
        : draw(panel->surfaces_[0]), panel_(panel), rect_{15, 10, 185, 25} {}

    const std::string& T(int index) const { return panel_->itemTexts_[index]; }
    const std::string& Skill(int index) const { return panel_->skillTexts_[index]; }

    void Down() { gameapi::OffsetRect(&rect_, 0, 15); }
    void Text(std::string_view text) { draw->DrawTextIn(text, &rect_, 0, kText, 0, 1); }
    void Value(std::string_view text) {
        RECT shifted = rect_;
        gameapi::OffsetRect(&shifted, -10, 0);
        draw->DrawTextIn(text, &shifted, 0, kText, 2, 1);
    }
    void Title(std::string_view text) { draw->DrawWrappedText(text, &rect_, 2, 0, kWhite, 0x11, 1); }
    void Wrapped(std::string_view text, int maxLines) {
        draw->DrawWrappedText(text, &rect_, maxLines, 0, kText, 0x11, 1);
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
        std::string key;
        key = std::format("school_{}", school);
        g_clientWorld.texts_->GetString(key, &line);
        std::string text = line;
        Joined(18, text);
    }

    std::string line;

    CUiSurface* draw;   // the panel's first surface

private:
    CItemInfoPanel* panel_;

public:
    RECT rect_;
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
                      x87::FromUnsigned(ticks) * D(0.06666667f), w.T(27));
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
        line("%s %d%%", 0, Ftol(stats->StrengthRatio() * kPercentScale));
        line("%s %d", 1, Ftol(stats->ScaledStrength()));
        w.Down();
        break;
    case 1:
        line("%s %d%%", 2, Ftol(stats->DexterityRatio() * kPercentScale));
        line("%s %d", 3, Ftol(stats->RateValue()));
        w.Down();
        break;
    case 2:
        // 1.04 ^ (intelligence - 25) * 100.
        line("%s %d%%", 4, PowFtol(kIntelligenceGrowth, stats->intelligence, kStatBase, kPercentScale));
        line("%s %d%%", 24, Ftol(stats->Period20() * kPercentScale));
        w.Down();
        break;
    default:
        break;
    }
}

void PerkLines(Writer& w, const ItemHoverInfo& info, std::string& title) {
    CTextManager* texts = g_clientWorld.texts_;
    std::string name = info.text;
    str::TrimRight(name, "0");
    name += "1";
    int group = g_skillDatabase.IndexOf(name) / 3;
    for (int i = 0; i < 3; ++i) {
        const SkillLevelRecord* level = unitdb::SkillLevelAt(static_cast<std::uint32_t>(group * 3 + i));
        if (!level) continue;
        std::string key("perk ");
        key += level->name;
        if (texts->Open(key)) texts->ReadLine(&title, 1);
        w.Text(title);
        if (info.index > i) w.draw->DrawTextIn(w.Skill(21), &w.rect_, 0, kText, 2, 1);   // learned
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
        w.rect_.right += 15;
        int cost = info.stats->SkillPrice(level);
        if (cost < 0) cost = 0x7FFFFFFF;
        w.line = str::Format(plain, w.Skill(18), cost);
        w.Text(w.line);
        w.rect_.right -= 15;
        w.Down();
    }
}

// HoverKind::Weapon, the weapon's attack or defence: its four parts and their sum (or, for the defence of a
// weapon of type 5 or 6, infoitem_34).
void WeaponLines(Writer& w, const ItemHoverInfo& info) {
    CItemWeapon* weapon = info.weapon;
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
    int skill = Ftol(stats->WeaponSkill(type));
    part(30, skill);
    sum += skill;
    w.Down();
    int level = Ftol(stats->SkillLevelValue(type));
    part(31, level);
    sum += level;
    w.Down();
    w.line = std::format("     {}", w.T(32));
    w.Text(w.line);
    int own = Ftol(WeaponPrototypeSkillBonus(weapon->prototypeId_));
    if (str::Compare(info.text, "defense") == 0)
        own += Ftol(WeaponDefenceBonus(weapon->prototypeId_));
    else
        own += Ftol(WeaponAttackBonus(weapon->prototypeId_));
    w.line = std::format("{}", own);
    w.Value(w.line);
    sum += own;
    w.Down();
    w.Down();
    part(33, sum);
}

} // namespace

DamageLists ItemDamageLists(const float* values, bool integer) {
    CTextManager* texts = g_clientWorld.texts_;
    DamageLists lists;
    std::string key, text;
    float threshold = values[6];
    // EI_Plugin's Fixes: the jump over the integer format is always taken.
    if (integer && !ei::rebirth::Enabled(ei::rebirth::Patch::EiPluginFixes))
        lists.threshold = std::format("{}", FtolOf(threshold));
    else
        lists.threshold = str::Format("%.1f", D(threshold));
    for (int i = 0; i < 6; ++i) {
        std::string* list;
        if (values[i] > threshold)
            list = &lists.above;
        else if (values[i] == threshold)
            continue;
        else
            list = &lists.below;   // also NaN
        key = std::format("dmg_{}", i);
        texts->GetString(key, &text);
        *list += text;
        *list += ", ";
    }
    str::TrimRight(lists.above, ", ");
    str::TrimRight(lists.below, ", ");
    return lists;
}

void CItemInfoPanel::DescribeItem() {
    CItem* item = FirstItem();
    CUiSurface* draw = surfaces_[0];
    draw->Clear();
    if (!item) return;

    ItemKind kind = item->Kind();
    if (kind == ItemKind::Loot) {
        LootKind sort = AsLoot(item)->kind_;
        LootRecordType sub = AsLoot(item)->recordType_;
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
    if ((interface_->ctrlDown_ && item->spell_) || kind == ItemKind::Spell) {
        DescribeCarriedSpell();
        return;
    }

    CTextManager* texts = g_clientWorld.texts_;
    // The material's name ("matshort <NAME>"): the record's name with '_' for ' ', lower case.
    std::string material;
    material = itemdb::MaterialAt(MaterialIndex(item->prototypeId_))->key;
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
        if (texts->Open(key)) texts->ReadLine(&text, 1);
        w.line = std::format("{} {} x {}", w.T(1), text,
                      static_cast<int>(item->GetPrototype()->materialUnits));
        w.Text(w.line);
        w.Down();
    }
    if (kind != ItemKind::Quest) w.Number(0, FtolOf(item->Weight()));

    if (kind == ItemKind::Armor || kind == ItemKind::Weapon || (kind == ItemKind::Quick && item->GetCategory() == QuickItemCategory::Wand)) {
        CSpell* spell = item->spell_;
        int spellValue = spell ? static_cast<int>(spell->skillNeeded_) : 0;
        w.Number(2, FtolOf(item->chargeMax_));
        w.line = std::format("{} {}/{}", w.T(14), spellValue, static_cast<int>(item->magicLevel_));
        w.Text(w.line);
        w.Down();
    } else if (kind == ItemKind::Quick) {
        goto tail;
    }

    if (kind == ItemKind::Armor || kind == ItemKind::Weapon) {
        // The condition (current only when positive).
        float current = item->condition_;
        int condition = current > 0.0f ? FtolOf(current) : 0;
        int maximum = FtolOf(item->conditionMax_);
        w.line = std::format("{} {}/{}", w.T(3), condition, maximum);
        w.Text(w.line);
        w.Down();
        if (kind == ItemKind::Weapon) {
            CItemWeapon* weapon = AsWeapon(item);
            // Damage: min-max and the first damage kind with a weight ("damage_<n>"; the text
            // keeps the material line's otherwise).
            float weights[7];
            weapon->GetDamageTypes(weights);
            unsigned first = FirstDamageKind(weights);
            if (first < 7) {
                w.line = std::format("damage_{}", first);
                texts->GetString(w.line, &text);
            }
            float low = weapon->damage_;
            float spread = weapon->damageSpread_;
            int high = SumFtol(low, spread);
            int lowValue = FtolOf(low);
            w.line = std::format("{} {}-{} {}", w.T(15), lowValue, high,
                          text);
            w.Text(w.line);
            w.Down();
            w.Number(20, static_cast<int>(item->GetPrototype()->category));
            w.Number(38, Ftol(WeaponAttackBonus(item->prototypeId_)));
            if (weapon->GetType() == 5 || weapon->GetType() == 6)
                w.Number(10, FtolOf(weapon->reach_));
            else
                w.Number(39, Ftol(WeaponDefenceBonus(item->prototypeId_)));
            goto tail;
        }
    }
    if (kind == ItemKind::Armor) {
        float values[7];
        std::memcpy(values, AsArmor(item)->absorb_[0], sizeof(values));
        w.DamageLines(values, 25);
    }

tail:
    w.Down();
    if (kind == ItemKind::Loot) {
        texts->GetString("loot_item", &text);
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
            (spell = item->spell_) != nullptr) {
            text = "Unknown";
            std::string key("spell ");
            key += spell->GetPrototype()->code;
            if (texts->Open(key)) texts->ReadLine(&text, 1);
            w.line = std::format("{} {}", w.T(5), text);
            w.Text(w.line);
            w.Down();
            w.Text(w.T(6));
        }
    }
}

void CItemInfoPanel::DescribeMaterial() {
    CItem* item = FirstItem();
    CTextManager* texts = g_clientWorld.texts_;
    const itemdb::MaterialRecordHead* material = itemdb::MaterialAt(AsLoot(item)->index_);
    if (!material) return;
    auto [name, description, kindText] = texts->GetItemTexts(item);
    Writer w(this);
    w.Head(name, kindText);
    std::string text = material->soundName;
    str::MakeLower(text);
    texts->GetString("mattype_" + text, &w.line);
    const std::string type = w.line;
    w.Joined(17, type);
    w.Number(0, FtolOf(material->weightFactor));
    w.Number(2, FtolOf(material->magicPower));
    w.Number(14, static_cast<int>(material->magicLevel));
    w.Number(3, FtolOf(material->conditionFactor));
    w.line = str::Format("%s %.1f", w.T(15), D(material->damageFactor));
    w.Text(w.line);
    w.Down();
    float values[7];
    std::memcpy(values, material->absorbFactors, sizeof(values));
    w.DamageLines(values, 4);
    w.Down();
    w.Wrapped(description, 4);
}

void CItemInfoPanel::DescribeBlueprint() {
    CItem* item = FirstItem();
    CTextManager* texts = g_clientWorld.texts_;
    // The record by the blueprint's kind: weapons, armour, quick items. The dispatcher calls this only
    // for kinds 0-2; the original leaves the record and kind uninitialised otherwise (here: none, and it
    // faults reading the record like a missing one).
    int sort = 3;
    const itemdb::ItemRecordHead* head = nullptr;
    std::uint32_t index = AsLoot(item)->index_;
    switch (AsLoot(item)->recordType_) {
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
    texts->GetString("mattype_" + text, &w.line);
    text = w.line;
    w.line = std::format("{} {} x {}", w.T(1), text,
                  static_cast<int>(head->materialUnits));
    w.Text(w.line);
    w.Down();
    w.Number(0, FtolOf(head->weight));
    if (sort <= 2) {
        w.Number(2, FtolOf(head->charge));
        w.Number(14, static_cast<int>(head->magicLevel));
    }
    if (sort <= 1) w.Number(3, FtolOf(head->condition));
    if (sort == 0) {
        unsigned first = FirstDamageKind(weapon->damageTypes);
        if (first < 7) {
            w.line = std::format("damage_{}", first);
            texts->GetString(w.line, &text);
        }
        int high = SumFtolScaled(weapon->damage, weapon->damageSpread, 10.0f);
        int low = ScaledFtol(weapon->damage, 10.0f);
        w.line = std::format("{} {}-{} {}", w.T(15), low, high,
                      text);
        w.Text(w.line);
        w.Down();
        w.Number(20, static_cast<int>(head->category));
        w.Number(38, FtolOf(weapon->attackBonus));
        int type = head->type;
        if (type == 5 || type == 6)
            w.Number(10, FtolOf(weapon->reach));
        else
            w.Number(39, FtolOf(weapon->defenceBonus));
    } else if (sort == 1) {
        float values[7];
        for (int i = 0; i < 7; ++i) values[i] = ArmourValue(armor->absorbFirst[i], armor->FirstFactor(), 10.0f);
        w.DamageLines(values, 4);
    }
    w.Down();
    w.Wrapped(description, 4);
}

void CItemInfoPanel::DescribeSpellRecord() {
    CItem* item = FirstItem();
    CTextManager* texts = g_clientWorld.texts_;
    const itemdb::SpellRecord* record = itemdb::SpellAt(AsLoot(item)->index_);
    auto [name, description, kindText] = texts->GetItemTexts(item);
    Writer w(this);
    w.Head(name, kindText);
    w.Number(7, FtolOf(record->mana));
    w.Number(14, record->skill);
    w.School(record->castType);
    bool known = record->allowedModifiers[3] != 0;
    SpellLevelLine(w, known, known ? FtolOf(record->damage) : 0);
    float power = record->speed;
    EI_NAN_AUDIT(power);
    if (power < 1.0f)
        w.line = std::format("{} {}", w.T(9), w.T(21));
    else
        w.line = std::format("{} {}", w.T(9), FtolOf(power));
    w.Text(w.line);
    w.Down();
    w.line = std::format("{} {}{}", w.T(10), FtolOf(record->range),
                  w.T(26));
    w.Text(w.line);
    w.Down();
    float radius = record->area;
    EI_NAN_AUDIT(radius);
    if (radius < 1.0f)
        w.line = std::format("{} {}", w.T(11), w.T(22));
    else
        w.line = str::Format("%s %.1f%s", w.T(11), D(radius),
                      w.T(26));
    w.Text(w.line);
    w.Down();
    DurationLine(w, record->duration);
    w.Number(13, static_cast<int>(record->targetCount));
    w.Down();
    w.Wrapped(description, 4);
}

void CItemInfoPanel::DescribeModifierRecord() {
    CItem* item = FirstItem();
    CTextManager* texts = g_clientWorld.texts_;
    auto [name, description, kindText] = texts->GetItemTexts(item);
    const itemdb::SpellModifierRecord* record = itemdb::SpellModifierAt(AsLoot(item)->index_);
    Writer w(this);
    w.Head(name, kindText);
    w.Number(7, FtolOf(record->mana));
    w.Number(14, record->skill);
    w.Down();
    w.Wrapped(description, 8);
}

void CItemInfoPanel::DescribeQuickItem() {
    CItem* item = FirstItem();
    CTextManager* texts = g_clientWorld.texts_;
    auto [name, description, kindText] = texts->GetItemTexts(item);
    Writer w(this);
    w.Head(name, kindText);
    if (CSpell* spell = item->spell_) {
        bool known = spell->GetPrototype()->allowedModifiers[3] != 0;
        SpellLevelLine(w, known, known ? FtolOf(spell->damage_) : 0);
        DurationLine(w, spell->duration_);
        w.Down();
    }
    w.Wrapped(description, 8);
}

void CItemInfoPanel::DescribeCarriedSpell() {
    CItem* item = FirstItem();
    CSpell* spell = item ? item->spell_.Get() : nullptr;
    if (!spell) {
        return;
    }
    CTextManager* texts = g_clientWorld.texts_;
    std::string title, description, kindText;
    {
        std::string key("spell ");
        key += spell->GetPrototype()->code;
        if (texts->Open(key)) {
            texts->ReadLine(&title, 1);
            texts->ReadToHash(&description, 1);
        }
    }
    texts->GetString("item_spell", &kindText);
    Writer w(this);
    std::string text;
    w.Head(title, kindText);
    w.Number(7, FtolOf(spell->manaCost_));
    w.Number(14, static_cast<int>(spell->skillNeeded_));
    w.School(spell->GetPrototype()->castType);
    bool known = spell->GetPrototype()->allowedModifiers[3] != 0;
    SpellLevelLine(w, known, known ? FtolOf(spell->damage_) : 0);
    float power = spell->speed_;
    EI_NAN_AUDIT(power);
    if (power < 1.0f)
        w.line = std::format("{} {}", w.T(9), w.T(21));
    else
        w.line = std::format("{} {}", w.T(9), FtolOf(power));
    w.Text(w.line);
    w.Down();
    w.line = std::format("{} {}{}", w.T(10), FtolOf(spell->range_),
                  w.T(26));
    w.Text(w.line);
    w.Down();
    if (spell->targetFlags_ & SpellTargetFlag::AtUnits) {
        w.line = std::format("{} {}", w.T(11), w.T(22));
    } else {
        // The area: pi * r * r in that order.
        double radius = D(spell->radius_);
        double area = D(3.1415927f) * radius * radius;
        w.line = str::Format("%s %.1f%s", w.T(11), area, w.T(26));
    }
    w.Text(w.line);
    w.Down();
    DurationLine(w, spell->duration_);
    w.Number(13, static_cast<int>(spell->targetCount_));
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
            std::uint32_t index = static_cast<std::uint32_t>(spell->modifiers_[i] - 1);
            const itemdb::SpellModifierRecord* modifier = itemdb::SpellModifierAt(index);
            std::string key("modifier ");
            key += modifier->code;
            if (texts->Open(key)) {
                texts->NextLine();
                texts->NextLine();
                texts->ReadLine(&text, 1);
            }
            std::string entry = text;
            entry += ",";
            w.line += entry;
        }
        str::TrimRight(w.line, ",");
        w.line += ".";
        w.draw->DrawWrappedText(w.line, &w.rect_, 3, 0, kText, 0x10, 1);
    }
}

// --- The statistic under the mouse ----------------------------------------------------------

void CItemInfoPanel::DescribeStats(const ItemHoverInfo& info) {
    itemParty_.Clear(1, 0);
    DescribeItem();
    if (info.kind == HoverKind::None) return;
    CTextManager* texts = g_clientWorld.texts_;
    Writer w(this);
    std::string text, title, description;
    if (info.kind <= HoverKind::Perk) {
        std::string key("perk ");
        key += info.text;
        if (texts->Open(key)) {
            texts->ReadLine(&title, 1);
            texts->ReadLine(&description, 1);
        }
    } else if (info.kind <= HoverKind::Text) {
        texts->GetString(info.text, &title);
        std::string key = info.text;
        key += "_desc";
        texts->GetString(key, &description);
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
        w.line = std::format("{} {}", w.Skill(5), Ftol(stats->SkillBase(index)));
        w.Text(w.line);
        w.Down();
        if (index >= 0) {
            bool drawn = true;
            if (index <= 1)
                w.line = std::format("{} {}", w.Skill(6),
                              Ftol(stats->SkillWithBonus(static_cast<std::uint32_t>(index))));
            else if (index == 2)
                w.line = std::format("{} {}%", w.Skill(7),
                              Ftol(stats->BackstabFactor() * kPercentScale));
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
                      Ftol(stats->AttributeBase(info.index)));
        w.Text(w.line);
        w.Down();
        if (info.index == 0) {
            w.line = std::format("{} {}", w.Skill(9), Ftol(stats->AttributeWithBonus(0)));
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
            texts->GetString(w.line, &text);
            w.line = std::format("     {}:", text);
            w.Text(w.line);
            w.line = str::Format("%.1f", D(sums[i]));
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
            w.line = std::format("{}", Ftol(stats->SpellSchoolSkill(school)));
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

void CItemInfoPanel::ShowHelp(int topic) {
    itemParty_.Clear(1, 0);
    DescribeItem();
    CTextManager* texts = g_clientWorld.texts_;
    Writer w(this);
    std::string title, description, key;
    key = std::format("camphelp {}", topic);
    if (texts->Open(key)) {
        texts->ReadLine(&title, 1);
        texts->ReadLine(&description, 1);
    }
    w.Title(title);
    w.Down();
    w.Down();
    w.Wrapped(description, 16);
}

void CItemInfoPanel::Describe(CItem* item) {
    if (FirstItem() && item) {
        if (FirstItem()->IsSameAs(item)) return;
    }
    itemParty_.Clear(1, 0);
    if (item) itemParty_.AddItem(item, 0, 1);
    DescribeItem();
}

// --- Input side: the input virtuals (mouse, key down, key up) ------------------

void CItemInfoPanel::KeyDown(int key, int, int flags) {
    if (key == VK_CONTROL && !(flags & 0x4000) && FirstItem()) DescribeItem();
}

void CItemInfoPanel::KeyUp(int key, int, int) {
    if (key == VK_CONTROL && FirstItem()) DescribeItem();
}

void CItemInfoPanel::Build(int x, int y) {
    x_ = x;
    y_ = y;
    CUiSurface* surface = new CUiSurface(interface_, x_, y_, x_ + 200, y_ + 300, SurfaceBlend::Keyed, true);
    AddSurface(surface);
    CTextManager* texts = g_clientWorld.texts_;
    std::string text, key;
    for (int i = 0; i < 45; ++i) {
        key = std::format("infoitem_{}", i);
        texts->GetString(key, &text);
        itemTexts_.push_back(text);
    }
    for (int i = 0; i < 25; ++i) {
        key = std::format("infoskill_{}", i);
        texts->GetString(key, &text);
        skillTexts_.push_back(text);
    }
    itemParty_.Create();
}
