// The item description panel (005EE6C0-005F2A80): what the camp, the multiplayer hero screen and
// the exchange window write about the item or statistic under the mouse. See ui_item_info.h and
// docs/notes/ui.md ("Item descriptions").
// Each function draws text lines into the panel's surface 0 between Begin and End
//: Text (005FDE20, Rebirth-patched, called through its address) for single lines and
// DrawWrappedText for the wrapped title and description. Values the original truncates
// with the game's _ftol are converted the same way; values of the statistics functions (UnitStats,
// WeaponStat84 / 88, WeaponPrototypeSkillBonus) are truncated as they come back in ST0: the ones the
// original leaves unrounded are declared double there, so no rounding happens in between. Compiled
// with precise floating point so that products keep the original's order.
#include "party.h"
#include "ui_screens.h"
#include "text_reader.h"
#include "text_split.h"
#include "ui_item_info.h"
#include "game_strings.h"

#include "console.h"
#include "item.h"
#include "item_db.h"
#include "item_db_text.h"
#include "spell.h"
#include "unit.h"
#include "unit_db.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "ei/x87.h"
#include "lib/nan_audit.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"

#include <concepts>
#include <cstdint>
#include <cstring>
#include <new>
#include "game_data.h"
#include "game_singletons.h"

namespace {

using console::CStringArray;

inline double D(float f) { return f; }

// A float the item database keeps as its bits (a dword field).
float FloatOf(std::uint32_t bits) {
    float v;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
}
// A float argument would convert to an integer first: callers pass raw bits only.
template <std::floating_point T> float FloatOf(T) = delete;

int FtolOf(float value) { return static_cast<int>(gamecrt::Ftol(D(value))); }

int MbsICmp(const char* a, const char* b) {
    return gamecrt::mbsicmp(a, b);
}
int MbsCmp(const char* a, const char* b) {
    return gamecrt::mbscmp(a, b);
}

// _ftol of a value as it is in ST0 (a statistic's result, kept unrounded).
int Ftol(double value) { return static_cast<int>(gamecrt::Ftol(value)); }

constexpr float kPercentScale = 100.0f;
constexpr float kStatBase = 25.0f;        // 0073E124 (an attribute's neutral value)
constexpr double kIntelligenceGrowth = 1.04;

// _ftol(_CIpow(base, exponent - subtrahend) * scale): the exponent a float minus `subtrahend`
// (FSUB), the product with `scale`, all unrounded.
int PowFtol(double base, float exponent, float subtrahend, float scale) {
    const double power = libcrt::CiPow(base, static_cast<double>(exponent) - subtrahend);
    return Ftol(power * scale);
}
// _ftol of a + b (FLD b; FADD a), optionally times the float at `scale`, unrounded.
int SumFtol(float a, float b) { return Ftol(static_cast<double>(b) + a); }
int SumFtolScaled(float a, float b, float scale) { return Ftol((static_cast<double>(b) + a) * scale); }
int ScaledFtol(float a, float scale) { return Ftol(static_cast<double>(a) * scale); }
// _ftol(a - the float at `subtrahend`) (FLD a; FSUB).
int DifferenceFtol(float a, float subtrahend) { return Ftol(static_cast<double>(a) - subtrahend); }
// value * factor * the float at `scale`, stored as float (FLD; FMUL; FMUL; FSTP).
float ArmourValue(const void* value, const void* factor, float scale) {
    return static_cast<float>(static_cast<double>(*static_cast<const float*>(value)) * *static_cast<const float*>(factor) * scale);
}
// 0 + p[0] + p[0x12] + p[0x24] (three armour layers 0x48 bytes apart), stored as float.
float LayerSum(const float* p) { return static_cast<float>(((0.0 + p[0]) + p[0x12]) + p[0x24]); }


// The text manager (007AF788; thiscall).
struct Texts : CTextManager {
    BOOL Open(const char* key);
    BOOL NextLine();
    BOOL ReadLine(mfc::CString* out, BOOL advance);
    BOOL GetString(const char* name, mfc::CString* out);
    BOOL ItemTexts(CItem* item, mfc::CString* name, mfc::CString* description, mfc::CString* kind);
};
BOOL Texts::Open(const char* key) { return CTextManager::Open(key); }
BOOL Texts::NextLine() { return TextNextLine(this); }
BOOL Texts::ReadLine(mfc::CString* a0, BOOL a1) { return CTextManager::ReadLine(a0, a1); }
BOOL Texts::GetString(const char* key, mfc::CString* out) { return CTextManager::GetString(key, out); }
BOOL Texts::ItemTexts(CItem* item, mfc::CString* name, mfc::CString* description, mfc::CString* kind) {
    return GetItemTexts(item, name, description, kind);
}
Texts* TextManager() { return static_cast<Texts*>(TheTextManager()); }

// Items and spells through their addresses (thiscall): the prototype record, the
// category, a weapon's type (006A07F0, CItemWeapon::GetType) and damage weights
// (006A0770, CItemWeapon::GetDamageTypes, RET 4); a spell's prototype and modifier
// count (0068BA60, CSpell::GetModifierCount).
const itemdb::ItemRecordHead* PrototypeOf(CItem* item) {
    return item->GetPrototype();
}
int CategoryOf(CItem* item) { return item->GetCategory(); }
const itemdb::SpellRecord* SpellPrototypeOf(CSpell* spell) {
    return spell->GetPrototype();
}

// The panel's item list (+0x150) is a party object (class 00661050): emptied by 00661700
// (PartyResetOriginal), filled by 00661DB0 (CParty::AddItem).

} // namespace

