// The save/load window's list (SaveLoadWindow::RefreshListVanilla and PaintList): the selection's marker, and the
// visible rows with their dates, the name being typed with its caret and the selected save's details, painted on
// the window's first surface. Rebirth's WideScreen patches it (the caret's points scaled without the margin),
// which this reproduces behind the flag. Rebirth-patched callees: the surface's text, the text extent, the
// point scaling and the sprite's Place. See ui.h (SaveLoadWindow) and docs/notes/items_spells.md.
#include "ui.h"

#include "text_manager.h"       // TextManager
#include "game_singletons.h"
#include "world.h"

#include "lib/rebirth.h"
#include "lib/calendar.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/rect.h"
#include "platform/platform.h"
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
constexpr TextStyle kRowText{kFont, kUiTextColour};
constexpr TextStyle kRowTextNoPrefix{kFont, kUiTextColour, render::text::kNoPrefix};
constexpr TextStyle kRowTextRight{kFont, kUiTextColour, render::text::kRight};
constexpr std::uint32_t kCaretColour = 0x31B3FF;
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
    if (cursor > static_cast<int>(name.size())) cursor = lib::Count(name);
    if (cursor < 0) cursor = 0;
    UiMesh* marker = sceneObjects[kMarkerSprite];
    marker->visible = false;
    int y = kFirstRowY;
    for (unsigned index = static_cast<unsigned>(firstVisible); index < static_cast<unsigned>(EntryCount()); ++index) {
        if (static_cast<int>(index) == selected) {
            marker->visible = true;
            marker->Place(239.0f, static_cast<float>(y), 3.0f);
        }
        y += kRowHeight;
        if (y >= kRowsEndY) break;
    }
}

void SaveLoadWindow::PaintList(UiSurface& surface) const {
    // The rows from the scroll position: the title (the typed name for the selected row of a save
    // window) and, but for a save window's new save, the date.
    Rect titleRect{12, 11, 190, 32};
    Rect dateRect{190, 11, 265, 32};
    int y = kFirstRowY;
    for (unsigned index = static_cast<unsigned>(firstVisible); index < static_cast<unsigned>(EntryCount()); ++index) {
        const SaveEntry& entry = entries[index];
        if (static_cast<int>(index) == selected) {
            const std::string& title = isSave && nameTyped ? name : entry.title;
            surface.DrawTextIn(title, titleRect, kRowTextNoPrefix);
            // The caret, unless the save keeps its name (the quick and the automatic saves).
            if (isSave && !SameFolder(entry.folder, kQuickFolder) && !SameFolder(entry.folder, kAutoFolderA) &&
                !SameFolder(entry.folder, kAutoFolderB)) {
                const int width = ui->TextExtent(str::Left(name, cursor), kFont).w;
                if (width < titleRect.Width()) {
                    Point top{titleRect.left + width, titleRect.top};
                    Point bottom{titleRect.left + width, titleRect.bottom - 3};
                    // WideScreen (XOR ECX,ECX in the original): without the margin.
                    ui->ScalePointToScreenNoMargin(&top);
                    ui->ScalePointToScreenNoMargin(&bottom);
                    surface.Line(top, bottom, 1, kCaretColour);
                }
            }
        } else {
            surface.DrawTextIn(entry.title, titleRect, kRowTextNoPrefix);
        }
        if (!isSave || index != 0) {
            const std::string date = lib::FormatLocalTime(entry.savedAt, kDateFormat);
            surface.DrawTextIn(date, dateRect, kRowTextRight);
        }
        titleRect.Offset(0, kRowHeight);
        dateRect.Offset(0, kRowHeight);
        y += kRowHeight;
        if (y >= kRowsEndY) break;
    }

    // The selected save's details: the allod, the zone, the game's day and time, the folder.
    if (selected < 0 || static_cast<unsigned>(selected) >= static_cast<unsigned>(EntryCount()) ||
        entries[selected].broken) {
        return;
    }
    const SaveEntry& entry = entries[selected];
    Rect detailRect{340, 260, 590, 284};
    std::string line = str::Format(kPlainFormat, entry.AllodTitle());
    surface.DrawTextIn(line, detailRect, kRowText);
    detailRect.Offset(0, kRowHeight);
    line = str::Format(kPlainFormat, entry.ZoneTitle());
    surface.DrawTextIn(line, detailRect, kRowText);
    detailRect.Offset(0, kRowHeight);

    const int minutes = static_cast<int>(entry.gameTime * kMinutesPerHour);
    const int day = minutes / kMinutesPerDay;
    const int minuteOfDay = minutes - day * kMinutesPerDay;
    const int hour = minuteOfDay / 60;
    const int minute = minuteOfDay - hour * 60;
    std::string dayText = g_clientWorld.texts->GetString(kDayText);
    line = str::Format(kGameTimeFormat, dayText, day + 1, hour, minute);
    surface.DrawTextIn(line, detailRect, kRowText);

    Rect folderRect{340, 360, 580, 385};
    surface.DrawTextIn(entry.folder, folderRect, kRowTextRight);
}
