// The save / load window's saving, its entry list and the selected save's picture (ui.h, CSaveLoadWindow). The window shows
// two 256x256 5:6:5 textures; a 256x192 picture (0x18000 bytes, 5:6:5) is drawn into the top of one on the GPU
// (render::DrawIntoTexture), from shot.sav or the escape menu's copy of the screen, which the window keeps for the
// save's shot.sav. See docs/notes/ui.md, "Save / load window pictures", and docs/notes/renderer.md, section 6.
#include "app.h"
#include "app_helpers.h"
#include "lib/calendar.h"
#include "core_streams.h"
#include "lib/text_case.h"
#include "ui.h"
#include "game_strings.h"

#include "console.h"
#include "item_db_text.h"
#include "net_link.h"
#include "object_tables.h"
#include "texture.h"
#include "ui_cursor.h"
#include "world.h"

#include "lib/crt.h"
#include "lib/file_io.h"
#include "lib/gameapi.h"
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
constexpr unsigned kOpenWrite = lib::File::modeCreate | lib::File::modeWrite;
constexpr std::uint32_t kClosed = 0x100002;          // pending value after saving
constexpr int kVisibleRows = 16;

// --- Helpers -------------------------------------------------------------------------------------

// The 256x192 5:6:5 picture `pixels` (`bytes` of it: whole rows) drawn into the top of `texture`.
void DrawPicture(CTexture* texture, const void* pixels, std::uint32_t bytes) {
    const int rows = static_cast<int>(bytes / (kShotWidth * 2));
    const render::TextureImage picture{nullptr, pixels, bytes, kShotWidth, rows, render::ImageLayout::Rgb565};
    if (rows > 0) render::DrawIntoTexture(texture->surface_, &picture, 0, 0, kShotWidth, rows);
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
    std::string folder;
    folder = str::Format(flowdata::kSlotFolder, DataFolderText(), slot);
    return folder;
}

// The first line of the text `prefix` + `suffix`, or empty when there is none.
std::string FirstLineOf(const char* prefix, const std::string& suffix) {
    std::string line;
    CTextManager* texts = g_clientWorld.texts_;
    bool found = texts->Open(std::string(prefix) + suffix);
    if (found) texts->ReadLine(&line, 1);
    return line;
}

constexpr const char* kSoundOk = "buttons\\messbox\\ok.wav";
constexpr const char* kSoundDelete = "buttons\\save\\delete.wav";
constexpr int kAskOverwrite = 0x20000, kAskDelete = 0x10000;            // the boxes' results (high word)
constexpr const char* kFolderFormat = "%ssaves\\%s";
constexpr const char* kEveryFile = "\\*.*";

// A message box `name` / `text` on top of the interface (a failed allocation pushes null, as the
// original does).
void Ask(CInterface3D* ui, const char* name, const char* text, int result) {
    CUiWindow* box = new CMessageBox(ui, name, text, result, 3, 1);
    ui->PushWindow(box, 1, 1);
}

} // namespace


void CSaveLoadWindow::ShowPicture() {
    SceneMesh(3)->visible_ = false;
    int selected = selected_;
    if (selected < 0 || static_cast<unsigned>(selected) >= static_cast<unsigned>(EntryCount())) return;
    SaveEntry* entry = entries_.data() + selected;
    if (entry->broken_) return;
    if (isSave_ && selected == 0) {
        // The new save: the game's picture.
        if (!dialog_) return;
        SceneMesh(3)->visible_ = true;
        SceneMesh(3)->texture_ = dialog_;
        return;
    }
    std::string folder;
    folder = str::Format(flowdata::kSlotFolder, DataFolderText(), entry->folder_);
    lib::FileFind find;
    bool found;
    {
        std::string path = folder + game::savewords::ShotFile();
        found = find.FindFile(path);
    }
    if (!found) return;
    // The file's picture (the rows a short file has; the rest keeps the last picture's, as the original's read).
    std::vector<std::uint8_t> picture(kShotBytes);
    lib::File file;
    {
        std::string path = folder + game::savewords::ShotFile();
        file.Open(path, lib::File::modeRead);
    }
    const unsigned read = file.Read(picture.data(), kShotBytes);
    file.Close();
    DrawPicture(objects_[1], picture.data(), read);
    SceneMesh(3)->visible_ = true;
    SceneMesh(3)->texture_ = objects_[1];
}