// --- 005F13A0 -----------------------------------------------------------------------------------------

void __fastcall ItemDamageLists(const float* values, console::CStringArray* out, char integer) {
    Texts* texts = TextManager();
    out->SetSize(0, -1);
    mfc::CString value, above, below, key, text;
    float threshold = values[6];
    // EI_Plugin's Fixes: the jump over the integer format is always taken.
    if (integer && !ei::rebirth::Enabled(ei::rebirth::Patch::EiPluginFixes))
        value.Format("%d", FtolOf(threshold));
    else
        value.Format("%.1f", D(threshold));
    for (int i = 0; i < 6; ++i) {
        mfc::CString* list;
        if (values[i] > threshold)
            list = &above;
        else if (values[i] == threshold)
            continue;
        else
            list = &below;   // also NaN
        key.Format("dmg_%d", i);
        texts->GetString(key, &text);
        *list += text;
        *list += ", ";
    }
    above.TrimRight(", ");
    below.TrimRight(", ");
    out->SetAtGrow(out->GetSize(), value);
    out->SetAtGrow(out->GetSize(), above);
    out->SetAtGrow(out->GetSize(), below);
}
EI_REWRITTEN(0x005F13A0, ItemDamageLists);

namespace {
void DamageLists(const float* values, CStringArray* out, char integer) { ItemDamageLists(values, out, integer); }

// The spell and modifier records the spell book reads (itemdb::SpellAt / SpellModifierAt) and the
// item records (item_db.h).

constexpr std::uint32_t kWhite = 0xFFFFFF;
constexpr std::uint32_t kText = 0xA7D7E4;

// What every description shares: the surface, the line rectangle and the formatted line.
class Writer {
public:
    explicit Writer(CItemInfoPanel* panel)
        : panel_(panel), draw_(panel->surfaces_.data[0]), rect_{15, 10, 185, 25} {}

    CUiSurface* Draw() { return draw_; }
    RECT* Rect() { return &rect_; }
    const mfc::CString& T(int index) const { return panel_->itemTexts_[index]; }
    const mfc::CString& Skill(int index) const { return panel_->skillTexts_[index]; }

    void Down() { gameapi::OffsetRect(&rect_, 0, 15); }
    void Text(const char* text) { draw_->DrawLine(text, &rect_, 0, kText, 0, 1); }
    // A value right-aligned 10 pixels in from the line's right edge (format 2).
    void Value(const char* text) {
        RECT shifted = rect_;
        gameapi::OffsetRect(&shifted, -10, 0);
        draw_->DrawLine(text, &shifted, 0, kText, 2, 1);
    }
    void Title(const char* text) { draw_->DrawWrappedText(text, &rect_, 2, 0, kWhite, 0x11, 1); }
    void Wrapped(const char* text, int maxLines) { draw_->DrawWrappedText(text, &rect_, maxLines, 0, kText, 0x11, 1); }

    // The common head of an item description: the title, a gap, the item's kind.
    void Head(const char* name, const char* kind) {
        Title(name);
        Down();
        Down();
        line.Format("%s %s", T(37).GetString(), kind);
        Text(line);
        Down();
    }
    // "%s %d" with item text `label`, then the next line.
    void Number(int label, int value) {
        line.Format("%s %d", T(label).GetString(), value);
        Text(line);
        Down();
    }
    // "<label> <text>" (operator+ with " ").
    void Joined(int label, const char* text) {
        line = T(label);
        line += " ";
        line += text;
        Text(line);
        Down();
    }
    // 005F13A0's lists of 7 damage values: "<label> <threshold>" and, when some kinds lie below it,
    // "<infoitem_24> <those kinds>".
    void DamageLines(const float* values, int label) {
        CStringArray lists;
        DamageLists(values, &lists, 0);
        Joined(label, lists[0]);
        if (lists[2].GetLength() != 0) Joined(24, lists[2]);
    }
    // The "school_<n>" text after infoitem_18.
    void School(int school) {
        mfc::CString key;
        key.Format("school_%d", school);
        TextManager()->GetString(key, &line);
        mfc::CString text = line;
        Joined(18, text);
    }

