// The item description panel (005EE6C0-005F2A80): what the camp, the multiplayer hero screen and
// the exchange window write about the item or statistic under the mouse. See ui_item_info.h and
// docs/notes/ui.md ("Item descriptions").
//
// Each function draws text lines into the panel's surface 0 between Begin (005FDC00) and End
// (005FDC60): Text (005FDE20, Rebirth-patched, called through its address) for single lines and
// DrawWrappedText (005FDFA0) for the wrapped title and description. Values the original truncates
// with the game's _ftol are converted the same way; values computed by original functions are
// truncated straight from the FPU stack (StatFtol), as the original does, so no rounding happens
// in between. Compiled with precise floating point so that products keep the original's order.
#include "ui_item_info.h"
#include "game_strings.h"

#include "console.h"
#include "item.h"
#include "item_db.h"
#include "item_db_text.h"
#include "spell.h"
#include "unit.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"

#include <concepts>
#include <cstdint>
#include <cstring>
#include "originals_effects.h"
#include "originals_ui.h"
#include "originals_engine.h"
#include "originals_gameplay.h"
#include "originals_units.h"

namespace {

using console::CStringArray;

inline double D(float f) { return f; }
float GameFloat(std::uintptr_t address) { return *reinterpret_cast<const volatile float*>(address); }

template <typename T>
T Read(const void* p, int offset) {
    T v;
    std::memcpy(&v, static_cast<const std::uint8_t*>(p) + offset, sizeof(T));
    return v;
}
// A float the item database keeps as its bits (a dword field).
float FloatOf(std::uint32_t bits) {
    float v;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
}
// A float argument would convert to an integer first: callers pass raw bits only.
template <std::floating_point T> float FloatOf(T) = delete;

const itemdb::SpellRecord* AsSpell(const char* record) { return reinterpret_cast<const itemdb::SpellRecord*>(record); }

// The game's _ftol (006E853C): truncation of the value in ST0.
__declspec(naked) int __cdecl GameFtol(double) {
    __asm {
        fld qword ptr [esp + 4]
        mov eax, 0x006E853C
        call eax
        ret
    }
}
int FtolOf(float value) { return GameFtol(D(value)); }

int MbsICmp(const char* a, const char* b) {
    return gamecrt::mbsicmp(reinterpret_cast<const unsigned char*>(a), reinterpret_cast<const unsigned char*>(b));
}
int MbsCmp(const char* a, const char* b) {
    return gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(a), reinterpret_cast<const unsigned char*>(b));
}

// _ftol of what an original __thiscall function leaves in ST0 (optionally with one stack argument
// and a multiplication by the float at `scale`), without the value leaving the FPU stack.
int StatFtol(std::uintptr_t fn, const void* self) {
    int result;
    __asm {
        mov ecx, self
        mov eax, fn
        call eax
        mov eax, 0x006E853C
        call eax
        mov result, eax
    }
    return result;
}
int StatFtolScaled(std::uintptr_t fn, const void* self, std::uintptr_t scale) {
    int result;
    __asm {
        mov ecx, self
        mov eax, fn
        call eax
        mov eax, scale
        fmul dword ptr [eax]
        mov eax, 0x006E853C
        call eax
        mov result, eax
    }
    return result;
}
int StatFtolArg(std::uintptr_t fn, const void* self, int arg) {
    int result;
    __asm {
        push arg
        mov ecx, self
        mov eax, fn
        call eax
        mov eax, 0x006E853C
        call eax
        mov result, eax
    }
    return result;
}
int StatFtolArgScaled(std::uintptr_t fn, const void* self, int arg, std::uintptr_t scale) {
    int result;
    __asm {
        push arg
        mov ecx, self
        mov eax, fn
        call eax
        mov eax, scale
        fmul dword ptr [eax]
        mov eax, 0x006E853C
        call eax
        mov result, eax
    }
    return result;
}
// _ftol(_CIpow(base, exponent - subtrahend) * scale): the base is the double at `base`, the exponent a
// float minus the float at `subtrahend` (FSUB), the product with the float at `scale`, all on the FPU.
int PowFtol(std::uintptr_t base, float exponent, std::uintptr_t subtrahend, std::uintptr_t scale) {
    int result;
    __asm {
        mov eax, base
        fld qword ptr [eax]
        fld exponent
        mov eax, subtrahend
        fsub dword ptr [eax]
        mov eax, 0x006E81A0
        call eax
        mov eax, scale
        fmul dword ptr [eax]
        mov eax, 0x006E853C
        call eax
        mov result, eax
    }
    return result;
}
// _ftol of a + b (FLD b; FADD a), optionally times the float at `scale`, on the FPU.
int SumFtol(float a, float b) {
    int result;
    __asm {
        fld b
        fadd a
        mov eax, 0x006E853C
        call eax
        mov result, eax
    }
    return result;
}
int SumFtolScaled(float a, float b, std::uintptr_t scale) {
    int result;
    __asm {
        fld b
        fadd a
        mov eax, scale
        fmul dword ptr [eax]
        mov eax, 0x006E853C
        call eax
        mov result, eax
    }
    return result;
}
int ScaledFtol(float a, std::uintptr_t scale) {
    int result;
    __asm {
        fld a
        mov eax, scale
        fmul dword ptr [eax]
        mov eax, 0x006E853C
        call eax
        mov result, eax
    }
    return result;
}
// _ftol(a - the float at `subtrahend`) on the FPU (FLD a; FSUB).
int DifferenceFtol(float a, std::uintptr_t subtrahend) {
    int result;
    __asm {
        fld a
        mov eax, subtrahend
        fsub dword ptr [eax]
        mov eax, 0x006E853C
        call eax
        mov result, eax
    }
    return result;
}
// value * factor * the float at `scale`, stored as float (FLD; FMUL; FMUL; FSTP).
float ArmourValue(const void* value, const void* factor, std::uintptr_t scale) {
    float result;
    __asm {
        mov eax, value
        fld dword ptr [eax]
        mov eax, factor
        fmul dword ptr [eax]
        mov eax, scale
        fmul dword ptr [eax]
        fstp result
    }
    return result;
}
// 0 + p[0] + p[0x12] + p[0x24] (three armour layers 0x48 bytes apart), stored as float.
float LayerSum(const float* p) {
    float result;
    __asm {
        mov eax, p
        fldz
        fadd dword ptr [eax]
        fadd dword ptr [eax + 0x48]
        fadd dword ptr [eax + 0x90]
        fstp result
    }
    return result;
}
// An original function returning a float in ST0 with the packed id in ECX (006A2D40 / 006A2D90),
// truncated.
int PackedFtol(std::uintptr_t fn, std::uint32_t packedId) {
    int result;
    __asm {
        mov ecx, packedId
        mov eax, fn
        call eax
        mov eax, 0x006E853C
        call eax
        mov result, eax
    }
    return result;
}

