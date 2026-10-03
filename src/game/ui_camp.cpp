// The camp's page switch (0060E9E0) and the camp panels' layout code (see docs/notes/ui.md, "Camp,
// global map and battle screen setup").
#include "ui_camp.h"

#include "world.h"

#include "ei/hook.h"

#include <cstdint>
#include <cstring>
#include <initializer_list>

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