    mfc::CString line;

private:
    CItemInfoPanel* panel_;
    CUiSurface* draw_;
    RECT rect_;
};

// The kinds of items (CEngineObject +0x10).
constexpr int kWeapon = 0x3004;
constexpr int kArmor = 0x3005;
constexpr int kQuick = 0x3006;
constexpr int kQuest = 0x3007;
constexpr int kSpellItem = 0x3008;
constexpr int kLoot = 0x3009;

int KindOf(CItem* item) { return item->type_; }
std::uint32_t PackedIdOf(CItem* item) { return item->prototypeId_; }
// The loot and quest items' kind (+0x4C), record type (+0x50) and record index (+0x54).
const CItemLoot* LootOf(CItem* item) { return AsLoot(item); }
CSpell* SpellOf(CItem* item) { return item->spell_; }

// Whether a 7-entry damage weight array has a weight that compares unequal (and ordered) to 0:
// FCOMP; TEST AH,0x40; JZ. Returns the index, or 7.
unsigned FirstDamageKind(const float* weights) {
    unsigned i = 0;
    for (; i < 7; ++i) {
        EI_NAN_AUDIT(weights[i]);
        if (weights[i] != 0.0f) break;
    }
    return i;
}

// "%s %s" / "%s %d" lines that pick their format by a value.
void SpellLevelLine(Writer& w, bool known, int level) {
    if (!known)
        w.line.Format("%s %s", w.T(8).GetString(), w.T(28).GetString());
    else
        w.line.Format("%s %d", w.T(8).GetString(), level);
    w.Text(w.line);
    w.Down();
}
// The duration: "<infoitem_12> <infoitem_21>" under 2, else in seconds (FILD of the unsigned value
// times 0.0666667f) with infoitem_27.
void DurationLine(Writer& w, std::uint32_t ticks) {
    if (ticks < 2)
        w.line.Format("%s %s", w.T(12).GetString(), w.T(21).GetString());
    else
        w.line.Format("%s %.1f%s", w.T(12).GetString(),
                      x87::FromUnsigned(ticks) * D(0.06666667f /*00740544*/), w.T(27).GetString());
    w.Text(w.line);
    w.Down();
}

} // namespace

// --- 005EE760 ---------------------------------------------------------------------------------------

void CItemInfoPanel::DescribeItem() {
    CItem* item = Item();
    CUiSurface* draw = surfaces_.data[0];
    draw->GetDC();
    draw->Clear();
    draw->ReleaseDC();
    if (!item) return;

    int kind = KindOf(item);
    if (kind == kQuest) {
        int sort = static_cast<int>(LootOf(item)->kind_);
        int sub = static_cast<int>(LootOf(item)->recordType_);
        if (sort == 1) {
            DescribeMaterial();
            return;
        }
        if (sort == 2 && (sub == 0 || sub == 1 || sub == 2)) {
            DescribeBlueprint();
            return;
        }
        if (sort == 2 && sub == 3) {
            DescribeSpellRecord();
            return;
        }
        if (sort == 2 && sub == 4) {
            DescribeModifierRecord();
            return;
        }
    }
    if (kind == kQuick && CategoryOf(item) == 8) {
        DescribeQuickItem();
        return;
    }
    if ((interface_->ctrlDown_ && SpellOf(item)) || kind == kSpellItem) {
        DescribeCarriedSpell();
        return;
    }

    Texts* texts = TextManager();
    // The material's name ("matshort <NAME>"): the record's name with '_' for ' ', lower case.
    mfc::CString material;
    material = itemdb::MaterialAt(MaterialIndex(PackedIdOf(item)))->key;
    text::ReplaceChar(&material, ' ', '_');
    material.MakeLower();
    mfc::CString name, description, kindText;
    texts->ItemTexts(item, &name, &description, &kindText);

    Writer w(this);
    w.Draw()->GetDC();
    mfc::CString text;
    w.Head(name, kindText);

    // The material and count, unless a quick item whose prototype's +0x10 is "none"; then the price.
    bool materialLine;
    if (kind == kQuick) {
        const itemdb::ItemRecordHead* prototype = PrototypeOf(item);
        materialLine = !prototype || MbsICmp(prototype->materialClass.GetString(), "none") != 0;
    } else {
        materialLine = kind != kQuest && kind != kLoot;
    }
    if (materialLine) {
        text = "Unknown";
        mfc::CString key("matshort ");
        key += material;
        if (texts->Open(key)) texts->ReadLine(&text, 1);
        w.line.Format("%s %s x %d", w.T(1).GetString(), text.GetString(),
                      static_cast<int>(PrototypeOf(item)->materialUnits));   // "%s %s x %d"
        w.Text(w.line);
        w.Down();
    }
    if (kind != kLoot) w.Number(0, FtolOf(item->Weight()));   // +0x34

    if (kind == kArmor || kind == kWeapon || (kind == kQuick && CategoryOf(item) == 5)) {
        // The charge and the carried spell's level.
        CSpell* spell = SpellOf(item);
        int spellValue = spell ? static_cast<int>(spell->skillNeeded_) : 0;
        w.Number(2, FtolOf(item->chargeMax_));
        w.line.Format("%s %d/%d", w.T(14).GetString(), spellValue, static_cast<int>(item->magicLevel_));
        w.Text(w.line);
        w.Down();
    } else if (kind == kQuick) {
        goto tail;
    }

    if (kind == kArmor || kind == kWeapon) {
        // The condition (current only when positive: FCOMP; TEST AH,0x41).
        float current = item->condition_;
        int condition = current > 0.0f ? FtolOf(current) : 0;
        int maximum = FtolOf(item->conditionMax_);
        w.line.Format("%s %d/%d", w.T(3).GetString(), condition, maximum);
        w.Text(w.line);
        w.Down();
        if (kind == kWeapon) {
            CItemWeapon* weapon = AsWeapon(item);
            // Damage: min-max and the first damage kind with a weight ("damage_<n>"; the text
            // keeps the material line's otherwise).
            float weights[7];
            weapon->GetDamageTypes(weights);
            unsigned first = FirstDamageKind(weights);
            if (first < 7) {
                w.line.Format("damage_%d", first);
                texts->GetString(w.line, &text);
            }
            float low = weapon->damage_;
            float spread = weapon->damageSpread_;
            int high = SumFtol(low, spread);
            int lowValue = FtolOf(low);
            w.line.Format("%s %d-%d %s", w.T(15).GetString(), lowValue, high,
                          text.GetString());   // "%s %d-%d %s"
            w.Text(w.line);
            w.Down();
            w.Number(20, static_cast<int>(PrototypeOf(item)->category));
            w.Number(38, Ftol(WeaponStat84(PackedIdOf(item))));
            if (weapon->GetType() == 5 || weapon->GetType() == 6)
                w.Number(10, FtolOf(weapon->reach_));
            else
                w.Number(39, Ftol(WeaponStat88(PackedIdOf(item))));
            goto tail;
        }
    }
    if (kind == kArmor) {
        // Armour: the seven values at +0x4C against their threshold.
        float values[7];
        std::memcpy(values, static_cast<const CItemArmor*>(item)->absorb_[0], sizeof(values));
        w.DamageLines(values, 25);
    }

tail:
    w.Down();
    if (kind == kQuest) {
        texts->GetString("loot_item", &text);
        description += text;
        w.Wrapped(description, 7);
    } else {
        w.Wrapped(description, 4);
        w.Down();
        w.Down();
        w.Down();
        w.Down();
        if (kind != kWeapon) w.Down();
        CSpell* spell;
        if ((kind == kArmor || kind == kWeapon || (kind == kQuick && CategoryOf(item) == 5)) &&
            (spell = SpellOf(item)) != nullptr) {
            // The carried spell's name ("spell <name>").
            text = "Unknown";
            mfc::CString key("spell ");
            key += SpellPrototypeOf(spell)->code;
            if (texts->Open(key)) texts->ReadLine(&text, 1);
            w.line.Format("%s %s", w.T(5).GetString(), text.GetString());
            w.Text(w.line);
            w.Down();
            w.Text(w.T(6));
        }
    }
    w.Draw()->ReleaseDC();
}
EI_REWRITTEN(0x005EE760, CItemInfoPanel::DescribeItem);

