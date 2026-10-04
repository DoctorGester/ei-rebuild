// The camp's page switch (0060E9E0) and the camp panels' layout code (see docs/notes/ui.md, "Camp,
// global map and battle screen setup").
#include "ui_camp.h"

#include "item_db_text.h"
#include "world.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/mfc.h"

#include <cstdint>
#include <cstring>
#include <initializer_list>

#pragma float_control(precise, on)

namespace {

const char* GameText(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }

// --- Not rewritten -----------------------------------------------------------------------------

// Panel methods called by the page switch (thiscall on the panel).
struct CampPanel {
    void ShowHelp(int topic);                    // 005F2910 (help panel: "camphelp%d")
    void ShowStats(int page);                    // 006233C0 (stats panel, ui_hero_panels.cpp)
    void SelectTrade(int index);                 // 00625860 (trade panel)
    int SettleTrade(char a, char all);           // 006255A0 (trade panel)
    void AddItem(void* item);                    // 00621570 (an item strip)
};
__declspec(naked) void CampPanel::ShowHelp(int) EI_ORIGINAL(0x005F2910)
__declspec(naked) void CampPanel::ShowStats(int) EI_ORIGINAL(0x006233C0)
__declspec(naked) void CampPanel::SelectTrade(int) EI_ORIGINAL(0x00625860)
__declspec(naked) int CampPanel::SettleTrade(char, char) EI_ORIGINAL(0x006255A0)
__declspec(naked) void CampPanel::AddItem(void*) EI_ORIGINAL(0x00621570)
CampPanel* Panel(void* window) { return static_cast<CampPanel*>(window); }

// The camp's item helpers (thiscall on the camp; 005EE050.. in ui_screens.cpp are their siblings).
struct CampItems {
    // 005EE2C0: moves every item of `list` back to `owner`.
    void ReturnAll(void* owner, void* list);
    // 005EE1B0: moves `item` from `from` to `to`; returns the item now held.
    void* Move(void* item, void* to, void* from, int a, int b, int c);
};
__declspec(naked) void CampItems::ReturnAll(void*, void*) EI_ORIGINAL(0x005EE2C0)
__declspec(naked) void* CampItems::Move(void*, void*, void*, int, int, int) EI_ORIGINAL(0x005EE1B0)

// 00578B20 (on 007AFEA5): the client leaves the interface screen for the world.
struct LocalClient {
    void BackToWorld();
};
__declspec(naked) void LocalClient::BackToWorld() EI_ORIGINAL(0x00578B20)

// Surface text (005FDE20 is Rebirth-patched).
struct SurfaceDraw {
    void Begin();                                                                   // 005FDC00
    void Clear();                                                                   // 005FE2F0
    void Text(const char* text, RECT* rect, int a, std::uint32_t colour, int format, int b);   // 005FDE20
    void End();                                                                     // 005FDC60
};
__declspec(naked) void SurfaceDraw::Begin() EI_ORIGINAL(0x005FDC00)
__declspec(naked) void SurfaceDraw::Clear() EI_ORIGINAL(0x005FE2F0)
__declspec(naked) void SurfaceDraw::Text(const char*, RECT*, int, std::uint32_t, int, int) EI_ORIGINAL(0x005FDE20)
__declspec(naked) void SurfaceDraw::End() EI_ORIGINAL(0x005FDC60)

// An item list of the camp (0x70 bytes, 00661050): the first node's item at **(+0x40), the
// count at +0x44.
struct ItemList {
    std::uint8_t unknown_00[0x40];
    void** first;                    // +0x40
    int count;                       // +0x44
};

// Takes the made items out of `results` (re-read every time, as the original does): an item whose
// record (+0x1C) has its +0x10 flag goes to `owner` and into `strip`, the others to the local
// player and into the item strip.
void TakeResults(CCampPages* camp, void* const& results, void* owner, CUiItemStrip* strip) {
    CampItems* items = reinterpret_cast<CampItems*>(camp);
    for (ItemList* list = static_cast<ItemList*>(results); list->count != 0; list = static_cast<ItemList*>(results)) {
        std::uint8_t* item = static_cast<std::uint8_t*>(*list->first);
        std::uint8_t* record = *reinterpret_cast<std::uint8_t**>(item + 0x1C);
        if (record && record[0x10]) {
            Panel(strip)->AddItem(items->Move(item, owner, list, 0, 1, 0));
        } else {
            void* player = LocalPlayerData();
            Panel(camp->itemStrip_)->AddItem(items->Move(item, player, list, 0, 1, 0));
        }
    }
}

// Scene object fields: +0x220 shown, +0x224..+0x22C colour.
void SetShown(CEngineObject* object, std::uint32_t shown) {
    std::memcpy(reinterpret_cast<std::uint8_t*>(object) + 0x220, &shown, 4);
}
void SetShade(CEngineObject* object, std::uint32_t bits) {
    for (int offset : {0x224, 0x228, 0x22C}) std::memcpy(reinterpret_cast<std::uint8_t*>(object) + offset, &bits, 4);
}

} // namespace

// --- 0060E9E0 ----------------------------------------------------------------------------------

