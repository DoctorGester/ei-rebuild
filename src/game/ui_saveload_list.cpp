// The save/load window's list (CSaveLoadWindow::RefreshList): the visible rows with their dates, the name
// being typed with its caret, the selection's marker and the selected save's details, all drawn into the
// window's first surface. Rebirth's WideScreen patches it (the caret's points scaled without the margin),
// which this reproduces behind the flag. Rebirth-patched callees: the surface's text, the text extent, the
// point scaling and the sprite's Place. See ui.h (CSaveLoadWindow) and docs/notes/items_spells.md.
#include "ui.h"
#include "game_strings.h"

#include "item_db_text.h"       // CTextManager
#include "ui_gdi.h"
#include "world_glue.h"         // GameClientTexts

#include "ei/rebirth.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "lib/str.h"
#include "lib/vecmath.h"
#include "game_data.h"

#include <cstdint>
#include <cstring>
#include <string>

namespace {

constexpr const char* kQuickFolder = "saveq";
constexpr const char* kAutoFolderA = "savea";
constexpr const char* kAutoFolderB = "saveb";
constexpr const char* kDateFormat = "%d/%m, %H:%M";
constexpr const char* kPlainFormat = "%s";
constexpr const char* kDayText = "save_day";
constexpr const char* kGameTimeFormat = "%s  %d,  %d:%02d";
float MinutesPerHour() { return 60.0f /*0073F090*/; }

constexpr int kFont = 1;
constexpr COLORREF kTextColour = 0xA7D7E4;
constexpr COLORREF kCaretColour = 0x31B3FF;
constexpr int kRowHeight = 24;
constexpr int kFirstRowY = 120;          // the marker's layout y for the first row
constexpr int kRowsEndY = 504;           // sixteen rows
constexpr int kMarkerSprite = 4;
constexpr int kMinutesPerDay = 1440;


// The width of `text` in interface font `font` (CInterface3D::TextExtent).
int TextWidth(CInterface3D* ui, const std::string& text, int font) {
    SIZE size;
    ui->TextExtent(&size, text, font);
    return size.cx;
}

bool SameFolder(std::string_view folder, std::string_view name) {
    return gamecrt::mbsicmp(folder,
                           name) == 0;
}

} // namespace

void CSaveLoadWindow::RefreshListVanilla() {
    auto& name = name_;
    if (cursor_ > static_cast<int>(name.size())) cursor_ = name.size();
    if (cursor_ < 0) cursor_ = 0;
    CUiSurface* surface = surfaces_.data[0];
    mfc::impl::CDC* dc = UiSurfaceGetDC(surface);
    surface->Clear();
    CUiMesh* marker = SceneMesh(kMarkerSprite);
    marker->visible_ = 0;

    // The rows from the scroll position: the title (the typed name for the selected row of a save
    // window) and, but for a save window's new save, the date.
    RECT titleRect{12, 11, 190, 32};
    RECT dateRect{190, 11, 265, 32};
    int y = kFirstRowY;
    for (unsigned index = static_cast<unsigned>(firstVisible_); index < static_cast<unsigned>(EntryCount()); ++index) {
        const SaveEntry& entry = entries_.begin()[index];
        if (static_cast<int>(index) == selected_) {
            const char* title = (isSave_ && nameTyped_ ? name_ : entry.title_).c_str();
            surface->DrawTextIn(title, &titleRect, kFont, kTextColour, DT_NOPREFIX, 1);
            // The caret, unless the save keeps its name (the quick and the automatic saves).
            if (isSave_ && !SameFolder(entry.folder_, kQuickFolder) && !SameFolder(entry.folder_, kAutoFolderA) &&
                !SameFolder(entry.folder_, kAutoFolderB)) {
                const int width = TextWidth(interface_, str::Left(name, cursor_).c_str(), kFont);
                if (width < titleRect.right - titleRect.left) {
                    mfc::impl::CPen pen(PS_SOLID, 1, kCaretColour);
                    mfc::impl::CGdiObject* oldPen = dc->SelectObject(&pen);
                    POINT top{titleRect.left + width, titleRect.top};
                    POINT bottom{titleRect.left + width, titleRect.bottom - 3};
                    // WideScreen (XOR ECX,ECX in the original): without the margin.
                    interface_->CallScalePointToScreenNoMargin(&top);
                    interface_->CallScalePointToScreenNoMargin(&bottom);
                    dc->MoveTo(top.x, top.y);
                    dc->LineTo(bottom.x, bottom.y);
                    dc->SelectObject(oldPen);
                }
            }
            marker = SceneMesh(kMarkerSprite);
            marker->visible_ = 1;
            marker->PlaceBody(239.0f, static_cast<float>(y), 3.0f);
        } else {
            surface->DrawTextIn(entry.title_, &titleRect, kFont, kTextColour, DT_NOPREFIX, 1);
        }
        if (!isSave_ || index != 0) {
            // CTime::Format.
            const std::string date = mfc::CTime{entry.savedAt_}.Format(kDateFormat).GetString();
            surface->DrawTextIn(date, &dateRect, kFont, kTextColour, DT_RIGHT, 1);
        }
        gameapi::OffsetRect(&titleRect, 0, kRowHeight);
        gameapi::OffsetRect(&dateRect, 0, kRowHeight);
        y += kRowHeight;
        if (y >= kRowsEndY) break;
    }

    // The selected save's details: the allod, the zone, the game's day and time, the folder.
    if (selected_ < 0 || static_cast<unsigned>(selected_) >= static_cast<unsigned>(EntryCount()) ||
        entries_.begin()[selected_].broken_) {
        UiSurfaceReleaseDC(surface);
        return;
    }
    const SaveEntry& entry = entries_.begin()[selected_];
    RECT detailRect{340, 260, 590, 284};
    std::string line;
    line = str::Format(kPlainFormat, entry.AllodTitle().c_str());
    surface->DrawTextIn(line, &detailRect, kFont, kTextColour, 0, 1);
    gameapi::OffsetRect(&detailRect, 0, kRowHeight);
    line = str::Format(kPlainFormat, entry.ZoneTitle().c_str());
    surface->DrawTextIn(line, &detailRect, kFont, kTextColour, 0, 1);
    gameapi::OffsetRect(&detailRect, 0, kRowHeight);

    const int minutes = static_cast<int>(gamecrt::Ftol(static_cast<float>(entry.gameTime_ * MinutesPerHour())));
    const int day = minutes / kMinutesPerDay;
    const int minuteOfDay = minutes - day * kMinutesPerDay;
    const int hour = minuteOfDay / 60;
    const int minute = minuteOfDay - hour * 60;
    std::string dayText;
    GameClientTexts()->GetString(kDayText, &dayText);
    line = str::Format(kGameTimeFormat, dayText.c_str(), day + 1, hour, minute);
    surface->DrawTextIn(line, &detailRect, kFont, kTextColour, 0, 1);

    RECT folderRect{340, 360, 580, 385};
    surface->DrawTextIn(entry.folder_, &folderRect, kFont, kTextColour, DT_RIGHT, 1);
    UiSurfaceReleaseDC(surface);
}
