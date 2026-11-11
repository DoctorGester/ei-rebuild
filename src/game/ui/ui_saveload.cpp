// The save / load window (SaveLoadWindow, ui_saveload.h): its layout, mouse and key handlers, saving, its entry list
// and the selected save's picture. A picture (shot.sav's, or the new save's made from the escape menu's copy of the
// screen) is a texture of its own size drawn over the window's picture rectangle. The list paints the selection's
// marker and the visible rows with their dates, the name being typed with its caret and the selected save's details
// on the window's first surface; Rebirth's WideScreen patches that (the caret's points scaled without the margin), which
// it reproduces behind the flag. The key handler passes the key to the container first and ignores keys while the window
// slides in. See docs/notes/ui.md, section 8 and "Save / load window pictures", and docs/notes/renderer.md, section 6.
#include "game/ui/ui_saveload.h"
#include "game/app/app.h"
#include "engine/core/asset_text.h"
#include "game/console/console.h"
#include "engine/core/core_streams.h"
#include "game/app/game_flow.h"
#include "game/app/singletons.h"
#include "engine/scene/scene_renderer.h"
#include "engine/models/texture.h"
#include "game/net/net_link.h"
#include "game/map/map_database.h"
#include "game/saves/save_picture.h"
#include "game/sound/sound.h"
#include "engine/data/text_manager.h"
#include "game/ui/ui.h"
#include "game/ui/ui_cursor.h"
#include "game/ui/ui_menu_pieces.h"
#include "game/ui/ui_mesh.h"
#include "game/ui/ui_message_box.h"
#include "game/ui/ui_scrollbar.h"
#include "game/world/world.h"
#include "lib/calendar.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/intro_sort.h"
#include "lib/rect.h"
#include "lib/str.h"
#include "lib/text_case.h"
#include "platform/platform.h"
#include "render/render.h"
#include "render/text_format.h"
#include "game/ui/screen_layout.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>