void CCampPages::ShowPage(int page) {
    equipment_->SetEnabled(0, 1);
    equipment2_->SetEnabled(0, 1);
    trade_->SetEnabled(0, 1);
    statsPanel_->SetEnabled(0, 1);
    helpPanel_->SetEnabled(0, 1);
    if (spellConstructor_) {
        spellConstructor1_->SetEnabled(0, 1);
        spellConstructor2_->SetEnabled(0, 1);
        reinterpret_cast<CUiWindow*>(spellStrip_)->SetEnabled(0, 1);
    }
    if (itemConstructor_) {
        itemConstructor1_->SetEnabled(0, 1);
        itemConstructor3_->SetEnabled(0, 1);
        itemConstructor2_->SetEnabled(0, 1);
        reinterpret_cast<CUiWindow*>(partStrip_)->SetEnabled(0, 1);
    }
    itemStrip_->slots_ = 9;   // 00621F20

    bool offered = true;
    switch (page) {
    case 0:
        statsPanel_->SetEnabled(1, 1);
        equipment_->SetEnabled(1, 1);
        break;
    case 1:
        statsPanel_->SetEnabled(1, 1);
        equipment2_->SetEnabled(1, 1);
        trade_->SetEnabled(1, 1);
        break;
    case 2:
        if (!spellConstructor_) { offered = false; break; }
        helpPanel_->SetEnabled(1, 1);
        Panel(helpPanel_)->ShowHelp(0x65);
        spellConstructor1_->SetEnabled(1, 1);
        reinterpret_cast<CUiWindow*>(spellStrip_)->SetEnabled(1, 1);
        itemStrip_->slots_ = 1;
        break;
    case 3:
        if (!spellConstructor_ || noConstructor_) { offered = false; break; }
        helpPanel_->SetEnabled(1, 1);
        spellConstructor2_->SetEnabled(1, 1);
        reinterpret_cast<CUiWindow*>(spellStrip_)->SetEnabled(1, 1);
        break;
    case 4:
        if (!itemConstructor_) { offered = false; break; }
        helpPanel_->SetEnabled(1, 1);
        Panel(helpPanel_)->ShowHelp(0x64);
        itemConstructor1_->SetEnabled(1, 1);
        reinterpret_cast<CUiWindow*>(partStrip_)->SetEnabled(1, 1);
        itemStrip_->slots_ = 5;
        break;
    case 5:
        if (!itemConstructor_ || noConstructor_) { offered = false; break; }
        helpPanel_->SetEnabled(1, 1);
        itemConstructor2_->SetEnabled(1, 1);
        reinterpret_cast<CUiWindow*>(partStrip_)->SetEnabled(1, 1);
        break;
    case 6:
        if (!itemConstructor_) { offered = false; break; }
        helpPanel_->SetEnabled(1, 1);
        Panel(helpPanel_)->ShowHelp(0x68);
        itemConstructor3_->SetEnabled(1, 1);
        reinterpret_cast<CUiWindow*>(partStrip_)->SetEnabled(1, 1);
        break;
    case 7: {
        // Leave: the trade is settled, the constructors' unused parts go back to their owners and
        // what they made goes into the strips.
        Panel(trade_)->SettleTrade(0, 1);
        CampItems* items = reinterpret_cast<CampItems*>(this);
        items->ReturnAll(spellParts_[0], spellParts_[1]);
        items->ReturnAll(LocalPlayerData(), spellParts_[2]);
        items->ReturnAll(itemParts_[0], itemParts_[1]);
        items->ReturnAll(LocalPlayerData(), itemParts_[2]);
        items->ReturnAll(LocalPlayerData(), itemExtra_);
        TakeResults(this, spellResults_, spellParts_[0], spellStrip_);
        TakeResults(this, itemResults_, itemParts_[0], partStrip_);
        interface_->PlaySound(GameText(0x0079372C), 0);   // buttons\globalmap\transit.wav
        if (fromHeroScreen_) {
            interface_->SetPendingValue(0);
            return;
        }
        reinterpret_cast<LocalClient*>(0x007AFEA5)->BackToWorld();
        return;
    }
    default:
        break;
    }

    if (!offered) {
        ShowPage(page_);   // the page is not offered: show the current one again
    } else {
        if (page_ != page) interface_->PlaySound(GameText(0x0079374C), 0);   // buttons\camp\slot.wav
        page_ = page;
        Panel(statsPanel_)->ShowStats(page);
        Panel(trade_)->SelectTrade(hero_);
        SurfaceDraw* title = reinterpret_cast<SurfaceDraw*>(surfaces_.data_[1]);
        title->Begin();
        title->Clear();
        RECT rect = {0, 10, 200, 30};
        title->Text(pageTitles_[page_], &rect, 1, 0xFFFFFF, 1, 1);
        title->End();
    }

    // The hero arrows (scene objects 0/1, regions 0/1) only where a hero can be chosen.
    if (page_ == 0 && (!itemConstructor_ || noConstructor_)) {
        SetShown(sceneObjects_.data_[0], 0);
        SetShown(sceneObjects_.data_[1], 0);
        regions_.data_[0]->SetTip(0, 1, 0);
        regions_.data_[1]->SetTip(0, 1, 0);
    } else {
        SetShown(sceneObjects_.data_[0], 1);
        SetShown(sceneObjects_.data_[1], 1);
        regions_.data_[0]->SetTip(0x4EE8, 1, 0);
        regions_.data_[1]->SetTip(0x4EE9, 1, 0);
    }
    // The page buttons (scene objects 2-9) half bright, the current one full.
    for (int i = 2; i < 10; ++i) SetShade(sceneObjects_.data_[i], 0x3F000000);
    SetShade(sceneObjects_.data_[page_ + 2], 0x3F800000);
    interface_->ShowHelp(0);
}
EI_REPLACE_METHOD(0x0060E9E0, CCampPages::ShowPage);

// --- 006100E0: CanEquip ----------------------------------------------------------------------------

namespace {

// The camp's item predicates (thiscall on the camp, which they do not read; AL).
struct CampKinds {
    bool IsSpellContainer(const std::uint8_t* item);   // 005EE0B0
    bool IsSpell(const std::uint8_t* item);            // 005EE050
    bool IsModifier(const std::uint8_t* item);         // 005EE080
    bool IsPart(const std::uint8_t* item);             // 005EE0D0
    bool IsKindOne(const std::uint8_t* item);          // 005EE110
    bool IsBlueprint(const std::uint8_t* item);        // 005EE130
};
__declspec(naked) bool CampKinds::IsSpellContainer(const std::uint8_t*) EI_ORIGINAL(0x005EE0B0)
__declspec(naked) bool CampKinds::IsSpell(const std::uint8_t*) EI_ORIGINAL(0x005EE050)
__declspec(naked) bool CampKinds::IsModifier(const std::uint8_t*) EI_ORIGINAL(0x005EE080)
__declspec(naked) bool CampKinds::IsPart(const std::uint8_t*) EI_ORIGINAL(0x005EE0D0)
__declspec(naked) bool CampKinds::IsKindOne(const std::uint8_t*) EI_ORIGINAL(0x005EE110)
__declspec(naked) bool CampKinds::IsBlueprint(const std::uint8_t*) EI_ORIGINAL(0x005EE130)

// 0068B7C0 (thiscall on the spell): the spell has a cast-flag modifier.
__declspec(naked) bool __fastcall SpellHasCastFlag(const std::uint8_t*) EI_ORIGINAL(0x0068B7C0)

// The item databases' record tables ({begin, end}): the record `index`, or null past the end
// (the original then reads through null).
const std::uint8_t* RecordAt(std::uintptr_t table, std::uint32_t index, int stride) {
    const std::uint8_t* begin = *reinterpret_cast<std::uint8_t* const*>(table);
    const std::uint8_t* end = *reinterpret_cast<std::uint8_t* const*>(table + 4);
    std::uint32_t count = static_cast<std::uint32_t>(static_cast<int>(end - begin) / stride);
    return index < count ? begin + index * stride : nullptr;
}
std::uint32_t Index(const std::uint8_t* item) { return *reinterpret_cast<const std::uint32_t*>(item + 0x54); }
const std::uint8_t* SpellRecord(const std::uint8_t* item) { return RecordAt(0x007C8510, Index(item), 0x84); }
const std::uint8_t* ModifierRecord(const std::uint8_t* item) { return RecordAt(0x007C8530, Index(item), 0x24); }
const std::uint8_t* BlueprintRecord(const std::uint8_t* item) { return RecordAt(0x007C86D8, Index(item), 0x50); }

template <typename T>
T Read(const std::uint8_t* p, int offset) {
    T v;
    std::memcpy(&v, p + offset, sizeof(T));
    return v;
}

int ItemType(const std::uint8_t* item) { return Read<int>(item, 0x10); }

// An item list's entry `i` (00661050 lists: data at +0x40, count at +0x44).
std::uint8_t* Entry(void* list, int i) { return static_cast<std::uint8_t*>(static_cast<ItemList*>(list)->first[i]); }
int CountOf(void* list) { return static_cast<ItemList*>(list)->count; }

double D(float f) { return f; }
float Store(double v) {
    volatile float r = static_cast<float>(v);
    return r;
}
__declspec(naked) int __cdecl GameFtol(double) {
    __asm {
        fld qword ptr [esp + 4]
        mov eax, 0x006E853C
        call eax
        ret
    }
}

} // namespace

// 0060FAB0: prices (slot 54); CanEquip follows.

// The products and differences keep the original's order (no reassociation under /fp:fast).
#pragma float_control(precise, on, push)

namespace {
constexpr float kPriceZero = 0.0f;   // 0073B858
constexpr float kPriceOne = 1.0f;    // 0073B84C

// Slot 16 of an item: its base price, left unrounded in ST0.
double ItemPrice(const std::uint8_t* item) {
    using PriceFn = double(__thiscall*)(const std::uint8_t*);
    void** vtable;
    std::memcpy(&vtable, item, sizeof(vtable));
    return reinterpret_cast<PriceFn>(vtable[16])(item);
}

float Field(const void* object, int offset) {
    float v;
    std::memcpy(&v, static_cast<const char*>(object) + offset, sizeof(v));
    return v;
}

// How worn the item is: 1 - durability (+0x20) / maximum (+0x24). Rebirth's FixRepairPrice treats a
// negative (or NaN) durability or maximum as 0 and clamps the result to [0, 1] (NaN stays).
float WornFraction(const std::uint8_t* item) {
    float durability = Field(item, 0x20), maximum = Field(item, 0x24);
    if (!ei::rebirth::Enabled(ei::rebirth::Patch::FixRepairPrice))
        return static_cast<float>(static_cast<double>(kPriceOne) - static_cast<double>(durability) / maximum);
    float a = kPriceZero < durability ? durability : kPriceZero;
    float b = kPriceZero < maximum ? maximum : kPriceZero;
    double worn = static_cast<double>(kPriceOne) - static_cast<double>(a) / b;
    if (worn > kPriceOne) return kPriceOne;
    if (worn < kPriceZero) return kPriceZero;
    return static_cast<float>(worn);
}
} // namespace

