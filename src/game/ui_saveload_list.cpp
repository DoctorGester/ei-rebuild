// The save/load window's list (CSaveLoadWindow::RefreshList, 006567A0; A-H26): the visible rows
// with their dates, the name being typed with its caret, the selection's marker and the selected
// save's details, all drawn into the window's first surface.
// Rebirth's WideScreen patches it (the caret's points scaled without the margin), which this
// reproduces behind the flag (ticket W2); our callers keep the forwarder in ui_menus.cpp (rule 9). Patched callees through their addresses: the
// surface's text 005FDE20, the text extent 005F8340, the point scaling 005F7D20 and the sprite's
// Place 005E6FA0. See ui.h (CSaveLoadWindow) and docs/notes/items_spells.md ("A-H26").
#include "ui.h"
#include "game_strings.h"

#include "item_db_text.h"       // CTextManager
#include "ui_gdi.h"
#include "world_glue.h"         // GameClientTexts

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "lib/vecmath.h"
#include "originals_library.h"
#include "originals_ui.h"

#include <cstdint>
#include <cstring>

#pragma warning(disable : 4740) // naked forwarders: no optimisation to suppress

namespace {

constexpr std::uintptr_t kQuickFolder = std::uintptr_t(0x007933AC);    // "saveq"
constexpr std::uintptr_t kAutoFolderA = std::uintptr_t(0x007942E8);    // "savea"
constexpr std::uintptr_t kAutoFolderB = std::uintptr_t(0x007940F0);    // "saveb"
constexpr std::uintptr_t kDateFormat = std::uintptr_t(0x00794A44);     // "%d/%m, %H:%M"
constexpr std::uintptr_t kPlainFormat = std::uintptr_t(0x0079190C);    // "%s"
constexpr std::uintptr_t kDayText = std::uintptr_t(0x00794134);        // "save_day"
constexpr std::uintptr_t kGameTimeFormat = std::uintptr_t(0x00794120); // "%s  %d,  %d:%02d"
float MinutesPerHour() { return *reinterpret_cast<const float*>(std::uintptr_t(0x0073F090)); }   // 60

constexpr int kFont = 1;
constexpr COLORREF kTextColour = 0xA7D7E4;
constexpr COLORREF kCaretColour = 0x31B3FF;
constexpr int kRowHeight = 24;
constexpr int kFirstRowY = 120;          // the marker's layout y for the first row
constexpr int kRowsEndY = 504;           // sixteen rows
constexpr int kMarkerSprite = 4;
constexpr int kMinutesPerDay = 1440;


// The width of `text` in interface font `font` (005F8340, Rebirth-patched: through its address; it
// takes the CString by value and releases it, so a copy's data is handed over).
int TextWidth(CInterface3D* ui, const mfc::CString& text, int font) {
    mfc::CString copy(text);
    char* data;
    std::memcpy(&data, &copy, sizeof(data));
    mfc::CString empty;
    std::memcpy(&copy, &empty, sizeof(data));
    SIZE size;
    InterfaceTextExtentOriginal(ui, nullptr, &size, data, font);
    return size.cx;
}

bool SameFolder(const char* folder, std::uintptr_t name) {
    return libcrt::Mbsicmp(reinterpret_cast<const unsigned char*>(folder),
                           reinterpret_cast<const unsigned char*>(game::Text(name))) == 0;
}

} // namespace

