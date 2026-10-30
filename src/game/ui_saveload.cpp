// The save / load window's saving, its entry list and the selected save's picture (ui.h, SaveLoadWindow). The window shows
// two 256x256 5:6:5 textures; a 256x192 picture (0x18000 bytes, 5:6:5) is drawn into the top of one on the GPU
// (render::DrawIntoTexture), from shot.sav or the escape menu's copy of the screen, which the window keeps for the
// save's shot.sav. See docs/notes/ui.md, "Save / load window pictures", and docs/notes/renderer.md, section 6.
#include "app.h"
#include "app_helpers.h"
#include "lib/calendar.h"
#include "core_streams.h"
#include "lib/containers.h"
#include "lib/text_case.h"
#include "ui.h"
#include "game_strings.h"

#include "console.h"
#include "text_manager.h"
#include "net_link.h"
#include "object_tables.h"
#include "texture.h"
#include "ui_cursor.h"
#include "world.h"

#include "lib/crt.h"
#include "platform/platform.h"
#include "lib/intro_sort.h"
#include <cstdint>
#include <cstring>
#include <string>
#include "render/render.h"
#include "game_flow_data.h"
#include "world_glue.h"
#include "lib/nan_audit.h"
#include "game_singletons.h"
#include "lib/str.h"

#include <cstdlib>
#include <memory>
#include <new>
#include <vector>

namespace {

constexpr std::uint32_t kShotBytes = 0x18000;        // 256 x 192 pixels, 16 bits
constexpr int kShotWidth = 256, kShotHeight = 192;
constexpr unsigned kInfoMagic = 0x111;
constexpr unsigned kInfoVersion = 3;
constexpr std::uint32_t kClosed = 0x100002;          // pending value after saving
constexpr int kVisibleRows = 16;

// --- Helpers -------------------------------------------------------------------------------------

// The 256x192 5:6:5 picture `pixels` (`bytes` of it: whole rows) drawn into the top of `texture`.
void DrawPicture(Texture* texture, const void* pixels, std::uint32_t bytes) {
    const int rows = static_cast<int>(bytes / (kShotWidth * 2));
    const render::TextureImage picture{nullptr, pixels, bytes, kShotWidth, rows, render::ImageLayout::Rgb565};
    if (rows > 0) render::DrawIntoTexture(texture->surface, &picture, 0, 0, kShotWidth, rows);
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
    std::string folder = str::Format(flowdata::kSlotFolder, DataFolderText(), slot);
    return folder;
}

// The first line of the text `prefix` + `suffix`, or empty when there is none.
std::string FirstLineOf(const char* prefix, const std::string& suffix) {
    std::string line;
    TextManager* texts = g_clientWorld.texts;
    bool found = texts->Open(std::string(prefix) + suffix);
    if (found) line = texts->ReadLine(true);
    return line;
}

constexpr const char* kSoundOk = "buttons\\messbox\\ok.wav";
constexpr const char* kSoundDelete = "buttons\\save\\delete.wav";
constexpr int kAskOverwrite = 0x20000, kAskDelete = 0x10000;            // the boxes' results (high word)
constexpr const char* kFolderFormat = "%ssaves\\%s";
constexpr const char* kEveryFile = "\\*.*";

// A message box `name` / `text` on top of the interface (a failed allocation pushes null, as the
// original does).
void Ask(Interface3D* ui, const char* name, const char* text, int result) {
    UiWindow* box = new MessageWindow(ui, name, text, result, 3, 1);
    ui->PushWindow(box, true, true);
}

} // namespace