int CCampPages::Price(std::uint8_t* item, int kind) {
    const float* multipliers = reinterpret_cast<const float*>(reinterpret_cast<const char*>(this) + 0x21C);
    float repair = kPriceZero;
    int type = ItemType(item);
    if (type == 0x3004 || type == 0x3005 || kind == 8) {
        float worn = WornFraction(item);
        repair = static_cast<float>(ItemPrice(item) * multipliers[8] * worn);
        if (kind == 8) {
            if (!(worn < kPriceZero || worn > kPriceZero)) return 0;                // FCOMP; TEST AH,0x40
            if (!(multipliers[8] < kPriceZero || multipliers[8] > kPriceZero)) return 0;
            int price = GameFtol(D(repair));
            return price > 0 ? price : 1;
        }
    }
    if (type == 0x3004 || type == 0x3005) {
        // Rebirth's FixRepairPrice also JMPs over this FMUL (0060FB87, written without an addon.log
        // line): for kind 7 (taking an item apart in the item constructor) the repair cost of the
        // wear is added instead of subtracted.
        if (kind == 7 && ei::rebirth::Enabled(ei::rebirth::Patch::FixRepairPrice))
            return GameFtol(ItemPrice(item) * multipliers[kind] + repair);
        return GameFtol(ItemPrice(item) * multipliers[kind] - repair);
    }
    return GameFtol(ItemPrice(item) * multipliers[kind]);
}

#pragma float_control(pop)

namespace {
int __fastcall CampPrice(CCampPages* self, void*, std::uint8_t* item, int kind) { return self->Price(item, kind); }
} // namespace
EI_REPLACE(0x0060FAB0, CampPrice);
EI_REBIRTH_IMPLEMENTS(0x0060FAB0, ei::rebirth::Patch::FixRepairPrice);

bool CCampPages::CanEquip(std::uint8_t* item, int mode) {
    CampKinds* kinds = reinterpret_cast<CampKinds*>(this);
    // Slot 45: the party's best skill in a school and most mana units (00610020).
    using BestFn = void(__thiscall*)(CCampPages*, int, int*, int*);
    BestFn best = reinterpret_cast<BestFn>(vtable_[45]);
    switch (page_) {
    case 0: {   // equipment: weapons, armour, spell containers (worn-out ones not to a hero)
        int type = ItemType(item);
        if ((type == 0x3004 || type == 0x3005) && x87::fistp(Read<float>(item, 0x20)) <= 0 && mode == 1) return false;
        return type == 0x3004 || type == 0x3005 || type == 0x3006;
    }
    case 1: {   // trade: a spell container only for a hero who can cast its spell
        if (mode != 1) return true;
        if (!kinds->IsSpellContainer(item)) return false;
        char* spell = Read<char*>(item, 0x44);
        using HeroFn = char*(__thiscall*)(CCampPages*);
        char* hero = reinterpret_cast<HeroFn>(vtable_[51])(this);   // +0xCC
        return HeroCanUseSpell(hero + 0x20, spell);
    }
    case 2:     // the spell constructor
        return kinds->IsSpellContainer(item) || kinds->IsSpell(item) || kinds->IsModifier(item);
    case 3: {   // the spell being built: a container, one spell, up to eight modifiers
        if (mode != 1 && mode != 2) return mode == 6;
        if (kinds->IsSpellContainer(item)) return CountOf(spellResults_) <= 0;
        if (kinds->IsSpell(item)) {
            if (CountOf(spellResults_) > 0) return false;
            const std::uint8_t* record = SpellRecord(item);
            int skill = 0, mana = 0;
            best(this, Read<int>(record, 0x14), &skill, &mana);
            if (mana < GameFtol(D(Read<float>(record, 0x18)))) return false;
            return skill >= Read<int>(record, 0x44);
        }
        if (!kinds->IsModifier(item)) return false;
        int modifiers = 0, skillSum = 0;
        float manaSum = 0.0f;
        bool hasSpell = false;
        int school = 0;
        for (int i = 0; i < CountOf(spellResults_); ++i) {
            if (kinds->IsSpellContainer(Entry(spellResults_, i))) return false;
            if (kinds->IsSpell(Entry(spellResults_, i))) {
                hasSpell = true;
                const std::uint8_t* record = SpellRecord(Entry(spellResults_, i));
                skillSum += Read<int>(record, 0x44);
                school = Read<int>(record, 0x14);
                manaSum = Store(D(manaSum) + D(Read<float>(record, 0x18)));
                // The modifier must suit the spell (its kind +0xC, flags at +0x78 of the spell).
                std::uint32_t kind = Read<std::uint32_t>(ModifierRecord(item), 0x0C);
                if (kind < 5 && record[0x78 + kind] == 0) return false;
            } else if (kinds->IsModifier(Entry(spellResults_, i))) {
                ++modifiers;
                const std::uint8_t* modifier = ModifierRecord(Entry(spellResults_, i));
                manaSum = Store(D(manaSum) + D(Read<float>(modifier, 0x10)));
                skillSum += Read<int>(modifier, 0x18);
            }
        }
        if (!hasSpell || modifiers >= 8) return false;
        const std::uint8_t* modifier = ModifierRecord(item);
        float manaTotal = Store(D(manaSum) + D(Read<float>(modifier, 0x10)));
        int skillTotal = skillSum + Read<int>(modifier, 0x18);
        int skill = 0, mana = 0;
        best(this, school, &skill, &mana);
        if (skill < skillTotal) return false;
        return mana >= GameFtol(D(manaTotal));
    }
    case 4:     // the item constructor: anything but quest items and magic
        if (ItemType(item) == 0x3009) return false;
        if (kinds->IsSpellContainer(item) || kinds->IsSpell(item)) return false;
        return !kinds->IsModifier(item);
    case 5: {   // the item being built: a blueprint, its parts, a container's spell
        if (mode != 1 && mode != 3) return mode == 7;
        if (kinds->IsPart(item) || kinds->IsBlueprint(item)) return CountOf(itemResults_) <= 0;
        if (kinds->IsKindOne(item)) {
            using SlotFn = void(__thiscall*)(void*);
            void** listVtable = *static_cast<void***>(itemResults_);
            reinterpret_cast<SlotFn>(listVtable[18])(itemResults_);   // +0x48
            listVtable = *static_cast<void***>(itemResults_);
            reinterpret_cast<SlotFn>(listVtable[22])(itemResults_);   // +0x58
            std::uint32_t kindOnes = 0;
            const std::uint8_t* part = nullptr;
            const std::uint8_t* firstKindOne = nullptr;
            if (CountOf(itemResults_) <= 0) return false;
            for (int i = 0; i < CountOf(itemResults_); ++i) {
                if (kinds->IsBlueprint(Entry(itemResults_, i))) return false;
                if (kinds->IsPart(Entry(itemResults_, i))) {
                    const std::uint8_t* entry = Entry(itemResults_, i);
                    switch (Read<int>(entry, 0x50)) {
                    case 0: part = RecordAt(0x007C86F8, Index(entry), 0xB4); break;
                    case 1: part = RecordAt(0x007C8718, Index(entry), 200); break;
                    case 2: part = RecordAt(0x007C8738, Index(entry), 0x8C); break;
                    default: break;
                    }
                } else if (kinds->IsKindOne(Entry(itemResults_, i))) {
                    ++kindOnes;
                    if (!firstKindOne) firstKindOne = BlueprintRecord(Entry(itemResults_, i));
                }
            }
            if (!part) return false;
            const std::uint8_t* own = BlueprintRecord(item);
            if (firstKindOne) return own == firstKindOne && kindOnes < Read<std::uint32_t>(part, 0x4C);
            return gamecrt::mbsicmp(Read<const unsigned char*>(part, 0x10), Read<const unsigned char*>(own, 0x04)) == 0;
        }
        if (!kinds->IsSpellContainer(item)) return false;
        if (!itemMade_) return false;
        std::uint8_t* spell = Read<std::uint8_t*>(item, 0x44);
        if (!spell) return false;
        if (ItemType(itemMade_) != 0x3006 && !SpellHasCastFlag(spell)) return false;
        if (Read<std::uint32_t>(spell, 0x50) > Read<std::uint32_t>(itemMade_, 0x40)) return false;
        // FCOMPP / TEST AH,1: the container's +0x2C below the spell's +0x40 (or unordered) fails.
        float need = Read<float>(spell, 0x40);
        float room = Read<float>(itemMade_, 0x2C);
        if (!(room >= need)) return false;
        for (int i = 0; i < CountOf(itemResults_); ++i)
            if (kinds->IsSpellContainer(Entry(itemResults_, i))) return false;
        return true;
    }
    case 6: {   // repair: worn weapons and armour
        int type = ItemType(item);
        if (type != 0x3004 && type != 0x3005) return false;
        return Read<float>(item, 0x24) > Read<float>(item, 0x20);
    }
    default:
        return false;
    }
}

