// The multiplayer connection screen's list (MpConnectScreen::DrawListVanilla and PaintList): the servers' rows, the
// new game's controls and the selected entry's details, painted on the screen's first surface. Rebirth patches it
// twice and both are reproduced: WideScreen for every window (the line under the titles scaled without the
// margin) and ExtendedServerName behind its flag (wider columns, the servers' names and the new game's row drawn shortened
// with an ellipsis, the selected server's name wrapped). Ours: the host's mod in the details, and the rows of hosts
// this game cannot join for the mods dimmed. See ui_screens.h and docs/notes/ui.md, section 5.
#include "game/core/asset_text.h"
#include "game/ui/ui.h"
#include "game/ui/ui_scrollbar.h"
#include "game/ui/ui_mesh.h"
#include "game/ui/ui_screens.h"

#include "game/net/mod_identity.h"
#include "game/net/net_session.h"

#include "lib/crt.h"
#include "lib/rect.h"
#include "game/ui/screen_layout.h"
#include <cstddef>
#include <cstdint>
#include <string>

#include <cstring>
#include <format>

namespace {

constexpr std::uint32_t kColourAsked = 0x31B3FF;    // an entry in state 1 (asked directly)
constexpr std::uint32_t kColourState2 = 0x31E3EE;   // in state 2
constexpr int kStateAsked = 1;
constexpr int kState2 = 2;
constexpr int kFont = 1;
constexpr int kRowHeight = 24;
constexpr int kRowsTop = 144;
constexpr int kRowsBottom = 504;
constexpr int kTipUnlisted = 0x9EA4, kTipPassword = 0x9EA5;
constexpr int kNewGameType = 4;
constexpr int kGameTypes = 4;

// Rebirth's ExtendedServerName layout (the original's: 265, 270, 220, 239).
struct Layout {
    int rowRight = 0, lineRight = 0, rowLeftPing = 0;
    float selectionX = 0.0f;
};
constexpr Layout kLayout{278, 283, 233, 244.75f};
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

std::uint32_t RowColour(const MpAddressEntry& entry) {
    return entry.state == kStateAsked ? kColourAsked : entry.state == kState2 ? kColourState2 : kUiTextColour;
}
// Ours: a row's texts, dimmed for a host this game cannot join for the mods.
constexpr std::uint32_t kColourCannotJoin = 0x808080;
std::uint32_t ShownRowColour(const MpAddressEntry& entry) {
    return entry.joinable ? RowColour(entry) : kColourCannotJoin;
}

void DrawLabel(UiSurface& surface, std::string_view text, Rect* rect, render::text::Format format, std::uint32_t colour) {
    surface.DrawTextIn(text, *rect, TextStyle{kFont, colour, format});
}
// addon's wrapper of the text draw: the text goes through a 90-byte buffer (strcpy_s there; a longer one
// would hit the CRT's invalid parameter handler, here it is cut at a character's end) and
// is drawn with an ellipsis.
void DrawEllipsis(UiSurface& surface, std::string_view text, Rect* rect, render::text::Format format, std::uint32_t colour) {
    char buffer[kExtendedTextSize];
    text::CopyFitting(buffer, kExtendedTextSize, text);
    DrawLabel(surface, buffer, rect, format | render::text::Format::EndEllipsis, colour);
}
void Down(Rect* rect, int dy = kRowHeight) { rect->Offset(0, dy); }

// The new game's pause modes (net::PauseMode, in order) and the switch's tip: ours, in Russian where the game's
// texts are (GameTextsInRussian), else in English.
struct AddedText {
    const char* russian = nullptr;
    const char* english = nullptr;
};
constexpr AddedText kPauseModeTexts[] = {
    {"Пауза: как в оригинале", "Pause: as the original"},
    {"Пауза: ведущий, с объявлением", "Pause: the leader, announced"},
    {"Пауза: любой игрок", "Pause: any player"},
    {"Пауза: нет", "Pause: off"},
};
constexpr AddedText kPauseTip = {
    "Кто может ставить игру на паузу. Как в оригинале: только ведущий, пауза объявляется. Ведущий, с объявлением: "
    "объявляются и пауза, и продолжение. Любой игрок: каждый, с объявлением. Нет: пауз нет",
    "Who may pause the game. As the original: the leader only, the pause announced. The leader, announced: pauses and "
    "resumes announced. Any player: everyone, announced. Off: no pauses"};
// Ours: the new game's companions switch (off, or the most a player may add) and its tip.
constexpr AddedText kCompanionTexts[] = {
    {"Спутники: нет", "Companions: off"},
    {"Спутники: два", "Companions: two"},
};
constexpr AddedText kCompanionTip = {
    "В зоне каждый игрок может взять с собой двух своих других героев (F3). Видно только клиентам этой программы: "
    "игроки оригинальной игры играют одним героем",
    "In a zone each player may take two of their other saved heroes along (F3). Only this program's clients see it: "
    "players of the original game play one hero"};
const char* Shown(const AddedText& text) { return GameTextsInRussian() ? text.russian : text.english; }

} // namespace