// --- 005EFD90: a material ------------------------------------------------------------------------

void CItemInfoPanel::DescribeMaterial() {
    CItem* item = Item();
    Texts* texts = TextManager();
    const itemdb::MaterialRecordHead* material = itemdb::MaterialAt(LootOf(item)->index_);
    if (!material) return;
    mfc::CString name, description, kindText;
    texts->ItemTexts(item, &name, &description, &kindText);
    Writer w(this);
    w.Draw()->GetDC();
    w.Head(name, kindText);
    // The material's type: "mattype_<NAME>".
    mfc::CString text = material->soundName;
    text.MakeLower();
    {
        mfc::CString key("mattype_");
        key += text;
        text = key;
    }
    texts->GetString(text, &w.line);
    {
        mfc::CString type = w.line;
        w.Joined(17, type);
    }
    w.Number(0, FtolOf(material->factor38));
    w.Number(2, FtolOf(material->magicPower));
    w.Number(14, static_cast<int>(material->magicLevel));
    w.Number(3, FtolOf(material->conditionFactor));
    w.line.Format("%s %.1f", w.T(15).GetString(), D(material->damageFactor));
    w.Text(w.line);
    w.Down();
    float values[7];
    std::memcpy(values, material->absorbFactors, sizeof(values));
    w.DamageLines(values, 4);
    w.Down();
    w.Wrapped(description, 4);
    w.Draw()->ReleaseDC();
}
EI_REWRITTEN(0x005EFD90, CItemInfoPanel::DescribeMaterial);

// --- 005F0370: a blueprint (a weapon, armour or quick item record) -----------------------------------

