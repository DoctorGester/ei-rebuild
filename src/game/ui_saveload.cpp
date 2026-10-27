// The save / load window's saving, its entry list and the selected save's picture (ui.h, CSaveLoadWindow). The window shows
// two 256x256 5:6:5 textures; the 0x18000 bytes of a 256x192 picture are one block from the start of the pixels (a pitch
// of 512 bytes is taken for granted). The pixels live only in the new renderer's memory, written and read with no Lock
// (DirectPixels). See docs/notes/ui.md, "Save / load window pictures", and docs/notes/renderer.md, section 11.
#include "app.h"
#include "app_helpers.h"
#include "lib/mfc_time.h"
#include "core_streams.h"
#include "stl_sorts.h"
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

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "render/render.h"
#include "record_lifetime.h"
#include "game_flow_data.h"
#include "world_glue.h"
#include "record_vectors.h"
#include "game_data.h"
#include "lib/nan_audit.h"
#include "game_singletons.h"
#include "lib/mfc_string.h"
#include "lib/str.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <string>

namespace {

constexpr std::uint32_t kShotBytes = 0x18000;        // 256 x 192 pixels, 16 bits
constexpr unsigned kInfoMagic = 0x111;
constexpr unsigned kInfoVersion = 3;
constexpr unsigned kOpenWrite = 0x1001;              // CFile::modeCreate | modeWrite
constexpr std::uint32_t kClosed = 0x100002;          // pending value after saving
constexpr int kVisibleRows = 16;

// --- Helpers -------------------------------------------------------------------------------------

// An entry copy-constructed in place (the list's STLport vector).
void EntryConstruct(SaveEntry* at, const SaveEntry* from) { stlp::ConstructCopyAt<SaveEntry, recordvec::SaveEntryElement>(at, from); }
// CFileFind's MatchesMask(FILE_ATTRIBUTE_DIRECTORY) through the vtable as the original calls it.
BOOL IsDirectory(const mfc::CFileFind& find) {
    return reinterpret_cast<const mfc::CFileFindVirtuals*>(&find)->MatchesMask(0x10ul);
}
mfc::CString FilePath(const mfc::CFileFind& find) { return find.GetFilePath(); }
mfc::CString FileName(const mfc::CFileFind& find) { return find.GetFileName(); }

// A file stream on the stack with the reads and writes the save list uses.
struct LocalFileStream {
    netlink::FileStream s;

