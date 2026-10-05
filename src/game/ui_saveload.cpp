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
// reads (render::ClientSurface). Outside the switch-over the pixels still go through a Lock of
// the DirectDraw texture, which the old path draws, and the filled block is copied into the
// memory; in the switch-over ("present") they are written into and read from the memory only,
// with no Lock (DirectPixels). Without the option everything is as the original.
#include "ui.h"

#include "console.h"
#include "dx_surface.h"
#include "item_db_text.h"
#include "net_link.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "render/render.h"
#include "originals_engine.h"
#include "originals_library.h"
#include "originals_ui.h"
#include "originals_world.h"

#include <cstdint>
#include <cstring>
#include <new>

namespace {

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(object) + offset);
}

template <typename... Args>
long ComCall(void* object, int slot, Args... args) {
    return reinterpret_cast<long(__stdcall*)(void*, Args...)>((*static_cast<void***>(object))[slot])(object, args...);
}

const char* Text(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }
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

constexpr int kLock = 25, kUnlock = 32;              // IDirectDrawSurface7
constexpr int kSlotMatchesMask = 14;                 // CFileFind::MatchesMask

// --- Callees not rewritten (thiscall declared __fastcall with the unused EDX) --------------------

// 004800E0 (cdecl): DirectX error report; nonzero to go on.
__declspec(naked) int __cdecl DirectXError(long, const char*) EI_ORIGINAL(0x004800E0)
// 00455A90 (fastcall): the free space on the drive of `path` (0 when unknown).
__declspec(naked) double __fastcall DiskFreeSpace(const char* /*path*/) EI_ORIGINAL(0x00455A90)
// 0057BC30 (thiscall on the game flow): the space a save needs.
__declspec(naked) int __fastcall SaveSpaceNeeded(void* /*flow*/) EI_ORIGINAL(0x0057BC30)
// 00581610 (RET 4): GameFlow::SaveGame's thunk (world_game.cpp), through its address.
__declspec(naked) int __fastcall SaveGameAt(void* /*flow*/, void*, const char* /*slot*/) EI_ORIGINAL(0x00581610)
// 005E5200 (RET 4): the cursor's shape (3 busy, 0 normal).
__declspec(naked) int __fastcall CursorSetShape(void*, void*, int) EI_ORIGINAL(0x005E5200)
// 005F75B0 (RET 4): the interface shows `text` in a notice band and draws a frame.
// 00634650 (RET 0x18): the message box; 005F7350 (RET 0xC): opens a window on top.
__declspec(naked) CUiWindow* __fastcall ConstructMessageBox(void* /*memory*/, void*, CInterface3D*, const char*,
                                                            const char* /*text key*/, int, int, int)
    EI_ORIGINAL(0x00634650)
__declspec(naked) void __fastcall ActivateWindow(CInterface3D*, void*, void* /*window*/, int, int) EI_ORIGINAL(0x005F7350)
// 00578B50 / 00578A90 (RET 4): the local player's variable `name` (ST0) and zone (a CString
// constructed at `out`, returned).
__declspec(naked) mfc::CString* __fastcall PlayerZone(void*, void*, void* /*out*/) EI_ORIGINAL(0x00578A90)
// 0045E850 (RET 4): CMapDatabase::FindMap, the zone record (+4 its map) named `name`, or null.
// 0065AFE0 (RET 4): the scroll bar's range (0..maximum).
__declspec(naked) void __fastcall ScrollbarSetRange(CUiScrollbar*, void*, int /*maximum*/) EI_ORIGINAL(0x0065AFE0)
// 006FE508: CTime::GetCurrentTime (time(), returned through `out`).
__declspec(naked) std::int32_t* __stdcall CurrentTime(std::int32_t* /*out*/) EI_ORIGINAL(0x006FE508)
// 006E80D8: atoi.
// 0070133F (RET 8): CFile::Write.
__declspec(naked) void __fastcall FileWrite(mfc::CFile*, void*, const void*, unsigned) EI_ORIGINAL(0x0070133F)
// 006FE925: CStringArray's constructor.
__declspec(naked) void __fastcall StringArrayConstruct(void*) EI_ORIGINAL(0x006FE925)

