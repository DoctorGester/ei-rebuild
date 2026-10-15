// The save / load window's saving (00655550), its entry list (00655D40, with its tail 00656464)
// and the selected save's picture (00656E10). See ui.h (CSaveLoadWindow) and
// docs/notes/renderer.md (section 11).
//
// The window shows two 256x256 5:6:5 textures made by 005FF0B0 (a CTexture whose holder, +0x5C,
// holds the DirectDraw texture): objects_[1], into which ShowPicture reads the selected save's
// shot.sav (256x192 pixels, 0x18000 bytes), and dialog_ (+0x184), which FillEntries fills from
// the escape menu's 256x192 copy of the screen (interface +0x5C) for the new save of a save window
// and SaveGame writes to the new save's shot.sav. Like the original, the 0x18000 bytes are one
// block from the start of the pixels (a pitch of 512 bytes is taken for granted).
//
// With the new renderer's "cpuwindows" option both textures also keep their pixels in game-owned
// memory (GivePictureMemory: a DIB section per holder, dx_surface.cpp), which the new renderer
// owns and reads (render::SurfaceMemory). Outside the switch-over the pixels still go through a Lock of
// the DirectDraw texture, which the old path draws, and the filled block is copied into the
// memory; in the switch-over ("present") they are written into and read from the memory only,
// with no Lock (DirectPixels). Without the option everything is as the original.
#include "ui.h"
#include "game_strings.h"

#include "console.h"
#include "dx_surface.h"
#include "item_db_text.h"
#include "text_reader.h"
#include "net_link.h"
#include "object_tables.h"
#include "texture.h"
#include "world.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "render/render.h"
#include "originals_engine.h"
#include "originals_library.h"
#include "originals_ui.h"
#include "originals_world.h"

#include <ddraw.h>

#include <cstdint>
#include <cstring>
#include <new>

