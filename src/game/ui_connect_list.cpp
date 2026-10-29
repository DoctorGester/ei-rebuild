// The multiplayer connection screen's list (CMpConnectScreen::DrawListVanilla): the servers' rows, the new
// game's controls and the selected entry's details, drawn on the screen's first surface. Rebirth patches it
// twice and both are reproduced behind their flags: WideScreen (the line under the titles scaled without the
// margin) and ExtendedServerName (wider columns, the servers' names and the new game's row drawn shortened
// with an ellipsis, the selected server's name wrapped). See ui_screens.h and docs/notes/ui.md, section 5.
#include "asset_text.h"
#include "ui.h"
#include "ui_screens.h"

#include "net_classes.h"

#include "ei/rebirth.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include "game_data.h"

#include <cstring>
#include <format>

namespace {

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

// ExtendedServerName (the addon's replacement of the draw calls'
// constants): vanilla / extended.
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

// Where the sort arrow stands for each column pair of sortKey.
float SortArrowX(int column) {
    switch (column) {
    case 0: return 112.0f;
    case 1: return 242.0f;
    case 2: return 270.0f;
    case 3: return 340.0f;
    }
    return 0.0f;   // (not reached with keys 1-8)
}

void DrawLabel(CUiSurface* surface, std::string_view text, RECT* rect, int format) {
    surface->DrawTextIn(text, rect, kFont, g_connectTextColour, format, 1);
}
// addon's wrapper of the text draw: the text goes through a 90-byte buffer (strcpy_s there; a longer one
// would hit the CRT's invalid parameter handler, here it is cut at a character's end) and
// is drawn with an ellipsis.
void DrawEllipsis(CUiSurface* surface, std::string_view text, RECT* rect, int format) {
    char buffer[kExtendedTextSize];
    text::CopyFitting(buffer, kExtendedTextSize, text);
    DrawLabel(surface, buffer, rect, format | kFormatEllipsis);
}
void Down(RECT* rect, int dy = kRowHeight) { gameapi::OffsetRect(rect, 0, dy); }

} // namespace