void MpConnectScreen::DrawListVanilla() {
    titleColour = g_connectTextColour;   // what the last drawing left

    const bool newGame = selected == 0;
    const int show = newGame ? 1 : 0;
    playersSlider->SetEnabled(show);
    nameEdit->SetEnabled(show);
    nameEdit->SetFocused(show != 0);
    unlistedBox->visible = show;
    passwordBox->visible = show;
    if (!newGame) {
        unlistedSwitch->SetTip(0);
        passwordSwitch->SetTip(0);
        pauseSwitch->SetTip(0);
        companionSwitch->SetTip(0);
    } else if (!newGameTips) {
        companionSwitch->SetTipString(Shown(kCompanionTip));
        unlistedSwitch->SetTip(kTipUnlisted);
        passwordSwitch->SetTip(kTipPassword);
        pauseSwitch->SetTipString(Shown(kPauseTip));
    }
    newGameTips = newGame;

    const Layout& layout = kLayout;
    selectionMarker->visible = false;
    if (sortKey % 2 == 0)
        sortArrow->SetRectangle(4.0f, kSpriteScale, 0.0f, 0.0f, 10.0f, 12.0f, 0x28, 0x80, 0x50, 0xB0);
    else
        sortArrow->SetRectangle(4.0f, kSpriteScale, 0.0f, 0.0f, 10.0f, 12.0f, 0x28, 0xB0, 0x50, 0x80);
    if (sortKey != 0) {
        sortArrow->visible = true;
        sortArrow->Place(SortArrowX((sortKey - 1) / 2), 116.0f, 4.0f);
    }

    // The selection's marker. The text colour is left as the drawing left it: the last shown row's, or the plain one
    // after a selected entry's details (the titles and the connecting note are drawn in it).
    unsigned index = static_cast<unsigned>(scrollTop);
    for (int y = kRowsTop; y < kRowsBottom; y += kRowHeight, ++index) {
        if (index >= static_cast<unsigned>(entries.size())) break;
        if (index == static_cast<unsigned>(selected)) {
            selectionMarker->visible = true;
            selectionMarker->Place(layout.selectionX, static_cast<float>(y), 3.0f);
        }
        g_connectTextColour = RowColour(entries[index]);
    }

    for (UiMesh* box : tabBoxes) box->visible = false;
    if (selected >= 0 && static_cast<unsigned>(selected) < static_cast<unsigned>(entries.size())) {
        g_connectTextColour = kUiTextColour;
        if (selected == 0)
            for (UiMesh* box : tabBoxes) box->visible = true;
    }
}