void CItemInfoPanel::DescribeBlueprint() {
    CItem* item = Item();
    Texts* texts = TextManager();
    // The record by the blueprint's kind (+0x50): weapons, armour, quick items. The dispatcher
    // calls this only for kinds 0-2; the original leaves the record and kind uninitialised
    // otherwise (here: none, and it faults reading the record like a missing one).
    int sort = 3;
    const itemdb::ItemRecordHead* head = nullptr;
    std::uint32_t index = LootOf(item)->index_;
    switch (static_cast<int>(LootOf(item)->recordType_)) {
    case 0: sort = 0; head = itemdb::WeaponAt(index); break;
    case 1: sort = 1; head = itemdb::ArmorAt(index); break;
    case 2: sort = 2; head = itemdb::QuickItemAt(index); break;
    default: break;
    }
    mfc::CString name, description, kindText;
    texts->ItemTexts(item, &name, &description, &kindText);
    Writer w(this);
    w.Draw()->GetDC();
    w.Head(name, kindText);
    // The material type ("mattype_<NAME>" of the record's +0x10) and the count.
    // (The weapon and armour views are read only for their own sort.)
    const auto* weapon = itemdb::RecordAs<itemdb::WeaponRecord>(head);
    const auto* armor = itemdb::RecordAs<itemdb::ArmorRecord>(head);
    mfc::CString text = head->materialClass;
    text.MakeLower();
    {
        mfc::CString key("mattype_");
        key += text;
        text = key;
    }
    texts->GetString(text, &w.line);
    text = w.line;
    w.line.Format("%s %s x %d", w.T(1).GetString(), text.GetString(),
                  static_cast<int>(head->materialUnits));   // "%s %s x %d"
    w.Text(w.line);
    w.Down();
    w.Number(0, FtolOf(head->value38));
    if (sort <= 2) {
        w.Number(2, FtolOf(head->charge));
        w.Number(14, static_cast<int>(head->magicLevel));
    }
    if (sort <= 1) w.Number(3, FtolOf(head->condition));
    if (sort == 0) {
        // A weapon: damage (times 10), the first damage kind with a weight, and its figures.
        unsigned first = FirstDamageKind(weapon->damageTypes);
        if (first < 7) {
            w.line.Format("damage_%d", first);
            texts->GetString(w.line, &text);
        }
        int high = SumFtolScaled(weapon->damage, weapon->damageSpread, 10.0f /*0073BAAC*/);
        int low = ScaledFtol(weapon->damage, 10.0f /*0073BAAC*/);
        w.line.Format("%s %d-%d %s", w.T(15).GetString(), low, high,
                      text.GetString());   // "%s %d-%d %s"
        w.Text(w.line);
        w.Down();
        w.Number(20, static_cast<int>(head->category));
        w.Number(38, FtolOf(weapon->attackBonus));
        int type = head->type;
        if (type == 5 || type == 6)
            w.Number(10, FtolOf(FloatOf(weapon->reach)));
        else
            w.Number(39, FtolOf(weapon->defenceBonus));
    } else if (sort == 1) {
        // Armour: the seven values scaled by +0x6C (a float here) and 10, stored as floats.
        float values[7];
        for (int i = 0; i < 7; ++i) values[i] = ArmourValue(&armor->absorbFirst[i], &armor->category, 10.0f /*0073BAAC*/);
        w.DamageLines(values, 4);
    }
    w.Down();
    w.Wrapped(description, 4);
    w.Draw()->ReleaseDC();
}
EI_REWRITTEN(0x005F0370, CItemInfoPanel::DescribeBlueprint);

// --- 005F0B90: a spell record ---------------------------------------------------------------------

void CItemInfoPanel::DescribeSpellRecord() {
    CItem* item = Item();
    Texts* texts = TextManager();
    const itemdb::SpellRecord* record = itemdb::SpellAt(LootOf(item)->index_);
    mfc::CString name, description, kindText;
    texts->ItemTexts(item, &name, &description, &kindText);
    Writer w(this);
    w.Draw()->GetDC();
    w.Head(name, kindText);
    w.Number(7, FtolOf(record->mana));
    w.Number(14, record->skill);
    w.School(record->castType);
    bool known = record->bytes78[3] != 0;
    // The level (damage), power (speed), range, radius (area), duration and target count.
    SpellLevelLine(w, known, known ? FtolOf(record->damage) : 0);
    float power = record->speed;
    EI_NAN_AUDIT(power);
    if (power < 1.0f)
        w.line.Format("%s %s", w.T(9).GetString(), w.T(21).GetString());
    else
        w.line.Format("%s %d", w.T(9).GetString(), FtolOf(power));
    w.Text(w.line);
    w.Down();
    w.line.Format("%s %d%s", w.T(10).GetString(), FtolOf(record->range),
                  w.T(26).GetString());   // "%s %d%s"
    w.Text(w.line);
    w.Down();
    float radius = record->area;
    EI_NAN_AUDIT(radius);
    if (radius < 1.0f)
        w.line.Format("%s %s", w.T(11).GetString(), w.T(22).GetString());
    else
        w.line.Format("%s %.1f%s", w.T(11).GetString(), D(radius),
                      w.T(26).GetString());   // "%s %.1f%s"
    w.Text(w.line);
    w.Down();
    DurationLine(w, record->duration);
    w.Number(13, static_cast<int>(record->targetCount));
    w.Down();
    w.Wrapped(description, 4);
    w.Draw()->ReleaseDC();
}
EI_REWRITTEN(0x005F0B90, CItemInfoPanel::DescribeSpellRecord);

// --- 005F1130: a spell modifier record ------------------------------------------------------------

void CItemInfoPanel::DescribeModifierRecord() {
    CItem* item = Item();
    Texts* texts = TextManager();
    mfc::CString name, description, kindText;
    texts->ItemTexts(item, &name, &description, &kindText);
    const itemdb::SpellModifierRecord* record = itemdb::SpellModifierAt(LootOf(item)->index_);
    Writer w(this);
    w.Draw()->GetDC();
    w.Head(name, kindText);
    w.Number(7, FtolOf(record->mana));
    w.Number(14, record->skill);
    w.Down();
    w.Wrapped(description, 8);
    w.Draw()->ReleaseDC();
}
EI_REWRITTEN(0x005F1130, CItemInfoPanel::DescribeModifierRecord);

// --- 005EF300: a quick item of category 8 (its spell's level and duration) ------------------------

void CItemInfoPanel::DescribeQuickItem() {
    CItem* item = Item();
    Texts* texts = TextManager();
    mfc::CString name, description, kindText;
    texts->ItemTexts(item, &name, &description, &kindText);
    Writer w(this);
    w.Draw()->GetDC();
    w.Head(name, kindText);
    if (CSpell* spell = SpellOf(item)) {
        bool known = SpellPrototypeOf(spell)->bytes78[3] != 0;
        SpellLevelLine(w, known, known ? FtolOf(spell->damage_) : 0);
        DurationLine(w, spell->duration_);
        w.Down();
    }
    w.Wrapped(description, 8);
    w.Draw()->ReleaseDC();
}
EI_REWRITTEN(0x005EF300, CItemInfoPanel::DescribeQuickItem);