// The stream's header (004A5340, RET 8: magic, version) and reads (004A52D0, RET 0xC: true when
// the magic and version fit, the version kept in +8; 00461190 / 004E5840, RET 4: a string up to
// a zero into a new CString at `out` / into the CString `out`).
__declspec(naked) void __fastcall StreamWriteHeader(void*, void*, unsigned, unsigned) EI_ORIGINAL(0x004A5340)
__declspec(naked) bool __fastcall StreamReadHeader(void*, void*, unsigned, unsigned, int) EI_ORIGINAL(0x004A52D0)
__declspec(naked) mfc::CString* __fastcall StreamReadString(void*, void*, void* /*out*/) EI_ORIGINAL(0x00461190)
__declspec(naked) void __fastcall StreamReadInto(void*, void*, mfc::CString* /*out*/) EI_ORIGINAL(0x004E5840)

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

// CFileFind members beyond lib/mfc.h, called on existing objects through this view.
struct FileFindOps : mfc::CFileFind {
    BOOL Next();                       // 006E6653 FindNextFile
    BOOL IsDots() const;               // 006E661A
    mfc::CString FilePath() const;     // 006E6722 GetFilePath
    mfc::CString FileName() const;     // 006E684B GetFileName
    void Close();                      // 006E63E9
    // MatchesMask(FILE_ATTRIBUTE_DIRECTORY), through the vtable as the original calls it.
    BOOL IsDirectory() const { return ei::VCall<BOOL>(this, kSlotMatchesMask, 0x10ul); }
};
__declspec(naked) BOOL FileFindOps::Next() EI_ORIGINAL(0x006E6653)
__declspec(naked) BOOL FileFindOps::IsDots() const EI_ORIGINAL(0x006E661A)
__declspec(naked) mfc::CString FileFindOps::FilePath() const EI_ORIGINAL(0x006E6722)
__declspec(naked) mfc::CString FileFindOps::FileName() const EI_ORIGINAL(0x006E684B)
__declspec(naked) void FileFindOps::Close() EI_ORIGINAL(0x006E63E9)
FileFindOps& Ops(mfc::CFileFind& find) { return static_cast<FileFindOps&>(find); }

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
        StringArrayConstruct(e.stringArrays_);
        StringArrayConstruct(e.stringArrays_ + 0x14);
    }
    explicit LocalEntry(const SaveEntry& from) { EntryCopy(&e, nullptr, &from); }
    ~LocalEntry() { EntryDestruct(&e); }
    LocalEntry(const LocalEntry&) = delete;
    LocalEntry& operator=(const LocalEntry&) = delete;
};

char* HolderOf(CEngineObject* texture) { return At<char*>(texture, 0x5C); }   // CTexture::surface_

// The picture sprite (scene object 3, a CUiMesh): +0x220 shown, +0x280 its texture.
void ShowSprite(CEngineObject* sprite, int shown) { At<int>(sprite, 0x220) = shown; }
void SetSpriteTexture(CEngineObject* sprite, CEngineObject* texture) { At<CEngineObject*>(sprite, 0x280) = texture; }

// The pixels of a picture texture for one fill or read. In the switch-over with game-owned memory
// they are that memory (no DirectDraw); otherwise the surface is locked with flags 0 as the
// original does (the result in the holder's +8, a failure reported), and Mirror copies the filled
// block into the game-owned memory, when there is some, before the Unlock.
class PicturePixels {
public:
    explicit PicturePixels(char* holder) : holder_(holder) { desc_[0] = 0x7C; }

    // False when the Lock failed and the report said stop.
    bool Begin() {
        direct_ = DirectPixels(holder_, desc_);
        if (direct_) return true;
        long hr = ComCall(At<void*>(holder_, 0x10), kLock, static_cast<void*>(nullptr), static_cast<void*>(desc_), 0ul,
                          static_cast<void*>(nullptr));
        At<long>(holder_, 8) = hr;
        return hr == 0 || DirectXError(hr, Text(kCantLock)) != 0;
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
        long hr = ComCall(At<void*>(holder_, 0x10), kUnlock, static_cast<void*>(nullptr));
        At<long>(holder_, 8) = hr;
        if (hr != 0) DirectXError(hr, Text(kCantUnlock));
    }

private:
    char* holder_;
    std::uint32_t desc_[0x1F] = {};
    bool direct_ = false;
};