namespace {

mfc::CString& Str(char*& field) { return *reinterpret_cast<mfc::CString*>(&field); }

CRITICAL_SECTION* DeviceLock() { return reinterpret_cast<CRITICAL_SECTION*>(0x007AEE78); }
// The game's folder (the CString 007AF460 that 00578DB0 returns).
const char* GameFolder() { return *reinterpret_cast<char* const*>(0x007AF460); }
CTextManager* Texts() { return *reinterpret_cast<CTextManager**>(0x007AF788); }

void* const kFlow = reinterpret_cast<void*>(0x007AFE20);         // the game flow (world_game.cpp)
void* const kGlue = reinterpret_cast<void*>(0x007AFEA5);         // ECX of the player getters (unused by them)
void* const kMapDatabase = reinterpret_cast<void*>(0x0079BF00);
void* const kCursor = reinterpret_cast<void*>(0x007C7BD8);
void* const kInterface = reinterpret_cast<void*>(0x007C7D50);

constexpr std::uintptr_t kSlotFolder = 0x007905E4;   // "%ssaves\\%s\\"
constexpr std::uintptr_t kSavePattern = 0x00794A34;  // "%ssaves\\save*"
constexpr std::uintptr_t kInfoFile = 0x007949B8;     // "info.sav"
constexpr std::uintptr_t kInfoInFolder = 0x00794A28; // "\\info.sav"
constexpr std::uintptr_t kShotFile = 0x007949AC;     // "shot.sav"
constexpr std::uintptr_t kBrokenTitle = 0x00794A1C;  // "Error Save"
constexpr std::uintptr_t kNewFolder = 0x00794A14;    // "save%d"
constexpr std::uintptr_t kGameTime = 0x00742B88;     // "gtime"
constexpr std::uintptr_t kNotifySaving = 0x007949C4; // "notify_saving"
constexpr std::uintptr_t kNoSpace = 0x007949D4;      // "no_disc_space"
constexpr std::uintptr_t kNoSpaceText = 0x007949E4;  // "no_disc_space_msg"
constexpr std::uintptr_t kCantLock = 0x0078E30C;     // "Can't lock surface.\n"
constexpr std::uintptr_t kCantUnlock = 0x0078E2F4;   // "Can't unlock surface.\n"

constexpr std::uint32_t kShotBytes = 0x18000;        // 256 x 192 pixels, 16 bits
constexpr unsigned kInfoMagic = 0x111;
constexpr unsigned kInfoVersion = 3;
constexpr unsigned kOpenWrite = 0x1001;              // CFile::modeCreate | modeWrite
constexpr std::uint32_t kClosed = 0x100002;          // pending value after saving
constexpr int kVisibleRows = 16;


// --- Callees not rewritten (thiscall declared __fastcall with the unused EDX) --------------------

// 004800E0 (cdecl): DirectX error report; nonzero to go on.
// 00455A90 (fastcall): the free space on the drive of `path` (0 when unknown).
__declspec(naked) double __fastcall DiskFreeSpace(const char* /*path*/) EI_ORIGINAL(0x00455A90)
// 0057BC30 (thiscall on the game flow): the space a save needs.
__declspec(naked) int __fastcall SaveSpaceNeeded(void* /*flow*/) EI_ORIGINAL(0x0057BC30)
// 00581610 (RET 4): GameFlow::SaveGame's thunk (world_game.cpp), through its address.
__declspec(naked) int __fastcall SaveGameAt(void* /*flow*/, void*, const char* /*slot*/) EI_ORIGINAL(0x00581610)
// 005E5200 (RET 4): the cursor's shape (3 busy, 0 normal).
int __fastcall CursorSetShape(void* a0, void*, int a2) { return CursorSetShapeOriginal(a0, nullptr, a2); }
// 005F75B0 (RET 4): the interface shows `text` in a notice band and draws a frame.
// 00634650 (RET 0x18): the message box; 005F7350 (RET 0xC): opens a window on top.
CUiWindow* __fastcall ConstructMessageBox(void* a0, void*, CInterface3D* a2, const char* a3, const char* a4, int a5, int a6, int a7) { return (CUiWindow*)MessageBoxConstructOriginal(a0, nullptr, a2, a3, a4, a5, a6, a7); }
// 00578B50 / 00578A90 (RET 4): the local player's variable `name` (ST0) and zone (a CString
// constructed at `out`, returned).
__declspec(naked) mfc::CString* __fastcall PlayerZone(void*, void*, void* /*out*/) EI_ORIGINAL(0x00578A90)
// 0045E850 (RET 4): CMapDatabase::FindMap, the zone record (+4 its map) named `name`, or null.
// 006FE508: CTime::GetCurrentTime (time(), returned through `out`).
__declspec(naked) std::int32_t* __stdcall CurrentTime(std::int32_t* /*out*/) EI_ORIGINAL(0x006FE508)
// 006E80D8: atoi.
// 006FE925: CStringArray's constructor.

// The stream's header (004A5340, RET 8: magic, version) and reads (004A52D0, RET 0xC: true when
// the magic and version fit, the version kept in +8; 00461190 / 004E5840, RET 4: a string up to
// a zero into a new CString at `out` / into the CString `out`).
void __fastcall StreamWriteHeader(void* a0, void*, unsigned a2, unsigned a3) { StreamWriteHeaderOriginal(a0, nullptr, a2, a3); }
bool __fastcall StreamReadHeader(void* a0, void*, unsigned a2, unsigned a3, int a4) { return StreamReadHeaderOriginal(a0, nullptr, a2, (int)a3, a4); }
void __fastcall StreamReadInto(void* a0, void*, mfc::CString* a2) { StreamReadCStringOriginal(a0, nullptr, (void*)a2); }

// The list's entries (SaveEntry, ui.h) and its STLport vector at +0x164 (begin, end, capacity).
__declspec(naked) void __fastcall EntryConstruct(SaveEntry* /*at*/, const SaveEntry* /*from*/) EI_ORIGINAL(0x00657C60)
__declspec(naked) SaveEntry* __fastcall EntryCopy(SaveEntry* /*at*/, void*, const SaveEntry* /*from*/) EI_ORIGINAL(0x00656640)
__declspec(naked) void __fastcall EntryAssign(SaveEntry*, void*, const SaveEntry*) EI_ORIGINAL(0x006538E0)
__declspec(naked) void __fastcall EntryDestruct(SaveEntry*) EI_ORIGINAL(0x00656720)
// 00653A50 (RET 4): a title from the folder (quick / automatic saves) or the save's time.
__declspec(naked) void __fastcall EntryMakeTitle(SaveEntry*, void*, int) EI_ORIGINAL(0x00653A50)
__declspec(naked) SaveEntry* __fastcall EntryCopyBackward(SaveEntry* /*first*/, SaveEntry* /*last*/,
                                                          SaveEntry* /*result end*/) EI_ORIGINAL(0x00657D50)
__declspec(naked) void __fastcall EntriesInsertOverflow(void* /*vector*/, void*, SaveEntry* /*at*/, const SaveEntry*,
                                                        unsigned /*count*/) EI_ORIGINAL(0x006579C0)
__declspec(naked) SaveEntry* __fastcall EntriesErase(void* /*vector*/, void*, SaveEntry*, SaveEntry*) EI_ORIGINAL(0x006578F0)
// The sort (STLport's introsort loop and final insertion sort; `order` the empty comparison object
// passed by value, `tag` the null value-type pointer).
__declspec(naked) void __fastcall EntriesSortLoop(SaveEntry*, SaveEntry*, void* /*tag*/, int /*depth*/, int /*order*/)
    EI_ORIGINAL(0x00657DF0)
__declspec(naked) void __fastcall EntriesFinalSort(SaveEntry*, SaveEntry*, int /*order*/) EI_ORIGINAL(0x00657F10)

// CFileFind members beyond lib/mfc.h: FindNextFile (006E6653), IsDots (006E661A), Close
// (006E63E9), and MatchesMask(FILE_ATTRIBUTE_DIRECTORY) through the vtable (slot 14) as the
// original calls it.
BOOL FindNext(mfc::CFileFind& find) { return CFileFindNext(&find); }
BOOL IsDots(const mfc::CFileFind& find) { return CFileFindIsDotsOriginal(&find); }
void CloseFind(mfc::CFileFind& find) { CFileFindCloseOriginal(&find); }
BOOL IsDirectory(const mfc::CFileFind& find) {
    return reinterpret_cast<const mfc::CFileFindVirtuals*>(&find)->MatchesMask(0x10ul);
}
// GetFilePath (006E6722) and GetFileName (006E684B): the CString they construct at the slot,
// copied out and destroyed.
mfc::CString FindResult(mfc::CString* (__fastcall* get)(const void*, void*, mfc::CString*), const mfc::CFileFind& find) {
    alignas(mfc::CString) unsigned char slot[sizeof(mfc::CString)];
    mfc::CString* text = get(&find, nullptr, reinterpret_cast<mfc::CString*>(slot));
    mfc::CString result(*text);
    text->~CString();
    return result;
}
mfc::CString FilePath(const mfc::CFileFind& find) { return FindResult(CFileFindPathOriginal, find); }
mfc::CString FileName(const mfc::CFileFind& find) { return FindResult(CFileFindNameOriginal, find); }

// A file stream local as the original builds and tears it down inline (net_link.h).
struct LocalFileStream {
    netlink::FileStream s;
    LocalFileStream() { s.Construct(); }
    ~LocalFileStream() { s.Destruct(); }
    LocalFileStream(const LocalFileStream&) = delete;
    LocalFileStream& operator=(const LocalFileStream&) = delete;