// --- 005EF5D0: the spell an item carries (or a spell container) -----------------------------------

void CItemInfoPanel::DescribeCarriedSpell() {
    CItem* item = Item();
    CSpell* spell = item ? SpellOf(item) : nullptr;
    if (!spell) {
        surfaces_.data[0]->ReleaseDC();   // without a Begin, as the original
        return;
    }
    Texts* texts = TextManager();
    mfc::CString title, description, kindText;
    {
        mfc::CString key("spell ");
        key += SpellPrototypeOf(spell)->code;
        if (texts->Open(key)) {
            texts->ReadLine(&title, 1);
            texts->ReadToHash(&description, 1);
        }
    }
    texts->GetString("item_spell", &kindText);
    Writer w(this);
    w.Draw()->GetDC();
    mfc::CString text;
    w.Head(title, kindText);
    w.Number(7, FtolOf(spell->manaCost_));
    w.Number(14, static_cast<int>(spell->skillNeeded_));
    w.School(SpellPrototypeOf(spell)->castType);
    bool known = SpellPrototypeOf(spell)->bytes78[3] != 0;
    SpellLevelLine(w, known, known ? FtolOf(spell->damage_) : 0);
    float power = spell->speed_;
    EI_NAN_AUDIT(power);
    if (power < 1.0f)
        w.line.Format("%s %s", w.T(9).GetString(), w.T(21).GetString());
    else
        w.line.Format("%s %d", w.T(9).GetString(), FtolOf(power));
    w.Text(w.line);
    w.Down();
    w.line.Format("%s %d%s", w.T(10).GetString(), FtolOf(spell->range_),
                  w.T(26).GetString());   // "%s %d%s"
    w.Text(w.line);
    w.Down();
    if (spell->targetFlags_ & 0x10000000) {
        w.line.Format("%s %s", w.T(11).GetString(), w.T(22).GetString());
    } else {
        // The area: pi * r * r in that order (FLD r; FLD pi; FMUL ST1; FMUL ST1).
        double radius = D(spell->radius_);
        double area = D(3.1415927f /*007402C0*/) * radius * radius;
        w.line.Format("%s %.1f%s", w.T(11).GetString(), area, w.T(26).GetString());
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
            mfc::CString key("modifier ");
            key += modifier->code;
            if (texts->Open(key)) {
                texts->NextLine();
                texts->NextLine();
                texts->ReadLine(&text, 1);
            }
            mfc::CString entry = text;
            entry += ",";
            w.line += entry;
        }
        w.line.TrimRight(",");
        w.line += ".";
        w.Draw()->DrawWrappedText(w.line, w.Rect(), 3, 0, kText, 0x10, 1);
    }
    w.Draw()->ReleaseDC();
}
EI_REWRITTEN(0x005EF5D0, CItemInfoPanel::DescribeCarriedSpell);

// --- 005F1590: the statistic under the mouse ----------------------------------------------------------