// The surface's drawing (thiscall on the surface).
struct SurfaceDraw {
    void* Begin() { return SurfaceGetDC(this); }                                                                   // 005FDC00
    void Clear() { SurfaceClearOriginal(this); }                                                                   // 005FE2F0
    int Text(const char* text, RECT* rect, int font, std::uint32_t colour, int format, int flag);   // 005FDE20
    int Wrapped(const char* text, RECT* rect, int maxLines, int font, std::uint32_t colour, int format, int flag);   // 005FDFA0
    void End() { SurfaceReleaseDC(this); }                                                                     // 005FDC60
};
int SurfaceDraw::Text(const char* text, RECT* rect, int format, std::uint32_t colour, int a, int b) { return SurfaceDrawText(this, nullptr, text, rect, format, colour, a, b); }
__declspec(naked) int SurfaceDraw::Wrapped(const char*, RECT*, int, int, std::uint32_t, int, int) EI_ORIGINAL(0x005FDFA0)

// The text manager (007AF788; thiscall).
struct Texts {
    BOOL Open(const char* key);                          // 004DEFC0
    BOOL NextLine();                                     // 004DF510
    BOOL ReadLine(mfc::CString* out, BOOL advance);      // 004DF8D0
    BOOL GetString(const char* name, mfc::CString* out); // 004E0020
    BOOL ItemTexts(CItem* item, mfc::CString* name, mfc::CString* description, mfc::CString* kind);   // 004E0150
};
BOOL Texts::Open(const char* key) { return ::TextsOpenSection(this, nullptr, key); }
BOOL Texts::NextLine() { return ReaderNextContentLine(reinterpret_cast<CTextManager*>(this)); }
BOOL Texts::ReadLine(mfc::CString* a0, BOOL a1) { return (BOOL)TextsReadLineOriginal((void*)this, nullptr, a0, (int)a1); }
BOOL Texts::GetString(const char* key, mfc::CString* out) { return ::TextsGetString(this, nullptr, key, out); }
BOOL Texts::ItemTexts(CItem* item, mfc::CString* name, mfc::CString* description, mfc::CString* kind) {
    return TextManagerItemTexts(reinterpret_cast<CTextManager*>(this), nullptr, item, name, description, kind);
}
Texts* TextManager() { return *reinterpret_cast<Texts**>(0x007AF788); }

int Length(const mfc::CString& s) { return reinterpret_cast<const int*>(static_cast<const char*>(s))[-2]; }

// Items and spells through their addresses (thiscall): the prototype record (006A0110), the
// category (006A4230), a weapon's type (006A07F0, CItemWeapon::GetType) and damage weights
// (006A0770, CItemWeapon::GetDamageTypes, RET 4); a spell's prototype (0068BA20) and modifier
// count (0068BA60, CSpell::GetModifierCount).
const itemdb::ItemRecordHead* PrototypeOf(CItem* item) {
    return reinterpret_cast<const itemdb::ItemRecordHead*>(ItemGetPrototypeOriginal(item));
}
int CategoryOf(CItem* item) { return ItemGetCategoryOriginal(item); }
__declspec(naked) int __fastcall WeaponTypeOf(CItem*) EI_ORIGINAL(0x006A07F0)
__declspec(naked) void __fastcall DamageTypesOf(CItem*, void*, float*) EI_ORIGINAL(0x006A0770)
const itemdb::SpellRecord* SpellPrototypeOf(void* spell) {
    return reinterpret_cast<const itemdb::SpellRecord*>(SpellGetPrototypeOriginal(static_cast<CSpell*>(spell)));
}
__declspec(naked) int __fastcall ModifierCountOf(void*) EI_ORIGINAL(0x0068BA60)

// The panel's item list (+0x150) is a party object (class 00661050): emptied by 00661700
// (PartyResetOriginal), filled by 00661DB0 (PartyAddItemOriginal).

} // namespace

// --- 005F13A0 -----------------------------------------------------------------------------------------

void __fastcall ItemDamageLists(const float* values, console::CStringArray* out, char integer) {
    Texts* texts = TextManager();
    out->SetSize(0, -1);
    mfc::CString value, above, below, key, text;
    float threshold = values[6];
    // EI_Plugin's Fixes (005F1401): the jump over the integer format is always taken.
    if (integer && !ei::rebirth::Enabled(ei::rebirth::Patch::EiPluginFixes))
        value.Format(game::Text(0x0078EEEC), FtolOf(threshold));   // "%d"
    else
        value.Format(game::Text(0x00792F78), D(threshold));        // "%.1f"
    for (int i = 0; i < 6; ++i) {
        // FCOMP; TEST AH,0x41: above (ordered); else TEST AH,1: below or unordered.
        mfc::CString* list;
        if (values[i] > threshold)
            list = &above;
        else if (!(values[i] >= threshold))
            list = &below;
        else
            continue;
        key.Format(game::Text(0x00792F70), i);   // "dmg_%d"
        texts->GetString(key, &text);
        *list += text;
        *list += game::Text(0x0078DA54);         // ", "
    }
    above.TrimRight(game::Text(0x0078DA54));
    below.TrimRight(game::Text(0x0078DA54));
    out->SetAtGrow(out->GetSize(), value);
    out->SetAtGrow(out->GetSize(), above);
    out->SetAtGrow(out->GetSize(), below);
}
EI_REPLACE(0x005F13A0, ItemDamageLists);