    // The stream's direction byte, then the CFile's virtual Open.
    void Open(const char* path, unsigned flags, std::uint8_t reading) { s.Open(path, flags, reading); }
    void Close() { s.Close(); }
    unsigned Read(void* buffer, unsigned size) { return Stream()->Read(buffer, size); }
    void Write(const void* buffer, unsigned size) { Stream()->Write(buffer, size); }
    CStream* Stream() { return &s; }
    // A string with its terminating zero.
    void WriteText(const char* text) { Write(text, static_cast<unsigned>(std::strlen(text) + 1)); }
};

// An entry being built (the original's stack temporaries): empty strings and string arrays; the
// other fields keep whatever the stack held, as in the original. Destroyed through 00656720.
struct LocalEntry {
    SaveEntry e;
    LocalEntry() {
        char* empty = *reinterpret_cast<char**>(0x00796E48);   // afxEmptyString
        e.title_ = empty;
        e.folder_ = empty;
        e.map_ = empty;
        e.zone_ = empty;
        StringArrayConstruct(&e.stringArrays_[0]);
        StringArrayConstruct(&e.stringArrays_[1]);
    }
    explicit LocalEntry(const SaveEntry& from) { EntryCopy(&e, nullptr, &from); }
    ~LocalEntry() { EntryDestruct(&e); }
    LocalEntry(const LocalEntry&) = delete;
    LocalEntry& operator=(const LocalEntry&) = delete;
};

CDXSurface* HolderOf(CEngineObject* texture) { return static_cast<CTexture*>(texture)->surface_; }

// The pixels of a picture texture for one fill or read. In the switch-over with game-owned memory
// they are that memory (no DirectDraw); otherwise the surface is locked with flags 0 as the
// original does (the result in the holder's +8, a failure reported), and Mirror copies the filled
// block into the game-owned memory, when there is some, before the Unlock.
class PicturePixels {
public:
    explicit PicturePixels(CDXSurface* holder) : holder_(holder) { desc_[0] = 0x7C; }

    // False when the Lock failed and the report said stop.
    bool Begin() {
        direct_ = DirectPixels(holder_, desc_);
        if (direct_) return true;
        long hr = static_cast<IDirectDrawSurface7*>(holder_->surface)
                      ->Lock(nullptr, reinterpret_cast<DDSURFACEDESC2*>(desc_), 0, nullptr);
        holder_->result = hr;
        return hr == 0 || FatalDirectX(hr, game::Text(kCantLock)) != 0;
    }
    std::uint8_t* Bits() const { return reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(desc_[9])); }
    void Mirror() const {
        std::uint32_t memory[0x1F] = {};
        if (direct_ || !Bits() || !ClientPixels(holder_, memory)) return;
        std::uint32_t size = memory[2] * memory[4];   // height * pitch
        std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(memory[9])), Bits(),
                    size < kShotBytes ? size : kShotBytes);
    }
    void End() {
        if (direct_) return;
        long hr = static_cast<IDirectDrawSurface7*>(holder_->surface)->Unlock(nullptr);
        holder_->result = hr;
        if (hr != 0) FatalDirectX(hr, game::Text(kCantUnlock));
    }

private:
    CDXSurface* holder_;
    std::uint32_t desc_[0x1F] = {};
    bool direct_ = false;
};

