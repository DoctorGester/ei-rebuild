// The save / load window's saving, its entry list and the selected save's picture (ui.h, CSaveLoadWindow). The window shows
// two 256x256 5:6:5 textures; the 0x18000 bytes of a 256x192 picture are one block from the start of the pixels (a pitch
// of 512 bytes is taken for granted). The pixels live only in the new renderer's memory, written and read with no Lock
// (DirectPixels). See docs/notes/ui.md, "Save / load window pictures", and docs/notes/renderer.md, section 11.
#include "app.h"
#include "app_helpers.h"
#include "lib/calendar.h"
#include "core_streams.h"
#include "lib/text_case.h"
#include "ui.h"
#include "game_strings.h"

#include "console.h"
#include "dx_surface.h"
#include "item_db_text.h"
#include "text_reader.h"
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
#include "game_data.h"
#include "lib/nan_audit.h"
#include "game_singletons.h"
#include "lib/str.h"

#include <cstdlib>
#include <memory>
#include <new>

namespace {

constexpr std::uint32_t kShotBytes = 0x18000;        // 256 x 192 pixels, 16 bits
constexpr unsigned kInfoMagic = 0x111;
constexpr unsigned kInfoVersion = 3;
constexpr unsigned kOpenWrite = lib::File::modeCreate | lib::File::modeWrite;
constexpr std::uint32_t kClosed = 0x100002;          // pending value after saving
constexpr int kVisibleRows = 16;

// --- Helpers -------------------------------------------------------------------------------------

// The entry is a folder.
BOOL IsDirectory(const lib::FileFind& find) { return find.MatchesMask(0x10ul); }
std::string FilePath(const lib::FileFind& find) { return find.GetFilePath(); }
std::string FileName(const lib::FileFind& find) { return find.GetFileName(); }

// A file stream on the stack with the reads and writes the save list uses.
struct LocalFileStream {
    netlink::FileStream s;

    // The stream's direction byte, then the file's Open.
    void Open(std::string_view path, unsigned flags, std::uint8_t reading) { s.Open(path, flags, reading); }
    void Close() { s.Close(); }
    unsigned Read(void* buffer, unsigned size) { return Stream()->Read(buffer, size); }
    void Write(const void* buffer, unsigned size) { Stream()->Write(buffer, size); }
    CStream* Stream() { return &s; }
    // A string with its terminating zero.
    void WriteText(std::string_view text) {
        Stream()->WriteString(text);
    }
};

CDXSurface* HolderOf(CTexture* texture) { return texture->surface_; }

// The pixels of a picture texture for one fill or read: the holder's memory (the original: a Lock
// with flags 0 and the Unlock after).
class PicturePixels {
public:
    explicit PicturePixels(CDXSurface* holder) : holder_(holder) {}

    // False without memory (the textures always have it; the original's failed Lock whose report said stop).
    bool Begin() { return DirectPixels(holder_, &pixels_); }
    std::uint8_t* Bits() const { return pixels_.bits; }

private:
    CDXSurface* holder_;
    SurfacePixels pixels_;
};

// The escape menu's 256x192 5:6:5 copy of the screen, or null.
const void* SavePicture(CInterface3D* ui) { return ui->block5C_; }

} // namespace


void CSaveLoadWindow::ShowPicture() {
    SceneMesh(3)->visible_ = 0;
    int selected = selected_;
    if (selected < 0 || static_cast<unsigned>(selected) >= static_cast<unsigned>(EntryCount())) return;
    SaveEntry* entry = entries_.data() + selected;
    if (entry->broken_) return;
    if (isSave_ && selected == 0) {
        // The new save: the game's picture.
        if (!dialog_) return;
        SceneMesh(3)->visible_ = 1;
        SceneMesh(3)->texture_ = dialog_;
        return;
    }
    std::string folder;
    folder = str::Format(flowdata::kSlotFolder, DataFolderText(), entry->folder_.c_str());
    lib::FileFind find;
    BOOL found;
    {
        std::string path = folder + game::savewords::ShotFile();
        found = find.FindFile(path);
    }
    if (!found) return;
    CDXSurface* holder = HolderOf(objects_[1]);
    gameapi::EnterCriticalSection(TheDeviceLock());
    PicturePixels pixels(holder);
    if (!pixels.Begin()) return;   // (the device lock stays held, as in the original)
    lib::File file;
    {
        std::string path = folder + game::savewords::ShotFile();
        file.Open(path, lib::File::modeRead);
    }
    file.Read(pixels.Bits(), kShotBytes);
    file.Close();
    gameapi::LeaveCriticalSection(TheDeviceLock());
    SceneMesh(3)->visible_ = 1;
    SceneMesh(3)->texture_ = objects_[1];
}