namespace {

// Kind 0, an attribute: two of its derived values (UnitStats methods), percentages where scaled.
void AttributeLines(Writer& w, UnitStats* stats, int index) {
    auto line = [&](const char* format, int label, int value) {
        w.line.Format(format, w.Skill(label).GetString(), value);
        w.Text(w.line);
        w.Down();
    };
    switch (index) {
    case 0:
        line("%s %d%%", 0, Ftol(stats->StrengthRatio() * kPercentScale));
        line("%s %d", 1, Ftol(stats->Value6E8()));
        w.Down();
        break;
    case 1:
        line("%s %d%%", 2, Ftol(stats->DexterityRatio() * kPercentScale));
        line("%s %d", 3, Ftol(stats->RateValue()));
        w.Down();
        break;
    case 2:
        // 1.04 ^ (intelligence - 25) * 100.
        line("%s %d%%", 4, PowFtol(kIntelligenceGrowth, stats->stat20, kStatBase, kPercentScale));
        line("%s %d%%", 24, Ftol(stats->Period20() * kPercentScale));
        w.Down();
        break;
    default:
        break;
    }
}

// Kind 3, a perk: the three levels of the perk's group with their effect and cost.
void PerkLines(Writer& w, const ItemHoverInfo* info, mfc::CString& title) {
    Texts* texts = TextManager();
    mfc::CString name = info->text;
    name.TrimRight("0");
    name += "1";
    int group = SkillDb()->IndexOf(name) / 3;
    for (int i = 0; i < 3; ++i) {
        const SkillLevelRecord* level = unitdb::SkillLevelAt(static_cast<std::uint32_t>(group * 3 + i));
        if (!level) continue;
        mfc::CString key("perk ");
        key += level->name;
        if (texts->Open(key)) texts->ReadLine(&title, 1);
        w.Text(title);
        if (info->index > i) w.Draw()->DrawLine(w.Skill(21), w.Rect(), 0, kText, 2, 1);   // learned
        w.Down();
        // The level's effect by the perk group.
        int value = static_cast<int>(level->values[7]);
        const char* percent = "     %s %d%%";
        const char* plain = "     %s %d";
        bool drawn = true;
        switch (group) {
        case 0: case 1: case 2: case 3: case 4: case 5: case 6:
            w.line.Format(plain, w.Skill(10).GetString(), value);
            break;
        case 7: case 8: case 9: case 10: case 11: case 12: case 13: case 14:
            w.line.Format(plain, w.Skill(11).GetString(), value);
            break;
        case 15:
            w.line.Format(percent, w.Skill(12).GetString(), static_cast<std::uint32_t>(value + 100) >> 1);
            break;
        case 16: w.line.Format(percent, w.Skill(14).GetString(), value + 100); break;
        case 17: w.line.Format(percent, w.Skill(15).GetString(), value + 100); break;
        case 18: w.line.Format(percent, w.Skill(16).GetString(), value + 100); break;
        case 19: w.line.Format(percent, w.Skill(17).GetString(), value + 100); break;
        case 20: w.line.Format(percent, w.Skill(3).GetString(), value + 100); break;
        case 21: w.line.Format(percent, w.Skill(1).GetString(), value + 100); break;
        case 22: w.line.Format(percent, w.Skill(22).GetString(), value + 300); break;
        case 23: case 24: case 25:
            w.line.Format(plain, w.Skill(23).GetString(), value);
            break;
        default:
            drawn = false;
            break;
        }
        if (drawn) w.Text(w.line);
        w.Down();
        // The cost (negative: 0x7FFFFFFF), on a line 15 pixels wider.
        w.Rect()->right += 15;
        int cost = info->stats->SkillPrice(level);
        if (cost < 0) cost = 0x7FFFFFFF;
        w.line.Format(plain, w.Skill(18).GetString(), cost);
        w.Text(w.line);
        w.Rect()->right -= 15;
        w.Down();
    }
}

// Kind 5, the weapon's attack or defence: its four parts and their sum (or, for the defence of a
// weapon of type 5 or 6, infoitem_34).
void WeaponLines(Writer& w, const ItemHoverInfo* info) {
    CItemWeapon* weapon = info->weapon;
    UnitStats* stats = info->stats;
    int type = weapon->GetType();
    if (MbsCmp(info->text, "defense") == 0 && (type == 5 || type == 6)) {
        w.Wrapped(w.T(34), 2);
        w.Down();
        return;
    }
    auto part = [&](int label, int value) {
        w.line.Format("     %s", w.T(label).GetString());
        w.Text(w.line);
        w.line.Format("%d", value);
        w.Value(w.line);
    };
    int base = DifferenceFtol(stats->stat18, kStatBase);   // dexterity - 25
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
    w.line.Format("     %s", w.T(32).GetString());
    w.Text(w.line);
    int own = Ftol(WeaponPrototypeSkillBonus(PackedIdOf(weapon)));
    if (MbsCmp(info->text, "defense") == 0)
        own += Ftol(WeaponStat88(PackedIdOf(weapon)));
    else
        own += Ftol(WeaponStat84(PackedIdOf(weapon)));
    w.line.Format("%d", own);
    w.Value(w.line);
    sum += own;
    w.Down();
    w.Down();
    part(33, sum);
}

} // namespace