namespace {
constexpr unsigned kInfoMagic = 0x111;
constexpr unsigned kInfoVersion = 3;
constexpr std::uint32_t kClosed = 0x100002;          // pending value after saving

// The window's picture rectangle on this screen, in pixels: the size a save's picture is kept at and shown at.
Size ShownSize() {
    Rect shown = screen_layout::ToScreen(SaveLoadWindow::kPictureRect);
    return shown.Extent();
}

// The shot.sav bytes of the screen `surface` copied: the old picture from the whole copy, the trailer's shrunk to the
// window's picture rectangle.
std::vector<std::uint8_t> ShotOf(const UiSurface& surface) {
    const save_picture::Picture screen = surface.ReadPicture();
    const Size size = ShownSize();
    return save_picture::ShotFile(screen, save_picture::Shrink(screen, size.w, size.h));
}

// A texture of `picture`'s size holding it (one reference), or null.
Texture* PictureTexture(const save_picture::Picture& picture) {
    if (picture.Empty()) return nullptr;
    Texture* texture = g_clientWorld.textureManager->NewTexture(picture.width, picture.height, TextureKind::Blank565);
    if (!texture) return nullptr;
    texture->AddRef();
    const render::TextureImage image{picture.rgba.data(), static_cast<std::uint32_t>(picture.rgba.size()), picture.width,
                                     picture.height, render::ImageLayout::Rgba};
    render::DrawIntoTexture(texture->surface, &image, 0, 0, picture.width, picture.height);
    return texture;
}

// The picture sprite showing the whole of `texture`, its texels nudged half a texel as SetRectangle's are.
void ShowTexture(UiMesh* sprite, Texture* texture) {
    const float du = 0.5f / static_cast<float>(texture->width), dv = 0.5f / static_cast<float>(texture->height);
    sprite->texCoords = {{du, -dv}, {du, 1.0f - dv}, {1.0f + du, -dv}, {1.0f + du, 1.0f - dv}};
    sprite->RefreshBuffers();
    sprite->texture = texture;
    sprite->visible = true;
}

constexpr const char* kQuickFolder = "saveq";
constexpr const char* kAutoFolderA = "savea";
constexpr const char* kAutoFolderB = "saveb";
constexpr const char* kQuickTitle = "save_quick";
constexpr const char* kAutoEnterTitle = "save_auto_enter";
constexpr const char* kAutoExitTitle = "save_auto_exit";
constexpr const char* kPlainFormat = "%s";

constexpr const char* kNotifyLoading = "notify_loading";
constexpr int kScreenWidth = 800, kScreenHeight = 600;   // the copy's layout rectangle (immediates)

// The save folder saves\<slot>\ under the game's folder.
std::string SlotFolder(const char* slot) {
    std::string folder = str::Format("%ssaves\\%s\\", g_dataFolder, slot);
    return folder;
}

// The first line of the text `prefix` + `suffix`, or empty when there is none.
std::string FirstLineOf(const char* prefix, const std::string& suffix) {
    std::string line;
    TextManager* texts = g_clientWorld.texts.get();
    bool found = texts->Open(std::string(prefix) + suffix);
    if (found) line = texts->ReadLine(true);
    return line;
}

constexpr const char* kSoundDelete = "buttons\\save\\delete.wav";
constexpr MessageQuestion kAskOverwrite = MessageQuestion::Second, kAskDelete = MessageQuestion::First;
constexpr const char* kFolderFormat = "%ssaves\\%s";
constexpr const char* kEveryFile = "\\*.*";

// A message box `name` / `text` on top of the interface.
void Ask(std::string_view name, std::string_view text, MessageQuestion question) {
    g_interface.PushWindow(std::make_unique<MessageWindow>(name, text, question, MessageButtons::OkCancel), WindowBackdrop::FirstWindow);
}

constexpr const char* kDateFormat = "%d/%m, %H:%M";
constexpr const char* kDayText = "save_day";
constexpr const char* kGameTimeFormat = "%s  %d,  %d:%02d";
constexpr float kMinutesPerHour = 60.0f;

constexpr int kFont = 1;
constexpr TextStyle kRowText{kFont, kUiTextColour};
constexpr TextStyle kRowTextNoPrefix{kFont, kUiTextColour, render::text::Format::NoPrefix};
constexpr TextStyle kRowTextRight{kFont, kUiTextColour, render::text::Format::Right};
constexpr std::uint32_t kCaretColour = 0x31B3FF;
constexpr int kRowHeight = 24;
constexpr int kFirstRowY = 120;          // the marker's layout y for the first row
constexpr int kRowsEndY = 504;           // sixteen rows
constexpr int kMinutesPerDay = 1440;

bool SameFolder(std::string_view folder, std::string_view name) {
    return text::CompareIgnoringCase(folder,
                           name) == 0;
}

constexpr float kSaveLoadTilt = /*007C81C8*/ 1.5707964f;

// Pending values a window hands the interface when it closes (Interface3D::SetPendingValue):
// the low word 1 closes the top window, the high word is the result its parent receives.
constexpr std::uint32_t kCloseSaveLoad = 0x100001;
constexpr std::uint32_t kCloseSaveLoadLoad = 0x100002;

// Results of the save/load window's message boxes.
constexpr std::uint32_t kResultDelete = BoxResult(MessageQuestion::First, MessageAnswer::Ok);
constexpr std::uint32_t kResultOverwrite = BoxResult(MessageQuestion::Second, MessageAnswer::Ok);
constexpr std::uint32_t kResultNoSpace = BoxResult(MessageQuestion::Third, MessageAnswer::Ok);

constexpr int kMaxNameLength = 60;       // characters of a typed save name
constexpr int kMaxNameWidth = 0xB2;      // its width limit in pixels (font 1)

constexpr int kListRows = 16;            // visible rows of the save list
} // namespace

void SaveLoadWindow::ShowPicture() {
    pictureSprite->visible = false;
    int currentSelected = selected;
    if (currentSelected < 0 || static_cast<unsigned>(currentSelected) >= static_cast<unsigned>(EntryCount())) return;
    SaveEntry* entry = entries.data() + currentSelected;
    if (entry->broken) return;
    if (isSave && currentSelected == 0) {
        // The new save: the game's picture.
        if (dialog) ShowTexture(pictureSprite, dialog);
        return;
    }
    // The file's picture, no larger than shown here.
    std::string folder = str::Format("%ssaves\\%s\\", g_dataFolder, entry->folder);
    std::string bytes;
    if (!platform::ReadWholeFile(folder + "shot.sav", &bytes)) return;
    const Size size = ShownSize();
    const auto* data = reinterpret_cast<const std::uint8_t*>(bytes.data());
    Texture* made = PictureTexture(save_picture::Fit(save_picture::ReadShot(data, bytes.size()), size.w, size.h));
    if (!made) return;
    if (picture) picture->Release();
    picture = made;
    ShowTexture(pictureSprite, picture);
}