// The escape menu's 256x192 5:6:5 copy of the screen (interface +0x5C), or null.
const void* SavePicture(CInterface3D* ui) { return ui->block5C_; }

// The entry vector (+0x164) as the STLport helpers take it.
void* EntryVector(CSaveLoadWindow* w) { return &w->entriesBegin_; }

} // namespace

// --- Forwarders through the original addresses ---------------------------------------------------

__declspec(naked) void CSaveLoadWindow::CallSaveGame() EI_ORIGINAL(0x00655550)
__declspec(naked) void CSaveLoadWindow::CallShowPicture() EI_ORIGINAL(0x00656E10)
__declspec(naked) void CSaveLoadWindow::CallFillEntries() EI_ORIGINAL(0x00655D40)
__declspec(naked) void CSaveLoadWindow::CallConfirm() EI_ORIGINAL(0x00655290)
__declspec(naked) void CSaveLoadWindow::CallAskDelete() EI_ORIGINAL(0x00655380)
__declspec(naked) void CSaveLoadWindow::CallDeleteSave() EI_ORIGINAL(0x00655B20)
__declspec(naked) void CSaveLoadWindow::CallLoadSelected() EI_ORIGINAL(0x00655A10)

// --- Game-owned memory ---------------------------------------------------------------------------

void CSaveLoadWindow::GivePictureMemory(CEngineObject* texture) {
    if (!texture || !render::CpuWindows()) return;
    CDXSurface* holder = HolderOf(texture);
    if (!holder) return;
    // The surface's size (004EFA60 put it there) and the texture's pixel format (the device's
    // format for the texture's format id).
    AttachTextureMemory(holder, holder->width, holder->height,
                        reinterpret_cast<const std::uint32_t*>(&static_cast<CTexture*>(texture)->format_));
}

// --- 00656E10 ------------------------------------------------------------------------------------

void CSaveLoadWindow::ShowPicture() {
    SceneMesh(3)->visible_ = 0;
    int selected = selected_;
    if (selected < 0 || static_cast<unsigned>(selected) >= static_cast<unsigned>(EntryCount())) return;
    SaveEntry* entry = entriesBegin_ + selected;
    if (entry->broken_) return;
    if (isSave_ && selected == 0) {
        // The new save: the game's picture.
        if (!dialog_) return;
        SceneMesh(3)->visible_ = 1;
        SceneMesh(3)->texture_ = dialog_;
        return;
    }
    mfc::CString folder;
    folder.Format(game::Text(kSlotFolder), GameFolder(), entry->folder_);
    mfc::CFileFind find;
    BOOL found;
    {
        mfc::CString path = folder + game::Text(kShotFile);
        found = find.FindFile(path, 0);
    }
    if (!found) return;
    CDXSurface* holder = HolderOf(objects_.data[1]);
    gameapi::EnterCriticalSection(DeviceLock());
    PicturePixels pixels(holder);
    if (!pixels.Begin()) return;   // (the device lock stays held, as in the original)
    mfc::CFile file;
    {
        mfc::CString path = folder + game::Text(kShotFile);
        file.Open(path, 0, nullptr);
    }
    file.Read(pixels.Bits(), kShotBytes);
    file.Close();
    pixels.Mirror();
    gameapi::LeaveCriticalSection(DeviceLock());
    pixels.End();
    SceneMesh(3)->visible_ = 1;
    SceneMesh(3)->texture_ = objects_.data[1];
}
EI_REPLACE_METHOD(0x00656E10, CSaveLoadWindow::ShowPicture);

// --- 00653A50 ------------------------------------------------------------------------------------

namespace {
constexpr std::uintptr_t kQuickFolder = std::uintptr_t(0x007933AC);     // "saveq"
constexpr std::uintptr_t kAutoFolderA = std::uintptr_t(0x007942E8);     // "savea"
constexpr std::uintptr_t kAutoFolderB = std::uintptr_t(0x007940F0);     // "saveb"
constexpr std::uintptr_t kQuickTitle = std::uintptr_t(0x00794924);      // "save_quick"
constexpr std::uintptr_t kAutoEnterTitle = std::uintptr_t(0x00794914);  // "save_auto_enter"
constexpr std::uintptr_t kAutoExitTitle = std::uintptr_t(0x00794904);   // "save_auto_exit"
constexpr std::uintptr_t kPlainFormat = std::uintptr_t(0x0079190C);     // "%s"

// 00653BD0 (RET 4): the entry's zone name ("zone" + zone_ in the texts, else "save_camp"), a
// CString constructed at `out`.
__declspec(naked) mfc::CString* __fastcall EntryZoneTitle(SaveEntry*, void*, void* /*out*/) EI_ORIGINAL(0x00653BD0)

bool SameFolder(const char* folder, std::uintptr_t name) {
    return gamecrt::mbsicmp(reinterpret_cast<const unsigned char*>(folder), reinterpret_cast<const unsigned char*>(game::Text(name))) == 0;
}
} // namespace