void CMpConnectScreen::DrawListVanilla() {
    auto sprite = [this](int index) { return SceneMesh(index); };
    auto label = [this](int index) -> const char* { return texts[index].c_str(); };
    auto allod = [this](int index) -> const char* { return allodNames[index].c_str(); };

    const bool newGame = selected == 0;
    const int show = newGame ? 1 : 0;
    playersSlider->SetEnabled(show);
    nameEdit->SetEnabled(show);
    nameEdit->SetFocused(show != 0);
    sprite(kSpriteUnlisted)->visible = show;
    sprite(kSpritePassword)->visible = show;
    if (!newGame) {
        regions[kRegionUnlisted]->SetTip(0);
        regions[kRegionPassword]->SetTip(0);
    } else if (!newGameTips) {
        regions[kRegionUnlisted]->SetTip(kTipUnlisted);
        regions[kRegionPassword]->SetTip(kTipPassword);
    }
    newGameTips = newGame;

    const bool extended = ei::rebirth::Enabled(ei::rebirth::Patch::ExtendedServerName);
    const Layout& layout = extended ? kExtendedLayout : kVanillaLayout;
    std::string text;
    CUiSurface* surface = surfaces[0];
    surface->Clear();
    sprite(kSpriteSelection)->visible = false;
    // The two buttons' texts.
    RECT button{390, 345, 520, 365};
    DrawLabel(surface, label(10), &button, kFormatCentre);
    button = RECT{390, 369, 520, 389};
    DrawLabel(surface, label(11), &button, kFormatCentre);
    // The column titles.
    RECT row{20, 8, 290, 29};
    DrawLabel(surface, label(12), &row, 0);
    row.left = 150;
    DrawLabel(surface, label(13), &row, 0);
    row.left = 178;
    DrawLabel(surface, label(14), &row, 0);
    row.left = 248;
    DrawLabel(surface, label(15), &row, 0);
    row.left = 12;
    row.right = layout.rowRight;
    CUiMesh* arrow = sprite(kSpriteSortArrow);
    if (sortKey % 2 == 0)
        arrow->SetRectangle(4.0f, 100.0f, 0.0f, 0.0f, 10.0f, 12.0f, 0x28, 0x80, 0x50, 0xB0);
    else
        arrow->SetRectangle(4.0f, 100.0f, 0.0f, 0.0f, 10.0f, 12.0f, 0x28, 0xB0, 0x50, 0x80);
    if (sortKey != 0) {
        arrow->visible = true;
        arrow->Place(SortArrowX((sortKey - 1) / 2), 116.0f, 4.0f);
    }
    // The line under the titles.
    POINT from{8, row.bottom - 2};
    POINT to{layout.lineRight, row.bottom - 2};
    // WideScreen: without the margin.
    ui->CallScalePointToScreenNoMargin(&from);
    ui->CallScalePointToScreenNoMargin(&to);
    surface->Line(from.x, from.y, to.x, to.y, 1, g_connectTextColour);
    Down(&row, 27);

    unsigned index = static_cast<unsigned>(scrollTop);
    for (int y = kRowsTop; y < kRowsBottom; y += kRowHeight, ++index) {
        if (index >= static_cast<unsigned>(entries.size())) break;
        if (index == static_cast<unsigned>(selected)) {
            sprite(kSpriteSelection)->visible = true;
            sprite(kSpriteSelection)->Place(layout.selectionX, static_cast<float>(y), 3.0f);
        }
        const MpAddressEntry& entry = entries[index];
        g_connectTextColour = entry.state == kStateAsked ? kColourAsked : entry.state == kState2 ? kColourState2 : kColourPlain;
        if (index == 0) {
            // The new game: its name as typed.
            text = std::format("{} {}", label(7), nameEdit->text);
            if (extended)
                DrawEllipsis(surface, text, &row, kFormatNoPrefix);
            else
                DrawLabel(surface, text, &row, kFormatNoPrefix);
        } else {
            if (extended) {
                RECT nameRect = row;   // (addon's wrapper draws a copy with the right edge at 140)
                nameRect.right = kExtendedNameRight;
                DrawEllipsis(surface, entry.name, &nameRect, kFormatNoPrefix);
            } else {
                DrawLabel(surface, entry.name, &row, kFormatNoPrefix);
            }
            row.left = 142;
            text = std::format("{}/{}", entry.players, entry.maxPlayers);
            DrawLabel(surface, text, &row, 0);
            row.left = 170;
            if (entry.gameType == kNewGameType)
                text = "?";
            else
                text = std::format("{}", allod(entry.gameType));
            DrawLabel(surface, text, &row, 0);
            row.left = layout.rowLeftPing;
            text = std::format("{}", entry.ping);
            DrawLabel(surface, text, &row, kFormatRight);
            row.left = 12;
        }
        Down(&row);
    }

    for (int i = kSpriteTabFirst; i < kSpriteTabEnd; ++i) sprite(i)->visible = false;
    if (selected >= 0 && static_cast<unsigned>(selected) < static_cast<unsigned>(entries.size())) {
        // The selected entry's details.
        g_connectTextColour = kColourPlain;
        const MpAddressEntry& entry = entries[selected];
        row = RECT{340, 20, 590, 44};
        if (selected == 0) {
            for (int i = kSpriteTabFirst; i < kSpriteTabEnd; ++i) sprite(i)->visible = true;
            DrawLabel(surface, label(0), &row, 0);
            Down(&row);
            Down(&row);
            text = std::format("{} {}", label(2), maxPlayers);
            DrawLabel(surface, text, &row, 0);
            Down(&row);
            Down(&row);
            DrawLabel(surface, label(3), &row, 0);
            Down(&row);
            row.left = 390;
            for (int i = 0; i < kGameTypes; ++i) {
                DrawLabel(surface, allod(i), &row, 0);
                Down(&row);
            }
            row.left = 370;
            Down(&row, 12);
            DrawLabel(surface, label(16), &row, 0);
            Down(&row);
            DrawLabel(surface, label(17), &row, 0);
        } else {
            text = std::format("{} {}", label(0), entry.name);
            if (extended) {
                // addon: wrapped over up to three lines; the rectangle moves down by the extra lines.
                int lines = surface->DrawWrappedText(text, &row, kDetailsNameLines, kFont, g_connectTextColour, 0, 1);
                row.top += lines * kRowHeight - kRowHeight;
                row.bottom += lines * kRowHeight - kRowHeight;
            } else {
                DrawLabel(surface, text, &row, 0);
            }
            Down(&row);
            text = std::format("{} {} / {}", label(1), entry.players, entry.maxPlayers);
            DrawLabel(surface, text, &row, 0);
            Down(&row);
            if (entry.gameType == kNewGameType)
                text = texts[3] + " ?";
            else
                text = std::format("{} {}", label(3), allod(entry.gameType));
            DrawLabel(surface, text, &row, 0);
            Down(&row);
            text = std::format("{} {}", label(4), entry.description);
            int lines = surface->DrawWrappedText(text, &row, 3, kFont, g_connectTextColour, 0, 1);
            for (; lines != 0; --lines) Down(&row);
            std::string address = entry.address.ToString();
            text = std::format("{} {}", label(5), address.data());
            DrawLabel(surface, text, &row, 0);
            Down(&row);
            text = std::format("{} {}", label(6), entry.ping);
            DrawLabel(surface, text, &row, 0);
            Down(&row);
            if (entry.password) {
                DrawLabel(surface, label(21), &row, 0);
                Down(&row);
            }
        }
    }
}