void SaveEntry::MakeTitle() {
    std::string quick;
    std::string autoEnter;
    std::string autoExit;
    quick = g_clientWorld.texts->GetString(kQuickTitle);
    autoEnter = g_clientWorld.texts->GetString(kAutoEnterTitle);
    autoExit = g_clientWorld.texts->GetString(kAutoExitTitle);
    auto& currentTitle = title;
    if (text::CompareIgnoringCase(folder, kQuickFolder) == 0) {
        currentTitle = str::Format(kPlainFormat, quick);
    } else if (text::CompareIgnoringCase(folder, kAutoFolderA) == 0) {
        currentTitle = str::Format(kPlainFormat, autoEnter);
    } else if (text::CompareIgnoringCase(folder, kAutoFolderB) == 0) {
        currentTitle = str::Format(kPlainFormat, autoExit);
    } else {
        // The zone name ("zone" + zone in the texts, else "save_camp").
        const std::string name = ZoneTitle();
        currentTitle = str::Format(kPlainFormat, name);
    }
}

void SaveLoadWindow::SaveGame() {
    int currentSelected = selected;
    if (currentSelected < 0 || static_cast<unsigned>(currentSelected) >= static_cast<unsigned>(EntryCount())) return;
    SaveEntry* entry = entries.data() + currentSelected;
    double space = platform::FreeDiskSpace(g_dataFolder);
    // 0 means unknown: no check.
    if (space != 0.0) {
        int needed = CurrentSaveSize();
        if (static_cast<double>(needed) > space) {
            g_interface.PushWindow(std::make_unique<MessageWindow>("no_disc_space", "no_disc_space_msg", MessageQuestion::Third, MessageButtons::Ok), WindowBackdrop::FirstWindow);
            return;
        }
    }
    std::string folder = str::Format("%ssaves\\%s\\", g_dataFolder, entry->folder);
    std::string notice = g_clientWorld.texts->GetString("notify_saving");
    g_interface.ShowStatusText(notice);
    g_gameFlow.SaveGame(entry->folder);
    g_cursor.SetShape(CursorShape::Wait);

    // info.sav: the new save's entry (the first) with the title being given.
    FileStream info;
    info.Open(folder + "info.sav", platform::FileMode::Create);
    if (nameTyped)
        entries.data()->title = name;
    else if (selected == 0)
        entries.data()->title.clear();
    else
        entries.data()->title = entry->title;
    entries.data()->savedAt = lib::CurrentTime();
    SaveEntry* first = entries.data();
    info.WriteHeader(kInfoMagic, kInfoVersion);
    float gameTime = first->gameTime;
    info.WriteValue(gameTime);
    info.WriteString(first->map);
    info.WriteString(first->zone);
    std::int32_t savedAt = first->savedAt;
    info.WriteValue(savedAt);
    info.WriteString(first->title);
    info.Close();

    // shot.sav: the game's picture.
    if (dialog) {
        platform::File shot;
        shot.Open(folder + "shot.sav", platform::FileMode::Create);
        shot.Write(newPicture.data(), static_cast<unsigned>(newPicture.size()));
        shot.Close();
    }
    g_cursor.SetShape(CursorShape::Default);
    g_interface.SetPendingValue(kClosed);
}

