// The multiplayer connection screen's list (CMpConnectScreen::DrawListVanilla 0061D780): the
// servers' rows, the new game's controls and the selected entry's details, drawn on the screen's
// first surface. Rebirth patches it: WideScreen is reproduced behind its flag (W2), ExtendedServerName
// is not, so it is hooked in the vanilla launch only; the screen's other members call it through its
// address. See ui_screens.h and
// docs/notes/ui.md, section 5.
#include "ui_screens.h"

#include "net_classes.h"
#include "ui_gdi.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "originals_library.h"
#include "originals_ui.h"

#include <cstdint>

namespace {

const char* GameText(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }
// The colour the list's texts and the line are drawn in (00793AD8): set per row and for the
// details, and left as it is for the next draw's titles.
std::uint32_t& TextColour() { return *reinterpret_cast<std::uint32_t*>(0x00793AD8); }

constexpr std::uint32_t kColourAsked = 0x31B3FF;    // an entry in state 1 (asked directly)
constexpr std::uint32_t kColourState2 = 0x31E3EE;   // in state 2
constexpr std::uint32_t kColourPlain = 0xA7D7E4;
constexpr int kStateAsked = 1;
constexpr int kState2 = 2;
constexpr int kFont = 1;
constexpr int kRowHeight = 24;
constexpr int kRowsTop = 144;
constexpr int kRowsBottom = 504;
constexpr int kFormatCentre = 1;       // DT_CENTER
constexpr int kFormatRight = 2;        // DT_RIGHT
constexpr int kFormatNoPrefix = 0x800; // DT_NOPREFIX
constexpr int kSpriteSelection = 2, kSpriteSortArrow = 3, kSpriteUnlisted = 4, kSpritePassword = 5, kSpriteTabFirst = 6,
              kSpriteTabEnd = 10;
constexpr int kRegionUnlisted = 4, kRegionPassword = 5;
constexpr int kTipUnlisted = 0x9EA4, kTipPassword = 0x9EA5;
constexpr int kNewGameType = 4;
constexpr int kGameTypes = 4;

// Where the sort arrow stands for each column pair of sortKey_.
float SortArrowX(int column) {
    switch (column) {
    case 0: return 112.0f;
    case 1: return 242.0f;
    case 2: return 270.0f;
    case 3: return 340.0f;
    }
    return 0.0f;   // (not reached with keys 1-8; the original leaves a stack slot as it is)
}

void Draw(CUiSurface* surface, const char* text, RECT* rect, int format) {
    SurfaceDrawText(surface, nullptr, text, rect, kFont, TextColour(), format, 1);
}
void Down(RECT* rect, int dy = kRowHeight) { gameapi::OffsetRect(rect, 0, dy); }

} // namespace