void SaveEntry::MakeTitle(int) {
    mfc::CString quick;
    mfc::CString autoEnter;
    mfc::CString autoExit;
    Texts()->GetString(game::Text(kQuickTitle), &quick);
    Texts()->GetString(game::Text(kAutoEnterTitle), &autoEnter);
    Texts()->GetString(game::Text(kAutoExitTitle), &autoExit);
    mfc::CString& title = Str(title_);
    if (SameFolder(folder_, kQuickFolder)) {
        title.Format(game::Text(kPlainFormat), static_cast<const char*>(quick));
    } else if (SameFolder(folder_, kAutoFolderA)) {
        title.Format(game::Text(kPlainFormat), static_cast<const char*>(autoEnter));
    } else if (SameFolder(folder_, kAutoFolderB)) {
        title.Format(game::Text(kPlainFormat), static_cast<const char*>(autoExit));
    } else {
        alignas(mfc::CString) unsigned char zone[sizeof(mfc::CString)];
        EntryZoneTitle(this, nullptr, zone);
        auto* name = reinterpret_cast<mfc::CString*>(zone);
        title.Format(game::Text(kPlainFormat), static_cast<const char*>(*name));
        name->~CString();
    }
}
EI_REPLACE_METHOD(0x00653A50, SaveEntry::MakeTitle);

// --- 00655550 ------------------------------------------------------------------------------------

void CSaveLoadWindow::SaveGame() {
    int selected = selected_;
    if (selected < 0 || static_cast<unsigned>(selected) >= static_cast<unsigned>(EntryCount())) return;
    SaveEntry* entry = entriesBegin_ + selected;
    double space = DiskFreeSpace(GameFolder());
    // 0 means unknown: no check (the original tests C3 alone, so a NaN counts as 0).
    if (space < 0.0 || space > 0.0) {
        int needed = SaveSpaceNeeded(kFlow);
        if (static_cast<double>(needed) > space) {
            void* memory = gamecrt::operator_new(0x180);
            CUiWindow* box = memory ? ConstructMessageBox(memory, nullptr, interface_, game::Text(kNoSpace), game::Text(kNoSpaceText),
                                                          0x30000, 1, 1)
                                    : nullptr;
            InterfacePushWindowOriginal(interface_, nullptr, box, 1, 1);
            return;
        }
    }
    mfc::CString folder;
    folder.Format(game::Text(kSlotFolder), GameFolder(), entry->folder_);
    mfc::CString notice;
    Texts()->GetString(game::Text(kNotifySaving), &notice);
    ShowStatusText(kInterface, nullptr, notice);
    SaveGameAt(kFlow, nullptr, entry->folder_);
    CursorSetShape(kCursor, nullptr, 3);

    // info.sav: the new save's entry (the first) with the title being given.
    LocalFileStream info;
    {
        mfc::CString path = folder + game::Text(kInfoFile);
        info.Open(path, kOpenWrite, 0);
    }
    if (nameTyped_)
        Str(entriesBegin_->title_) = Str(name_);
    else if (selected_ == 0)
        Str(entriesBegin_->title_).Empty();
    else
        Str(entriesBegin_->title_) = Str(entry->title_);
    std::int32_t now;
    entriesBegin_->savedAt_ = *CurrentTime(&now);
    SaveEntry* first = entriesBegin_;
    StreamWriteHeader(&info.s, nullptr, kInfoMagic, kInfoVersion);
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
        gameapi::EnterCriticalSection(DeviceLock());
        PicturePixels pixels(holder);
        if (!pixels.Begin()) {
            // (The device lock stays held and the cursor busy, as in the original.)
            interface_->SetPendingValue(kClosed);
            return;
        }
        mfc::CFile shot;
        {
            mfc::CString path = folder + game::Text(kShotFile);
            shot.Open(path, kOpenWrite, nullptr);
        }
        CFileWrite(&shot, nullptr, pixels.Bits(), kShotBytes);
        shot.Close();
        gameapi::LeaveCriticalSection(DeviceLock());
        pixels.End();
    }
    CursorSetShape(kCursor, nullptr, 0);
    interface_->SetPendingValue(kClosed);
}
EI_REPLACE_METHOD(0x00655550, CSaveLoadWindow::SaveGame);

// --- 00655D40 ------------------------------------------------------------------------------------

