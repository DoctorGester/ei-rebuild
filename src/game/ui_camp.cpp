// The camp's page switch (0060E9E0) and the camp panels' layout code (see docs/notes/ui.md, "Camp,
// global map and battle screen setup").
#include "ui_camp.h"

#include "world.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/crt.h"

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
        if (!container_) return false;
        std::uint8_t* spell = Read<std::uint8_t*>(item, 0x44);
        if (!spell) return false;
        if (ItemType(container_) != 0x3006 && !SpellHasCastFlag(spell)) return false;
        if (Read<std::uint32_t>(spell, 0x50) > Read<std::uint32_t>(container_, 0x40)) return false;
        // FCOMPP / TEST AH,1: the container's +0x2C below the spell's +0x40 (or unordered) fails.
        float need = Read<float>(spell, 0x40);
        float room = Read<float>(container_, 0x2C);
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