void SaveLoadWindow::ShowPicture() {
    SceneMesh(3)->visible = false;
    int currentSelected = selected;
    if (currentSelected < 0 || static_cast<unsigned>(currentSelected) >= static_cast<unsigned>(EntryCount())) return;
    SaveEntry* entry = entries.data() + currentSelected;
    if (entry->broken) return;
    if (isSave && currentSelected == 0) {
        // The new save: the game's picture.
        if (!dialog) return;
        SceneMesh(3)->visible = true;
        SceneMesh(3)->texture = dialog;
        return;
    }
    std::string folder = str::Format(flowdata::kSlotFolder, DataFolderText(), entry->folder);
    platform::FileFind find;
    bool found;
    {
        std::string path = folder + game::savewords::ShotFile();
        found = find.FindFile(path);
    }
    if (!found) return;
    // The file's picture (the rows a short file has; the rest keeps the last picture's, as the original's read).
    std::vector<std::uint8_t> picture(kShotBytes);
    platform::File file;
    {
        std::string path = folder + game::savewords::ShotFile();
        file.Open(path, platform::FileMode::Read);
    }
    const unsigned read = file.Read(picture.data(), kShotBytes);
    file.Close();
    DrawPicture(objects[1], picture.data(), read);
    SceneMesh(3)->visible = true;
    SceneMesh(3)->texture = objects[1];
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
    double space = platform::FreeDiskSpace(DataFolderText());
    // 0 means unknown: no check.
    EI_NAN_AUDIT(space);
    if (space != 0.0) {
        int needed = CurrentSaveSize();
        if (static_cast<double>(needed) > space) {
            UiWindow* box = new MessageWindow(ui, game::savewords::NoSpace(), game::savewords::NoSpaceText(), 0x30000, 1, 1);
            ui->PushWindow(box, true, true);
            return;
        }
    }
    std::string folder = str::Format(flowdata::kSlotFolder, DataFolderText(), entry->folder);
    std::string notice = g_clientWorld.texts->GetString(game::savewords::NotifySaving());
    g_interface.ShowStatusText(notice);
    g_gameFlow.SaveGameThunk(entry->folder);
    g_cursor.SetShape(3);

    // info.sav: the new save's entry (the first) with the title being given.
    netlink::FileStream info;
    {
        std::string path = folder + game::savewords::InfoFile();
        info.Open(path, platform::FileMode::Create, 0);
    }
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
        {
            std::string path = folder + game::savewords::ShotFile();
            shot.Open(path, platform::FileMode::Create);
        }
        shot.Write(newPicture.data(), kShotBytes);
        shot.Close();
    }
    g_cursor.SetShape(0);
    ui->SetPendingValue(kClosed);
}