void CSaveLoadWindow::FillEntries() {
    mfc::CString pattern;
    pattern.Format(game::Text(kSavePattern), GameFolder());
    mfc::CFileFind find;
    EntriesErase(EntryVector(this), nullptr, entriesBegin_, entriesEnd_);
    int highest = 0;     // the highest number after "save" in the folder names
    int found = 0;       // the last FindNextFile result (the sort's comparison object below)
    BOOL more = find.FindFile(pattern, 0);
    while (more) {
        found = FindNext(find);
        more = found;
        if (IsDots(find) || !IsDirectory(find)) continue;
        LocalEntry fresh;
        if (entriesEnd_ != entriesCapacity_) {
            EntryConstruct(entriesEnd_, &fresh.e);
            ++entriesEnd_;
        } else {
            EntriesInsertOverflow(EntryVector(this), nullptr, entriesEnd_, &fresh.e, 1);
        }
        SaveEntry* entry = entriesEnd_ - 1;
        Str(entry->folder_) = FileName(find);
        mfc::CFileFind infoFind;
        BOOL hasInfo;
        {
            mfc::CString folderPath = FilePath(find);
            mfc::CString path = folderPath + game::Text(kInfoInFolder);
            hasInfo = infoFind.FindFile(path, 0);
        }
        if (!hasInfo) {
            entry->broken_ = true;
            Str(entry->title_) = game::Text(kBrokenTitle);
        } else {
            FindNext(infoFind);
            LocalFileStream info;
            info.Open(FilePath(infoFind), 0, 1);
            if (StreamReadHeader(&info.s, nullptr, kInfoMagic, kInfoVersion, 1)) {
                info.Read(&entry->gameTime_, 4);
                {
                    alignas(mfc::CString) unsigned char storage[sizeof(mfc::CString)];
                    mfc::CString* map = StreamReadString(reinterpret_cast<CStream*>(&info.s), nullptr, storage);
                    Str(entry->map_) = *map;
                    map->~CString();
                }
                StreamReadInto(&info.s, nullptr, &Str(entry->zone_));
                // +8 of the stream: the version the header read.
                if (static_cast<int>(info.s.version_) > 1) {
                    std::int32_t savedAt;
                    info.Read(&savedAt, 4);
                    entry->savedAt_ = savedAt;
                }
                if (static_cast<int>(info.s.version_) > 2) StreamReadInto(&info.s, nullptr, &Str(entry->title_));
            }
            info.Close();
            if (console::Length(Str(entry->title_)) == 0) EntryMakeTitle(entry, nullptr, 1);
            entry->broken_ = false;
        }
        CloseFind(infoFind);
        if (console::Length(Str(entry->folder_)) > 4) {
            int number = GameAtoi(console::Ops(Str(entry->folder_)).Mid(4));
            if (!(highest > number)) highest = number;
        }
    }
    CloseFind(find);
    SaveEntry* first = entriesBegin_;
    SaveEntry* end = entriesEnd_;
    if (first != end) {
        int depth = 0;
        for (int n = static_cast<int>(end - first); n != 1; n >>= 1) ++depth;
        EntriesSortLoop(first, end, nullptr, depth * 2, found);
        EntriesFinalSort(first, end, found);
    }

    if (isSave_) {
        // The new save first (STLport's insert at begin(), inlined by the original).
        LocalEntry fresh;
        SaveEntry* at = entriesBegin_;
        if (entriesEnd_ == entriesCapacity_) {
            EntriesInsertOverflow(EntryVector(this), nullptr, at, &fresh.e, 1);
        } else if (at == entriesEnd_) {
            if (entriesEnd_) EntryCopy(entriesEnd_, nullptr, &fresh.e);
            ++entriesEnd_;
        } else {
            EntryConstruct(entriesEnd_, entriesEnd_ - 1);
            ++entriesEnd_;
            LocalEntry copy(fresh.e);
            EntryCopyBackward(at, entriesEnd_ - 2, entriesEnd_ - 1);
            EntryAssign(at, nullptr, &copy.e);
        }
        SaveEntry* entry = entriesBegin_;
        Str(entry->folder_).Format(game::Text(kNewFolder), highest + 1);
        entry->gameTime_ = static_cast<float>(HoverPlayerVariable(kGlue, nullptr, game::Text(kGameTime)));
        {
            alignas(mfc::CString) unsigned char storage[sizeof(mfc::CString)];
            mfc::CString* zone = PlayerZone(kGlue, nullptr, storage);
            Str(entry->zone_) = *zone;
            zone->~CString();
        }
        if (auto* record = reinterpret_cast<MapRecord*>(MapDatabaseFind(kMapDatabase, nullptr, entry->zone_)))
            Str(entry->map_) = Str(record->allod);
        EntryMakeTitle(entry, nullptr, 1);
        entry->broken_ = false;

        // The game's picture: a new texture filled from the escape menu's copy of the screen.
        if (dialog_) dialog_->Release();
        dialog_ = nullptr;
        if (SavePicture(interface_)) {
            CEngineObject* shot = CreateTexture(0x100, 0x100, 0x1E);
            dialog_ = shot;
            ++shot->refCount_;
            RemoveObjectAt(objects_.size - 1);
            GivePictureMemory(dialog_);
            CDXSurface* holder = HolderOf(dialog_);
            gameapi::EnterCriticalSection(DeviceLock());
            PicturePixels pixels(holder);
            // A failed Lock whose report says stop leaves with the device lock held and the scroll
            // bar as it was (the original's tail 00656464).
            if (!pixels.Begin()) return;
            std::memcpy(pixels.Bits(), SavePicture(interface_), kShotBytes);
            pixels.Mirror();
            gameapi::LeaveCriticalSection(DeviceLock());
            pixels.End();
        }
    }

    if (static_cast<unsigned>(EntryCount()) > static_cast<unsigned>(kVisibleRows)) {
        int position;
        scrollbar_->TakePosition(&position);
        ScrollbarSetRangeOriginal(scrollbar_, nullptr, EntryCount() - kVisibleRows);
        scrollbar_->CallSetPosition(position);
    } else {
        ScrollbarSetRangeOriginal(scrollbar_, nullptr, 0);
    }
    firstVisible_ = 0;
}
EI_REPLACE_METHOD(0x00655D40, CSaveLoadWindow::FillEntries);

