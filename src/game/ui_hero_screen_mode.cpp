// The multiplayer hero screen's mode (CMpHeroScreen::SetModeVanilla 00615D30): the hero list (9), the new
// hero (10) and its statistics page (11) laid out. Rebirth's WideScreen replaces it with the
// original followed by the curtains' visibility (widescreen::ShowHeroCurtains), which this
// reproduces, so it is hooked in both launches; the screen's other members call it through their
// forwarder. See ui_screens.h and docs/notes/ui.md, section 5.
#include "ui.h"
#include "ui_screens.h"
#include "game_strings.h"

#include "ui_camp.h"
#include "ui_item_info.h"
#include "ui_widescreen.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "originals_ui.h"

#include <cstdint>

namespace {


constexpr int kModeHeroList = 9;
constexpr int kModeNewHero = 10;
constexpr int kModeStatistics = 11;

// The sprites: the selection frame, the four quarters of the frame around the figure, and the
// three shown only on the new hero's pages (the last is the create button).
constexpr int kSelectionFrame = 2;
constexpr int kFirstQuarter = 3;
constexpr int kFirstNewHeroSprite = 7;
constexpr int kSpritesEnd = 10;
// The regions every mode keeps (the buttons); the list rows come after them.
constexpr int kFixedRegions = 9;
constexpr int kRowsBottom = 490;

constexpr float kQuarterDepth = 7.0f;
constexpr float kFrameDepth = 3.0f;
constexpr float kScale = 100.0f;

void Enable(void* window, int enabled) { static_cast<CUiWindow*>(window)->SetEnabled(enabled, 1); }

CUiMesh* Sprite(CMpHeroScreen* screen, int index) {
    return reinterpret_cast<CUiMesh*>(screen->window.sceneObjects_.data[index]);
}

void ShowSprites(CMpHeroScreen* screen, int first, int end, int visible) {
    for (int i = first; i < end; ++i) Sprite(screen, i)->visible_ = visible;
}

// A quarter of the frame around the figure: texels (u0, 0x1C)-(u1, 0xE4).
void SetQuarter(CMpHeroScreen* screen, int quarter, float left, float top, float right, float bottom, int u0, int u1) {
    Sprite(screen, kFirstQuarter + quarter)->SetRectangle(kQuarterDepth, kScale, left, top, right, bottom, u0, 0x1C, u1, 0xE4);
}

// The selection frame over a list's first row (the whole texture).
void SetSelectionFrame(CMpHeroScreen* screen, float left, float top, float right, float bottom) {
    Sprite(screen, kSelectionFrame)->SetRectangle(kFrameDepth, kScale, left, top, right, bottom, 0, 0, 0x100, 0x100);
}

// The rows of a list as regions: `height` apart from `top` down to kRowsBottom.
void AddRows(CMpHeroScreen* screen, int left, int right, int top, int height) {
    int y = top;
    do {
        const int next = y + height;
        screen->window.AddRegion()->CallSetRect(left, y, right, next);
        y = next;
    } while (y < kRowsBottom);
}

} // namespace