void SaveLoadWindow::FillEntries() {
    std::string pattern = str::Format(game::savewords::SavePattern(), DataFolderText());
    platform::FileFind find;
    entries.clear();
    int highest = 0;     // the highest number after "save" in the folder names
    bool more = find.FindFile(pattern);
    while (more) {
        more = find.FindNext();
        if (find.IsDots() || !find.IsDirectory()) continue;
        // A new entry (the original copies a stack temporary whose numbers are left as the stack held them).
        SaveEntry* entry = &entries.emplace_back();
        entry->folder = find.GetFileName();
        platform::FileFind infoFind;
        bool hasInfo;
        {
            std::string folderPath = find.GetFilePath();
            std::string path = folderPath + game::savewords::InfoInFolder();
            hasInfo = infoFind.FindFile(path);
        }
        if (!hasInfo) {
            entry->broken = true;
            entry->title = game::savewords::BrokenTitle();
        } else {
            infoFind.FindNext();
            netlink::FileStream info;
            info.Open(infoFind.GetFilePath(), platform::FileMode::Read, 1);
            if (info.ReadHeader(kInfoMagic, (int)kInfoVersion, 1)) {
                info.ReadValue(entry->gameTime);
                entry->map = info.ReadString();
                entry->zone = info.ReadString();
                // The version the header read.
                if (static_cast<int>(info.version) > 1) {
                    std::int32_t savedAt;
                    info.ReadValue(savedAt);
                    entry->savedAt = savedAt;
                }
                if (static_cast<int>(info.version) > 2) entry->title = info.ReadString();
            }
            info.Close();
            if (entry->title.size() == 0) entry->MakeTitle();
            entry->broken = false;
        }
        infoFind.Close();
        if (static_cast<int>(entry->folder.size()) > 4) {
            int number = str::ToInt(str::Mid(entry->folder, 4));
            if (!(highest > number)) highest = number;
        }
    }
    find.Close();
    // The newest first: the order is "not older than" (!(a < b)), so saves of the same time compare true both ways
    // (not a strict weak ordering: the game's sort, whose unguarded scans can then run off the range as in the
    // game, and the order it leaves such saves in).
    lib::IntroSort(entries.data(), entries.data() + entries.size(),
                   [](const SaveEntry& a, const SaveEntry& b) { return !(a.savedAt < b.savedAt); });

    if (isSave) {
        // The new save first.
        SaveEntry* entry = &*entries.emplace(entries.begin());
        entry->folder = str::Format(game::savewords::NewFolder(), highest + 1);
        entry->gameTime = LocalPlayerVariable(game::savewords::GameTime());
        entry->zone = LocalPlayerZone();
        if (MapRecord* record = g_mapDatabase.FindMap(entry->zone))
            entry->map = record->allod;
        entry->MakeTitle();
        entry->broken = false;

        // The game's picture: a new texture drawn from the escape menu's copy of the screen, the copy kept for shot.sav.
        if (dialog) dialog->Release();
        dialog = nullptr;
        // The escape menu's 256x192 5:6:5 copy of the screen, or null.
        if (!ui->screenCopy.empty()) {
            const auto* copy = reinterpret_cast<const std::uint8_t*>(ui->screenCopy.data());
            Texture* shot = CreateTexture(0x100, 0x100, TextureKind::Blank565);
            dialog = shot;
            shot->AddRef();
            RemoveObjectAt(lib::Count(objects) - 1);
            newPicture.assign(copy, copy + kShotBytes);
            DrawPicture(dialog, newPicture.data(), kShotBytes);
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
    double space = platform::FreeDiskSpace(DataFolderText());
    // 0 means unknown: no check.
    EI_NAN_AUDIT(space);
    if (space != 0.0 && static_cast<double>(CurrentSaveSize()) > space) return;
    if (stepFrame) app::FrameStep(false, true);
    // A null surface (no memory) is used all the same, as in the original.
    UiSurface* surface = new UiSurface(&g_interface, 0, 0, kScreenWidth, kScreenHeight, SurfaceBlend::Opaque, true);
    surface->CaptureScreen();
    g_cursor.SetShape(3);
    g_interface.serverFlag = static_cast<std::uint8_t>(g_serverWorld.paused);
    std::string notice = g_clientWorld.texts->GetString(game::savewords::NotifySaving());
    g_interface.ShowStatusText(notice);
    g_gameFlow.SaveGameThunk(slot);

    // info.sav: the "gtime" variable, the zone's map, the zone, the time and an empty title.
    std::string title;
    std::string map;
    const std::int32_t savedAt = lib::CurrentTime();
    const float gameTime = LocalPlayerVariable(game::savewords::GameTime());
    const std::string zone = LocalPlayerZone();
    if (const MapRecord* record = g_mapDatabase.FindMap(zone))
        map = record->allod;
    std::string folder = SlotFolder(slot);
    netlink::FileStream info;
    info.Open(folder + game::savewords::InfoFile(), platform::FileMode::Create, 0);
    info.WriteHeader(kInfoMagic, kInfoVersion);
    info.WriteValue(gameTime);
    info.WriteString(map);
    info.WriteString(zone);
    info.WriteValue(savedAt);
    info.WriteString(title);
    info.Close();

    // shot.sav: the copied screen at 256x192.
    const std::vector<std::uint16_t> pixels = surface->Rgb565Copy(kShotWidth, kShotHeight);
    platform::File shot;
    shot.Open(folder + game::savewords::ShotFile(), platform::FileMode::Create);
    shot.Write(pixels.data(), kShotBytes);
    shot.Close();
    if (surface != nullptr) {
        delete surface;
    }
    g_cursor.SetShape(0);
}

void QuickLoad(const char* slot) {
    if (IsMultiplayerClient()) return;
    std::string folder = SlotFolder(slot);
    if (!platform::FileExists(folder)) return;
    std::string notice = g_clientWorld.texts->GetString(kNotifyLoading);
    g_interface.ShowStatusText(notice);
    GameStopSounds(1);
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
    ui->PlayInterfaceSound(kSoundOk);
    if (selected == 0) {
        SaveGame();
        return;
    }
    Ask(ui, "overwrite", "overwrite_msg", kAskOverwrite);
}

void SaveLoadWindow::AskDelete() {
    if (selected < 0 || (isSave && selected == 0)) return;
    ui->PlayInterfaceSound(kSoundDelete);
    Ask(ui, "delete", "delete_msg", kAskDelete);
}

void SaveLoadWindow::LoadSelected() {
    int currentSelected = selected;
    if (currentSelected < 0 || static_cast<unsigned>(currentSelected) >= static_cast<unsigned>(EntryCount())) return;
    if (entries.data()[currentSelected].broken) return;
    std::string notice = g_clientWorld.texts->GetString(kNotifyLoading);
    g_interface.ShowStatusText(notice);
    GameStopSounds(1);
    ui->PlayInterfaceSound(kSoundOk);
    g_gameFlow.RequestLoad(entries.data()[selected].folder);
}

void SaveLoadWindow::DeleteSave() {
    int currentSelected = selected;
    if (currentSelected < 0 || static_cast<unsigned>(currentSelected) >= static_cast<unsigned>(EntryCount())) return;
    std::string folder = str::Format(kFolderFormat, DataFolderText(), entries[currentSelected].folder);
    platform::FileFind find;
    bool more = find.FindFile(folder + kEveryFile);
    while (more) {
        more = find.FindNext();
        platform::RemoveFile(find.GetFilePath().c_str());
    }
    find.Close();
    platform::RemoveFolder(folder.c_str());
    FillEntries();
    int count = EntryCount();
    if (static_cast<unsigned>(selected) >= static_cast<unsigned>(count)) selected = count - 1;
    TakeSelectedTitle();
    RefreshListVanilla();
    ShowPicture();
}