namespace {
void DamageLists(const float* values, CStringArray* out, char integer) { ItemDamageLists(values, out, integer); }

// The skill database (007AF268), for SkillIndexOfOriginal (0051FE80): the index of the level record named `name`.
const void* Skills() { return reinterpret_cast<const void*>(0x007AF268); }

// The spell and modifier record tables the spell book reads (UiRecordTable, ui.h).
const UiRecordTable* SpellRecords() { return reinterpret_cast<const UiRecordTable*>(0x007C8510); }
const UiRecordTable* ModifierRecords() { return reinterpret_cast<const UiRecordTable*>(0x007C8530); }

// Record tables by stride (item_db.h): materials 0x50, weapons 0xB4, armour 200, quick items 0x8C.
const char* RecordOf(std::uintptr_t table, std::uint32_t index, int stride) {
    return reinterpret_cast<const RecordTable*>(table)->RecordAt(index, stride);
}

constexpr std::uint32_t kWhite = 0xFFFFFF;
constexpr std::uint32_t kText = 0xA7D7E4;

// What every description shares: the surface, the line rectangle and the formatted line.
class Writer {
public:
    explicit Writer(CItemInfoPanel* panel)
        : panel_(panel), draw_(reinterpret_cast<SurfaceDraw*>(panel->surfaces_.data[0])), rect_{15, 10, 185, 25} {}

    SurfaceDraw* Draw() { return draw_; }
    RECT* Rect() { return &rect_; }
    const mfc::CString& T(int index) const { return panel_->itemTexts_[index]; }
    const mfc::CString& Skill(int index) const { return panel_->skillTexts_[index]; }

    void Down() { gameapi::OffsetRect(&rect_, 0, 15); }
    void Text(const char* text) { draw_->Text(text, &rect_, 0, kText, 0, 1); }
    // A value right-aligned 10 pixels in from the line's right edge (format 2).
    void Value(const char* text) {
        RECT shifted = rect_;
        gameapi::OffsetRect(&shifted, -10, 0);
        draw_->Text(text, &shifted, 0, kText, 2, 1);
    }
    void Title(const char* text) { draw_->Wrapped(text, &rect_, 2, 0, kWhite, 0x11, 1); }
    void Wrapped(const char* text, int maxLines) { draw_->Wrapped(text, &rect_, maxLines, 0, kText, 0x11, 1); }

    // The common head of an item description: the title, a gap, the item's kind.
    void Head(const char* name, const char* kind) {
        Title(name);
        Down();
        Down();
        line.Format(game::Text(0x00792F34), static_cast<const char*>(T(37)), kind);   // "%s %s"
        Text(line);
        Down();
    }
    // "%s %d" with item text `label`, then the next line.
    void Number(int label, int value) {
        line.Format(game::Text(0x00792F14), static_cast<const char*>(T(label)), value);
        Text(line);
        Down();
    }
    // "<label> <text>" (operator+ with " ").
    void Joined(int label, const char* text) {
        line = T(label);
        line += game::Text(0x0078DA40);   // " "
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
        if (Length(lists[2]) != 0) Joined(24, lists[2]);
    }
    // The "school_<n>" text after infoitem_18.
    void School(int school) {
        mfc::CString key;
        key.Format(game::Text(0x00792F50), school);   // "school_%d"
        TextManager()->GetString(key, &line);
        mfc::CString text = line;
        Joined(18, text);
    }

    mfc::CString line;

private:
    CItemInfoPanel* panel_;
    SurfaceDraw* draw_;
    RECT rect_;
};

// The kinds of items (CEngineObject +0x10).
constexpr int kWeapon = 0x3004;
constexpr int kArmor = 0x3005;
constexpr int kQuick = 0x3006;
constexpr int kQuest = 0x3007;
constexpr int kSpellItem = 0x3008;
constexpr int kLoot = 0x3009;

int KindOf(CItem* item) { return Read<int>(item, 0x10); }
std::uint32_t PackedIdOf(CItem* item) { return Read<std::uint32_t>(item, 0x18); }
void* SpellOf(CItem* item) { return Read<void*>(item, 0x44); }

// Whether a 7-entry damage weight array has a weight that compares unequal (and ordered) to 0:
// FCOMP; TEST AH,0x40; JZ. Returns the index, or 7.
unsigned FirstDamageKind(const float* weights) {
    unsigned i = 0;
    for (; i < 7; ++i)
        if (!x87::IsNaN(weights[i]) && !(weights[i] == 0.0f)) break;
    return i;
}

// "%s %s" / "%s %d" lines that pick their format by a value.
void SpellLevelLine(Writer& w, bool known, int level) {
    if (!known)
        w.line.Format(game::Text(0x00792F34), static_cast<const char*>(w.T(8)), static_cast<const char*>(w.T(28)));
    else
        w.line.Format(game::Text(0x00792F14), static_cast<const char*>(w.T(8)), level);
    w.Text(w.line);
    w.Down();
}
// The duration: "<infoitem_12> <infoitem_21>" under 2, else in seconds (FILD of the unsigned value
// times 0.0666667f) with infoitem_27.
void DurationLine(Writer& w, std::uint32_t ticks) {
    if (ticks < 2)
        w.line.Format(game::Text(0x00792F34), static_cast<const char*>(w.T(12)), static_cast<const char*>(w.T(21)));
    else
        w.line.Format(game::Text(0x00792F3C), static_cast<const char*>(w.T(12)),
                      x87::FromUnsigned(ticks) * D(GameFloat(0x00740544)), static_cast<const char*>(w.T(27)));
    w.Text(w.line);
    w.Down();
}

} // namespace

// --- 005EE760 ---------------------------------------------------------------------------------------