void CSaveLoadWindow::RefreshListVanilla() {
    mfc::CString& name = *reinterpret_cast<mfc::CString*>(&name_);
    if (cursor_ > name.GetLength()) cursor_ = name.GetLength();
    if (cursor_ < 0) cursor_ = 0;
    CUiSurface* surface = surfaces_.data[0];
    ui_gdi::GameDC* dc = UiSurfaceGetDC(surface);
    surface->Clear();
    CUiMesh* marker = reinterpret_cast<CUiMesh*>(sceneObjects_.data[kMarkerSprite]);
    marker->visible_ = 0;

    // The rows from the scroll position: the title (the typed name for the selected row of a save
    // window) and, but for a save window's new save, the date.
    RECT titleRect{12, 11, 190, 32};
    RECT dateRect{190, 11, 265, 32};
    int y = kFirstRowY;
    for (unsigned index = static_cast<unsigned>(firstVisible_); index < static_cast<unsigned>(EntryCount()); ++index) {
        const SaveEntry& entry = entriesBegin_[index];
        if (static_cast<int>(index) == selected_) {
            const char* title = isSave_ && nameTyped_ ? name_ : entry.title_;
            SurfaceDrawText(surface, nullptr, title, &titleRect, kFont, kTextColour, DT_NOPREFIX, 1);
            // The caret, unless the save keeps its name (the quick and the automatic saves).
            if (isSave_ && !SameFolder(entry.folder_, kQuickFolder) && !SameFolder(entry.folder_, kAutoFolderA) &&
                !SameFolder(entry.folder_, kAutoFolderB)) {
                const int width = TextWidth(interface_, name.Left(cursor_), kFont);
                if (width < titleRect.right - titleRect.left) {
                    ui_gdi::GamePen pen;
                    pen.Construct(PS_SOLID, 1, kCaretColour);
                    mfc::impl::CGdiObject* oldPen = dc->SelectObject(&pen);
                    POINT top{titleRect.left + width, titleRect.top};
                    POINT bottom{titleRect.left + width, titleRect.bottom - 3};
                    // WideScreen (006569F7 / 00656A08: XOR ECX,ECX): without the margin.
                    interface_->CallScalePointToScreenNoMargin(&top);
                    interface_->CallScalePointToScreenNoMargin(&bottom);
                    dc->MoveTo(top.x, top.y);
                    dc->LineTo(bottom.x, bottom.y);
                    dc->SelectObject(oldPen);
                    pen.Destruct();
                }
            }
            marker = reinterpret_cast<CUiMesh*>(sceneObjects_.data[kMarkerSprite]);
            marker->visible_ = 1;
            SpritePlaceOriginal(marker, nullptr, 239.0f, static_cast<float>(y), 3.0f);
        } else {
            SurfaceDrawText(surface, nullptr, entry.title_, &titleRect, kFont, kTextColour, DT_NOPREFIX, 1);
        }
        if (!isSave_ || index != 0) {
            // 006FE795: CTime::Format.
            const mfc::CString date = reinterpret_cast<const mfc::CTime*>(&entry.savedAt_)->Format(game::Text(kDateFormat));
            SurfaceDrawText(surface, nullptr, date, &dateRect, kFont, kTextColour, DT_RIGHT, 1);
        }
        gameapi::OffsetRect(&titleRect, 0, kRowHeight);
        gameapi::OffsetRect(&dateRect, 0, kRowHeight);
        y += kRowHeight;
        if (y >= kRowsEndY) break;
    }

    // The selected save's details: the allod, the zone, the game's day and time, the folder.
    if (selected_ < 0 || static_cast<unsigned>(selected_) >= static_cast<unsigned>(EntryCount()) ||
        entriesBegin_[selected_].broken_) {
        UiSurfaceReleaseDC(surface);
        return;
    }
    const SaveEntry& entry = entriesBegin_[selected_];
    RECT detailRect{340, 260, 590, 284};
    mfc::CString line;
    line.Format(game::Text(kPlainFormat), static_cast<const char*>(entry.AllodTitle()));
    SurfaceDrawText(surface, nullptr, line, &detailRect, kFont, kTextColour, 0, 1);
    gameapi::OffsetRect(&detailRect, 0, kRowHeight);
    line.Format(game::Text(kPlainFormat), static_cast<const char*>(entry.ZoneTitle()));
    SurfaceDrawText(surface, nullptr, line, &detailRect, kFont, kTextColour, 0, 1);
    gameapi::OffsetRect(&detailRect, 0, kRowHeight);

    const int minutes = static_cast<int>(gamecrt::Ftol(static_cast<float>(entry.gameTime_ * MinutesPerHour())));
    const int day = minutes / kMinutesPerDay;
    const int minuteOfDay = minutes - day * kMinutesPerDay;
    const int hour = minuteOfDay / 60;
    const int minute = minuteOfDay - hour * 60;
    mfc::CString dayText;
    reinterpret_cast<CTextManager*>(GameClientTexts())->GetString(game::Text(kDayText), &dayText);
    line.Format(game::Text(kGameTimeFormat), static_cast<const char*>(dayText), day + 1, hour, minute);
    SurfaceDrawText(surface, nullptr, line, &detailRect, kFont, kTextColour, 0, 1);

    RECT folderRect{340, 360, 580, 385};
    SurfaceDrawText(surface, nullptr, entry.folder_, &folderRect, kFont, kTextColour, DT_RIGHT, 1);
    UiSurfaceReleaseDC(surface);
}
EI_REPLACE_METHOD(0x006567A0, CSaveLoadWindow::RefreshListVanilla);