    // The stream's direction byte, then the CFile's virtual Open.
    void Open(const char* path, unsigned flags, std::uint8_t reading) { s.Open(path, flags, reading); }
    void Close() { s.Close(); }
    unsigned Read(void* buffer, unsigned size) { return Stream()->Read(buffer, size); }
    void Write(const void* buffer, unsigned size) { Stream()->Write(buffer, size); }
    CStream* Stream() { return &s; }
    // A string with its terminating zero.
    void WriteText(const char* text) { Write(text, static_cast<unsigned>(std::strlen(text) + 1)); }
};

// An entry being built (the original's stack temporaries): empty strings and string arrays; the other fields
// keep whatever the stack held, like the original. Destroyed as the original does: the arrays here, then the
// strings by SaveEntry's destructor (from the last).
struct LocalEntry {
    SaveEntry e;
    LocalEntry() {
        e.stringArrays_[0].Construct();
        e.stringArrays_[1].Construct();
    }
    explicit LocalEntry(const SaveEntry& from) : LocalEntry() { e.Assign(&from); }
    ~LocalEntry() {
        e.stringArrays_[1].Destruct();
        e.stringArrays_[0].Destruct();
    }
    LocalEntry(const LocalEntry&) = delete;
    LocalEntry& operator=(const LocalEntry&) = delete;
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
    SaveEntry* entry = entries_.begin() + selected;
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
    mfc::CFileFind find;
    BOOL found;
    {
        std::string path = folder + game::savewords::ShotFile();
        found = find.FindFile(path.c_str(), 0);
    }
    if (!found) return;
    CDXSurface* holder = HolderOf(objects_.data[1]);
    gameapi::EnterCriticalSection(TheDeviceLock());
    PicturePixels pixels(holder);
    if (!pixels.Begin()) return;   // (the device lock stays held, as in the original)
    mfc::CFile file;
    {
        std::string path = folder + game::savewords::ShotFile();
        file.Open(path.c_str(), 0, nullptr);
    }
    file.Read(pixels.Bits(), kShotBytes);
    file.Close();
    gameapi::LeaveCriticalSection(TheDeviceLock());
    SceneMesh(3)->visible_ = 1;
    SceneMesh(3)->texture_ = objects_.data[1];
}
EI_REWRITTEN(0x00656E10, CSaveLoadWindow::ShowPicture);


namespace {
constexpr const char* kQuickFolder = "saveq";
constexpr const char* kAutoFolderA = "savea";
constexpr const char* kAutoFolderB = "saveb";
constexpr const char* kQuickTitle = "save_quick";
constexpr const char* kAutoEnterTitle = "save_auto_enter";
constexpr const char* kAutoExitTitle = "save_auto_exit";
constexpr const char* kPlainFormat = "%s";

bool SameFolder(const char* folder, const char* name) {
    return gamecrt::mbsicmp(folder, name) == 0;
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
    if (SameFolder(folder_.c_str(), kQuickFolder)) {
        title = str::Format(kPlainFormat, quick.c_str());
    } else if (SameFolder(folder_.c_str(), kAutoFolderA)) {
        title = str::Format(kPlainFormat, autoEnter.c_str());
    } else if (SameFolder(folder_.c_str(), kAutoFolderB)) {
        title = str::Format(kPlainFormat, autoExit.c_str());
    } else {
        // The zone name ("zone" + zone_ in the texts, else "save_camp").
        const std::string name = ZoneTitle().GetString();
        title = str::Format(kPlainFormat, name.c_str());
    }
}
EI_REWRITTEN(0x00653A50, SaveEntry::MakeTitle);


void CSaveLoadWindow::SaveGame() {
    int selected = selected_;
    if (selected < 0 || static_cast<unsigned>(selected) >= static_cast<unsigned>(EntryCount())) return;
    SaveEntry* entry = entries_.begin() + selected;
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
    TheInterface()->ShowStatusText(notice.c_str());
    TheGameFlow()->SaveGameThunk(entry->folder_.c_str());
    TheCursor()->ChangeShape(3);

    // info.sav: the new save's entry (the first) with the title being given.
    LocalFileStream info;
    {
        std::string path = folder + game::savewords::InfoFile();
        info.Open(path.c_str(), kOpenWrite, 0);
    }
    if (nameTyped_)
        entries_.begin()->title_ = name_;
    else if (selected_ == 0)
        entries_.begin()->title_.clear();
    else
        entries_.begin()->title_ = entry->title_;
    mfc::impl::CTime now;
    entries_.begin()->savedAt_ = mfc::impl::CTime::CurrentTime(&now)->time;
    SaveEntry* first = entries_.begin();
    info.Stream()->WriteHeader(kInfoMagic, kInfoVersion);
    float gameTime = first->gameTime_;
    info.Write(&gameTime, 4);
    info.WriteText(first->map_.c_str());
    info.WriteText(first->zone_.c_str());
    std::int32_t savedAt = first->savedAt_;
    info.Write(&savedAt, 4);
    info.WriteText(first->title_.c_str());
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
        mfc::CFile shot;
        {
            std::string path = folder + game::savewords::ShotFile();
            shot.Open(path.c_str(), kOpenWrite, nullptr);
        }
        shot.Write(pixels.Bits(), kShotBytes);
        shot.Close();
        gameapi::LeaveCriticalSection(TheDeviceLock());
    }
    TheCursor()->ChangeShape(0);
    interface_->SetPendingValue(kClosed);
}
EI_REWRITTEN(0x00655550, CSaveLoadWindow::SaveGame);


void CSaveLoadWindow::FillEntries() {
    std::string pattern;
    pattern = str::Format(game::savewords::SavePattern(), DataFolderText());
    mfc::CFileFind find;
    entries_.EraseRange<recordvec::SaveEntryElement>(entries_.begin(), entries_.end());
    int highest = 0;     // the highest number after "save" in the folder names
    int found = 0;       // the last FindNextFile result (the sort's comparison object below)
    BOOL more = find.FindFile(pattern.c_str(), 0);
    while (more) {
        found = find.FindNext();
        more = found;
        if (find.IsDots() || !IsDirectory(find)) continue;
        LocalEntry fresh;
        if (entries_.end() != entries_.EndOfStorage()) {
            EntryConstruct(entries_.end(), &fresh.e);
            ++entries_.Finish();
        } else {
            entries_.InsertOverflow<recordvec::SaveEntryElement>(entries_.end(), fresh.e, 1);
        }
        SaveEntry* entry = entries_.end() - 1;
        entry->folder_ = FileName(find);
        mfc::CFileFind infoFind;
        BOOL hasInfo;
        {
            std::string folderPath = FilePath(find).GetString();
            std::string path = folderPath + game::savewords::InfoInFolder();
            hasInfo = infoFind.FindFile(path.c_str(), 0);
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
                    mfc::CString map(mfc::CString::kUnset);
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
            int number = std::atoi(console::Ops(entry->folder_).Mid(4));
            if (!(highest > number)) highest = number;
        }
    }
    find.Close();
    SaveEntry* first = entries_.begin();
    SaveEntry* end = entries_.end();
    if (first != end) {
        int depth = 0;
        for (int n = static_cast<int>(end - first); n != 1; n >>= 1) ++depth;
        stl_sorts::SaveIntroSort(first, end, nullptr, depth * 2, found);
        stl_sorts::SaveFinalSort(first, end, found);
    }

    if (isSave_) {
        // The new save first (STLport's insert at begin(), inlined by the original).
        LocalEntry fresh;
        SaveEntry* at = entries_.begin();
        if (entries_.end() == entries_.EndOfStorage()) {
            entries_.InsertOverflow<recordvec::SaveEntryElement>(at, fresh.e, 1);
        } else if (at == entries_.end()) {
            if (entries_.end()) entries_.end()->CopyConstruct(&fresh.e);
            ++entries_.Finish();
        } else {
            EntryConstruct(entries_.end(), entries_.end() - 1);
            ++entries_.Finish();
            LocalEntry copy(fresh.e);
            stl_sorts::SaveCopyBackward(at, entries_.end() - 2, entries_.end() - 1);
            at->Assign(&copy.e);
        }
        SaveEntry* entry = entries_.begin();
        entry->folder_ = str::Format(game::savewords::NewFolder(), highest + 1);
        entry->gameTime_ = LocalPlayerVariable(game::savewords::GameTime());
        entry->zone_ = LocalPlayerZone();
        if (MapRecord* record = TheMapDatabase()->FindMap(entry->zone_.c_str()))
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
            RemoveObjectAt(objects_.size - 1);
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
EI_REWRITTEN(0x00655D40, CSaveLoadWindow::FillEntries);

// --- Quick and automatic saves ------------------------------------------------------------------------

namespace {
constexpr const char* kNotifyLoading = "notify_loading";
constexpr int kScreenWidth = 800, kScreenHeight = 600;   // the copy's layout rectangle (immediates)
constexpr int kShotWidth = 256, kShotHeight = 192;

CInterface3D* Interface() { return TheInterface(); }
GameFlow* Flow() { return TheGameFlow(); }

// The save folder saves\<slot>\ under the game's folder.
mfc::CString SlotFolder(const char* slot) {
    std::string folder;
    folder = str::Format(flowdata::kSlotFolder, DataFolderText(), slot);
    return folder;
}
} // namespace

void __fastcall QuickSave(const char* slot, char stepFrame) {
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
    TheInterface()->ShowStatusText(notice.c_str());
    TheGameFlow()->SaveGameThunk(slot);

    // info.sav: the "gtime" variable, the zone's map, the zone, the time and an empty title.
    std::string title;
    std::string map;
    mfc::impl::CTime now;
    const std::int32_t savedAt = mfc::impl::CTime::CurrentTime(&now)->time;
    const float gameTime = LocalPlayerVariable(game::savewords::GameTime());
    const std::string zone = LocalPlayerZone();
    if (const MapRecord* record = TheMapDatabase()->FindMap(zone.c_str()))
        map = record->allod;
    std::string folder = SlotFolder(slot).GetString();
    LocalFileStream info;
    info.Open((folder + game::savewords::InfoFile()).c_str(), kOpenWrite, 0);
    info.Stream()->WriteHeader(kInfoMagic, kInfoVersion);
    info.Write(&gameTime, 4);
    info.WriteText(map.c_str());
    info.WriteText(zone.c_str());
    info.Write(&savedAt, 4);
    info.WriteText(title.c_str());
    info.Close();

    // shot.sav: the copied screen at 256x192.
    std::uint16_t* pixels = surface->Rgb565Copy(kShotWidth, kShotHeight);
    mfc::CFile shot;
    shot.Open((folder + game::savewords::ShotFile()).c_str(), kOpenWrite, nullptr);
    shot.Write(pixels, kShotBytes);
    shot.Close();
    gamecrt::operator_delete(pixels);
    if (surface != nullptr) {
        delete surface;
    }
    TheCursor()->ChangeShape(0);
}
EI_REWRITTEN(0x006570E0, QuickSave);

void __fastcall QuickLoad(const char* slot) {
    if (IsMultiplayerClient()) return;
    std::string folder = SlotFolder(slot).GetString();
    if (libcrt::Access(folder.c_str(), 0) == -1) return;
    std::string notice;
    TheTextManager()->GetString(kNotifyLoading, &notice);
    TheInterface()->ShowStatusText(notice.c_str());
    GameStopSounds(1);
    Flow()->RequestLoad(slot, 0);
}
EI_REWRITTEN(0x006575B0, QuickLoad);

// --- Entry titles, OK / Delete / load / delete ----------------------------------------------

namespace {

// The first line of the text `prefix` + `suffix`, or empty when there is none.
mfc::CString FirstLineOf(const char* prefix, const std::string& suffix) {
    std::string line;
    CTextManager* texts = TheTextManager();
    BOOL found = texts->Open(mfc::CString(prefix) + suffix);
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

mfc::CString SaveEntry::ZoneTitle() const {
    std::string title = FirstLineOf("zone ", zone_.c_str()).GetString();   // the key has a space
    if (title.size() == 0) TheTextManager()->GetString("save_camp", &title);
    return title;
}
EI_REWRITTEN(0x00653BD0, SaveEntry::ZoneTitle);

mfc::CString SaveEntry::AllodTitle() const {
    std::string title = FirstLineOf("allod ", map_.c_str()).GetString();  // the key has a space
    if (title.size() == 0) title = "Unknown";
    return title;
}
EI_REWRITTEN(0x00653CD0, SaveEntry::AllodTitle);

SaveEntry* SaveEntry::Assign(const SaveEntry* from) {
    title_ = from->title_;
    folder_ = from->folder_;
    savedAt_ = from->savedAt_;
    gameTime_ = from->gameTime_;
    map_ = from->map_;
    zone_ = from->zone_;
    stringArrays_[0].Copy(from->stringArrays_[0]);
    stringArrays_[1].Copy(from->stringArrays_[1]);
    broken_ = from->broken_;
    return this;
}
EI_REWRITTEN(0x006538E0, SaveEntry::Assign);

SaveEntry* SaveEntry::CopyConstruct(const SaveEntry* from) {
    std::construct_at(&title_);
    std::construct_at(&folder_);
    std::construct_at(&map_);
    std::construct_at(&zone_);
    stringArrays_[0].Construct();
    stringArrays_[1].Construct();
    return Assign(from);
}
EI_REWRITTEN(0x00656640, SaveEntry::CopyConstruct);

void SaveEntry::Destruct() {
    stringArrays_[1].Destruct();
    stringArrays_[0].Destruct();
    std::destroy_at(&zone_);
    std::destroy_at(&map_);
    std::destroy_at(&folder_);
    std::destroy_at(&title_);
}
EI_REWRITTEN(0x00656720, SaveEntry::Destruct);

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
EI_REWRITTEN(0x00655290, CSaveLoadWindow::Confirm);

void CSaveLoadWindow::AskDelete() {
    if (selected_ < 0 || (isSave_ && selected_ == 0)) return;
    interface_->PlaySound(kSoundDelete, 0);
    Ask(interface_, "delete", "delete_msg", kAskDelete);
}
EI_REWRITTEN(0x00655380, CSaveLoadWindow::AskDelete);

void CSaveLoadWindow::LoadSelected() {
    int selected = selected_;
    if (selected < 0 || static_cast<unsigned>(selected) >= static_cast<unsigned>(EntryCount())) return;
    if (entries_.begin()[selected].broken_) return;
    std::string notice;
    TheTextManager()->GetString(kNotifyLoading, &notice);
    TheInterface()->ShowStatusText(notice.c_str());
    GameStopSounds(1);
    interface_->PlaySound(kSoundOk, 0);
    Flow()->RequestLoad(entries_.begin()[selected_].folder_.c_str(), 0);
}
EI_REWRITTEN(0x00655A10, CSaveLoadWindow::LoadSelected);

void CSaveLoadWindow::DeleteSave() {
    int selected = selected_;
    if (selected < 0 || static_cast<unsigned>(selected) >= static_cast<unsigned>(EntryCount())) return;
    std::string folder;
    folder = str::Format(kFolderFormat, DataFolderText(), entries_.begin()[selected].folder_.c_str());
    mfc::CFileFind find;
    BOOL more = find.FindFile((folder + kEveryFile).c_str(), 0);
    while (more) {
        more = find.FindNext();
        gameapi::DeleteFileA(FilePath(find));
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
EI_REWRITTEN(0x00655B20, CSaveLoadWindow::DeleteSave);