// The escape menu's 256x192 5:6:5 copy of the screen (interface +0x5C), or null.
const void* SavePicture(CInterface3D* ui) { return At<const void*>(ui, 0x5C); }

// The entry vector (+0x164) as the STLport helpers take it.
void* EntryVector(CSaveLoadWindow* w) { return &w->entriesBegin_; }

} // namespace

// --- Forwarders through the original addresses ---------------------------------------------------

__declspec(naked) void CSaveLoadWindow::CallSaveGame() EI_ORIGINAL(0x00655550)
__declspec(naked) void CSaveLoadWindow::CallShowPicture() EI_ORIGINAL(0x00656E10)
__declspec(naked) void CSaveLoadWindow::CallFillEntries() EI_ORIGINAL(0x00655D40)

// --- Game-owned memory ---------------------------------------------------------------------------

void CSaveLoadWindow::GivePictureMemory(CEngineObject* texture) {
    if (!texture || !render::CpuWindows()) return;
    char* holder = HolderOf(texture);
    if (!holder) return;
    // The surface's size (004EFA60 put it in +0x14 / +0x18) and the texture's pixel format
    // (CTexture +0x3C, the device's format for the texture's format id).
    AttachTextureMemory(holder, At<int>(holder, 0x14), At<int>(holder, 0x18), &At<std::uint32_t>(texture, 0x3C));
}

// --- 00656E10 ------------------------------------------------------------------------------------