namespace {
bool __fastcall CampCanEquip(CCampPages* self, void*, std::uint8_t* item, int mode) { return self->CanEquip(item, mode); }
} // namespace
EI_REPLACE(0x006100E0, CampCanEquip);

// --- 00611930: UpdateAction ------------------------------------------------------------------------

namespace {

// The trade panel's experience trade (thiscall on the panel).
struct TradeAt {
    bool CanLevelUp();     // 00625510
    int LevelUpCost();     // 00625560
    int Experience();      // 00625530
};
__declspec(naked) bool TradeAt::CanLevelUp() EI_ORIGINAL(0x00625510)
__declspec(naked) int TradeAt::LevelUpCost() EI_ORIGINAL(0x00625560)
__declspec(naked) int TradeAt::Experience() EI_ORIGINAL(0x00625530)

// The help panel: 005EE6C0 describes an item (null: nothing), 005F2910 shows "camphelp<topic>".
struct HelpAt {
    void Describe(const void* item);
    void ShowHelp(int topic);
};
__declspec(naked) void HelpAt::Describe(const void*) EI_ORIGINAL(0x005EE6C0)
__declspec(naked) void HelpAt::ShowHelp(int) EI_ORIGINAL(0x005F2910)

// Whether the constructors can make something from their slots (they also set spellMade_ /
// itemMade_; thiscall on the camp, AL).
struct MakeAt {
    bool CanMakeSpell();   // 00612290
    bool CanMakeItem();    // 00612600
};
__declspec(naked) bool MakeAt::CanMakeSpell() EI_ORIGINAL(0x00612290)
__declspec(naked) bool MakeAt::CanMakeItem() EI_ORIGINAL(0x00612600)

// Whether an item has a "used" record flag (+0x1C record, its +0x10).
bool Flagged(const std::uint8_t* item) {
    const std::uint8_t* record = Read<const std::uint8_t*>(item, 0x1C);
    return record && record[0x10];
}
int Count(const std::uint8_t* item) { return Read<int>(item, 0x48); }

void SetShadeOf(CEngineObject* object, bool full) { SetShade(object, full ? 0x3F800000 : 0x3F000000); }

} // namespace

void CCampPages::UpdateAction() {
    mfc::CString action;
    mfc::CString label(GameText(0x0079388C));   // "camp_current_money"
    cost_ = 0;
    actionEnabled_ = false;
    actionShown_ = false;
    // Slot 54 (+0xD8, Rebirth-patched 0060FAB0): the price of `item` for deal `kind`.
    using PriceFn = int(__thiscall*)(CCampPages*, void*, int);
    auto price = [this](void* item, int kind) { return reinterpret_cast<PriceFn>(vtable_[54])(this, item, kind); };
    CampKinds* kinds = reinterpret_cast<CampKinds*>(this);
    HelpAt* help = reinterpret_cast<HelpAt*>(helpPanel_);
    MakeAt* make = reinterpret_cast<MakeAt*>(this);

    // A trade page: what the camp buys (`buy`) minus what it sells (`sell`).
    auto trade = [&](void* bought, int buyKind, void* sold, int sellKind, std::uintptr_t text) {
        if (CountOf(bought) <= 0 && CountOf(sold) <= 0) return;
        action = GameText(text);
        actionEnabled_ = true;
        actionShown_ = true;
        for (int i = 0; i < CountOf(bought); ++i) {
            std::uint8_t* item = Entry(bought, i);
            int count = Count(item);
            cost_ += price(item, buyKind) * count;
        }
        for (int i = 0; i < CountOf(sold); ++i) {
            std::uint8_t* item = Entry(sold, i);
            int count = Count(item);
            cost_ -= price(item, sellKind) * count;
        }
    };
    // A constructor's page: its slots' cost; a finished item in the slots is taken apart,
    // otherwise the constructor makes what the slots allow.
    auto construct = [&](std::uint8_t*& made, void* slots, int slotKind, int recordKind, int apartKind,
                         bool (CampKinds::*isFinished)(const std::uint8_t*),
                         bool (CampKinds::*isMaterial)(const std::uint8_t*), bool (MakeAt::*canMake)(),
                         std::uintptr_t apartText, std::uintptr_t makeText, int topic) {
        if (made) reinterpret_cast<CEngineObject*>(made)->Release();
        made = nullptr;
        deconstructing_ = false;
        bool described = false;
        if (CountOf(slots) > 0) {
            actionShown_ = true;
            for (int i = 0; i < CountOf(slots); ++i) {
                std::uint8_t* item = Entry(slots, i);
                cost_ += price(item, slotKind);
                if (Flagged(item)) cost_ += price(item, recordKind);
                if ((kinds->*isFinished)(item)) {
                    actionEnabled_ = true;
                    cost_ = price(item, apartKind);
                    if (Flagged(item)) cost_ += price(item, recordKind);
                    deconstructing_ = true;
                    help->Describe(item);
                    described = true;
                    action = GameText(apartText);
                    break;
                }
                if ((kinds->*isMaterial)(item)) {
                    actionEnabled_ = (make->*canMake)();
                    help->Describe(made);
                    described = made != nullptr;
                    if (actionEnabled_) action = GameText(makeText);
                }
            }
            if (!actionEnabled_) cost_ = 0;
            if (described) return;
        }
        help->ShowHelp(topic);
    };

    switch (page_) {
    case 0:
        // Repair all the hero's weapons and armour (where the item constructor works).
        if (itemConstructor_ && !noConstructor_) {
            using HeroFn = std::uint8_t*(__thiscall*)(CCampPages*);
            std::uint8_t* hero = reinterpret_cast<HeroFn>(vtable_[51])(this);   // +0xCC
            for (int i = 0; i < Read<int>(hero, 0x734); ++i) cost_ += price(Read<void**>(hero, 0x730)[i], 8);
            for (int i = 0; i < Read<int>(hero, 0x754); ++i) cost_ += price(Read<void**>(hero, 0x750)[i], 8);
            actionEnabled_ = cost_ > 0;
            if (actionEnabled_) action = GameText(0x0079387C);   // "camp_repair_all"
        }
        break;
    case 1: {
        TradeAt* panel = reinterpret_cast<TradeAt*>(trade_);
        bool up = panel->CanLevelUp();
        actionShown_ = up;
        actionEnabled_ = up;
        cost_ = panel->LevelUpCost();
        if (actionEnabled_) action = GameText(0x0079386C);   // "camp_level_up"
        label = GameText(0x00793858);                       // "camp_current_exp"
        break;
    }
    case 2:
        trade(spellParts_[1], 0, spellParts_[2], 1, 0x007936FC);   // "camp_spell_trade"
        break;
    case 3:
        construct(spellMade_, spellResults_, 2, 0, 3, &CampKinds::IsSpellContainer, &CampKinds::IsSpell,
                  &MakeAt::CanMakeSpell, 0x00793844, 0x007936E8, 0x67);   // "camp_spell_deconstr", "camp_spell_constr"
        break;
    case 4:
        trade(itemParts_[1], 4, itemParts_[2], 5, 0x007936D8);     // "camp_item_trade"
        break;
    case 5:
        construct(itemMade_, itemResults_, 6, 4, 7, &CampKinds::IsBlueprint, &CampKinds::IsPart,
                  &MakeAt::CanMakeItem, 0x0079381C, 0x007936C4, 0x66);    // "camp_item_deconstr", "camp_item_constr"
        break;
    case 6:
        if (CountOf(itemExtra_) > 0) {
            action = GameText(0x00793830);   // "camp_item_repair"
            actionEnabled_ = true;
            actionShown_ = true;
            for (int i = 0; i < CountOf(itemExtra_); ++i) {
                std::uint8_t* item = Entry(itemExtra_, i);
                int count = Count(item);
                cost_ += price(item, 8) * count;
            }
        }
        break;
    default:
        break;
    }

    int amount;
    if (page_ == 1) {
        amount = reinterpret_cast<TradeAt*>(trade_)->Experience();
    } else {
        amount = Read<int>(LocalPlayerData(), 0x50);   // money
        if (Read<int>(LocalPlayerData(), 0x50) < cost_) actionEnabled_ = false;
    }

    mfc::CString text;
    mfc::CString line;
    CTextManager* texts = *reinterpret_cast<CTextManager**>(0x007AF788);
    SurfaceDraw* draw = reinterpret_cast<SurfaceDraw*>(surfaces_.data_[0]);
    draw->Begin();
    draw->Clear();
    if (reinterpret_cast<const int*>(static_cast<const char*>(action))[-2] != 0) {
        texts->GetString(action, &text);
        RECT name = {0, 30, 200, 50};
        draw->Text(text, &name, 1, 0xFFFFFF, 1, 1);
        if (cost_ != 0)
            text.Format(GameText(0x00793818), -cost_);   // "%+d"
        else
            text = GameText(0x007856B8);                 // "0"
        RECT value = {0, 50, 200, 70};
        draw->Text(text, &value, 1, 0xFFFFFF, 1, 1);
    }
    texts->GetString(label, &text);
    line.Format(GameText(0x00792F14), static_cast<const char*>(text), amount);   // "%s %d"
    RECT top = {0, 10, 200, 30};
    draw->Text(line, &top, 1, 0xFFFFFF, 1, 1);
    draw->End();

    SetShadeOf(sceneObjects_.data_[0], actionEnabled_);
    SetShadeOf(sceneObjects_.data_[1], actionShown_);
}
EI_REPLACE_METHOD(0x00611930, CCampPages::UpdateAction);