void MpConnectScreen::PaintList(UiSurface& surface) const {
    auto label = [this](int index) -> const std::string& { return texts[index]; };
    auto allod = [this](int index) -> const std::string& { return allodNames[index]; };

    const Layout& layout = kLayout;
    std::string text;
    std::uint32_t colour = titleColour;
    // The two buttons' texts.
    Rect button{390, 345, 520, 365};
    DrawLabel(surface, label(10), &button, render::text::Format::Center, colour);
    button = Rect{390, 369, 520, 389};
    DrawLabel(surface, label(11), &button, render::text::Format::Center, colour);
    // The column titles.
    Rect row{20, 8, 290, 29};
    DrawLabel(surface, label(12), &row, render::text::Format::Left, colour);
    row.left = 150;
    DrawLabel(surface, label(13), &row, render::text::Format::Left, colour);
    row.left = 178;
    DrawLabel(surface, label(14), &row, render::text::Format::Left, colour);
    row.left = 248;
    DrawLabel(surface, label(15), &row, render::text::Format::Left, colour);
    row.left = 12;
    row.right = layout.rowRight;
    // The line under the titles.
    Point from{8, row.bottom - 2};
    Point to{layout.lineRight, row.bottom - 2};
    // WideScreen: without the margin.
    from = screen_layout::ToScreenNoMargin(from);
    to = screen_layout::ToScreenNoMargin(to);
    surface.Line(from, to, 1, colour);
    Down(&row, 27);

    unsigned index = static_cast<unsigned>(scrollTop);
    for (int y = kRowsTop; y < kRowsBottom; y += kRowHeight, ++index) {
        if (index >= static_cast<unsigned>(entries.size())) break;
        const MpAddressEntry& entry = entries[index];
        colour = ShownRowColour(entry);
        if (index == 0) {
            // The new game: its name as typed.
            text = std::format("{} {}", label(7), nameEdit->text);
            DrawEllipsis(surface, text, &row, render::text::Format::NoPrefix, colour);
        } else {
            Rect nameRect = row;   // (addon's wrapper draws a copy with the right edge at 140)
            nameRect.right = kExtendedNameRight;
            DrawEllipsis(surface, entry.name, &nameRect, render::text::Format::NoPrefix, colour);
            row.left = 142;
            text = std::format("{}/{}", entry.players, entry.maxPlayers);
            DrawLabel(surface, text, &row, render::text::Format::Left, colour);
            row.left = 170;
            if (entry.gameType == kNewGameType)
                text = "?";
            else
                text = std::format("{}", allod(entry.gameType));
            DrawLabel(surface, text, &row, render::text::Format::Left, colour);
            row.left = layout.rowLeftPing;
            text = std::format("{}", entry.ping);
            DrawLabel(surface, text, &row, render::text::Format::Right, colour);
            row.left = 12;
        }
        Down(&row);
    }

    if (selected < 0 || static_cast<unsigned>(selected) >= static_cast<unsigned>(entries.size())) return;
    // The selected entry's details.
    colour = kUiTextColour;
    const MpAddressEntry& entry = entries[selected];
    row = Rect{340, 20, 590, 44};
    if (selected == 0) {
        DrawLabel(surface, label(0), &row, render::text::Format::Left, colour);
        Down(&row);
        Down(&row);
        text = std::format("{} {}", label(2), maxPlayers);
        DrawLabel(surface, text, &row, render::text::Format::Left, colour);
        Down(&row);
        Down(&row);
        DrawLabel(surface, label(3), &row, render::text::Format::Left, colour);
        {
            Rect companionRow{460, row.top, 600, row.bottom};
            DrawLabel(surface, Shown(kCompanionTexts[companions != 0 ? 1 : 0]), &companionRow,
                      render::text::Format::Left, colour);
        }
        Down(&row);
        row.left = 390;
        for (int i = 0; i < kGameTypes; ++i) {
            DrawLabel(surface, allod(i), &row, render::text::Format::Left, colour);
            Down(&row);
        }
        row.left = 370;
        Down(&row, 12);
        DrawLabel(surface, label(16), &row, render::text::Format::Left, colour);
        Down(&row);
        DrawLabel(surface, label(17), &row, render::text::Format::Left, colour);
        Down(&row);
        row.left = 340;
        DrawLabel(surface, Shown(kPauseModeTexts[static_cast<int>(pauseMode)]), &row, render::text::Format::Left,
                  colour);
        return;
    }
    text = std::format("{} {}", label(0), entry.name);
    // Wrapped over up to three lines; the rectangle moves down by the extra lines.
    const int nameLines = surface.DrawWrappedText(text, row, kDetailsNameLines, TextStyle{kFont, colour});
    row.top += nameLines * kRowHeight - kRowHeight;
    row.bottom += nameLines * kRowHeight - kRowHeight;
    Down(&row);
    text = std::format("{} {} / {}", label(1), entry.players, entry.maxPlayers);
    DrawLabel(surface, text, &row, render::text::Format::Left, colour);
    Down(&row);
    if (entry.gameType == kNewGameType)
        text = texts[3] + " ?";
    else
        text = std::format("{} {}", label(3), allod(entry.gameType));
    DrawLabel(surface, text, &row, render::text::Format::Left, colour);
    Down(&row);
    text = std::format("{} {}", label(4), entry.description);
    int lines = surface.DrawWrappedText(text, row, 3, TextStyle{kFont, colour});
    for (; lines != 0; --lines) Down(&row);
    std::string address = entry.address.ToString();
    text = std::format("{} {}", label(5), address.data());
    DrawLabel(surface, text, &row, render::text::Format::Left, colour);
    Down(&row);
    text = std::format("{} {}", label(6), entry.ping);
    DrawLabel(surface, text, &row, render::text::Format::Left, colour);
    Down(&row);
    if (entry.password) {
        DrawLabel(surface, label(21), &row, render::text::Format::Left, colour);
        Down(&row);
    }
    if (!entry.modRead) return;
    // Ours: the host's mod, and why this game cannot join it.
    const modidentity::Listing listing = modidentity::Listed(entry.mod);
    DrawEllipsis(surface, listing.mod, &row, render::text::Format::NoPrefix, colour);
    Down(&row);
    if (!listing.cannotJoin.empty()) {
        DrawEllipsis(surface, listing.cannotJoin, &row, render::text::Format::NoPrefix, kColourCannotJoin);
        Down(&row);
    }
}