void SaveEntry::MakeTitle() {
    std::string quick;
    std::string autoEnter;
    std::string autoExit;
    g_clientWorld.texts_->GetString(kQuickTitle, &quick);
    g_clientWorld.texts_->GetString(kAutoEnterTitle, &autoEnter);
    g_clientWorld.texts_->GetString(kAutoExitTitle, &autoExit);
    auto& title = title_;
    if (text::CompareIgnoringCase(folder_, kQuickFolder) == 0) {
        title = str::Format(kPlainFormat, quick);
    } else if (text::CompareIgnoringCase(folder_, kAutoFolderA) == 0) {
        title = str::Format(kPlainFormat, autoEnter);
    } else if (text::CompareIgnoringCase(folder_, kAutoFolderB) == 0) {
        title = str::Format(kPlainFormat, autoExit);
    } else {
        // The zone name ("zone" + zone_ in the texts, else "save_camp").
        const std::string name = ZoneTitle();
        title = str::Format(kPlainFormat, name);
    }
}

void CSaveLoadWindow::SaveGame() {
    int selected = selected_;
    if (selected < 0 || static_cast<unsigned>(selected) >= static_cast<unsigned>(EntryCount())) return;
    SaveEntry* entry = entries_.data() + selected;
    double space = app::DiskFreeSpace(DataFolderText());
    // 0 means unknown: no check.
    EI_NAN_AUDIT(space);
    if (space != 0.0) {
        int needed = CurrentSaveSize();
        if (static_cast<double>(needed) > space) {
            CUiWindow* box = new CMessageBox(interface_, game::savewords::NoSpace(), game::savewords::NoSpaceText(), 0x30000, 1, 1);
            interface_->PushWindow(box, 1, 1);
            return;
        }
    }
    std::string folder;
    folder = str::Format(flowdata::kSlotFolder, DataFolderText(), entry->folder_);
    std::string notice;
    g_clientWorld.texts_->GetString(game::savewords::NotifySaving(), &notice);
    g_interface.ShowStatusText(notice);
    g_gameFlow.SaveGameThunk(entry->folder_);
    g_cursor.SetShape(3);

    // info.sav: the new save's entry (the first) with the title being given.
    netlink::FileStream info;
    {
        std::string path = folder + game::savewords::InfoFile();
        info.Open(path, kOpenWrite, 0);
    }
    if (nameTyped_)
        entries_.data()->title_ = name_;
    else if (selected_ == 0)
        entries_.data()->title_.clear();
    else
        entries_.data()->title_ = entry->title_;
    entries_.data()->savedAt_ = lib::CurrentTime();
    SaveEntry* first = entries_.data();
    info.WriteHeader(kInfoMagic, kInfoVersion);
    float gameTime = first->gameTime_;
    info.WriteValue(gameTime);
    info.WriteString(first->map_);
    info.WriteString(first->zone_);
    std::int32_t savedAt = first->savedAt_;
    info.WriteValue(savedAt);
    info.WriteString(first->title_);
    info.Close();

    // shot.sav: the game's picture.
    if (dialog_) {
        lib::File shot;
        {
            std::string path = folder + game::savewords::ShotFile();
            shot.Open(path, kOpenWrite);
        }
        shot.Write(newPicture_.data(), kShotBytes);
        shot.Close();
    }
    g_cursor.SetShape(0);
    interface_->SetPendingValue(kClosed);
}