// --- 0060F050: Draw ------------------------------------------------------------------------------

namespace {

// What the statistics under the mouse are (filled by the trade and stats panels, read by the
// description panel 005F1590): kind (-1 none), index, stats pointer at +0x10, a text.
struct HoverInfo {
    int kind = -1;
    int index = 0;
    std::uint8_t unknown_08[8] = {};
    void* stats = nullptr;
    mfc::CString text;
};
static_assert(sizeof(HoverInfo) == 0x18);

struct HoverAt {
    void TradeInfo(int x, int y, HoverInfo* info);   // 00625690 (trade panel)
    void StatsInfo(int x, int y, HoverInfo* info);   // 00624CB0 (stats panel)
    void Describe(HoverInfo* info);                   // 005F1590 (description panel)
    void DrawHero(int pass, void* hero, std::uint32_t turn, float depth, int flag);   // 005EC300 (the camp)
};
__declspec(naked) void HoverAt::TradeInfo(int, int, HoverInfo*) EI_ORIGINAL(0x00625690)
__declspec(naked) void HoverAt::StatsInfo(int, int, HoverInfo*) EI_ORIGINAL(0x00624CB0)
__declspec(naked) void HoverAt::Describe(HoverInfo*) EI_ORIGINAL(0x005F1590)
__declspec(naked) void HoverAt::DrawHero(int, void*, std::uint32_t, float, int) EI_ORIGINAL(0x005EC300)

// A panel's or strip's item under (x, y) (slot 25, +0x64); also gives a help topic.
std::uint8_t* ItemUnder(void* panel, int x, int y, int* topic) {
    using Fn = std::uint8_t*(__thiscall*)(void*, int, int, int*);
    return reinterpret_cast<Fn>((*static_cast<void***>(panel))[25])(panel, x, y, topic);
}

} // namespace

BOOL CCampPages::Draw(int pass) {
    int x = *reinterpret_cast<const int*>(0x007C7C50);   // the mouse in screen pixels
    int y = *reinterpret_cast<const int*>(0x007C7C54);
    hoveredItem_ = nullptr;
    int topic = 0;
    // The page buttons' help topics.
    switch (RegionAt(x, y)) {
    case 4: topic = 0x69; break;
    case 5: topic = 0x6A; break;
    case 6: if (spellConstructor_) topic = 0x65; break;
    case 7: if (spellConstructor_ && !noConstructor_) topic = 0x67; break;
    case 8: if (itemConstructor_) topic = 100; break;
    case 9: if (itemConstructor_ && !noConstructor_) topic = 0x66; break;
    case 10: if (itemConstructor_) topic = 0x68; break;
    case 11: topic = 0x6B; break;
    default: break;
    }
    // The items under the mouse, panel by panel until one is found or a topic is set.
    auto probe = [&](void* panel) {
        if (!hoveredItem_ && topic == 0) hoveredItem_ = ItemUnder(panel, x, y, &topic);
    };
    probe(equipment_);
    probe(equipment2_);
    probe(itemStrip_);
    if (spellConstructor_) {
        probe(spellConstructor1_);
        probe(spellConstructor2_);
        probe(spellStrip_);
    }
    if (itemConstructor_) {
        probe(itemConstructor1_);
        probe(itemConstructor2_);
        probe(itemConstructor3_);
        probe(partStrip_);
    }

    if (hoveredItem_) {
        if (descPanel_) reinterpret_cast<HelpAt*>(descPanel_)->Describe(hoveredItem_);
    } else if (topic != 0) {
        if (descPanel_) reinterpret_cast<HelpAt*>(descPanel_)->ShowHelp(topic);
    } else {
        // The statistics under the mouse: the trade panel's, else the stats panel's.
        HoverInfo info;
        if (trade_->enabled_ == 0 || (reinterpret_cast<HoverAt*>(trade_)->TradeInfo(x, y, &info), info.kind < 0)) {
            if (statsPanel_->enabled_ != 0) reinterpret_cast<HoverAt*>(statsPanel_)->StatsInfo(x, y, &info);
        }
        if (descPanel_) reinterpret_cast<HoverAt*>(descPanel_)->Describe(&info);
    }
    UpdateAction();
    reinterpret_cast<CUiContainer*>(this)->DrawContainer(pass);
    if (page_ == 0)
        reinterpret_cast<HoverAt*>(this)->DrawHero(pass, heroes_[hero_], heroTurn_, 400.0f, 1);
    return 1;
}