void SaveLoadWindow::FillEntries() {
    std::string pattern = str::Format("%ssaves\\save*", g_dataFolder);
    entries.clear();
    int highest = 0;     // the highest number after "save" in the folder names
    for (const platform::FolderEntry& found : platform::FolderEntries(pattern)) {
        if (found.IsDots() || !found.folder) continue;
        // A new entry (the original copies a stack temporary whose numbers are left as the stack held them).
        SaveEntry* entry = &entries.emplace_back();
        entry->folder = found.name;
        const std::string infoPath = found.path + "\\info.sav";
        if (!platform::PathExists(infoPath)) {
            entry->broken = true;
            entry->title = "Error Save";
        } else {
            FileStream info;
            info.Open(infoPath, platform::FileMode::Read);
            if (info.ReadHeader(kInfoMagic, (int)kInfoVersion, 1)) {
                info.ReadValue(entry->gameTime);
                entry->map = info.ReadString();
                entry->zone = info.ReadString();
                // The version the header read.
                if (info.version > 1) {
                    std::int32_t savedAt;
                    info.ReadValue(savedAt);
                    entry->savedAt = savedAt;
                }
                if (info.version > 2) entry->title = info.ReadString();
            }
            info.Close();
            if (entry->title.size() == 0) entry->MakeTitle();
            entry->broken = false;
        }
        if (static_cast<int>(entry->folder.size()) > 4) {
            int number = str::ToInt(str::Mid(entry->folder, 4));
            if (!(highest > number)) highest = number;
        }
    }
    // The newest first: the order is "not older than" (!(a < b)), so saves of the same time compare true both ways
    // (not a strict weak ordering: the game's sort, whose unguarded scans can then run off the range as in the
    // game, and the order it leaves such saves in).
    lib::IntroSort(entries.data(), entries.data() + entries.size(),
                   [](const SaveEntry& a, const SaveEntry& b) { return !(a.savedAt < b.savedAt); });

    if (isSave) {
        // The new save first.
        SaveEntry* entry = &*entries.emplace(entries.begin());
        entry->folder = str::Format("save%d", highest + 1);
        entry->gameTime = LocalPlayerVariable("gtime");
        entry->zone = LocalPlayerZone();
        if (MapRecord* record = g_mapDatabase.FindMap(entry->zone))
            entry->map = record->allod;
        entry->MakeTitle();
        entry->broken = false;

        // The game's picture: the new save's shot.sav made from the escape menu's copy of the screen, and its texture.
        if (dialog) dialog->Release();
        dialog = nullptr;
        if (g_interface.screenCopy) {
            newPicture = ShotOf(*g_interface.screenCopy);
            dialog = PictureTexture(save_picture::ReadShot(newPicture.data(), newPicture.size()));
        }
    }

    if (static_cast<unsigned>(EntryCount()) > static_cast<unsigned>(kVisibleRows)) {
        const int position = scrollbar->TakePosition();
        scrollbar->SetMaximum(EntryCount() - kVisibleRows);
        scrollbar->SetPosition(position);
    } else {
        scrollbar->SetMaximum(0);
    }
    firstVisible = 0;
}

// --- Quick and automatic saves ------------------------------------------------------------------------

void QuickSave(const char* slot, bool stepFrame) {
    if (IsMultiplayerClient()) return;
    double space = platform::FreeDiskSpace(g_dataFolder);
    // 0 means unknown: no check.
    if (space != 0.0 && static_cast<double>(CurrentSaveSize()) > space) return;
    if (stepFrame) app::FrameStep(false);
    UiSurface surface(0, 0, kScreenWidth, kScreenHeight, SurfaceBlend::Opaque, true);
    surface.CaptureScreen();
    g_cursor.SetShape(CursorShape::Wait);
    g_interface.serverFlag = static_cast<std::uint8_t>(g_serverWorld.paused);
    std::string notice = g_clientWorld.texts->GetString("notify_saving");
    g_interface.ShowStatusText(notice);
    g_gameFlow.SaveGame(slot);

    // info.sav: the "gtime" variable, the zone's map, the zone, the time and an empty title.
    std::string title;
    std::string map;
    const std::int32_t savedAt = lib::CurrentTime();
    const float gameTime = LocalPlayerVariable("gtime");
    const std::string zone = LocalPlayerZone();
    if (const MapRecord* record = g_mapDatabase.FindMap(zone))
        map = record->allod;
    std::string folder = SlotFolder(slot);
    FileStream info;
    info.Open(folder + "info.sav", platform::FileMode::Create);
    info.WriteHeader(kInfoMagic, kInfoVersion);
    info.WriteValue(gameTime);
    info.WriteString(map);
    info.WriteString(zone);
    info.WriteValue(savedAt);
    info.WriteString(title);
    info.Close();

    // shot.sav: the copied screen.
    const std::vector<std::uint8_t> bytes = ShotOf(surface);
    platform::File shot;
    shot.Open(folder + "shot.sav", platform::FileMode::Create);
    shot.Write(bytes.data(), static_cast<unsigned>(bytes.size()));
    shot.Close();
    g_cursor.SetShape(CursorShape::Default);
}