void CSaveLoadWindow::FillEntries() {
    std::string pattern;
    pattern = str::Format(game::savewords::SavePattern(), DataFolderText());
    lib::FileFind find;
    entries_.clear();
    int highest = 0;     // the highest number after "save" in the folder names
    bool more = find.FindFile(pattern);
    while (more) {
        more = find.FindNext();
        if (find.IsDots() || !find.MatchesMask(FILE_ATTRIBUTE_DIRECTORY)) continue;
        // A new entry (the original copies a stack temporary whose numbers are left as the stack held them).
        SaveEntry* entry = &entries_.emplace_back();
        entry->folder_ = find.GetFileName();
        lib::FileFind infoFind;
        bool hasInfo;
        {
            std::string folderPath = find.GetFilePath();
            std::string path = folderPath + game::savewords::InfoInFolder();
            hasInfo = infoFind.FindFile(path);
        }
        if (!hasInfo) {
            entry->broken_ = true;
            entry->title_ = game::savewords::BrokenTitle();
        } else {
            infoFind.FindNext();
            netlink::FileStream info;
            info.Open(infoFind.GetFilePath(), 0, 1);
            if (info.ReadHeader(kInfoMagic, (int)kInfoVersion, 1)) {
                info.ReadValue(entry->gameTime_);
                {
                    std::string map;
                    info.ReadString(&map);
                    entry->map_ = map;
                }
                info.ReadInto(&entry->zone_);
                // The version the header read.
                if (static_cast<int>(info.version_) > 1) {
                    std::int32_t savedAt;
                    info.ReadValue(savedAt);
                    entry->savedAt_ = savedAt;
                }
                if (static_cast<int>(info.version_) > 2) info.ReadInto(&entry->title_);
            }
            info.Close();
            if (entry->title_.size() == 0) entry->MakeTitle();
            entry->broken_ = false;
        }
        infoFind.Close();
        if (static_cast<int>(entry->folder_.size()) > 4) {
            int number = str::ToInt(str::Mid(entry->folder_, 4));
            if (!(highest > number)) highest = number;
        }
    }
    find.Close();
    // The newest first: the order is "not older than" (!(a < b)), so saves of the same time compare true both ways
    // (not a strict weak ordering: the game's sort, whose unguarded scans can then run off the range as in the
    // game, and the order it leaves such saves in).
    lib::IntroSort(entries_.data(), entries_.data() + entries_.size(),
                   [](const SaveEntry& a, const SaveEntry& b) { return !(a.savedAt_ < b.savedAt_); });

    if (isSave_) {
        // The new save first.
        SaveEntry* entry = &*entries_.emplace(entries_.begin());
        entry->folder_ = str::Format(game::savewords::NewFolder(), highest + 1);
        entry->gameTime_ = LocalPlayerVariable(game::savewords::GameTime());
        entry->zone_ = LocalPlayerZone();
        if (MapRecord* record = g_mapDatabase.FindMap(entry->zone_))
            entry->map_ = record->allod;
        entry->MakeTitle();
        entry->broken_ = false;

        // The game's picture: a new texture drawn from the escape menu's copy of the screen, the copy kept for shot.sav.
        if (dialog_) dialog_->Release();
        dialog_ = nullptr;
        // The escape menu's 256x192 5:6:5 copy of the screen, or null.
        if (const auto* copy = static_cast<const std::uint8_t*>(interface_->screenCopy_)) {
            CTexture* shot = CreateTexture(0x100, 0x100, TextureKind::Blank565);
            dialog_ = shot;
            shot->AddRef();
            RemoveObjectAt(std::ssize(objects_) - 1);
            newPicture_.assign(copy, copy + kShotBytes);
            DrawPicture(dialog_, newPicture_.data(), kShotBytes);
        }
    }

    if (static_cast<unsigned>(EntryCount()) > static_cast<unsigned>(kVisibleRows)) {
        int position;
        scrollbar_->TakePosition(&position);
        scrollbar_->SetMaximum(EntryCount() - kVisibleRows);
        scrollbar_->SetPosition(position);
    } else {
        scrollbar_->SetMaximum(0);
    }
    firstVisible_ = 0;
}

// --- Quick and automatic saves ------------------------------------------------------------------------