void CItemInfoPanel::DescribeItem() {
    CItem* item = Item();
    SurfaceDraw* draw = reinterpret_cast<SurfaceDraw*>(surfaces_.data[0]);
    draw->Begin();
    draw->Clear();
    draw->End();
    if (!item) return;

    int kind = KindOf(item);
    if (kind == kQuest) {
        int sort = Read<int>(item, 0x4C);
        int sub = Read<int>(item, 0x50);
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
    if ((reinterpret_cast<const std::uint8_t*>(interface_)[0x7C] && SpellOf(item)) || kind == kSpellItem) {
        DescribeCarriedSpell();
        return;
    }

    Texts* texts = TextManager();
    // The material's name ("matshort <NAME>"): the record's name with '_' for ' ', lower case.
    mfc::CString material;
    material = *reinterpret_cast<const mfc::CString*>(itemdb::Materials().RecordAt(MaterialIndex(PackedIdOf(item)), itemdb::kMaterialSize));
    ReplaceChar(&material, ' ', '_');
    material.MakeLower();
    mfc::CString name, description, kindText;
    texts->ItemTexts(item, &name, &description, &kindText);

    Writer w(this);
    w.Draw()->Begin();
    mfc::CString text;
    w.Head(name, kindText);

    // The material and count, unless a quick item whose prototype's +0x10 is "none"; then the price.
    bool materialLine;
    if (kind == kQuick) {
        const itemdb::ItemRecordHead* prototype = PrototypeOf(item);
        materialLine = !prototype || MbsICmp(static_cast<const char*>(prototype->materialClass), game::Text(0x007846F0)) != 0;   // "none"
    } else {
        materialLine = kind != kQuest && kind != kLoot;
    }
    if (materialLine) {
        text = game::Text(0x0078DDEC);   // "Unknown"
        mfc::CString key(game::Text(0x00792F28));   // "matshort "
        key += material;
        if (texts->Open(key)) texts->ReadLine(&text, 1);
        w.line.Format(game::Text(0x00792F1C), static_cast<const char*>(w.T(1)), static_cast<const char*>(text),
                      static_cast<int>(PrototypeOf(item)->materialUnits));   // "%s %s x %d"
        w.Text(w.line);
        w.Down();
    }
    if (kind != kLoot) w.Number(0, FtolOf(Read<float>(item, 0x34)));   // price

    if (kind == kArmor || kind == kWeapon || (kind == kQuick && CategoryOf(item) == 5)) {
        // The charge and the carried spell's level.
        void* spell = SpellOf(item);
        int spellValue = spell ? Read<int>(spell, 0x50) : 0;
        w.Number(2, FtolOf(Read<float>(item, 0x2C)));
        w.line.Format(game::Text(0x00792F08), static_cast<const char*>(w.T(14)), spellValue, Read<int>(item, 0x40));   // "%s %d/%d"
        w.Text(w.line);
        w.Down();
    } else if (kind == kQuick) {
        goto tail;
    }

    if (kind == kArmor || kind == kWeapon) {
        // The condition (current only when positive: FCOMP; TEST AH,0x41).
        float current = Read<float>(item, 0x20);
        int condition = current > 0.0f ? FtolOf(current) : 0;
        int maximum = FtolOf(Read<float>(item, 0x24));
        w.line.Format(game::Text(0x00792F08), static_cast<const char*>(w.T(3)), condition, maximum);
        w.Text(w.line);
        w.Down();
        if (kind == kWeapon) {
            // Damage: min-max and the first damage kind with a weight ("damage_<n>"; the text
            // keeps the material line's otherwise).
            float weights[7];
            DamageTypesOf(item, nullptr, weights);
            unsigned first = FirstDamageKind(weights);
            if (first < 7) {
                w.line.Format(game::Text(0x00792EFC), first);   // "damage_%d"
                texts->GetString(w.line, &text);
            }
            float low = Read<float>(item, 0x50);
            float spread = Read<float>(item, 0x54);
            int high = SumFtol(low, spread);
            int lowValue = FtolOf(low);
            w.line.Format(game::Text(0x00792EF0), static_cast<const char*>(w.T(15)), lowValue, high,
                          static_cast<const char*>(text));   // "%s %d-%d %s"
            w.Text(w.line);
            w.Down();
            w.Number(20, static_cast<int>(PrototypeOf(item)->category));
            w.Number(38, PackedFtol(0x006A2D40, PackedIdOf(item)));
            if (WeaponTypeOf(item) == 5 || WeaponTypeOf(item) == 6)
                w.Number(10, FtolOf(Read<float>(item, 0x58)));
            else
                w.Number(39, PackedFtol(0x006A2D90, PackedIdOf(item)));
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
        texts->GetString(game::Text(0x00792EE4), &text);   // "loot_item"
        description += text;
        w.Wrapped(description, 7);
    } else {
        w.Wrapped(description, 4);
        w.Down();
        w.Down();
        w.Down();
        w.Down();
        if (kind != kWeapon) w.Down();
        void* spell;
        if ((kind == kArmor || kind == kWeapon || (kind == kQuick && CategoryOf(item) == 5)) &&
            (spell = SpellOf(item)) != nullptr) {
            // The carried spell's name ("spell <name>").
            text = game::Text(0x0078DDEC);   // "Unknown"
            mfc::CString key(game::Text(0x0078DE58));   // "spell "
            key += SpellPrototypeOf(spell)->code;
            if (texts->Open(key)) texts->ReadLine(&text, 1);
            w.line.Format(game::Text(0x00792F34), static_cast<const char*>(w.T(5)), static_cast<const char*>(text));
            w.Text(w.line);
            w.Down();
            w.Text(w.T(6));
        }
    }
    w.Draw()->End();
}
EI_REPLACE_METHOD(0x005EE760, CItemInfoPanel::DescribeItem);

// --- 005EFD90: a material ------------------------------------------------------------------------

void CItemInfoPanel::DescribeMaterial() {
    CItem* item = Item();
    Texts* texts = TextManager();
    const auto* material = reinterpret_cast<const itemdb::MaterialRecordHead*>(
        itemdb::Materials().RecordAt(Read<std::uint32_t>(item, 0x54), itemdb::kMaterialSize));
    if (!material) return;
    mfc::CString name, description, kindText;
    texts->ItemTexts(item, &name, &description, &kindText);
    Writer w(this);
    w.Draw()->Begin();
    w.Head(name, kindText);
    // The material's type: "mattype_<NAME>".
    mfc::CString text = *reinterpret_cast<const mfc::CString*>(&material->soundName);
    text.MakeLower();
    {
        mfc::CString key(game::Text(0x00792F64));   // "mattype_"
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
    w.line.Format(game::Text(0x00792F5C), static_cast<const char*>(w.T(15)), D(material->damageFactor));   // "%s %.1f"
    w.Text(w.line);
    w.Down();
    float values[7];
    std::memcpy(values, material->absorbFactors, sizeof(values));
    w.DamageLines(values, 4);
    w.Down();
    w.Wrapped(description, 4);
    w.Draw()->End();
}
EI_REPLACE_METHOD(0x005EFD90, CItemInfoPanel::DescribeMaterial);

// --- 005F0370: a blueprint (a weapon, armour or quick item record) -----------------------------------

void CItemInfoPanel::DescribeBlueprint() {
    CItem* item = Item();
    Texts* texts = TextManager();
    // The record by the blueprint's kind (+0x50): weapons, armour, quick items. The dispatcher
    // calls this only for kinds 0-2; the original leaves the record and kind uninitialised
    // otherwise (here: none, and it faults reading the record like a missing one).
    int sort = 3;
    const char* record = nullptr;
    std::uint32_t index = Read<std::uint32_t>(item, 0x54);
    switch (Read<int>(item, 0x50)) {
    case 0: sort = 0; record = RecordOf(0x007C86F8, index, 0xB4); break;
    case 1: sort = 1; record = RecordOf(0x007C8718, index, 200); break;
    case 2: sort = 2; record = RecordOf(0x007C8738, index, 0x8C); break;
    default: break;
    }
    mfc::CString name, description, kindText;
    texts->ItemTexts(item, &name, &description, &kindText);
    Writer w(this);
    w.Draw()->Begin();
    w.Head(name, kindText);
    // The material type ("mattype_<NAME>" of the record's +0x10) and the count.
    const auto* head = reinterpret_cast<const itemdb::ItemRecordHead*>(record);
    const auto* weapon = reinterpret_cast<const itemdb::WeaponRecord*>(record);
    const auto* armor = reinterpret_cast<const itemdb::ArmorRecord*>(record);
    mfc::CString text = head->materialClass;
    text.MakeLower();
    {
        mfc::CString key(game::Text(0x00792F64));   // "mattype_"
        key += text;
        text = key;
    }
    texts->GetString(text, &w.line);
    text = w.line;
    w.line.Format(game::Text(0x00792F1C), static_cast<const char*>(w.T(1)), static_cast<const char*>(text),
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
            w.line.Format(game::Text(0x00792EFC), first);   // "damage_%d"
            texts->GetString(w.line, &text);
        }
        int high = SumFtolScaled(weapon->damage, weapon->damageSpread, 0x0073BAAC);
        int low = ScaledFtol(weapon->damage, 0x0073BAAC);
        w.line.Format(game::Text(0x00792EF0), static_cast<const char*>(w.T(15)), low, high,
                      static_cast<const char*>(text));   // "%s %d-%d %s"
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
        for (int i = 0; i < 7; ++i) values[i] = ArmourValue(&armor->absorbFirst[i], &armor->category, 0x0073BAAC);
        w.DamageLines(values, 4);
    }
    w.Down();
    w.Wrapped(description, 4);
    w.Draw()->End();
}
EI_REPLACE_METHOD(0x005F0370, CItemInfoPanel::DescribeBlueprint);

// --- 005F0B90: a spell record ---------------------------------------------------------------------

void CItemInfoPanel::DescribeSpellRecord() {
    CItem* item = Item();
    Texts* texts = TextManager();
    const char* spell = SpellRecords()->SpellAt(Read<std::uint32_t>(item, 0x54));
    mfc::CString name, description, kindText;
    texts->ItemTexts(item, &name, &description, &kindText);
    Writer w(this);
    w.Draw()->Begin();
    w.Head(name, kindText);
    const itemdb::SpellRecord* record = AsSpell(spell);
    w.Number(7, FtolOf(record->mana));
    w.Number(14, record->skill);
    w.School(record->castType);
    bool known = record->bytes78[3] != 0;
    // The dwords +0x20 power, +0x24, +0x28 radius, +0x2C level (floats), +0x34, +0x38 duration.
    const std::uint32_t* values = record->effectValues;
    SpellLevelLine(w, known, known ? FtolOf(FloatOf(values[4])) : 0);
    float power = FloatOf(values[1]);
    if (!(power >= 1.0f))   // FCOMP; TEST AH,1: below 1 or unordered
        w.line.Format(game::Text(0x00792F34), static_cast<const char*>(w.T(9)), static_cast<const char*>(w.T(21)));
    else
        w.line.Format(game::Text(0x00792F14), static_cast<const char*>(w.T(9)), FtolOf(power));
    w.Text(w.line);
    w.Down();
    w.line.Format(game::Text(0x00792F48), static_cast<const char*>(w.T(10)), FtolOf(FloatOf(values[2])),
                  static_cast<const char*>(w.T(26)));   // "%s %d%s"
    w.Text(w.line);
    w.Down();
    float radius = FloatOf(values[3]);
    if (!(radius >= 1.0f))
        w.line.Format(game::Text(0x00792F34), static_cast<const char*>(w.T(11)), static_cast<const char*>(w.T(22)));
    else
        w.line.Format(game::Text(0x00792F3C), static_cast<const char*>(w.T(11)), D(radius),
                      static_cast<const char*>(w.T(26)));   // "%s %.1f%s"
    w.Text(w.line);
    w.Down();
    DurationLine(w, values[7]);
    w.Number(13, static_cast<int>(values[6]));
    w.Down();
    w.Wrapped(description, 4);
    w.Draw()->End();
}
EI_REPLACE_METHOD(0x005F0B90, CItemInfoPanel::DescribeSpellRecord);

// --- 005F1130: a spell modifier record ------------------------------------------------------------

void CItemInfoPanel::DescribeModifierRecord() {
    CItem* item = Item();
    Texts* texts = TextManager();
    mfc::CString name, description, kindText;
    texts->ItemTexts(item, &name, &description, &kindText);
    const char* modifier = ModifierRecords()->ModifierAt(Read<std::uint32_t>(item, 0x54));
    Writer w(this);
    w.Draw()->Begin();
    w.Head(name, kindText);
    const itemdb::SpellModifierRecord* record = reinterpret_cast<const itemdb::SpellModifierRecord*>(modifier);
    w.Number(7, FtolOf(record->mana));
    w.Number(14, record->skill);
    w.Down();
    w.Wrapped(description, 8);
    w.Draw()->End();
}
EI_REPLACE_METHOD(0x005F1130, CItemInfoPanel::DescribeModifierRecord);

// --- 005EF300: a quick item of category 8 (its spell's level and duration) ------------------------

void CItemInfoPanel::DescribeQuickItem() {
    CItem* item = Item();
    Texts* texts = TextManager();
    mfc::CString name, description, kindText;
    texts->ItemTexts(item, &name, &description, &kindText);
    Writer w(this);
    w.Draw()->Begin();
    w.Head(name, kindText);
    if (void* spell = SpellOf(item)) {
        bool known = SpellPrototypeOf(spell)->bytes78[3] != 0;
        SpellLevelLine(w, known, known ? FtolOf(Read<float>(spell, 0x24)) : 0);
        DurationLine(w, Read<std::uint32_t>(spell, 0x20));
        w.Down();
    }
    w.Wrapped(description, 8);
    w.Draw()->End();
}
EI_REPLACE_METHOD(0x005EF300, CItemInfoPanel::DescribeQuickItem);

// --- 005EF5D0: the spell an item carries (or a spell container) -----------------------------------

void CItemInfoPanel::DescribeCarriedSpell() {
    CItem* item = Item();
    void* spell = item ? SpellOf(item) : nullptr;
    if (!spell) {
        reinterpret_cast<SurfaceDraw*>(surfaces_.data[0])->End();   // without a Begin, as the original
        return;
    }
    Texts* texts = TextManager();
    mfc::CString title, description, kindText;
    {
        mfc::CString key(game::Text(0x0078DE58));   // "spell "
        key += SpellPrototypeOf(spell)->code;
        if (texts->Open(key)) {
            texts->ReadLine(&title, 1);
            reinterpret_cast<CTextManager*>(texts)->ReadToHash(&description, 1);
        }
    }
    texts->GetString(game::Text(0x0078DE4C), &kindText);   // "item_spell"
    Writer w(this);
    w.Draw()->Begin();
    mfc::CString text;
    w.Head(title, kindText);
    w.Number(7, FtolOf(Read<float>(spell, 0x40)));
    w.Number(14, Read<int>(spell, 0x50));
    w.School(SpellPrototypeOf(spell)->castType);
    bool known = SpellPrototypeOf(spell)->bytes78[3] != 0;
    SpellLevelLine(w, known, known ? FtolOf(Read<float>(spell, 0x24)) : 0);
    float power = Read<float>(spell, 0x30);
    if (!(power >= 1.0f))
        w.line.Format(game::Text(0x00792F34), static_cast<const char*>(w.T(9)), static_cast<const char*>(w.T(21)));
    else
        w.line.Format(game::Text(0x00792F14), static_cast<const char*>(w.T(9)), FtolOf(power));
    w.Text(w.line);
    w.Down();
    w.line.Format(game::Text(0x00792F48), static_cast<const char*>(w.T(10)), FtolOf(Read<float>(spell, 0x34)),
                  static_cast<const char*>(w.T(26)));   // "%s %d%s"
    w.Text(w.line);
    w.Down();
    if (Read<std::uint32_t>(spell, 0x3C) & 0x10000000) {
        w.line.Format(game::Text(0x00792F34), static_cast<const char*>(w.T(11)), static_cast<const char*>(w.T(22)));
    } else {
        // The area: pi * r * r in that order (FLD r; FLD pi; FMUL ST1; FMUL ST1).
        double radius = D(Read<float>(spell, 0x28));
        double area = D(GameFloat(0x007402C0)) * radius * radius;
        w.line.Format(game::Text(0x00792F3C), static_cast<const char*>(w.T(11)), area, static_cast<const char*>(w.T(26)));
    }
    w.Text(w.line);
    w.Down();
    DurationLine(w, Read<std::uint32_t>(spell, 0x20));
    w.Number(13, Read<int>(spell, 0x38));
    w.Down();
    w.Wrapped(description, 4);
    w.Down();
    w.Down();
    w.Down();
    w.Down();
    if (ModifierCountOf(spell) > 0) {
        // "<infoitem_19> name, name, ...": each modifier's third text line ("modifier <code>"),
        // "?" without one.
        w.line = w.T(19);
        for (int i = 0; i < ModifierCountOf(spell); ++i) {
            w.line += game::Text(0x0078DA40);   // " "
            text = game::Text(0x00783D6C);      // "?"
            std::uint32_t index = static_cast<std::uint32_t>(Read<signed char>(spell, 0x54 + i) - 1);
            const char* modifier = itemdb::SpellModifiers().RecordAt(index, itemdb::kSpellModifierSize);
            mfc::CString key(game::Text(0x0078DD1C));   // "modifier "
            key += reinterpret_cast<const char*>(reinterpret_cast<std::uintptr_t>(modifier) + 4);
            if (texts->Open(key)) {
                texts->NextLine();
                texts->NextLine();
                texts->ReadLine(&text, 1);
            }
            mfc::CString entry = text;
            entry += game::Text(0x00787B5C);   // ","
            w.line += entry;
        }
        w.line.TrimRight(game::Text(0x00787B5C));
        w.line += game::Text(0x00783D78);   // "."
        w.Draw()->Wrapped(w.line, w.Rect(), 3, 0, kText, 0x10, 1);
    }
    w.Draw()->End();
}
EI_REPLACE_METHOD(0x005EF5D0, CItemInfoPanel::DescribeCarriedSpell);

// --- 005F1590: the statistic under the mouse ----------------------------------------------------------

namespace {

// Kind 0, an attribute: two of its derived values (UnitStats methods), percentages where scaled.
void AttributeLines(Writer& w, const std::uint8_t* stats, int index) {
    auto line = [&](std::uintptr_t format, int label, int value) {
        w.line.Format(game::Text(format), static_cast<const char*>(w.Skill(label)), value);
        w.Text(w.line);
        w.Down();
    };
    switch (index) {
    case 0:
        line(0x00792FB8, 0, StatFtolScaled(0x00523CB0, stats, 0x0073BAB0));   // "%s %d%%", * 100
        line(0x00792F14, 1, StatFtol(0x00524280, stats));                      // "%s %d"
        w.Down();
        break;
    case 1:
        line(0x00792FB8, 2, StatFtolScaled(0x00523CC0, stats, 0x0073BAB0));
        line(0x00792F14, 3, StatFtol(0x00524320, stats));
        w.Down();
        break;
    case 2:
        // 1.04 ^ (intelligence - 25) * 100.
        line(0x00792FB8, 4, PowFtol(0x0073E128, Read<float>(stats, 0x20), 0x0073E124, 0x0073BAB0));
        line(0x00792FB8, 24, StatFtolScaled(0x00523CD0, stats, 0x0073BAB0));
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
    name.TrimRight(game::Text(0x007856B8));   // "0"
    name += game::Text(0x00787C00);                 // "1"
    int group = SkillIndexOfOriginal(Skills(), nullptr, name) / 3;
    const RecordTable& levels = *reinterpret_cast<const RecordTable*>(0x007AF274);
    for (int i = 0; i < 3; ++i) {
        std::uint32_t index = static_cast<std::uint32_t>(group * 3 + i);
        std::uint32_t count = static_cast<std::uint32_t>(static_cast<int>(levels.end - levels.begin) / 0x44);
        if (index >= count) continue;
        const char* level = levels.begin + group * 0xCC + i * 0x44;
        if (!level) continue;
        mfc::CString key(game::Text(0x00792FC8));   // "perk "
        key += *reinterpret_cast<const mfc::CString*>(level + 4);
        if (texts->Open(key)) texts->ReadLine(&title, 1);
        w.Text(title);
        if (info->index > i) w.Draw()->Text(w.Skill(21), w.Rect(), 0, kText, 2, 1);   // learned
        w.Down();
        // The level's effect by the perk group.
        int value = Read<int>(level, 0x34);
        const char* percent = game::Text(0x00792F9C);   // "     %s %d%%"
        const char* plain = game::Text(0x00792FAC);     // "     %s %d"
        bool drawn = true;
        switch (group) {
        case 0: case 1: case 2: case 3: case 4: case 5: case 6:
            w.line.Format(plain, static_cast<const char*>(w.Skill(10)), value);
            break;
        case 7: case 8: case 9: case 10: case 11: case 12: case 13: case 14:
            w.line.Format(plain, static_cast<const char*>(w.Skill(11)), value);
            break;
        case 15:
            w.line.Format(percent, static_cast<const char*>(w.Skill(12)), static_cast<std::uint32_t>(value + 100) >> 1);
            break;
        case 16: w.line.Format(percent, static_cast<const char*>(w.Skill(14)), value + 100); break;
        case 17: w.line.Format(percent, static_cast<const char*>(w.Skill(15)), value + 100); break;
        case 18: w.line.Format(percent, static_cast<const char*>(w.Skill(16)), value + 100); break;
        case 19: w.line.Format(percent, static_cast<const char*>(w.Skill(17)), value + 100); break;
        case 20: w.line.Format(percent, static_cast<const char*>(w.Skill(3)), value + 100); break;
        case 21: w.line.Format(percent, static_cast<const char*>(w.Skill(1)), value + 100); break;
        case 22: w.line.Format(percent, static_cast<const char*>(w.Skill(22)), value + 300); break;
        case 23: case 24: case 25:
            w.line.Format(plain, static_cast<const char*>(w.Skill(23)), value);
            break;
        default:
            drawn = false;
            break;
        }
        if (drawn) w.Text(w.line);
        w.Down();
        // The cost (00528050; negative: 0x7FFFFFFF), on a line 15 pixels wider.
        w.Rect()->right += 15;
        using CostFn = int(__thiscall*)(const std::uint8_t*, const char*);
        int cost = reinterpret_cast<CostFn>(0x00528050)(info->stats, level);
        if (cost < 0) cost = 0x7FFFFFFF;
        w.line.Format(plain, static_cast<const char*>(w.Skill(18)), cost);
        w.Text(w.line);
        w.Rect()->right -= 15;
        w.Down();
    }
}

// Kind 5, the weapon's attack or defence: its four parts and their sum (or, for the defence of a
// weapon of type 5 or 6, infoitem_34).
void WeaponLines(Writer& w, const ItemHoverInfo* info) {
    CItem* weapon = info->weapon;
    const std::uint8_t* stats = info->stats;
    int type = WeaponTypeOf(weapon);
    if (MbsCmp(info->text, game::Text(0x00792F88)) == 0 && (type == 5 || type == 6)) {   // "defense"
        w.Wrapped(w.T(34), 2);
        w.Down();
        return;
    }
    auto part = [&](int label, int value) {
        w.line.Format(game::Text(0x00792F80), static_cast<const char*>(w.T(label)));   // "     %s"
        w.Text(w.line);
        w.line.Format(game::Text(0x0078EEEC), value);   // "%d"
        w.Value(w.line);
    };
    int base = DifferenceFtol(Read<float>(stats, 0x18), 0x0073E124);   // dexterity - 25
    part(29, base);
    int sum = base;
    w.Down();
    int skill = StatFtolArg(0x00526780, stats, type);
    part(30, skill);
    sum += skill;
    w.Down();
    int level = StatFtolArg(0x005267E0, stats, type);
    part(31, level);
    sum += level;
    w.Down();
    w.line.Format(game::Text(0x00792F80), static_cast<const char*>(w.T(32)));
    w.Text(w.line);
    int own = StatFtolArg(0x00526860, stats, static_cast<int>(PackedIdOf(weapon)));
    if (MbsCmp(info->text, game::Text(0x00792F88)) == 0)
        own += PackedFtol(0x006A2D90, PackedIdOf(weapon));
    else
        own += PackedFtol(0x006A2D40, PackedIdOf(weapon));
    w.line.Format(game::Text(0x0078EEEC), own);
    w.Value(w.line);
    sum += own;
    w.Down();
    w.Down();
    part(33, sum);
}

} // namespace

void CItemInfoPanel::DescribeStats(ItemHoverInfo* info) {
    PartyResetOriginal(itemList_, nullptr, 1, 0);
    DescribeItem();
    if (info->kind < 0) return;
    Texts* texts = TextManager();
    Writer w(this);
    w.Draw()->Begin();
    mfc::CString text, title, description;
    if (info->kind <= 3) {
        mfc::CString key(game::Text(0x00792FC8));   // "perk "
        key += info->text;
        if (texts->Open(key)) {
            texts->ReadLine(&title, 1);
            texts->ReadLine(&description, 1);
        }
    } else if (info->kind <= 7) {
        texts->GetString(info->text, &title);
        mfc::CString key = info->text;
        key += game::Text(0x00792FC0);   // "_desc"
        texts->GetString(key, &description);
    }
    w.Title(title);
    w.Down();
    w.Down();
    const std::uint8_t* stats = info->stats;
    switch (info->kind) {
    case 0:
        AttributeLines(w, stats, info->index);
        w.Wrapped(description, 13);
        break;
    case 1: {
        // A skill: its level and, for the first three, a derived value.
        int index = info->index;
        w.line.Format(game::Text(0x00792F14), static_cast<const char*>(w.Skill(5)), StatFtolArg(0x00527030, stats, index));
        w.Text(w.line);
        w.Down();
        if (index >= 0) {
            bool drawn = true;
            if (index <= 1)
                w.line.Format(game::Text(0x00792F14), static_cast<const char*>(w.Skill(6)),
                              StatFtolArg(0x00527050, stats, index));
            else if (index == 2)
                w.line.Format(game::Text(0x00792FB8), static_cast<const char*>(w.Skill(7)),
                              StatFtolScaled(0x00525CD0, stats, 0x0073BAB0));
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
        w.line.Format(game::Text(0x00792F14), static_cast<const char*>(w.Skill(5)),
                      StatFtolArg(0x00525DF0, stats, info->index));
        w.Text(w.line);
        w.Down();
        if (info->index == 0) {
            w.line.Format(game::Text(0x00792F14), static_cast<const char*>(w.Skill(9)), StatFtolArg(0x00525E10, stats, 0));
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
        const UnitStatSlot& slot = reinterpret_cast<const UnitStats*>(stats)->slots[info->part];
        for (int i = 0; i < 7; ++i) sums[i] = LayerSum(&slot.layers[0].absorb[i]);
        for (int i = 0; i < 6; ++i) {
            w.line.Format(game::Text(0x00792EFC), i);   // "damage_%d"
            texts->GetString(w.line, &text);
            w.line.Format(game::Text(0x00792F90), static_cast<const char*>(text));   // "     %s:"
            w.Text(w.line);
            w.line.Format(game::Text(0x00792F78), D(sums[i]));   // "%.1f"
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
            w.line.Format(game::Text(0x00792F80), static_cast<const char*>(w.T(label++)));   // "     %s"
            w.Text(w.line);
            w.line.Format(game::Text(0x0078EEEC), StatFtolArg(0x00527EB0, stats, school));   // "%d"
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
    w.Draw()->End();
}
EI_REPLACE_METHOD(0x005F1590, CItemInfoPanel::DescribeStats);

// --- 005F2910: a camp help topic ----------------------------------------------------------------------

void CItemInfoPanel::ShowHelp(int topic) {
    PartyResetOriginal(itemList_, nullptr, 1, 0);
    DescribeItem();
    Texts* texts = TextManager();
    Writer w(this);
    w.Draw()->Begin();
    mfc::CString title, description, key;
    key.Format(game::Text(0x00792FD0), topic);   // "camphelp %d"
    if (texts->Open(key)) {
        texts->ReadLine(&title, 1);
        texts->ReadLine(&description, 1);
    }
    w.Title(title);
    w.Down();
    w.Down();
    w.Wrapped(description, 16);
    w.Draw()->End();
}
EI_REPLACE_METHOD(0x005F2910, CItemInfoPanel::ShowHelp);

// --- 005EE6C0: describe an item ---------------------------------------------------------------------

void CItemInfoPanel::Describe(CItem* item) {
    // Nothing to do when the described item is the same (its virtual slot 14, IsSameAs).
    if (itemCount_ && items_[0] && item) {
        using SameFn = BOOL(__thiscall*)(CItem*, CItem*);
        CItem* shown = items_[0];
        if (reinterpret_cast<SameFn>((*reinterpret_cast<void***>(shown))[14])(shown, item)) return;
    }
    PartyResetOriginal(itemList_, nullptr, 1, 0);
    if (item) PartyAddItemOriginal(itemList_, nullptr, item, 0, 1);
    DescribeItem();
}
EI_REPLACE_METHOD(0x005EE6C0, CItemInfoPanel::Describe);

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
void __fastcall InfoPanelKeyDown(void* input, void*, int key, int repeat, int flags) {
    PanelOf(input)->OnKeyDown(key, repeat, flags);
}
void __fastcall InfoPanelKeyUp(void* input, void*, int key, int repeat, int flags) {
    PanelOf(input)->OnKeyUp(key, repeat, flags);
}
} // namespace
EI_REPLACE(0x005EE6B0, InfoPanelRefresh);
EI_REPLACE(0x005EE640, InfoPanelKeyDown);
EI_REPLACE(0x005EE680, InfoPanelKeyUp);

// --- 005EE440: Build ------------------------------------------------------------------------------

void CItemInfoPanel::Build(int x, int y) {
    x_ = x;
    y_ = y;
    void* memory = gamecrt::operator_new(sizeof(CUiSurface));
    CUiSurface* surface = nullptr;
    if (memory) surface = static_cast<CUiSurface*>(memory)->Construct(interface_, x_, y_, x_ + 200, y_ + 300, 5, 1);
    AddSurface(surface);
    Texts* texts = TextManager();
    mfc::CString text, key;
    mfc::CStringArray* itemTexts = &itemTexts_;
    for (int i = 0; i < 45; ++i) {
        key.Format(game::Text(0x00792ED8), i);   // "infoitem_%d"
        texts->GetString(key, &text);
        itemTexts->SetAtGrow(itemTexts->GetSize(), text);
    }
    mfc::CStringArray* skillTexts = &skillTexts_;
    for (int i = 0; i < 25; ++i) {
        key.Format(game::Text(0x00792EC8), i);   // "infoskill_%d"
        texts->GetString(key, &text);
        skillTexts->SetAtGrow(skillTexts->GetSize(), text);
    }
    void* list = itemList_;
    using EmptyFn = void(__thiscall*)(void*);
    reinterpret_cast<EmptyFn>((*static_cast<void***>(list))[2])(list);
}
EI_REPLACE_METHOD(0x005EE440, CItemInfoPanel::Build);
