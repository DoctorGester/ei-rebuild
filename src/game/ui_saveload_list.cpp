// The save/load window's list (SaveLoadWindow::RefreshList): the visible rows with their dates, the name
// being typed with its caret, the selection's marker and the selected save's details, all drawn into the
// window's first surface. Rebirth's WideScreen patches it (the caret's points scaled without the margin),
// which this reproduces behind the flag. Rebirth-patched callees: the surface's text, the text extent, the
// point scaling and the sprite's Place. See ui.h (SaveLoadWindow) and docs/notes/items_spells.md.
#include "ui.h"

#include "text_manager.h"       // TextManager
#include "game_singletons.h"
#include "world.h"
#include "world_glue.h"

#include "ei/rebirth.h"
#include "lib/calendar.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/rect.h"
#include <string>
#include "lib/str.h"
#include "lib/text_case.h"
#include "lib/vecmath.h"
#include "render/text_format.h"

#include <cstdint>
#include <cstring>

namespace {

constexpr const char* kQuickFolder = "saveq";
constexpr const char* kAutoFolderA = "savea";
constexpr const char* kAutoFolderB = "saveb";
constexpr const char* kDateFormat = "%d/%m, %H:%M";
constexpr const char* kPlainFormat = "%s";
constexpr const char* kDayText = "save_day";
constexpr const char* kGameTimeFormat = "%s  %d,  %d:%02d";
constexpr float kMinutesPerHour = 60.0f;

constexpr int kFont = 1;
constexpr COLORREF kTextColour = 0xA7D7E4;
constexpr COLORREF kCaretColour = 0x31B3FF;
constexpr int kRowHeight = 24;
constexpr int kFirstRowY = 120;          // the marker's layout y for the first row
constexpr int kRowsEndY = 504;           // sixteen rows
constexpr int kMarkerSprite = 4;
constexpr int kMinutesPerDay = 1440;


bool SameFolder(std::string_view folder, std::string_view name) {
    return text::CompareIgnoringCase(folder,
                           name) == 0;
}

} // namespace

void SaveLoadWindow::RefreshListVanilla() {
    auto& currentName = name;
    if (cursor > static_cast<int>(currentName.size())) cursor = lib::Count(currentName);
    if (cursor < 0) cursor = 0;
    UiSurface* surface = surfaces[0];
    surface->Clear();
    UiMesh* marker = SceneMesh(kMarkerSprite);
    marker->visible = false;

    // The rows from the scroll position: the title (the typed name for the selected row of a save
    // window) and, but for a save window's new save, the date.
    RECT titleRect{12, 11, 190, 32};
    RECT dateRect{190, 11, 265, 32};
    int y = kFirstRowY;
    for (unsigned index = static_cast<unsigned>(firstVisible); index < static_cast<unsigned>(EntryCount()); ++index) {
        const SaveEntry& entry = entries[index];
        if (static_cast<int>(index) == selected) {
            const std::string& title = isSave && nameTyped ? name : entry.title;
            surface->DrawTextIn(title, &titleRect, kFont, kTextColour, render::text::kNoPrefix, 1);
            // The caret, unless the save keeps its name (the quick and the automatic saves).
            if (isSave && !SameFolder(entry.folder, kQuickFolder) && !SameFolder(entry.folder, kAutoFolderA) &&
                !SameFolder(entry.folder, kAutoFolderB)) {
                const int width = ui->TextExtent(str::Left(currentName, cursor), kFont).cx;
                if (width < titleRect.right - titleRect.left) {
                    POINT top{titleRect.left + width, titleRect.top};
                    POINT bottom{titleRect.left + width, titleRect.bottom - 3};
                    // WideScreen (XOR ECX,ECX in the original): without the margin.
                    ui->CallScalePointToScreenNoMargin(&top);
                    ui->CallScalePointToScreenNoMargin(&bottom);
                    surface->Line(top.x, top.y, bottom.x, bottom.y, 1, kCaretColour);
                }
            }
            marker = SceneMesh(kMarkerSprite);
            marker->visible = true;
            marker->Place(239.0f, static_cast<float>(y), 3.0f);
        } else {
            surface->DrawTextIn(entry.title, &titleRect, kFont, kTextColour, render::text::kNoPrefix, 1);
        }
        if (!isSave || index != 0) {
            const std::string date = lib::FormatTime(entry.savedAt, kDateFormat);
            surface->DrawTextIn(date, &dateRect, kFont, kTextColour, render::text::kRight, 1);
        }
        rects::Offset(&titleRect, 0, kRowHeight);
        rects::Offset(&dateRect, 0, kRowHeight);
        y += kRowHeight;
        if (y >= kRowsEndY) break;
    }

    // The selected save's details: the allod, the zone, the game's day and time, the folder.
    if (selected < 0 || static_cast<unsigned>(selected) >= static_cast<unsigned>(EntryCount()) ||
        entries[selected].broken) {
        return;
    }
    const SaveEntry& entry = entries[selected];
    RECT detailRect{340, 260, 590, 284};
    std::string line = str::Format(kPlainFormat, entry.AllodTitle());
    surface->DrawTextIn(line, &detailRect, kFont, kTextColour, 0, 1);
    rects::Offset(&detailRect, 0, kRowHeight);
    line = str::Format(kPlainFormat, entry.ZoneTitle());
    surface->DrawTextIn(line, &detailRect, kFont, kTextColour, 0, 1);
    rects::Offset(&detailRect, 0, kRowHeight);

    const int minutes = static_cast<int>(entry.gameTime * kMinutesPerHour);
    const int day = minutes / kMinutesPerDay;
    const int minuteOfDay = minutes - day * kMinutesPerDay;
    const int hour = minuteOfDay / 60;
    const int minute = minuteOfDay - hour * 60;
    std::string dayText = g_clientWorld.texts->GetString(kDayText);
    line = str::Format(kGameTimeFormat, dayText, day + 1, hour, minute);
    surface->DrawTextIn(line, &detailRect, kFont, kTextColour, 0, 1);

    RECT folderRect{340, 360, 580, 385};
    surface->DrawTextIn(entry.folder, &folderRect, kFont, kTextColour, render::text::kRight, 1);
}