void CSaveLoadWindow::ShowPicture() {
    ShowSprite(sceneObjects_.data_[3], 0);
    int selected = selected_;
    if (selected < 0 || static_cast<unsigned>(selected) >= static_cast<unsigned>(EntryCount())) return;
    SaveEntry* entry = entriesBegin_ + selected;
    if (entry->broken_) return;
    if (isSave_ && selected == 0) {
        // The new save: the game's picture.
        if (!dialog_) return;
        ShowSprite(sceneObjects_.data_[3], 1);
        SetSpriteTexture(sceneObjects_.data_[3], dialog_);
        return;
    }
    mfc::CString folder;
    folder.Format(Text(kSlotFolder), GameFolder(), entry->folder_);
    mfc::CFileFind find;
    BOOL found;
    {
        mfc::CString path = folder + Text(kShotFile);
        found = find.FindFile(path, 0);
    }
    if (!found) return;
    char* holder = HolderOf(objects_.data_[1]);
    gameapi::EnterCriticalSection(DeviceLock());
    PicturePixels pixels(holder);
    if (!pixels.Begin()) return;   // (the device lock stays held, as in the original)
    mfc::CFile file;
    {
        mfc::CString path = folder + Text(kShotFile);
        file.Open(path, 0, nullptr);
    }
    file.Read(pixels.Bits(), kShotBytes);
    file.Close();
    pixels.Mirror();
    gameapi::LeaveCriticalSection(DeviceLock());
    pixels.End();
    ShowSprite(sceneObjects_.data_[3], 1);
    SetSpriteTexture(sceneObjects_.data_[3], objects_.data_[1]);
}
EI_REPLACE_METHOD(0x00656E10, CSaveLoadWindow::ShowPicture);

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
            CUiWindow* box = memory ? ConstructMessageBox(memory, nullptr, interface_, Text(kNoSpace), Text(kNoSpaceText),
                                                          0x30000, 1, 1)
                                    : nullptr;
            ActivateWindow(interface_, nullptr, box, 1, 1);
            return;
        }
    }
    mfc::CString folder;
    folder.Format(Text(kSlotFolder), GameFolder(), entry->folder_);
    mfc::CString notice;
    Texts()->GetString(Text(kNotifySaving), &notice);
    ShowStatusText(kInterface, nullptr, notice);
    SaveGameAt(kFlow, nullptr, entry->folder_);
    CursorSetShape(kCursor, nullptr, 3);

    // info.sav: the new save's entry (the first) with the title being given.
    LocalFileStream info;
    {
        mfc::CString path = folder + Text(kInfoFile);
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
        char* holder = HolderOf(dialog_);
        gameapi::EnterCriticalSection(DeviceLock());
        PicturePixels pixels(holder);
        if (!pixels.Begin()) {
            // (The device lock stays held and the cursor busy, as in the original.)
            interface_->SetPendingValue(kClosed);
            return;
        }
        mfc::CFile shot;
        {
            mfc::CString path = folder + Text(kShotFile);
            shot.Open(path, kOpenWrite, nullptr);
        }
        FileWrite(&shot, nullptr, pixels.Bits(), kShotBytes);
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
    pattern.Format(Text(kSavePattern), GameFolder());
    mfc::CFileFind find;
    EntriesErase(EntryVector(this), nullptr, entriesBegin_, entriesEnd_);
    int highest = 0;     // the highest number after "save" in the folder names
    int found = 0;       // the last FindNextFile result (the sort's comparison object below)
    BOOL more = find.FindFile(pattern, 0);
    while (more) {
        found = Ops(find).Next();
        more = found;
        if (Ops(find).IsDots() || !Ops(find).IsDirectory()) continue;
        LocalEntry fresh;
        if (entriesEnd_ != entriesCapacity_) {
            EntryConstruct(entriesEnd_, &fresh.e);
            ++entriesEnd_;
        } else {
            EntriesInsertOverflow(EntryVector(this), nullptr, entriesEnd_, &fresh.e, 1);
        }
        SaveEntry* entry = entriesEnd_ - 1;
        Str(entry->folder_) = Ops(find).FileName();
        mfc::CFileFind infoFind;
        BOOL hasInfo;
        {
            mfc::CString folderPath = Ops(find).FilePath();
            mfc::CString path = folderPath + Text(kInfoInFolder);
            hasInfo = infoFind.FindFile(path, 0);
        }
        if (!hasInfo) {
            entry->broken_ = true;
            Str(entry->title_) = Text(kBrokenTitle);
        } else {
            Ops(infoFind).Next();
            LocalFileStream info;
            info.Open(Ops(infoFind).FilePath(), 0, 1);
            if (StreamReadHeader(&info.s, nullptr, kInfoMagic, kInfoVersion, 1)) {
                info.Read(&entry->gameTime_, 4);
                {
                    alignas(mfc::CString) unsigned char storage[sizeof(mfc::CString)];
                    mfc::CString* map = StreamReadString(&info.s, nullptr, storage);
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
        Ops(infoFind).Close();
        if (console::Length(Str(entry->folder_)) > 4) {
            int number = GameAtoi(console::Ops(Str(entry->folder_)).Mid(4));
            if (!(highest > number)) highest = number;
        }
    }
    Ops(find).Close();
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
        Str(entry->folder_).Format(Text(kNewFolder), highest + 1);
        entry->gameTime_ = static_cast<float>(HoverPlayerVariable(kGlue, nullptr, Text(kGameTime)));
        {
            alignas(mfc::CString) unsigned char storage[sizeof(mfc::CString)];
            mfc::CString* zone = PlayerZone(kGlue, nullptr, storage);
            Str(entry->zone_) = *zone;
            zone->~CString();
        }
        if (std::uint8_t* record = MapDatabaseFind(kMapDatabase, nullptr, entry->zone_))
            Str(entry->map_) = At<mfc::CString>(record, 4);
        EntryMakeTitle(entry, nullptr, 1);
        entry->broken_ = false;

        // The game's picture: a new texture filled from the escape menu's copy of the screen.
        if (dialog_) dialog_->Release();
        dialog_ = nullptr;
        if (SavePicture(interface_)) {
            CEngineObject* shot = CreateTexture(0x100, 0x100, 0x1E);
            dialog_ = shot;
            ++shot->refCount_;
            RemoveObjectAt(objects_.size_ - 1);
            GivePictureMemory(dialog_);
            char* holder = HolderOf(dialog_);
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
        ScrollbarSetRange(scrollbar_, nullptr, EntryCount() - kVisibleRows);
        scrollbar_->CallSetPosition(position);
    } else {
        ScrollbarSetRange(scrollbar_, nullptr, 0);
    }
    firstVisible_ = 0;
}
EI_REPLACE_METHOD(0x00655D40, CSaveLoadWindow::FillEntries);