// --- 006570E0 / 006575B0: quick and automatic saves ----------------------------------------------

namespace {
constexpr std::uintptr_t kNotifyLoading = std::uintptr_t(0x007949F8);   // "notify_loading"
constexpr int kScreenWidth = 800, kScreenHeight = 600;   // the copy's layout rectangle (immediates)
constexpr int kShotWidth = 256, kShotHeight = 192;

CInterface3D* Interface() { return static_cast<CInterface3D*>(kInterface); }
GameFlow* Flow() { return static_cast<GameFlow*>(kFlow); }

// The save folder saves\<slot>\ under the game's folder.
mfc::CString SlotFolder(const char* slot) {
    mfc::CString folder;
    folder.Format(game::Text(kSlotFolder), GameFolder(), slot);
    return folder;
}
} // namespace

void __fastcall QuickSave(const char* slot, char stepFrame) {
    if (IsMultiplayerClient()) return;
    double space = DiskFreeSpace(GameFolder());
    // 0 means unknown: no check (the original tests C3 alone, so a NaN counts as 0).
    if ((space < 0.0 || space > 0.0) && static_cast<double>(SaveSpaceNeeded(kFlow)) > space) return;
    if (stepFrame) FrameStepOriginal(0, 1);
    // A null surface (no memory) is used all the same, as in the original.
    void* memory = gamecrt::operator_new(sizeof(CUiSurface));
    CUiSurface* surface = memory != nullptr
                              ? static_cast<CUiSurface*>(memory)->Construct(Interface(), 0, 0, kScreenWidth,
                                                                            kScreenHeight, 4, 1)
                              : nullptr;
    surface->CaptureScreen();
    CursorSetShape(kCursor, nullptr, 3);
    Interface()->serverFlag_ = static_cast<std::uint8_t>(ServerFlagOriginal());
    mfc::CString notice;
    Texts()->GetString(game::Text(kNotifySaving), &notice);
    ShowStatusText(kInterface, nullptr, notice);
    SaveGameAt(kFlow, nullptr, slot);

    // info.sav: the "gtime" variable, the zone's map, the zone, the time and an empty title.
    mfc::CString title;
    mfc::CString map;
    mfc::CString zone;
    std::int32_t now;
    const std::int32_t savedAt = *CurrentTime(&now);
    const float gameTime = static_cast<float>(HoverPlayerVariable(kGlue, nullptr, game::Text(kGameTime)));
    {
        alignas(mfc::CString) unsigned char storage[sizeof(mfc::CString)];
        mfc::CString* current = PlayerZone(kGlue, nullptr, storage);
        zone = *current;
        current->~CString();
    }
    if (auto* record = reinterpret_cast<const MapRecord*>(MapDatabaseFind(kMapDatabase, nullptr, zone)))
        map = *reinterpret_cast<const mfc::CString*>(&record->allod);
    mfc::CString folder = SlotFolder(slot);
    LocalFileStream info;
    info.Open(folder + game::Text(kInfoFile), kOpenWrite, 0);
    StreamWriteHeader(&info.s, nullptr, kInfoMagic, kInfoVersion);
    info.Write(&gameTime, 4);
    info.WriteText(map);
    info.WriteText(zone);
    info.Write(&savedAt, 4);
    info.WriteText(title);
    info.Close();

    // shot.sav: the copied screen at 256x192.
    std::uint16_t* pixels = surface->Rgb565Copy(kShotWidth, kShotHeight);
    mfc::CFile shot;
    shot.Open(folder + game::Text(kShotFile), kOpenWrite, nullptr);
    CFileWrite(&shot, nullptr, pixels, kShotBytes);
    shot.Close();
    gamecrt::operator_delete(pixels);
    if (surface != nullptr) {
        surface->ReleaseContents();
        gamecrt::operator_delete(surface);
    }
    CursorSetShape(kCursor, nullptr, 0);
}
EI_REPLACE(0x006570E0, QuickSave);

void __fastcall QuickLoad(const char* slot) {
    if (IsMultiplayerClient()) return;
    mfc::CString folder = SlotFolder(slot);
    if (gamecrt::access(folder, 0) == -1) return;
    mfc::CString notice;
    Texts()->GetString(game::Text(kNotifyLoading), &notice);
    ShowStatusText(kInterface, nullptr, notice);
    MenuSetOriginal(1);
    Flow()->RequestLoad(slot, 0);
}
EI_REPLACE(0x006575B0, QuickLoad);