void CMpConnectScreen::DrawListVanilla() {
    auto sprite = [this](int index) { return reinterpret_cast<CUiMesh*>(sceneObjects_.data[index]); };
    auto label = [this](int index) -> const char* { return texts_[index]; };
    auto allod = [this](int index) -> const char* { return allodNames_[index]; };

    const bool newGame = selected_ == 0;
    const int show = newGame ? 1 : 0;
    WindowSetEnabled(playersSlider_, nullptr, show, 1);
    WindowSetEnabled(nameEdit_, nullptr, show, 1);
    EditFieldActivateOriginal(nameEdit_, nullptr, show);
    sprite(kSpriteUnlisted)->visible_ = show;
    sprite(kSpritePassword)->visible_ = show;
    if (!newGame) {
        regions_.data[kRegionUnlisted]->SetTip(0, 1, 0);
        regions_.data[kRegionPassword]->SetTip(0, 1, 0);
    } else if (!newGameTips_) {
        regions_.data[kRegionUnlisted]->SetTip(kTipUnlisted, 1, 0);
        regions_.data[kRegionPassword]->SetTip(kTipPassword, 1, 0);
    }
    newGameTips_ = newGame;

    mfc::CString text;
    CUiSurface* surface = surfaces_.data[0];
    ui_gdi::GameDC* dc = SurfaceGetDC(surface);
    surface->Clear();
    sprite(kSpriteSelection)->visible_ = 0;
    // The two buttons' texts.
    RECT button{390, 345, 520, 365};
    Draw(surface, label(10), &button, kFormatCentre);
    button = RECT{390, 369, 520, 389};
    Draw(surface, label(11), &button, kFormatCentre);
    // The column titles.
    RECT row{20, 8, 290, 29};
    Draw(surface, label(12), &row, 0);
    row.left = 150;
    Draw(surface, label(13), &row, 0);
    row.left = 178;
    Draw(surface, label(14), &row, 0);
    row.left = 248;
    Draw(surface, label(15), &row, 0);
    row.left = 12;
    row.right = 265;
    CUiMesh* arrow = sprite(kSpriteSortArrow);
    if (sortKey_ % 2 == 0)
        arrow->SetRectangle(4.0f, 100.0f, 0.0f, 0.0f, 10.0f, 12.0f, 0x28, 0x80, 0x50, 0xB0);
    else
        arrow->SetRectangle(4.0f, 100.0f, 0.0f, 0.0f, 10.0f, 12.0f, 0x28, 0xB0, 0x50, 0x80);
    if (sortKey_ != 0) {
        arrow->visible_ = 1;
        arrow->Place(SortArrowX((sortKey_ - 1) / 2), 116.0f, 4.0f);
    }
    // The line under the titles.
    ui_gdi::GamePen pen;
    pen.Construct(PS_SOLID, 1, TextColour());
    void* oldPen = dc->SelectPen(&pen);
    POINT from{8, row.bottom - 2};
    POINT to{270, row.bottom - 2};
    // WideScreen (0061DB0E / 0061DB3E: XOR ECX,ECX): without the margin.
    interface_->CallScalePointToScreenNoMargin(&from);
    interface_->CallScalePointToScreenNoMargin(&to);
    dc->MoveTo(from.x, from.y);
    dc->LineTo(to.x, to.y);
    Down(&row, 27);

    unsigned index = static_cast<unsigned>(scrollTop_);
    for (int y = kRowsTop; y < kRowsBottom; y += kRowHeight, ++index) {
        if (index >= static_cast<unsigned>(entries_.size())) break;
        if (index == static_cast<unsigned>(selected_)) {
            sprite(kSpriteSelection)->visible_ = 1;
            sprite(kSpriteSelection)->Place(239.0f, static_cast<float>(y), 3.0f);
        }
        const MpAddressEntry& entry = entries_[index];
        TextColour() = entry.state == kStateAsked ? kColourAsked : entry.state == kState2 ? kColourState2 : kColourPlain;
        if (index == 0) {
            // The new game: its name as typed.
            text.Format(GameText(0x00792F34), label(7), nameEdit_->text_);   // "%s %s"
            Draw(surface, text, &row, kFormatNoPrefix);
        } else {
            Draw(surface, entry.name, &row, kFormatNoPrefix);
            row.left = 142;
            text.Format(GameText(0x00793BB8), entry.players, entry.maxPlayers);   // "%d/%d"
            Draw(surface, text, &row, 0);
            row.left = 170;
            if (entry.gameType == kNewGameType)
                text = GameText(0x00783D6C);   // "?"
            else
                text.Format(GameText(0x0079190C), allod(entry.gameType));   // "%s"
            Draw(surface, text, &row, 0);
            row.left = 220;
            text.Format(GameText(0x0078EEEC), entry.ping);   // "%d"
            Draw(surface, text, &row, kFormatRight);
            row.left = 12;
        }
        Down(&row);
    }

    for (int i = kSpriteTabFirst; i < kSpriteTabEnd; ++i) sprite(i)->visible_ = 0;
    if (selected_ >= 0 && static_cast<unsigned>(selected_) < static_cast<unsigned>(entries_.size())) {
        // The selected entry's details.
        TextColour() = kColourPlain;
        const MpAddressEntry& entry = entries_[selected_];
        row = RECT{340, 20, 590, 44};
        if (selected_ == 0) {
            for (int i = kSpriteTabFirst; i < kSpriteTabEnd; ++i) sprite(i)->visible_ = 1;
            Draw(surface, label(0), &row, 0);
            Down(&row);
            Down(&row);
            text.Format(GameText(0x00792F14), label(2), maxPlayers_);   // "%s %d"
            Draw(surface, text, &row, 0);
            Down(&row);
            Down(&row);
            Draw(surface, label(3), &row, 0);
            Down(&row);
            row.left = 390;
            for (int i = 0; i < kGameTypes; ++i) {
                Draw(surface, allod(i), &row, 0);
                Down(&row);
            }
            row.left = 370;
            Down(&row, 12);
            Draw(surface, label(16), &row, 0);
            Down(&row);
            Draw(surface, label(17), &row, 0);
        } else {
            text.Format(GameText(0x00792F34), label(0), static_cast<const char*>(entry.name));   // "%s %s"
            Draw(surface, text, &row, 0);
            Down(&row);
            text.Format(GameText(0x00793BAC), label(1), entry.players, entry.maxPlayers);   // "%s %d / %d"
            Draw(surface, text, &row, 0);
            Down(&row);
            if (entry.gameType == kNewGameType)
                text = texts_[3] + GameText(0x00793BA8);   // " ?"
            else
                text.Format(GameText(0x00792F34), label(3), allod(entry.gameType));
            Draw(surface, text, &row, 0);
            Down(&row);
            text.Format(GameText(0x00792F34), label(4), static_cast<const char*>(entry.description));
            int lines = surface->DrawWrappedText(text, &row, 3, kFont, TextColour(), 0, 1);
            for (; lines != 0; --lines) Down(&row);
            net::StringRep address;
            text.Format(GameText(0x00792F34), label(5), entry.address.ToString(&address)->start);
            if (address.start) {
                NodeDeallocateOriginal(address.start, static_cast<std::size_t>(address.eos - address.start));
            }
            Draw(surface, text, &row, 0);
            Down(&row);
            text.Format(GameText(0x00792F14), label(6), entry.ping);
            Draw(surface, text, &row, 0);
            Down(&row);
            if (entry.password) {
                Draw(surface, label(21), &row, 0);
                Down(&row);
            }
        }
    }
    dc->SelectPen(oldPen);
    SurfaceReleaseDC(surface);
    pen.Destruct();
}
EI_REPLACE_METHOD(0x0061D780, CMpConnectScreen::DrawListVanilla);
// (Rebirth's ExtendedServerName patches it too, not reproduced: in a Rebirth launch it stays skipped.)
EI_REBIRTH_IMPLEMENTS(0x0061D780, ei::rebirth::Patch::WideScreen);