namespace {
BOOL __fastcall CampDraw(CCampPages* self, void*, int pass) { return self->Draw(pass); }
} // namespace
EI_REPLACE(0x0060F050, CampDraw);

// --- 00612290 / 00612600: what the constructors would make ---------------------------------------

namespace {

// The spell constructor (CSpellConstructor, not rewritten; thiscall).
struct SpellConstructorAt {
    void SetManager(void* manager);           // 006841A0
    BOOL Begin(const void* spell);            // 006841E0
    BOOL AddModifier(const void* modifier);   // 006842A0
    BOOL End();                               // 00684250
};
__declspec(naked) void SpellConstructorAt::SetManager(void*) EI_ORIGINAL(0x006841A0)
__declspec(naked) BOOL SpellConstructorAt::Begin(const void*) EI_ORIGINAL(0x006841E0)
__declspec(naked) BOOL SpellConstructorAt::AddModifier(const void*) EI_ORIGINAL(0x006842A0)
__declspec(naked) BOOL SpellConstructorAt::End() EI_ORIGINAL(0x00684250)

// The item constructor (not rewritten; thiscall).
struct ItemConstructorAt {
    void SetManager(void* manager);           // 006A5C00
    BOOL Begin(int type, int a);              // 006A5C50
    BOOL SetPart(const void* part);           // 006A5CD0
    BOOL SetBlueprint(const void* blueprint); // 006A5CC0
    BOOL SetSpell(void* spell);               // 006A5CE0
    std::uint8_t* Make();                     // 006A5CF0
};
__declspec(naked) void ItemConstructorAt::SetManager(void*) EI_ORIGINAL(0x006A5C00)
__declspec(naked) BOOL ItemConstructorAt::Begin(int, int) EI_ORIGINAL(0x006A5C50)
__declspec(naked) BOOL ItemConstructorAt::SetPart(const void*) EI_ORIGINAL(0x006A5CD0)
__declspec(naked) BOOL ItemConstructorAt::SetBlueprint(const void*) EI_ORIGINAL(0x006A5CC0)
__declspec(naked) BOOL ItemConstructorAt::SetSpell(void*) EI_ORIGINAL(0x006A5CE0)
__declspec(naked) std::uint8_t* ItemConstructorAt::Make() EI_ORIGINAL(0x006A5CF0)

// 0068B410 (thiscall on a spell): a copy of the spell made with `manager`.
struct SpellAt {
    std::uint8_t* Clone(void* manager);
};
__declspec(naked) std::uint8_t* SpellAt::Clone(void*) EI_ORIGINAL(0x0068B410)

// 0052C690: a reference-counted pointer's assignment (takes a reference, releases the old one).
struct RefPtrAt {
    void Assign(void* object);
};
__declspec(naked) void RefPtrAt::Assign(void*) EI_ORIGINAL(0x0052C690)

// 00613820 (on the spell container table 007C8738): the record named `name`.
struct ContainerTableAt {
    std::uint8_t* Find(const char* name);
};
__declspec(naked) std::uint8_t* ContainerTableAt::Find(const char*) EI_ORIGINAL(0x00613820)

// The item factory: 006AD000 makes an item (type, record index, a, b); 006A04D0 (on an item)
// puts a spell into it.
struct ItemFactoryAt {
    std::uint8_t* MakeItem(int type, int index, int a, int b);
};
__declspec(naked) std::uint8_t* ItemFactoryAt::MakeItem(int, int, int, int) EI_ORIGINAL(0x006AD000)
struct ItemAt {
    void SetSpell(void* spell);
};
__declspec(naked) void ItemAt::SetSpell(void*) EI_ORIGINAL(0x006A04D0)

// The slot list's own helpers (virtual slots 16 / 17 / 18 / 22 of the 00661050 lists): the item
// constructor, the spell constructor, the item factory and the manager they work for.
void* ListSlot(void* list, int slot) {
    using Fn = void*(__thiscall*)(void*);
    return reinterpret_cast<Fn>((*static_cast<void***>(list))[slot])(list);
}

void ReleaseObject(void* object) {
    if (object) reinterpret_cast<CEngineObject*>(object)->Release();
}

} // namespace

bool CCampPages::CanMakeSpell() {
    SpellConstructorAt* constructor = static_cast<SpellConstructorAt*>(ListSlot(spellResults_, 17));
    void* factory = ListSlot(spellResults_, 18);
    void* manager = ListSlot(spellResults_, 22);
    const std::uint8_t* spell = nullptr;
    // The original keeps the modifiers in an 8-entry stack array (more would overwrite its
    // frame; CanEquip allows at most eight).
    const std::uint8_t* modifiers[32];
    int modifierCount = 0;
    if (CountOf(spellResults_) <= 0) return false;
    CampKinds* kinds = reinterpret_cast<CampKinds*>(this);
    for (int i = 0; i < CountOf(spellResults_); ++i) {
        std::uint8_t* item = Entry(spellResults_, i);
        if (kinds->IsSpell(item))
            spell = SpellRecord(item);
        else if (kinds->IsModifier(item) && modifierCount < 32)
            modifiers[modifierCount++] = ModifierRecord(item);
    }
    if (!spell) return false;
    constructor->SetManager(manager);
    if (!constructor->Begin(spell)) {
        constructor->SetManager(nullptr);
        return false;
    }
    for (int i = 0; i < modifierCount; ++i) {
        if (!constructor->AddModifier(modifiers[i])) {
            constructor->SetManager(nullptr);
            return false;
        }
    }
    if (!constructor->End()) {
        constructor->SetManager(nullptr);
        return false;
    }
    std::uint8_t* made = reinterpret_cast<SpellAt*>(Read<std::uint8_t*>(static_cast<std::uint8_t*>(static_cast<void*>(constructor)), 0x20) + 0x30)
                             ->Clone(manager);
    constructor->SetManager(nullptr);
    std::uint8_t* record = reinterpret_cast<ContainerTableAt*>(0x007C8738)->Find(GameText(0x007938A0));   // "spell container"
    int index = static_cast<int>(record - *reinterpret_cast<std::uint8_t* const*>(0x007C8738)) / 0x8C;
    std::uint8_t* container = static_cast<ItemFactoryAt*>(factory)->MakeItem(0x3008, index, 0, -1);
    if (container) reinterpret_cast<ItemAt*>(container)->SetSpell(made);
    spellMade_ = container;
    if (!container) return false;
    ++*reinterpret_cast<int*>(container + 4);
    int skill = 0, mana = 0;
    using BestFn = void(__thiscall*)(CCampPages*, int, int*, int*);
    reinterpret_cast<BestFn>(vtable_[45])(this, Read<int>(spell, 0x14), &skill, &mana);
    if (mana < GameFtol(D(Read<float>(made, 0x40)))) return false;
    return skill >= Read<int>(made, 0x50);
}
namespace {
bool __fastcall CampCanMakeSpell(CCampPages* self, void*) { return self->CanMakeSpell(); }
} // namespace
EI_REPLACE(0x00612290, CampCanMakeSpell);