// --- Entry titles, OK / Delete / load / delete (A-H14) ----------------------------------------------

namespace {
const mfc::CString& View(char* const& field) { return *reinterpret_cast<const mfc::CString*>(&field); }

// The first line of the text `prefix` + `suffix`, or empty when there is none.
mfc::CString FirstLineOf(const char* prefix, const mfc::CString& suffix) {
    mfc::CString line;
    CTextManager* texts = Texts();
    BOOL found = texts->Open(mfc::CString(prefix) + suffix);
    if (found) texts->ReadLine(&line, 1);
    return line;
}

constexpr const char* kSoundOk = "buttons\\messbox\\ok.wav";            // 00793A88
constexpr const char* kSoundDelete = "buttons\\save\\delete.wav";       // 00794994
constexpr int kMessageBoxBytes = 0x180;
constexpr int kAskOverwrite = 0x20000, kAskDelete = 0x10000;            // the boxes' results (high word)
constexpr std::uintptr_t kFolderFormat = std::uintptr_t(0x00794A08);    // "%ssaves\\%s"
constexpr const char* kEveryFile = "\\*.*";                             // 0078B054

// A message box `name` / `text` on top of the interface (a failed allocation pushes null, as the
// original does).
void Ask(CInterface3D* ui, const char* name, const char* text, int result) {
    void* memory = gamecrt::operator_new(kMessageBoxBytes);
    CUiWindow* box = memory != nullptr ? ConstructMessageBox(memory, nullptr, ui, name, text, result, 3, 1) : nullptr;
    InterfacePushWindowOriginal(ui, nullptr, box, 1, 1);
}
} // namespace

mfc::CString SaveEntry::ZoneTitle() const {
    mfc::CString title = FirstLineOf("zone ", View(zone_));   // 007941F8: the key has a space
    if (title.GetLength() == 0) Texts()->GetString("save_camp", &title);
    return title;
}
EI_REPLACE_METHOD(0x00653BD0, SaveEntry::ZoneTitle);

mfc::CString SaveEntry::AllodTitle() const {
    mfc::CString title = FirstLineOf("allod ", View(map_));  // 00794200: the key has a space
    if (title.GetLength() == 0) title = "Unknown";
    return title;
}
EI_REPLACE_METHOD(0x00653CD0, SaveEntry::AllodTitle);

void CSaveLoadWindow::Confirm() {
    if (selected_ < 0) return;
    if (!isSave_) {
        CallLoadSelected();
        return;
    }
    interface_->PlaySound(kSoundOk, 0);
    if (selected_ == 0) {
        CallSaveGame();
        return;
    }
    Ask(interface_, "overwrite", "overwrite_msg", kAskOverwrite);
}
EI_REPLACE_METHOD(0x00655290, CSaveLoadWindow::Confirm);

void CSaveLoadWindow::AskDelete() {
    if (selected_ < 0 || (isSave_ && selected_ == 0)) return;
    interface_->PlaySound(kSoundDelete, 0);
    Ask(interface_, "delete", "delete_msg", kAskDelete);
}
EI_REPLACE_METHOD(0x00655380, CSaveLoadWindow::AskDelete);

void CSaveLoadWindow::LoadSelected() {
    int selected = selected_;
    if (selected < 0 || static_cast<unsigned>(selected) >= static_cast<unsigned>(EntryCount())) return;
    if (entriesBegin_[selected].broken_) return;
    mfc::CString notice;
    Texts()->GetString(game::Text(kNotifyLoading), &notice);
    ShowStatusText(kInterface, nullptr, notice);
    MenuSetOriginal(1);
    interface_->PlaySound(kSoundOk, 0);
    Flow()->RequestLoad(entriesBegin_[selected_].folder_, 0);
}
EI_REPLACE_METHOD(0x00655A10, CSaveLoadWindow::LoadSelected);

void CSaveLoadWindow::DeleteSave() {
    int selected = selected_;
    if (selected < 0 || static_cast<unsigned>(selected) >= static_cast<unsigned>(EntryCount())) return;
    mfc::CString folder;
    folder.Format(game::Text(kFolderFormat), GameFolder(), entriesBegin_[selected].folder_);
    mfc::CFileFind find;
    BOOL more = find.FindFile(folder + kEveryFile, 0);
    while (more) {
        more = FindNext(find);
        gameapi::DeleteFileA(FilePath(find));
    }
    CloseFind(find);
    gameapi::RemoveDirectoryA(folder);
    CallFillEntries();
    int count = EntryCount();
    if (static_cast<unsigned>(selected_) >= static_cast<unsigned>(count)) selected_ = count - 1;
    TakeSelectedTitle();
    RefreshList();
    CallShowPicture();
}
EI_REPLACE_METHOD(0x00655B20, CSaveLoadWindow::DeleteSave);