namespace {
constexpr const char* kQuickFolder = "saveq";
constexpr const char* kAutoFolderA = "savea";
constexpr const char* kAutoFolderB = "saveb";
constexpr const char* kQuickTitle = "save_quick";
constexpr const char* kAutoEnterTitle = "save_auto_enter";
constexpr const char* kAutoExitTitle = "save_auto_exit";
constexpr const char* kPlainFormat = "%s";

bool SameFolder(std::string_view folder, std::string_view name) {
    return text::CompareIgnoringCase(folder, name) == 0;
}
} // namespace

void SaveEntry::MakeTitle(int) {
    std::string quick;
    std::string autoEnter;
    std::string autoExit;
    TheTextManager()->GetString(kQuickTitle, &quick);
    TheTextManager()->GetString(kAutoEnterTitle, &autoEnter);
    TheTextManager()->GetString(kAutoExitTitle, &autoExit);
    auto& title = title_;
    if (SameFolder(folder_, kQuickFolder)) {
        title = str::Format(kPlainFormat, quick.c_str());
    } else if (SameFolder(folder_, kAutoFolderA)) {
        title = str::Format(kPlainFormat, autoEnter.c_str());
    } else if (SameFolder(folder_, kAutoFolderB)) {
        title = str::Format(kPlainFormat, autoExit.c_str());
    } else {
        // The zone name ("zone" + zone_ in the texts, else "save_camp").
        const std::string name = ZoneTitle();
        title = str::Format(kPlainFormat, name.c_str());
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
    folder = str::Format(flowdata::kSlotFolder, DataFolderText(), entry->folder_.c_str());
    std::string notice;
    TheTextManager()->GetString(game::savewords::NotifySaving(), &notice);
    TheInterface()->ShowStatusText(notice);
    TheGameFlow()->SaveGameThunk(entry->folder_.c_str());
    TheCursor()->ChangeShape(3);

    // info.sav: the new save's entry (the first) with the title being given.
    LocalFileStream info;
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
    info.Stream()->WriteHeader(kInfoMagic, kInfoVersion);
    float gameTime = first->gameTime_;
    info.Write(&gameTime, 4);
    info.WriteText(first->map_);
    info.WriteText(first->zone_);
    std::int32_t savedAt = first->savedAt_;
    info.Write(&savedAt, 4);
    info.WriteText(first->title_);
    info.Close();

    // shot.sav: the game's picture.
    if (dialog_) {
        CDXSurface* holder = HolderOf(dialog_);
        gameapi::EnterCriticalSection(TheDeviceLock());
        PicturePixels pixels(holder);
        if (!pixels.Begin()) {
            // (The device lock stays held and the cursor busy, as in the original.)
            interface_->SetPendingValue(kClosed);
            return;
        }
        lib::File shot;
        {
            std::string path = folder + game::savewords::ShotFile();
            shot.Open(path, kOpenWrite);
        }
        shot.Write(pixels.Bits(), kShotBytes);
        shot.Close();
        gameapi::LeaveCriticalSection(TheDeviceLock());
    }
    TheCursor()->ChangeShape(0);
    interface_->SetPendingValue(kClosed);
}

void CSaveLoadWindow::FillEntries() {
    std::string pattern;
    pattern = str::Format(game::savewords::SavePattern(), DataFolderText());
    lib::FileFind find;
    entries_.clear();
    int highest = 0;     // the highest number after "save" in the folder names
    BOOL more = find.FindFile(pattern);
    while (more) {
        more = find.FindNext();
        if (find.IsDots() || !IsDirectory(find)) continue;
        // A new entry (the original copies a stack temporary whose numbers are left as the stack held them).
        SaveEntry* entry = &entries_.emplace_back();
        entry->folder_ = FileName(find);
        lib::FileFind infoFind;
        BOOL hasInfo;
        {
            std::string folderPath = FilePath(find);
            std::string path = folderPath + game::savewords::InfoInFolder();
            hasInfo = infoFind.FindFile(path);
        }
        if (!hasInfo) {
            entry->broken_ = true;
            entry->title_ = game::savewords::BrokenTitle();
        } else {
            infoFind.FindNext();
            LocalFileStream info;
            info.Open(FilePath(infoFind), 0, 1);
            if (info.Stream()->ReadHeader(kInfoMagic, (int)kInfoVersion, 1)) {
                info.Read(&entry->gameTime_, 4);
                {
                    std::string map;
                    info.Stream()->ReadString(&map);
                    entry->map_ = map;
                }
                info.Stream()->ReadInto(&entry->zone_);
                // The version the header read.
                if (static_cast<int>(info.s.version_) > 1) {
                    std::int32_t savedAt;
                    info.Read(&savedAt, 4);
                    entry->savedAt_ = savedAt;
                }
                if (static_cast<int>(info.s.version_) > 2) info.Stream()->ReadInto(&entry->title_);
            }
            info.Close();
            if (entry->title_.size() == 0) entry->MakeTitle(1);
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
        if (MapRecord* record = TheMapDatabase()->FindMap(entry->zone_))
            entry->map_ = record->allod;
        entry->MakeTitle(1);
        entry->broken_ = false;

        // The game's picture: a new texture filled from the escape menu's copy of the screen.
        if (dialog_) dialog_->Release();
        dialog_ = nullptr;
        if (SavePicture(interface_)) {
            CTexture* shot = CreateTexture(0x100, 0x100, 0x1E);
            dialog_ = shot;
            ++shot->refCount_;
            RemoveObjectAt(std::ssize(objects_) - 1);
            CDXSurface* holder = HolderOf(dialog_);
            gameapi::EnterCriticalSection(TheDeviceLock());
            PicturePixels pixels(holder);
            // A failed Lock whose report says stop leaves with the device lock held and the scroll bar as it was (the
            // original's tail of the entry list).
            if (!pixels.Begin()) return;
            std::memcpy(pixels.Bits(), SavePicture(interface_), kShotBytes);
            gameapi::LeaveCriticalSection(TheDeviceLock());
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

namespace {
constexpr const char* kNotifyLoading = "notify_loading";
constexpr int kScreenWidth = 800, kScreenHeight = 600;   // the copy's layout rectangle (immediates)
constexpr int kShotWidth = 256, kShotHeight = 192;

CInterface3D* Interface() { return TheInterface(); }
GameFlow* Flow() { return TheGameFlow(); }

// The save folder saves\<slot>\ under the game's folder.
std::string SlotFolder(const char* slot) {
    std::string folder;
    folder = str::Format(flowdata::kSlotFolder, DataFolderText(), slot);
    return folder;
}
} // namespace

void QuickSave(const char* slot, char stepFrame) {
    if (IsMultiplayerClient()) return;
    double space = app::DiskFreeSpace(DataFolderText());
    // 0 means unknown: no check.
    EI_NAN_AUDIT(space);
    if (space != 0.0 && static_cast<double>(CurrentSaveSize()) > space) return;
    if (stepFrame) app::FrameStep(false, true);
    // A null surface (no memory) is used all the same, as in the original.
    CUiSurface* surface = new CUiSurface(Interface(), 0, 0, kScreenWidth, kScreenHeight, 4, 1);
    surface->CaptureScreen();
    TheCursor()->ChangeShape(3);
    Interface()->serverFlag_ = static_cast<std::uint8_t>(GameServerPaused());
    std::string notice;
    TheTextManager()->GetString(game::savewords::NotifySaving(), &notice);
    TheInterface()->ShowStatusText(notice);
    TheGameFlow()->SaveGameThunk(slot);

    // info.sav: the "gtime" variable, the zone's map, the zone, the time and an empty title.
    std::string title;
    std::string map;
    const std::int32_t savedAt = lib::CurrentTime();
    const float gameTime = LocalPlayerVariable(game::savewords::GameTime());
    const std::string zone = LocalPlayerZone();
    if (const MapRecord* record = TheMapDatabase()->FindMap(zone))
        map = record->allod;
    std::string folder = SlotFolder(slot);
    LocalFileStream info;
    info.Open((folder + game::savewords::InfoFile()), kOpenWrite, 0);
    info.Stream()->WriteHeader(kInfoMagic, kInfoVersion);
    info.Write(&gameTime, 4);
    info.WriteText(map);
    info.WriteText(zone);
    info.Write(&savedAt, 4);
    info.WriteText(title.c_str());
    info.Close();

    // shot.sav: the copied screen at 256x192.
    std::uint16_t* pixels = surface->Rgb565Copy(kShotWidth, kShotHeight);
    lib::File shot;
    shot.Open((folder + game::savewords::ShotFile()), kOpenWrite);
    shot.Write(pixels, kShotBytes);
    shot.Close();
    delete[] pixels;
    if (surface != nullptr) {
        delete surface;
    }
    TheCursor()->ChangeShape(0);
}

void QuickLoad(const char* slot) {
    if (IsMultiplayerClient()) return;
    std::string folder = SlotFolder(slot);
    if (!lib::FileExists(folder.c_str())) return;
    std::string notice;
    TheTextManager()->GetString(kNotifyLoading, &notice);
    TheInterface()->ShowStatusText(notice);
    GameStopSounds(1);
    Flow()->RequestLoad(slot, 0);
}

// --- Entry titles, OK / Delete / load / delete ----------------------------------------------

namespace {

// The first line of the text `prefix` + `suffix`, or empty when there is none.
std::string FirstLineOf(const char* prefix, const std::string& suffix) {
    std::string line;
    CTextManager* texts = TheTextManager();
    BOOL found = texts->Open((std::string(prefix) + suffix).c_str());
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

std::string SaveEntry::ZoneTitle() const {
    std::string title = FirstLineOf("zone ", zone_);   // the key has a space
    if (title.size() == 0) TheTextManager()->GetString("save_camp", &title);
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
    interface_->PlaySound(kSoundOk, 0);
    if (selected_ == 0) {
        SaveGame();
        return;
    }
    Ask(interface_, "overwrite", "overwrite_msg", kAskOverwrite);
}

void CSaveLoadWindow::AskDelete() {
    if (selected_ < 0 || (isSave_ && selected_ == 0)) return;
    interface_->PlaySound(kSoundDelete, 0);
    Ask(interface_, "delete", "delete_msg", kAskDelete);
}

void CSaveLoadWindow::LoadSelected() {
    int selected = selected_;
    if (selected < 0 || static_cast<unsigned>(selected) >= static_cast<unsigned>(EntryCount())) return;
    if (entries_.data()[selected].broken_) return;
    std::string notice;
    TheTextManager()->GetString(kNotifyLoading, &notice);
    TheInterface()->ShowStatusText(notice);
    GameStopSounds(1);
    interface_->PlaySound(kSoundOk, 0);
    Flow()->RequestLoad(entries_.data()[selected_].folder_.c_str(), 0);
}

void CSaveLoadWindow::DeleteSave() {
    int selected = selected_;
    if (selected < 0 || static_cast<unsigned>(selected) >= static_cast<unsigned>(EntryCount())) return;
    std::string folder;
    folder = str::Format(kFolderFormat, DataFolderText(), entries_[selected].folder_.c_str());
    lib::FileFind find;
    BOOL more = find.FindFile((folder + kEveryFile));
    while (more) {
        more = find.FindNext();
        gameapi::DeleteFileA(FilePath(find).c_str());
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