bool CCampPages::CanMakeItem() {
    ItemConstructorAt* constructor = static_cast<ItemConstructorAt*>(ListSlot(itemResults_, 16));
    void* factory = ListSlot(itemResults_, 18);
    void* manager = ListSlot(itemResults_, 22);
    const std::uint8_t* part = nullptr;
    void* spell = nullptr;   // a reference-counted pointer (0052C690), released on the way out
    const std::uint8_t* blueprint = nullptr;
    int blueprintCount = 0;
    int type = 0;            // the original's is uninitialised until a part sets it
    auto fail = [&]() {
        ReleaseObject(spell);
        return false;
    };
    CampKinds* kinds = reinterpret_cast<CampKinds*>(this);
    if (CountOf(itemResults_) <= 0) return fail();
    for (int i = 0; i < CountOf(itemResults_); ++i) {
        std::uint8_t* item = Entry(itemResults_, i);
        if (kinds->IsSpellContainer(item)) {
            std::uint8_t* copy = reinterpret_cast<SpellAt*>(Read<std::uint8_t*>(item, 0x44))->Clone(manager);
            reinterpret_cast<RefPtrAt*>(&spell)->Assign(copy);
        } else if (kinds->IsPart(item)) {
            switch (Read<int>(item, 0x50)) {
            case 0: type = 0x3004; part = RecordAt(0x007C86F8, Index(item), 0xB4); break;
            case 1: type = 0x3005; part = RecordAt(0x007C8718, Index(item), 200); break;
            case 2: type = 0x3006; part = RecordAt(0x007C8738, Index(item), 0x8C); break;
            default: break;
            }
        } else if (kinds->IsKindOne(item)) {
            const std::uint8_t* record = BlueprintRecord(item);
            if (!blueprint) {
                blueprint = record;
                blueprintCount = 1;
            } else if (blueprint != record) {
                return fail();
            } else {
                ++blueprintCount;
            }
        }
    }
    if (!blueprint || !part) return fail();
    if (Read<int>(part, 0x4C) != blueprintCount) return fail();
    if (gamecrt::mbsicmp(Read<const unsigned char*>(part, 0x10), Read<const unsigned char*>(blueprint, 0x04)) != 0)
        return fail();
    constructor->SetManager(factory);
    if (!constructor->Begin(type, -1)) {
        constructor->SetManager(nullptr);
        return fail();
    }
    if (!constructor->SetPart(part) || !constructor->SetBlueprint(blueprint) || !constructor->SetSpell(spell)) {
        constructor->SetManager(nullptr);
        return fail();
    }
    itemMade_ = constructor->Make();
    constructor->SetManager(nullptr);
    if (!itemMade_) return fail();
    ++*reinterpret_cast<int*>(itemMade_ + 4);
    ReleaseObject(spell);
    return true;
}
namespace {
bool __fastcall CampCanMakeItem(CCampPages* self, void*) { return self->CanMakeItem(); }
} // namespace
EI_REPLACE(0x00612600, CampCanMakeItem);

// --- 0060E6B0: leaving the camp ---------------------------------------------------------------

namespace {

// The record array CreateUnits-style callers pass (CArray of record pointers, vtable 0073FAA4).
struct CloseRecordArray {
    void* vtable;
    std::uint8_t** data;
    int size;
    int capacity;
    int growBy;
};
// 005719B0 (RET 8): its SetSize(size, growBy).
__declspec(naked) void __fastcall CloseRecordsSetSize(CloseRecordArray*, void*, int, int) EI_ORIGINAL(0x005719B0)

struct GameGlue {
    void SetServerFlag(std::uint8_t flag);              // 00578D10 (on 007AFEA4)
    std::uint8_t* PlayerData();                         // 00578BA0 (on 007AFEA5)
    void TakeRecords(CloseRecordArray* records);        // 005789E0 (on 007AFEA5)
};
__declspec(naked) void GameGlue::SetServerFlag(std::uint8_t) EI_ORIGINAL(0x00578D10)
__declspec(naked) std::uint8_t* GameGlue::PlayerData() EI_ORIGINAL(0x00578BA0)
__declspec(naked) void GameGlue::TakeRecords(CloseRecordArray*) EI_ORIGINAL(0x005789E0)
GameGlue* GlueAt(std::uintptr_t address) { return reinterpret_cast<GameGlue*>(address); }

// The description panels' Describe (005EE6C0) through its address.
struct DescribeAt {
    void Describe(void* item);
};
__declspec(naked) void DescribeAt::Describe(void*) EI_ORIGINAL(0x005EE6C0)

constexpr int kRecordSize = 0x784;
constexpr std::uint32_t kRecordBias = 100000000;

} // namespace

void CCampPages::Close() {
    GlueAt(0x007AFEA4)->SetServerFlag(0);
    {
        void* display = *reinterpret_cast<void* const*>(0x007AF080);
        void* device;
        std::memcpy(&device, static_cast<std::uint8_t*>(display) + 0x54, 4);
        using SetStateFn = long(__stdcall*)(void*, unsigned long, unsigned long);   // SetRenderState
        reinterpret_cast<SetStateFn>((*static_cast<void***>(device))[20])(device, 2, 0);
    }
    CloseRecordArray records = {reinterpret_cast<void*>(0x0073FAA4), nullptr, 0, 0, 0};
    std::uint8_t* player = GlueAt(0x007AFEA5)->PlayerData();
    auto recordsOf = [player] { return *reinterpret_cast<std::uint8_t**>(player + 0x30); };
    auto countOf = [player] { return *reinterpret_cast<int*>(player + 0x34); };
    for (int i = 0; i < countOf(); ++i) {
        std::uint8_t* record = recordsOf() + i * kRecordSize;
        *reinterpret_cast<std::uint32_t*>(record + 0x1C) += kRecordBias;
        int index = records.size;
        CloseRecordsSetSize(&records, nullptr, index + 1, -1);
        records.data[index] = recordsOf() + i * kRecordSize;
    }
    GlueAt(0x007AFEA5)->TakeRecords(&records);
    for (int i = 0; i < countOf(); ++i)
        *reinterpret_cast<std::uint32_t*>(recordsOf() + i * kRecordSize + 0x1C) -= kRecordBias;

    auto* self = reinterpret_cast<std::uint8_t*>(this);
    if (heroes_) {
        gamecrt::operator_delete(heroes_);
        heroes_ = nullptr;
    }
    std::memset(self + 0x1A8, 0, 4);
    std::memset(self + 0x1A4, 0, 4);
    if (spellMade_) reinterpret_cast<CEngineObject*>(spellMade_)->Release();
    spellMade_ = nullptr;
    if (itemMade_) reinterpret_cast<CEngineObject*>(itemMade_)->Release();
    itemMade_ = nullptr;
    reinterpret_cast<DescribeAt*>(helpPanel_)->Describe(nullptr);
    reinterpret_cast<DescribeAt*>(descPanel_)->Describe(nullptr);
    // The part lists (slot 3 empties a list, slot 0 is the deleting destructor); the first of
    // each constructor's lists (+0x1B4, +0x1C8) is not touched here.
    for (int offset : {0x1CC, 0x1D0, 0x1D4, 0x1B8, 0x1BC, 0x1C0, 0x1C4}) {
        void* list;
        std::memcpy(&list, self + offset, 4);
        using EmptyFn = void(__thiscall*)(void*);
        reinterpret_cast<EmptyFn>((*static_cast<void***>(list))[3])(list);
        std::memcpy(&list, self + offset, 4);
        if (list) {
            using DeleteFn = void(__thiscall*)(void*, int);
            reinterpret_cast<DeleteFn>((*static_cast<void***>(list))[0])(list, 1);
        }
    }
    // The record array's destructor.
    records.vtable = reinterpret_cast<void*>(0x0073FAA4);
    if (records.data) gamecrt::operator_delete(records.data);
}
namespace {
void __fastcall CampClose(CCampPages* self, void*) { self->Close(); }
} // namespace
EI_REPLACE(0x0060E6B0, CampClose);

// --- 0060E3E0: the trade panel's destructor body (vtable 007417A8) --------------------------------