void QuickLoad(const char* slot) {
    if (IsMultiplayerClient()) return;
    std::string folder = SlotFolder(slot);
    if (!platform::PathExists(folder)) return;
    std::string notice = g_clientWorld.texts->GetString(kNotifyLoading);
    g_interface.ShowStatusText(notice);
    g_clientWorld.sounds->StopAll(true);
    g_gameFlow.RequestLoad(slot);
}

// --- Entry titles, OK / Delete / load / delete ----------------------------------------------

std::string SaveEntry::ZoneTitle() const {
    std::string titleText = FirstLineOf("zone ", zone);    // the key has a space
    if (titleText.size() == 0) titleText = g_clientWorld.texts->GetString("save_camp");
    return titleText;
}

std::string SaveEntry::AllodTitle() const {
    std::string titleText = FirstLineOf("allod ", map);   // the key has a space
    if (titleText.size() == 0) titleText = "Unknown";
    return titleText;
}

void SaveLoadWindow::Confirm() {
    if (selected < 0) return;
    if (!isSave) {
        LoadSelected();
        return;
    }
    g_interface.PlayInterfaceSound(kMessageBoxOkSound);
    if (selected == 0) {
        SaveGame();
        return;
    }
    Ask("overwrite", "overwrite_msg", kAskOverwrite);
}

void SaveLoadWindow::AskDelete() {
    if (selected < 0 || (isSave && selected == 0)) return;
    g_interface.PlayInterfaceSound(kSoundDelete);
    Ask("delete", "delete_msg", kAskDelete);
}

void SaveLoadWindow::LoadSelected() {
    int currentSelected = selected;
    if (currentSelected < 0 || static_cast<unsigned>(currentSelected) >= static_cast<unsigned>(EntryCount())) return;
    if (entries.data()[currentSelected].broken) return;
    std::string notice = g_clientWorld.texts->GetString(kNotifyLoading);
    g_interface.ShowStatusText(notice);
    g_clientWorld.sounds->StopAll(true);
    g_interface.PlayInterfaceSound(kMessageBoxOkSound);
    g_gameFlow.RequestLoad(entries.data()[selected].folder);
}

void SaveLoadWindow::DeleteSave() {
    int currentSelected = selected;
    if (currentSelected < 0 || static_cast<unsigned>(currentSelected) >= static_cast<unsigned>(EntryCount())) return;
    std::string folder = str::Format(kFolderFormat, g_dataFolder, entries[currentSelected].folder);
    for (const platform::FolderEntry& entry : platform::FolderEntries(folder + kEveryFile))
        platform::RemoveFile(entry.path);
    platform::RemoveFolder(folder);
    FillEntries();
    int count = EntryCount();
    if (static_cast<unsigned>(selected) >= static_cast<unsigned>(count)) selected = count - 1;
    TakeSelectedTitle();
    RefreshListVanilla();
    ShowPicture();
}