void QuickSave(const char* slot, bool stepFrame) {
    if (IsMultiplayerClient()) return;
    double space = app::DiskFreeSpace(DataFolderText());
    // 0 means unknown: no check.
    EI_NAN_AUDIT(space);
    if (space != 0.0 && static_cast<double>(CurrentSaveSize()) > space) return;
    if (stepFrame) app::FrameStep(false, true);
    // A null surface (no memory) is used all the same, as in the original.
    CUiSurface* surface = new CUiSurface(&g_interface, 0, 0, kScreenWidth, kScreenHeight, SurfaceBlend::Opaque, true);
    surface->CaptureScreen();
    g_cursor.SetShape(3);
    g_interface.serverFlag_ = static_cast<std::uint8_t>(g_serverWorld.paused_);
    std::string notice;
    g_clientWorld.texts_->GetString(game::savewords::NotifySaving(), &notice);
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
    info.Open(folder + game::savewords::InfoFile(), kOpenWrite, 0);
    info.WriteHeader(kInfoMagic, kInfoVersion);
    info.WriteValue(gameTime);
    info.WriteString(map);
    info.WriteString(zone);
    info.WriteValue(savedAt);
    info.WriteString(title);
    info.Close();

    // shot.sav: the copied screen at 256x192.
    std::uint16_t* pixels = surface->Rgb565Copy(kShotWidth, kShotHeight);
    lib::File shot;
    shot.Open(folder + game::savewords::ShotFile(), kOpenWrite);
    shot.Write(pixels, kShotBytes);
    shot.Close();
    delete[] pixels;
    if (surface != nullptr) {
        delete surface;
    }
    g_cursor.SetShape(0);
}

void QuickLoad(const char* slot) {
    if (IsMultiplayerClient()) return;
    std::string folder = SlotFolder(slot);
    if (!lib::FileExists(folder)) return;
    std::string notice;
    g_clientWorld.texts_->GetString(kNotifyLoading, &notice);
    g_interface.ShowStatusText(notice);
    GameStopSounds(1);
    g_gameFlow.RequestLoad(slot, 0);
}

// --- Entry titles, OK / Delete / load / delete ----------------------------------------------

std::string SaveEntry::ZoneTitle() const {
    std::string title = FirstLineOf("zone ", zone_);   // the key has a space
    if (title.size() == 0) g_clientWorld.texts_->GetString("save_camp", &title);
    return title;
}

std::string SaveEntry::AllodTitle() const {
    std::string title = FirstLineOf("allod ", map_);  // the key has a space
    if (title.size() == 0) title = "Unknown";
    return title;
}

void CSaveLoadWindow::Confirm() {
    if (selected_ < 0) return;
    if (!isSave_) {
        LoadSelected();
        return;
    }
    interface_->PlaySound(kSoundOk);
    if (selected_ == 0) {
        SaveGame();
        return;
    }
    Ask(interface_, "overwrite", "overwrite_msg", kAskOverwrite);
}

void CSaveLoadWindow::AskDelete() {
    if (selected_ < 0 || (isSave_ && selected_ == 0)) return;
    interface_->PlaySound(kSoundDelete);
    Ask(interface_, "delete", "delete_msg", kAskDelete);
}

void CSaveLoadWindow::LoadSelected() {
    int selected = selected_;
    if (selected < 0 || static_cast<unsigned>(selected) >= static_cast<unsigned>(EntryCount())) return;
    if (entries_.data()[selected].broken_) return;
    std::string notice;
    g_clientWorld.texts_->GetString(kNotifyLoading, &notice);
    g_interface.ShowStatusText(notice);
    GameStopSounds(1);
    interface_->PlaySound(kSoundOk);
    g_gameFlow.RequestLoad(entries_.data()[selected_].folder_, 0);
}

void CSaveLoadWindow::DeleteSave() {
    int selected = selected_;
    if (selected < 0 || static_cast<unsigned>(selected) >= static_cast<unsigned>(EntryCount())) return;
    std::string folder;
    folder = str::Format(kFolderFormat, DataFolderText(), entries_[selected].folder_);
    lib::FileFind find;
    bool more = find.FindFile(folder + kEveryFile);
    while (more) {
        more = find.FindNext();
        gameapi::DeleteFileA(find.GetFilePath().c_str());
    }
    find.Close();
    gameapi::RemoveDirectoryA(folder.c_str());
    FillEntries();
    int count = EntryCount();
    if (static_cast<unsigned>(selected_) >= static_cast<unsigned>(count)) selected_ = count - 1;
    TakeSelectedTitle();
    RefreshListVanilla();
    ShowPicture();
}