void CMpHeroScreen::SetModeVanilla(int mode) {
    CUiModel* menu = window.models_.data[0];
    menu->ShowAllParts(0);
    Enable(weaponPanel_, 0);
    Enable(spellPanel_, 0);
    Enable(tradePanel_, 0);
    Enable(statsPanel_, 0);
    Enable(figureStrips_[0], 0);
    Enable(figureStrips_[1], 0);
    Enable(heroScroll_, 0);
    Enable(voiceScroll_, 0);
    Enable(iconWindow_, 0);
    for (int i = 0; i < window.sceneObjects_.size; ++i) Sprite(this, i)->visible_ = 0;
    for (int i = 0; i < window.surfaces_.size; ++i) window.surfaces_.data[i]->Freeze(0, 1);
    while (window.regions_.size > kFixedRegions) window.RemoveRegionAt(kFixedRegions);
    for (int i = 0; i < window.regions_.size; ++i) window.regions_.data[i]->SetTip(0, 1, 0);

    CUiRegion** regions = window.regions_.data;
    if (mode == kModeHeroList) {
        menu->ShowPart(game::Text(0x007939E8), 1);   // "but03"
        menu->ShowPart(game::Text(0x007939D0), 1);   // "button03_multiplayer"
        Enable(heroScroll_, 1);
        for (int i = 0; i < window.sceneObjects_.size; ++i) Sprite(this, i)->visible_ = 1;
        ShowSprites(this, kFirstNewHeroSprite, kSpritesEnd, 0);
        SetQuarter(this, 0, 410.0f, 100.0f, 555.0f, 300.0f, 0x53, 0xE4);
        SetQuarter(this, 1, 555.0f, 100.0f, 700.0f, 300.0f, 0x1C, 0xAD);
        SetQuarter(this, 2, 410.0f, 300.0f, 555.0f, 500.0f, 0x53, 0xE4);
        SetQuarter(this, 3, 555.0f, 300.0f, 700.0f, 500.0f, 0x1C, 0xAD);
        regions[0]->CallSetRect(563, 526, 637, 574);
        regions[1]->CallSetRect(163, 526, 237, 574);
        regions[2]->CallSetRect(455, 470, 485, 500);
        regions[3]->CallSetRect(625, 470, 655, 500);
        regions[0]->SetTip(0x9CA5, 1, 0);
        regions[1]->SetTip(0x9CA4, 1, 0);
        for (int i = 5; i < kFixedRegions; ++i) window.regions_.data[i]->SetTip(i + 0x9D03, 1, 0);
        window.surfaces_.data[0]->Freeze(1, 1);
        window.surfaces_.data[1]->Freeze(1, 1);
        Enable(nameEdit_, 0);
        nameEdit_->SetFocused(false);
        SetSelectionFrame(this, 110.0f, 250.0f, 370.0f, 274.0f);
        AddRows(this, 110, 370, 250, 24);
        CallShowHero(heroIndex_);
    } else if (mode == kModeNewHero) {
        Enable(voiceScroll_, 1);
        ShowSprites(this, kFirstQuarter, kSpritesEnd, 1);
        SetQuarter(this, 0, 200.0f, 100.0f, 400.0f, 300.0f, 0x1C, 0xE4);
        SetQuarter(this, 1, 400.0f, 100.0f, 600.0f, 300.0f, 0x1C, 0xE4);
        SetQuarter(this, 2, 200.0f, 300.0f, 400.0f, 500.0f, 0x1C, 0xE4);
        SetQuarter(this, 3, 400.0f, 300.0f, 600.0f, 500.0f, 0x1C, 0xE4);
        Enable(iconWindow_, 1);
        iconWindow_->PlaceBars(300);
        Enable(statsPanel_, 1);
        Enable(figureStrips_[0], 1);
        Enable(figureStrips_[1], 1);
        regions[0]->CallSetRect(620, 430, 660, 470);
        regions[1]->CallSetRect(740, 430, 780, 470);
        regions[2]->CallSetRect(300, 470, 330, 500);
        regions[3]->CallSetRect(470, 470, 500, 500);
        regions[0]->SetTip(0x9D6D, 1, 0);
        regions[1]->SetTip(0x9D6C, 1, 0);
        regions[4]->SetTip(0x9D6E, 1, 0);
        Enable(nameEdit_, 1);
        nameEdit_->SetFocused(true);
        window.surfaces_.data[2]->Freeze(1, 1);
        SetSelectionFrame(this, 15.0f, 310.0f, 180.0f, 325.0f);
        AddRows(this, 10, 180, 325, 15);
        CallDrawVoices();
    } else if (mode == kModeStatistics) {
        ShowSprites(this, kFirstNewHeroSprite, kSpritesEnd, 1);
        Enable(iconWindow_, 1);
        iconWindow_->PlaceBars(400);
        Enable(weaponPanel_, 1);
        Enable(spellPanel_, 1);
        Enable(tradePanel_, 1);
        Enable(statsPanel_, 1);
        regions[0]->CallSetRect(620, 430, 660, 470);
        regions[1]->CallSetRect(740, 430, 780, 470);
        regions[0]->SetTip(0x9E35, 1, 0);
        regions[1]->SetTip(0x9E34, 1, 0);
        regions[4]->SetTip(0x9E36, 1, 0);
        window.surfaces_.data[2]->Freeze(1, 1);
        Enable(nameEdit_, 0);
        nameEdit_->SetFocused(false);
        tradePanel_->SelectHero(0);
    }
    static_cast<CItemInfoPanel*>(descPanel_)->Describe(nullptr);
    statsPanel_->ShowStats(mode);
    mode_ = mode;
    // Rebirth's WideScreen (addon's handler after the original): the curtains shown but on the list.
    widescreen::ShowHeroCurtains(&window, mode);
}
EI_REPLACE_METHOD(0x00615D30, CMpHeroScreen::SetModeVanilla);