void SaveLoadWindow::RefreshListVanilla() {
    if (cursor > static_cast<int>(name.size())) cursor = lib::Count(name);
    if (cursor < 0) cursor = 0;
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
                const int width = g_interface.TextExtent(str::Left(name, cursor), kFont).w;
                if (width < titleRect.Width()) {
                    Point top{titleRect.left + width, titleRect.top};
                    Point bottom{titleRect.left + width, titleRect.bottom - 3};
                    // WideScreen (XOR ECX,ECX in the original): without the margin.
                    top = screen_layout::ToScreenNoMargin(top);
                    bottom = screen_layout::ToScreenNoMargin(bottom);
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

// --- Save/load window -------------------------------------------------------------

void SaveLoadWindow::CreateScreen() {
    rect = Rect{0, 0, 800, 600};
    LoadTexture("saveload", TextureKind::Interface);

    AddBlankSprite()->SetRectangle(kDialogDepth, kSpriteScale, 163.0f, 526.0f, 237.0f, 574.0f, 0x51, 2, 0x9B, 0x32);
    AddBlankSprite()->SetRectangle(kDialogDepth, kSpriteScale, 563.0f, 526.0f, 637.0f, 574.0f, 0xA0, 2, 0xEA, 0x32);
    AddBlankSprite()->SetRectangle(kDialogDepth, kSpriteScale, 363.0f, 526.0f, 437.0f, 574.0f, 2, 2, 0x4C, 0x32);
    okButton = AddRect(this, 163, 526, 237, 574);
    okButton->SetTip(isSave ? 0xEAC4 : 0xEAC5);
    cancelButton = AddRect(this, 563, 526, 637, 574);
    cancelButton->SetTip(0xEAC7);
    deleteButton = AddRect(this, 363, 526, 437, 574);
    deleteButton->SetTip(0xEAC6);

    // The picture of the selected save (ShowPicture gives it its texture and texels).
    pictureSprite = AddBlankSprite();
    pictureSprite->SetRectangle(kDialogDepth, kSpriteScale, kPictureRect, 0, 0x100, 0x100, 0);
    pictureSprite->texture = nullptr;
    marker = AddBlankSprite();
    marker->SetRectangle(kDialogDepth, kSpriteScale, 108.0f, 108.0f, 370.0f, 132.0f, 0, 0, kUiTextureSize, kUiTextureSize);
    marker->texture = nullptr;
    marker->flatColour = 0xC8A06800;
    marker->flags |= FigureMeshFlag::Flat;
    Frame(this, 2.0f, 95.0f, 95.0f, 395.0f, 505.0f);     // the list
    Frame(this, 2.0f, 405.0f, 95.0f, 705.0f, 325.0f);    // the picture
    Frame(this, 2.0f, 405.0f, 335.0f, 705.0f, 505.0f);   // the description
    Shade(this, 5.0f, 100.0f, 100.0f, 390.0f, 500.0f);
    Shade(this, 5.0f, 410.0f, 100.0f, 700.0f, 320.0f);
    Shade(this, 5.0f, 410.0f, 340.0f, 700.0f, 500.0f);

    for (int row = 0, top = 108; row < kVisibleRows; ++row, top += 24) rows[row] = AddRect(this, 108, top, 370, top + 24);

    UiSurface* list = AddSurface(NewSurface(Rect{100, 100, 700, 500}));
    list->PaintWith([this](UiSurface& out) { PaintList(out); });
    scrollbar = AddChild(std::make_unique<UiScrollbar>(Rect{108, 108, 384, 492}, 3.0f, 0, 1.0f));

    UiModel* model = AddMenuModel(this, kSaveLoadTilt);
    model->SetScreenPosition(400.0f, 700.0f, 4.8f);
    model->ShowAllParts(0);
    if (isSave) {
        model->ShowPart("save", 1);
        model->ShowPart("savelabel", 1);
    } else {
        model->ShowPart("load", 1);
        model->ShowPart("loadlabel", 1);
    }

    dialog = nullptr;
    picture = nullptr;
    if (sliding) slide = 0.0f;
    FillEntries();
    if (isSave) {
        // Saving: the first save is selected and its title offered as the name.
        selected = 0;
        if (EntryCount() != 0) {
            nameTyped = false;
            name = entries.front().title;
            cursor = lib::Count(name);
        }
    } else {
        selected = EntryCount() != 0 ? 0 : -1;
    }
    RefreshListVanilla();
    ShowPicture();
    if (!sliding) g_interface.OpenHelp(false);
}

// --- Save/load window --------------------------------------------------------------------------

void SaveLoadWindow::TakeSelectedTitle() {
    int currentSelected = selected;
    if (currentSelected < 0 || static_cast<unsigned>(currentSelected) >= static_cast<unsigned>(EntryCount())) return;
    nameTyped = false;
    name = entries[currentSelected].title;
    cursor = lib::Count(name);
}

void SaveLoadWindow::LeftDown(UiEvent& event) {
    if (sliding) return;
    UiRegion* region = RegionUnder(event.at);
    const int row = RegionIndex(rows, region);
    if (region == okButton) {
        Confirm();
    } else if (region == cancelButton) {
        g_interface.PlayInterfaceSound(kMessageBoxCancelSound);
        g_interface.SetPendingValue(kCloseSaveLoad);
    } else if (region == deleteButton) {
        AskDelete();
    } else if (row >= 0) {
        unsigned index = static_cast<unsigned>(firstVisible + row);
        if (index < static_cast<unsigned>(EntryCount())) {
            g_interface.PlayInterfaceSound(kSaveSelectSound);
            if (static_cast<int>(index) != selected) {
                selected = static_cast<int>(index);
                TakeSelectedTitle();
            }
            RefreshListVanilla();
            ShowPicture();
        }
    }
    UiContainer::OnEvent(event);
}

void SaveLoadWindow::LeftDouble(UiEvent& event) {
    if (sliding) return;
    const int row = RegionIndex(rows, RegionUnder(event.at));
    if (row >= 0) {
        unsigned index = static_cast<unsigned>(firstVisible + row);
        if (index < static_cast<unsigned>(EntryCount())) {
            if (static_cast<int>(index) != selected) {
                selected = static_cast<int>(index);
                TakeSelectedTitle();
            }
            Confirm();
        }
    }
    UiContainer::OnEvent(event);
}

void SaveLoadWindow::DrawSprites(SceneRenderer* pass) {
    if (!sliding) {
        surfaces[0]->Show();
        models[0]->SetScreenPosition(400.0f, isSave ? 620.0f : 550.0f, 4.8f);
        UiContainer::DrawSprites(pass);
        return;
    }
    surfaces[0]->Hide();
    float e = Ease(slide);
    UiModel* model = models[0].get();
    if (!model) return;
    float y = isSave ? (620.0f - 700.0f) * e + 700.0f : (550.0f - 700.0f) * e + 700.0f;
    model->SetScreenPosition(400.0f, y, 4.8f);
    model->Draw(pass);
}

void SaveLoadWindow::Update() {
    UiContainer::Update();
    if (sliding) {
        float slideAt = FrameTime() / 0.5f + slide;
        slide = slideAt;
        if (slideAt > 1.0f) {
            sliding = false;
            g_interface.OpenHelp(false);
        }
    }
    if (scrollbar->TakeMoved()) {
        firstVisible = scrollbar->Position();
        RefreshListVanilla();
    }
}

void SaveLoadWindow::CloseScreen() {
    if (dialog) dialog->Release();
    dialog = nullptr;
    if (picture) picture->Release();
    picture = nullptr;
}

void SaveLoadWindow::MessageResult(std::uint32_t result) {
    if (result == kResultDelete)
        DeleteSave();
    else if (result == kResultOverwrite)
        SaveGame();
    else if (result == kResultNoSpace)
        g_interface.SetPendingValue(kCloseSaveLoadLoad);
}

// --- Save/load window ----------------------------------------------------------------------------

bool SaveLoadWindow::IsReservedSave(int index) {
    const std::string& folder = entries[index].folder;
    if (text::CompareIgnoringCase(folder, "saveq") == 0) return true;
    if (text::CompareIgnoringCase(folder, "savea") == 0) return true;
    return text::CompareIgnoringCase(folder, "saveb") == 0;
}

void SaveLoadWindow::ShowSelection() {
    if (selected < 0) selected = 0;
    int count = EntryCount();
    if (!(static_cast<unsigned>(selected) < static_cast<unsigned>(count))) selected = count - 1;
    int currentSelected = selected;
    if (!(firstVisible + kListRows > currentSelected)) firstVisible = currentSelected - (kListRows - 1);
    if (firstVisible > currentSelected) firstVisible = currentSelected;
    scrollbar->SetPosition(firstVisible);
    firstVisible = scrollbar->TakePosition();
    TakeSelectedTitle();
    RefreshListVanilla();
    ShowPicture();
}

void SaveLoadWindow::OnEvent(UiEvent& event) {
    switch (event.kind) {
    case UiEventKind::LeftDown: LeftDown(event); break;
    case UiEventKind::LeftDouble: LeftDouble(event); break;
    case UiEventKind::KeyDown: KeyDown(event); break;
    case UiEventKind::Char: Char(event); break;
    default: UiContainer::OnEvent(event); break;
    }
}

void SaveLoadWindow::KeyDown(UiEvent& event) {
    if (sliding) return;
    UiContainer::OnEvent(event);
    int step = 0;
    switch (event.key) {
    case platform::Key::Return:
        Confirm();
        event.handled = true;
        break;
    case platform::Key::Escape:
        g_interface.PlayInterfaceSound(kMessageBoxCancelSound);
        g_interface.SetPendingValue(kCloseSaveLoad);
        event.handled = true;
        break;
    case platform::Key::PageUp: step = -kListRows; break;
    case platform::Key::PageDown: step = kListRows; break;
    case platform::Key::Up: step = -1; break;
    case platform::Key::Down: step = 1; break;
    default: break;
    }
    if (step != 0) {
        g_interface.PlayInterfaceSound(kSaveSelectSound);
        selected += step;
        ShowSelection();
        event.handled = true;
    }
    if (event.handled || !isSave) return;

    // Editing the name. The first edit of an ordinary save starts from its title (Backspace and
    // Delete from an empty name); the quick and automatic saves keep their names.
    int currentSelected = selected;
    bool editable = currentSelected >= 0 && static_cast<unsigned>(currentSelected) < static_cast<unsigned>(EntryCount()) &&
                    !IsReservedSave(currentSelected) && !nameTyped;
    auto& currentName = name;
    switch (event.key) {
    case platform::Key::Backspace:
        if (editable) {
            nameTyped = true;
            currentName.clear();
            cursor = 0;
        }
        if (cursor > 0) {    // by character, as Delete and the cursor's steps
            const int previous = text::PreviousCharacter(currentName, cursor);
            currentName = str::Left(currentName, previous) + str::Mid(currentName, cursor);
            cursor = previous;
        }
        break;
    case platform::Key::Delete:
        if (editable) {
            nameTyped = true;
            currentName.clear();
            cursor = 0;
        }
        if (cursor < static_cast<int>(currentName.size()))
            currentName = str::Left(currentName, cursor) + str::Mid(currentName, text::NextCharacter(currentName, cursor));
        break;
    case platform::Key::End:
    case platform::Key::Home:
    case platform::Key::Left:
    case platform::Key::Right:
        if (editable) {
            nameTyped = true;
            currentName = entries[selected].title;
            cursor = lib::Count(currentName);
        }
        if (event.key == platform::Key::End)
            cursor = lib::Count(currentName);
        else if (event.key == platform::Key::Home)
            cursor = 0;
        else if (event.key == platform::Key::Left)
            cursor = text::PreviousCharacter(currentName, cursor);
        else
            cursor = text::NextCharacter(currentName, cursor);
        break;
    default:
        return;
    }
    RefreshListVanilla();
    event.handled = true;
}

void SaveLoadWindow::Char(UiEvent& event) {
    if (!isSave) return;
    switch (event.character) {
    case '\b':
    case '\t':
    case '\r':
    case '\x1B':
        break;
    default: {
        int currentSelected = selected;
        if (currentSelected >= 0 && static_cast<unsigned>(currentSelected) < static_cast<unsigned>(EntryCount()) &&
            !IsReservedSave(currentSelected) && !nameTyped) {
            nameTyped = true;
            name.clear();
            cursor = 0;
        }
        auto& currentName = name;
        if (!g_interface.ctrlDown && text::CharacterCount(currentName) < kMaxNameLength) {
            std::string tail = str::Mid(currentName, cursor);
            // The character's UTF-8 bytes between the text before the cursor and the tail.
            std::string typed = text::Typed(str::Left(currentName, cursor), event.character) + tail;
            if (g_interface.TextExtent(typed, 1).w < kMaxNameWidth) {
                currentName = typed;
                cursor += text::TypedLength(event.character);
            }
        }
        RefreshListVanilla();
        break;
    }
    }
    UiContainer::OnEvent(event);
}