namespace {

// 006FE958: CStringArray's destructor (thiscall).
__declspec(naked) void __fastcall StringArrayDestruct(void*) EI_ORIGINAL(0x006FE958)
// 00613630 (__fastcall: block, count): frees a vector's block of 0x3C-byte entries (STLport node
// allocator for small blocks). 006135F0 (stdcall, RET 8): the same for 0x18-byte entries.
__declspec(naked) void __fastcall FreeBlock3C(void*, int) EI_ORIGINAL(0x00613630)
__declspec(naked) void __stdcall FreeBlock18(void*, int) EI_ORIGINAL(0x006135F0)
// 00613CF0 (thiscall): a 0x18-byte list entry's destructor.
__declspec(naked) void __fastcall DestroyEntry18(void*) EI_ORIGINAL(0x00613CF0)

struct VectorView {
    std::uint8_t* begin;
    std::uint8_t* end;
    std::uint8_t* capacity;
};
VectorView* VectorAt(void* object, int offset) {
    return reinterpret_cast<VectorView*>(static_cast<std::uint8_t*>(object) + offset);
}

// The entries' destructors, then the block (its capacity in entries, a signed division).
void DestroyVector18(VectorView* v) {
    for (std::uint8_t* at = v->begin; at != v->end; at += 0x18) DestroyEntry18(at);
    FreeBlock18(v->begin, static_cast<int>(v->capacity - v->begin) / 0x18);
}

} // namespace

void __fastcall TradePanelDestroy(CUiWindow* self) {
    StringArrayDestruct(reinterpret_cast<std::uint8_t*>(self) + 0x1A4);
    VectorView* records = VectorAt(self, 0x190);
    FreeBlock3C(records->begin, static_cast<int>(records->capacity - records->begin) / 0x3C);
    DestroyVector18(VectorAt(self, 0x180));
    DestroyVector18(VectorAt(self, 0x174));
    DestroyVector18(VectorAt(self, 0x168));
    DestroyVector18(VectorAt(self, 0x15C));
    self->DestroyBase();
}
EI_REPLACE(0x0060E3E0, TradePanelDestroy);

// --- 0060FE40: taking an item off the hero ---------------------------------------------------

namespace {

// The hero record (thiscall, RET 4): 00660A50 / 00660E10 / 00660C30 / 00660FF0 take a weapon,
// armour, a 0x3006 item or a 0x3008 item off.
struct HeroRecordAt {
    void RemoveWeapon(std::uint8_t* item);    // 00660A50
    void RemoveArmour(std::uint8_t* item);    // 00660E10
    void Remove3006(std::uint8_t* item);      // 00660C30
    void Remove3008(std::uint8_t* item);      // 00660FF0
};
__declspec(naked) void HeroRecordAt::RemoveWeapon(std::uint8_t*) EI_ORIGINAL(0x00660A50)
__declspec(naked) void HeroRecordAt::RemoveArmour(std::uint8_t*) EI_ORIGINAL(0x00660E10)
__declspec(naked) void HeroRecordAt::Remove3006(std::uint8_t*) EI_ORIGINAL(0x00660C30)
__declspec(naked) void HeroRecordAt::Remove3008(std::uint8_t*) EI_ORIGINAL(0x00660FF0)

// 00661DB0 (thiscall on the local player's data, RET 0xC): puts an item among the player's items.
__declspec(naked) int __fastcall PlayerTakeItem(std::uint8_t*, void*, std::uint8_t*, int, int) EI_ORIGINAL(0x00661DB0)
// 006A39E0 (thiscall on an armour item): its body part type. 00530840 (thiscall on a unit, RET
// 4): takes off the armour of that type.
__declspec(naked) int __fastcall ArmourType(std::uint8_t*) EI_ORIGINAL(0x006A39E0)
__declspec(naked) void __fastcall UnitUnequipArmour(std::uint8_t*, void*, int) EI_ORIGINAL(0x00530840)
// 005232B0 / 005230C0 (thiscall on the hero's statistics, record + 0x20): clears the outer
// layers / adds one (RET 4).
__declspec(naked) void __fastcall StatsClearOuterLayers(std::uint8_t*) EI_ORIGINAL(0x005232B0)
__declspec(naked) void __fastcall StatsAddLayer(std::uint8_t*, void*, void*) EI_ORIGINAL(0x005230C0)
// 00623880: the statistics panel's drawing, through its address.
struct StatsPanelAt {
    void DrawStats();
};
__declspec(naked) void StatsPanelAt::DrawStats() EI_ORIGINAL(0x00623880)

template <typename T>
T& CampField(void* p, int offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(p) + offset);
}

} // namespace

int CCampPages::TakeOff(std::uint8_t* item) {
    interface_->PlaySound(reinterpret_cast<const char*>(0x007937C8), 0);   // "buttons\camp\put_off.wav"
    int kind = CampField<int>(item, 0x10);
    if (kind < 0x3004 || (kind > 0x3006 && kind != 0x3008)) return 0;
    using HeroFn = std::uint8_t*(__thiscall*)(CCampPages*);
    std::uint8_t* hero = reinterpret_cast<HeroFn>(vtable_[51])(this);
    int result = PlayerTakeItem(GlueAt(0x007AFEA5)->PlayerData(), nullptr, item, 0, 1);
    auto* record = reinterpret_cast<HeroRecordAt*>(hero);
    switch (CampField<int>(item, 0x10)) {
    case 0x3004: {
        // The chosen weapon (+0x774) moves back when the item lies before it.
        int count = CampField<int>(hero, 0x734);
        std::uint8_t** weapons = CampField<std::uint8_t**>(hero, 0x730);
        for (int i = 0; i < count; ++i) {
            if (weapons[i] != item) continue;
            if (static_cast<std::uint32_t>(i) < CampField<std::uint32_t>(hero, 0x774))
                --CampField<std::uint32_t>(hero, 0x774);
            break;
        }
        record->RemoveWeapon(item);
        if (CampField<std::uint32_t>(hero, 0x774) >= CampField<std::uint32_t>(hero, 0x734))
            CampField<std::uint32_t>(hero, 0x774) = CampField<std::uint32_t>(hero, 0x734) - 1;
        using RedrawFn = void(__thiscall*)(CCampPages*);
        reinterpret_cast<RedrawFn>(vtable_[46])(this);
        break;
    }
    case 0x3005: {
        if (heroes_[hero_]) {
            int type = ArmourType(item);
            UnitUnequipArmour(heroes_[hero_], nullptr, type);
            using IntFn = void(__thiscall*)(void*, int);
            using VoidFn = void(__thiscall*)(void*);
            void* figure = CampField<void*>(heroes_[hero_], 0x240);
            reinterpret_cast<IntFn>((*static_cast<void***>(figure))[25])(figure, CampField<int>(hero, 0x774));
            figure = CampField<void*>(heroes_[hero_], 0x240);
            reinterpret_cast<VoidFn>((*static_cast<void***>(figure))[30])(figure);
            figure = CampField<void*>(heroes_[hero_], 0x240);
            reinterpret_cast<VoidFn>((*static_cast<void***>(figure))[29])(figure);
        }
        record->RemoveArmour(item);
        std::uint8_t* stats = hero + 0x20;
        StatsClearOuterLayers(stats);
        for (int i = 0; i < CampField<int>(hero, 0x754); ++i)
            StatsAddLayer(stats, nullptr, CampField<void**>(hero, 0x750)[i]);
        break;
    }
    case 0x3006:
        record->Remove3006(item);
        break;
    case 0x3008:
        record->Remove3008(item);
        break;
    default:
        break;
    }
    reinterpret_cast<StatsPanelAt*>(statsPanel_)->DrawStats();
    return result;
}
namespace {
int __fastcall CampTakeOff(CCampPages* self, void*, std::uint8_t* item) { return self->TakeOff(item); }
} // namespace
EI_REPLACE(0x0060FE40, CampTakeOff);
