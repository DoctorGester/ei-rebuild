// The multiplayer connection screen's list (CMpConnectScreen::DrawListVanilla 0061D780): the
// servers' rows, the new game's controls and the selected entry's details, drawn on the screen's
// first surface. Rebirth patches it twice and both are reproduced behind their flags: WideScreen (W2: the
// line under the titles scaled without the margin) and ExtendedServerName (W2a: wider columns, the
// servers' names and the new game's row drawn shortened with an ellipsis, the selected server's name
// wrapped); the screen's other members call it through its address. See ui_screens.h and
// docs/notes/ui.md, section 5.
#include "ui.h"
#include "ui_screens.h"
#include "game_strings.h"

#include "net_classes.h"
#include "ui_gdi.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "originals_library.h"
#include "originals_ui.h"
#include "game_data.h"

#include <cstdint>
#include <cstring>

namespace {

// The colour the list's texts and the line are drawn in (00793AD8): set per row and for the
// details, and left as it is for the next draw's titles.
std::uint32_t& TextColour() { return ei::GameData<std::uint32_t>(0x00793AD8); }

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

// ExtendedServerName (addon's replacements of the draw calls at 0061DC7F, 0061DC9E, 0061E009 and the
// constants at 0061D9FC, 0061DB23, 0061DBEF, 0061DD40): vanilla / extended.
struct Layout {
    int rowRight, lineRight, rowLeftPing;
    float selectionX;
};
constexpr Layout kVanillaLayout{265, 270, 220, 239.0f};
constexpr Layout kExtendedLayout{278, 283, 233, 244.75f};
constexpr int kFormatEllipsis = 0x18000;   // DT_END_ELLIPSIS | DT_MODIFYSTRING
constexpr std::size_t kExtendedTextSize = 90;
constexpr int kExtendedNameRight = 140;    // the servers' names are drawn within this column
constexpr int kDetailsNameLines = 3;

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
// addon's wrapper of the text draw: the text goes through a 90-byte buffer (strcpy_s there; a longer one
// would hit the CRT's invalid parameter handler, here it is cut) and is drawn with an ellipsis.
void DrawEllipsis(CUiSurface* surface, const char* text, RECT* rect, int format) {
    char buffer[kExtendedTextSize];
    strncpy_s(buffer, kExtendedTextSize, text, _TRUNCATE);
    Draw(surface, buffer, rect, format | kFormatEllipsis);
}
void Down(RECT* rect, int dy = kRowHeight) { gameapi::OffsetRect(rect, 0, dy); }

} // namespace

void CMpConnectScreen::DrawListVanilla() {
    auto sprite = [this](int index) { return reinterpret_cast<CUiMesh*>(sceneObjects_.data[index]); };
    auto label = [this](int index) -> const char* { return texts_[index]; };
    auto allod = [this](int index) -> const char* { return allodNames_[index]; };

    const bool newGame = selected_ == 0;
    const int show = newGame ? 1 : 0;
    playersSlider_->SetEnabled(show, 1);
    nameEdit_->SetEnabled(show, 1);
    nameEdit_->SetFocused(show != 0);
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

    const bool extended = ei::rebirth::Enabled(ei::rebirth::Patch::ExtendedServerName);
    const Layout& layout = extended ? kExtendedLayout : kVanillaLayout;
    mfc::CString text;
    CUiSurface* surface = surfaces_.data[0];
    ui_gdi::GameDC* dc = UiSurfaceGetDC(surface);
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
    row.right = layout.rowRight;
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
    mfc::impl::CGdiObject* oldPen = dc->SelectObject(&pen);
    POINT from{8, row.bottom - 2};
    POINT to{layout.lineRight, row.bottom - 2};
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
            sprite(kSpriteSelection)->Place(layout.selectionX, static_cast<float>(y), 3.0f);
        }
        const MpAddressEntry& entry = entries_[index];
        TextColour() = entry.state == kStateAsked ? kColourAsked : entry.state == kState2 ? kColourState2 : kColourPlain;
        if (index == 0) {
            // The new game: its name as typed.
            text.Format("%s %s", label(7), nameEdit_->text_);
            if (extended)
                DrawEllipsis(surface, text, &row, kFormatNoPrefix);
            else
                Draw(surface, text, &row, kFormatNoPrefix);
        } else {
            if (extended) {
                RECT nameRect = row;   // (addon's wrapper draws a copy with the right edge at 140)
                nameRect.right = kExtendedNameRight;
                DrawEllipsis(surface, entry.name, &nameRect, kFormatNoPrefix);
            } else {
                Draw(surface, entry.name, &row, kFormatNoPrefix);
            }
            row.left = 142;
            text.Format("%d/%d", entry.players, entry.maxPlayers);
            Draw(surface, text, &row, 0);
            row.left = 170;
            if (entry.gameType == kNewGameType)
                text = "?";
            else
                text.Format("%s", allod(entry.gameType));
            Draw(surface, text, &row, 0);
            row.left = layout.rowLeftPing;
            text.Format("%d", entry.ping);
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
            text.Format("%s %d", label(2), maxPlayers_);
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
            text.Format("%s %s", label(0), static_cast<const char*>(entry.name));
            if (extended) {
                // addon: wrapped over up to three lines; the rectangle moves down by the extra lines.
                int lines = surface->DrawWrappedText(text, &row, kDetailsNameLines, kFont, TextColour(), 0, 1);
                row.top += lines * kRowHeight - kRowHeight;
                row.bottom += lines * kRowHeight - kRowHeight;
            } else {
                Draw(surface, text, &row, 0);
            }
            Down(&row);
            text.Format("%s %d / %d", label(1), entry.players, entry.maxPlayers);
            Draw(surface, text, &row, 0);
            Down(&row);
            if (entry.gameType == kNewGameType)
                text = texts_[3] + " ?";
            else
                text.Format("%s %s", label(3), allod(entry.gameType));
            Draw(surface, text, &row, 0);
            Down(&row);
            text.Format("%s %s", label(4), static_cast<const char*>(entry.description));
            int lines = surface->DrawWrappedText(text, &row, 3, kFont, TextColour(), 0, 1);
            for (; lines != 0; --lines) Down(&row);
            stlp::string address;
            text.Format("%s %s", label(5), entry.address.ToString(&address)->data());
            Draw(surface, text, &row, 0);
            Down(&row);
            text.Format("%s %d", label(6), entry.ping);
            Draw(surface, text, &row, 0);
            Down(&row);
            if (entry.password) {
                Draw(surface, label(21), &row, 0);
                Down(&row);
            }
        }
    }
    dc->SelectObject(oldPen);
    UiSurfaceReleaseDC(surface);
    pen.Destruct();
}
EI_REPLACE_METHOD(0x0061D780, CMpConnectScreen::DrawListVanilla);