void CItemInfoPanel::DescribeStats(ItemHoverInfo* info) {
    reinterpret_cast<CParty*>(itemList_)->Clear(1, 0);
    DescribeItem();
    if (info->kind < 0) return;
    Texts* texts = TextManager();
    Writer w(this);
    w.Draw()->GetDC();
    mfc::CString text, title, description;
    if (info->kind <= 3) {
        mfc::CString key("perk ");
        key += info->text;
        if (texts->Open(key)) {
            texts->ReadLine(&title, 1);
            texts->ReadLine(&description, 1);
        }
    } else if (info->kind <= 7) {
        texts->GetString(info->text, &title);
        mfc::CString key = info->text;
        key += "_desc";
        texts->GetString(key, &description);
    }
    w.Title(title);
    w.Down();
    w.Down();
    UnitStats* stats = info->stats;
    switch (info->kind) {
    case 0:
        AttributeLines(w, stats, info->index);
        w.Wrapped(description, 13);
        break;
    case 1: {
        // A skill: its level and, for the first three, a derived value.
        int index = info->index;
        w.line.Format("%s %d", w.Skill(5).GetString(), Ftol(stats->SkillBase(index)));
        w.Text(w.line);
        w.Down();
        if (index >= 0) {
            bool drawn = true;
            if (index <= 1)
                w.line.Format("%s %d", w.Skill(6).GetString(),
                              Ftol(stats->SkillWithBonus(static_cast<std::uint32_t>(index))));
            else if (index == 2)
                w.line.Format("%s %d%%", w.Skill(7).GetString(),
                              Ftol(stats->Value6E9() * kPercentScale));
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
    case 2:
        // A skill school: its level and, for school 0, a second value.
        w.line.Format("%s %d", w.Skill(5).GetString(),
                      Ftol(stats->AttributeBase(info->index)));
        w.Text(w.line);
        w.Down();
        if (info->index == 0) {
            w.line.Format("%s %d", w.Skill(9).GetString(), Ftol(stats->AttributeWithBonus(0)));
            w.Text(w.line);
            w.Down();
        }
        w.Down();
        w.Wrapped(description, 13);
        break;
    case 3:
        PerkLines(w, info, title);
        w.Down();
        w.Wrapped(description, 6);
        break;
    case 4: {
        // A body part's armour: per damage kind the three layers' sum.
        w.Text(w.T(25));
        w.Down();
        float sums[7];
        const UnitStatSlot& slot = stats->slots[info->part];
        for (int i = 0; i < 7; ++i) sums[i] = LayerSum(&slot.layers[0].absorb[i]);
        for (int i = 0; i < 6; ++i) {
            w.line.Format("damage_%d", i);
            texts->GetString(w.line, &text);
            w.line.Format("     %s:", text.GetString());
            w.Text(w.line);
            w.line.Format("%.1f", D(sums[i]));
            w.Value(w.line);
            w.Down();
        }
        w.Down();
        w.Wrapped(description, 8);
        break;
    }
    case 5:
        WeaponLines(w, info);
        w.Down();
        w.Down();
        w.Wrapped(description, 9);
        break;
    case 6: {
        // The magic skills: schools 0-3 and 5 with infoitem_40..44.
        int label = 40;
        for (int school = 0; school < 8; ++school) {
            if (school == 4 || school == 6 || school == 7) continue;
            w.line.Format("     %s", w.T(label++).GetString());
            w.Text(w.line);
            w.line.Format("%d", Ftol(stats->SpellSchoolSkill(school)));
            w.Value(w.line);
            w.Down();
        }
        w.Down();
        w.Wrapped(description, 9);
        break;
    }
    case 7:
        w.Wrapped(description, 16);
        break;
    default:
        break;
    }
    w.Draw()->ReleaseDC();
}
EI_REWRITTEN(0x005F1590, CItemInfoPanel::DescribeStats);

// --- 005F2910: a camp help topic ----------------------------------------------------------------------

void CItemInfoPanel::ShowHelp(int topic) {
    reinterpret_cast<CParty*>(itemList_)->Clear(1, 0);
    DescribeItem();
    Texts* texts = TextManager();
    Writer w(this);
    w.Draw()->GetDC();
    mfc::CString title, description, key;
    key.Format("camphelp %d", topic);
    if (texts->Open(key)) {
        texts->ReadLine(&title, 1);
        texts->ReadLine(&description, 1);
    }
    w.Title(title);
    w.Down();
    w.Down();
    w.Wrapped(description, 16);
    w.Draw()->ReleaseDC();
}
EI_REWRITTEN(0x005F2910, CItemInfoPanel::ShowHelp);

// --- 005EE6C0: describe an item ---------------------------------------------------------------------

void CItemInfoPanel::Describe(CItem* item) {
    // Nothing to do when the described item is the same (its virtual slot 14, IsSameAs).
    if (itemCount_ && items_[0] && item) {
        if (items_[0]->IsSameAs(item)) return;
    }
    reinterpret_cast<CParty*>(itemList_)->Clear(1, 0);
    if (item) ItemParty()->AddItem(item, 0, 1);
    DescribeItem();
}
EI_REWRITTEN(0x005EE6C0, CItemInfoPanel::Describe);

// --- Input side (window + 0x94): 005EE6B0 slot 1, 005EE640 key down, 005EE680 key up ------------------

void CItemInfoPanel::OnKeyDown(int key, int, int flags) {
    // Not a repeat: bit 14 of the flags is the key's previous state.
    if (key == VK_CONTROL && !(flags & 0x4000) && Item()) DescribeItem();
}

void CItemInfoPanel::OnKeyUp(int key, int, int) {
    if (key == VK_CONTROL && Item()) DescribeItem();
}

namespace {
CItemInfoPanel* PanelOf(void* input) { return reinterpret_cast<CItemInfoPanel*>(static_cast<std::uint8_t*>(input) - 0x94); }

// 005EE6B0 (input slot 1): redraws the description.
void __fastcall InfoPanelRefresh(void* input) { PanelOf(input)->DescribeItem(); }
} // namespace
EI_REWRITTEN(0x005EE6B0, InfoPanelRefresh);
EI_REWRITTEN(0x005EE640, CItemInfoPanel::OnKeyDown);
EI_REWRITTEN(0x005EE680, CItemInfoPanel::OnKeyUp);

// --- 005EE440: Build ------------------------------------------------------------------------------

CItemInfoPanel::CItemInfoPanel(CInterface3D* ui, CItemScreen* owner) : CUiItemPanel(ui, owner) {
    new (ItemParty()) CParty();
    itemTexts_.Construct();
    skillTexts_.Construct();
}

void CItemInfoPanel::Build(int x, int y) {
    x_ = x;
    y_ = y;
    AddSurface(new CUiSurface(interface_, x_, y_, x_ + 200, y_ + 300, 5, 1));
    Texts* texts = TextManager();
    mfc::CString text, key;
    mfc::CStringArray* itemTexts = &itemTexts_;
    for (int i = 0; i < 45; ++i) {
        key.Format("infoitem_%d", i);
        texts->GetString(key, &text);
        itemTexts->SetAtGrow(itemTexts->GetSize(), text);
    }
    mfc::CStringArray* skillTexts = &skillTexts_;
    for (int i = 0; i < 25; ++i) {
        key.Format("infoskill_%d", i);
        texts->GetString(key, &text);
        skillTexts->SetAtGrow(skillTexts->GetSize(), text);
    }
    ItemParty()->Create();
}
EI_REWRITTEN(0x005EE440, CItemInfoPanel::Build);
