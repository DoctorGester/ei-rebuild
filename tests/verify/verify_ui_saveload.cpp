// Checks for the save / load window's pictures and list (src/game/ui_saveload.cpp).
//
// ShowPicture and FillEntries run the original first on the real window: both can run twice (the
// original's results are taken, ours then replaces them, as the game does when it calls them
// again). ShowPicture's texture is scribbled over between the two runs, so ours has to write the
// picture itself. SaveGame saves the game, so only ours runs and the files it wrote are read back.
//
// A picture texture's pixels are compared as the game writes them: the first 0x18000 bytes of
// the DirectDraw texture (a Lock) or of its game-owned memory ("cpuwindows"). With game-owned
// memory ours must have written the memory, and outside the switch-over the texture too. In the
// switch-over ("present") the textures have no DirectDraw texture at all (00691D80, ticket R8):
// the original's run is lent one (memory_texture.h) and ours writes the memory only.
#include "verify.h"
#include "memory_texture.h"

#include "../../src/game/dx_surface.h"
#include "../../src/game/ui.h"
#include "../../src/game/world.h"
#include "../../src/lib/mfc.h"
#include "../../src/render/render.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
void* s_unused;

constexpr std::uint32_t kShotBytes = 0x18000;

template <typename... Args>
long SurfaceCall(void* surface, int slot, Args... args) {
    return reinterpret_cast<long(__stdcall*)(void*, Args...)>((*static_cast<void***>(surface))[slot])(surface, args...);
}

CRITICAL_SECTION* DeviceLock() { return reinterpret_cast<CRITICAL_SECTION*>(0x007AEE78); }

char* HolderOf(CEngineObject* texture) {
    return texture ? *reinterpret_cast<char**>(reinterpret_cast<char*>(texture) + 0x5C) : nullptr;
}

// The texture's first 0x18000 bytes through a Lock (`write`: filled with `value` instead).
bool SurfaceBlock(char* holder, std::vector<std::uint8_t>* out, bool write = false, std::uint8_t value = 0) {
    void* surface = holder ? *reinterpret_cast<void**>(holder + 0x10) : nullptr;
    if (!surface) return false;
    std::uint32_t desc[0x1F] = {};
    desc[0] = 0x7C;
    ::EnterCriticalSection(DeviceLock());
    // DDLOCK_WAIT | DDLOCK_WRITEONLY or DDLOCK_READONLY
    bool ok = SurfaceCall(surface, 25, static_cast<RECT*>(nullptr), static_cast<void*>(desc), write ? 0x21ul : 0x11ul,
                          static_cast<void*>(nullptr)) == 0;
    auto* bits = reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(desc[9]));
    ok = ok && bits && desc[2] * desc[4] >= kShotBytes;
    if (ok) {
        if (write)
            std::memset(bits, value, kShotBytes);
        else
            out->assign(bits, bits + kShotBytes);
    }
    if (desc[9]) SurfaceCall(surface, 32, static_cast<RECT*>(nullptr));
    ::LeaveCriticalSection(DeviceLock());
    return ok;
}

// The same from the game-owned memory; false without.
bool MemoryBlock(char* holder, std::vector<std::uint8_t>* out, bool write = false, std::uint8_t value = 0) {
    std::uint32_t desc[0x1F] = {};
    if (!holder || !ClientPixels(holder, desc) || desc[2] * desc[4] < kShotBytes) return false;
    auto* bits = reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(desc[9]));
    if (write)
        std::memset(bits, value, kShotBytes);
    else
        out->assign(bits, bits + kShotBytes);
    return true;
}

// Ours' pixels against `picture`: the memory when there is some (it must be there with
// "cpuwindows"), the texture outside the switch-over or without memory.
const char* CheckOurs(char* holder, const std::vector<std::uint8_t>& picture) {
    std::vector<std::uint8_t> pixels;
    bool memory = MemoryBlock(holder, &pixels);
    if (render::CpuWindows() && !memory) return "no game memory";
    if (memory && pixels != picture) return "memory differs";
    if (!memory || !render::Presenting()) {
        if (!SurfaceBlock(holder, &pixels)) return "texture not readable";
        if (pixels != picture) return "texture differs";
    }
    return nullptr;
}

struct SpriteState {
    int shown;
    CEngineObject* texture;
    bool operator==(const SpriteState& o) const { return shown == o.shown && texture == o.texture; }
};
SpriteState SpriteOf(CEngineObject* sprite) {
    auto* p = reinterpret_cast<char*>(sprite);
    return SpriteState{*reinterpret_cast<int*>(p + 0x220), *reinterpret_cast<CEngineObject**>(p + 0x280)};
}
void PutSprite(CEngineObject* sprite, const SpriteState& s) {
    auto* p = reinterpret_cast<char*>(sprite);
    *reinterpret_cast<int*>(p + 0x220) = s.shown;
    *reinterpret_cast<CEngineObject**>(p + 0x280) = s.texture;
}

std::string Text(const char* s) { return s ? s : ""; }

} // namespace

// 00656E10: MOV EAX,FS:[0] (6 bytes). The original on the window, its sprite state and (when it
// read a shot.sav into objects_[1]) the picture taken; the texture (and its memory) scribbled
// over, the sprite put back, ours run: the same sprite state and picture. A texture with only the
// new renderer's memory (the switch-over, ticket R8) is lent a DirectDraw texture for the
// original's run (memory_texture.h); ours then writes the memory.
static void(__fastcall* s_showPicture)(CSaveLoadWindow*, void*);
static void __fastcall ShowPictureVerify(CSaveLoadWindow* self, void*) {
    CEngineObject* sprite = self->sceneObjects_.data[3];
    SpriteState before = SpriteOf(sprite);
    CEngineObject* texture = self->objects_.size > 1 ? self->objects_.data[1] : nullptr;
    char* holder = HolderOf(texture);
    auto* lentHolder = reinterpret_cast<CDXSurface*>(holder);
    bool lent = memory_texture::Lend(lentHolder);
    if (!lent && memory_texture::MemoryOnly(lentHolder)) {   // DirectDraw refused (logged): ours only
        self->CSaveLoadWindow::ShowPicture();
        return;
    }
    s_showPicture(self, nullptr);
    SpriteState expected = SpriteOf(sprite);
    std::vector<std::uint8_t> picture;
    bool read = holder && expected.shown == 1 && expected.texture == texture && SurfaceBlock(holder, &picture);
    if (lent) memory_texture::TakeBack(lentHolder, false);
    if (read) {
        SurfaceBlock(holder, nullptr, true, 0xA5);
        MemoryBlock(holder, nullptr, true, 0xA5);
    }
    PutSprite(sprite, before);
    self->CSaveLoadWindow::ShowPicture();
    SpriteState actual = SpriteOf(sprite);
    const char* pixels = read ? CheckOurs(holder, picture) : nullptr;
    ei::verify::Check("CSaveLoadWindow::ShowPicture", true, expected == actual && !pixels,
                      "selected %d save %d: shown %d/%d texture %p/%p, picture %s", self->selected_, self->isSave_,
                      expected.shown, actual.shown, static_cast<void*>(expected.texture),
                      static_cast<void*>(actual.texture), read ? (pixels ? pixels : "same") : "not read");
}
EI_VERIFY(0x00656E10, ShowPictureVerify, 6, s_showPicture);

namespace {

struct EntryState {
    std::string title, folder, map, zone;
    std::int32_t savedAt;
    float gameTime;
    bool broken;
};

struct ListState {
    std::vector<EntryState> entries;
    int firstVisible, maxPosition, position, objects;
    bool moved, shot;
};

ListState StateOf(CSaveLoadWindow* w) {
    ListState s{};
    for (SaveEntry* e = w->entriesBegin_; e != w->entriesEnd_; ++e)
        s.entries.push_back(EntryState{Text(e->title_), Text(e->folder_), Text(e->map_), Text(e->zone_), e->savedAt_,
                                       e->gameTime_, e->broken_});
    s.firstVisible = w->firstVisible_;
    s.maxPosition = w->scrollbar_->maxPosition_;
    s.position = w->scrollbar_->position_;
    s.moved = w->scrollbar_->moved_;
    s.objects = w->objects_.size;
    s.shot = w->dialog_ != nullptr;
    return s;
}

// The first entry that differs (-1: none). What the original leaves unwritten is not compared:
// the time and game time of a broken save (and of a save whose info.sav header did not fit: a
// mismatch there is expected), the time of a save window's new save.
int FirstDifference(const ListState& a, const ListState& b, bool save) {
    if (a.entries.size() != b.entries.size()) return static_cast<int>(a.entries.size() < b.entries.size() ? a.entries.size() : b.entries.size());
    for (std::size_t i = 0; i < a.entries.size(); ++i) {
        const EntryState& x = a.entries[i];
        const EntryState& y = b.entries[i];
        bool same = x.title == y.title && x.folder == y.folder && x.map == y.map && x.zone == y.zone && x.broken == y.broken;
        if (!x.broken) {
            same = same && ei::verify::NearlyEqual(x.gameTime, y.gameTime, 4, 1e-6f);
            if (!(save && i == 0)) same = same && x.savedAt == y.savedAt;
        }
        if (!same) return static_cast<int>(i);
    }
    return -1;
}

} // namespace

// 00655D40: PUSH -1; PUSH 0x72F30F (7 bytes). The original on the window, its list taken (every
// entry's strings and fields, the scroll bar's range, position and moved flag, the list's scroll
// position, the object count, the new save's picture); ours run over it: the same. In the
// switch-over the original's new picture texture (00691D80: only the new renderer's memory, ticket
// R8) is lent a DirectDraw texture while the original runs; its pixels are read from the memory.
static void(__fastcall* s_fillEntries)(CSaveLoadWindow*, void*);
static void __fastcall FillEntriesVerify(CSaveLoadWindow* self, void*) {
    {
        memory_texture::LendNewTextures lend;
        s_fillEntries(self, nullptr);
    }
    ListState expected = StateOf(self);
    std::vector<std::uint8_t> picture;
    char* shotHolder = HolderOf(self->dialog_);
    bool shot = expected.shot && (SurfaceBlock(shotHolder, &picture) ||
                                  (memory_texture::MemoryOnly(reinterpret_cast<CDXSurface*>(shotHolder)) &&
                                   MemoryBlock(shotHolder, &picture)));
    self->CSaveLoadWindow::FillEntries();
    ListState actual = StateOf(self);
    int entry = FirstDifference(expected, actual, self->isSave_);
    bool same = entry < 0 && expected.firstVisible == actual.firstVisible && expected.maxPosition == actual.maxPosition &&
                expected.position == actual.position && expected.moved == actual.moved &&
                expected.objects == actual.objects && expected.shot == actual.shot;
    const char* pixels = shot ? CheckOurs(HolderOf(self->dialog_), picture) : nullptr;
    ei::verify::Check("CSaveLoadWindow::FillEntries", true, same && !pixels,
                      "save %d: entries %u/%u (first difference %d), range %d/%d position %d/%d, picture %s",
                      self->isSave_, static_cast<unsigned>(expected.entries.size()),
                      static_cast<unsigned>(actual.entries.size()), entry, expected.maxPosition, actual.maxPosition,
                      expected.position, actual.position, shot ? (pixels ? pixels : "same") : "none");
}
EI_VERIFY(0x00655D40, FillEntriesVerify, 7, s_fillEntries);

namespace {
// The first `size` bytes of saves\<folder>\<file> (the game's MFC CFile, so harness runs read
// harness_saves); false when the file cannot be read.
bool ReadSaveFile(const std::string& folder, const char* file, std::vector<std::uint8_t>* out, unsigned size) {
    char path[MAX_PATH];
    std::snprintf(path, sizeof(path), "%ssaves\\%s\\%s", *reinterpret_cast<char* const*>(0x007AF460), folder.c_str(), file);
    mfc::CFile f;
    if (!f.Open(path, 0, nullptr)) return false;
    out->assign(size, 0);
    unsigned got = f.GetLength() >= size ? f.Read(out->data(), size) : 0;
    f.Close();
    return got == size;
}
} // namespace

// 00655550: ours only (it saves the game). A save that closes the window (pending value 0x100002)
// has written info.sav (header 0x111, version 3) and, with a picture, shot.sav holding it; a save
// stopped by the disc-full message box leaves the pending value alone.
static void __fastcall SaveGameVerify(CSaveLoadWindow* self, void*) {
    int selected = self->selected_;
    bool valid = selected >= 0 && selected < self->EntryCount();
    std::string folder = valid ? Text(self->entriesBegin_[selected].folder_) : std::string();
    CInterface3D* ui = self->interface_;
    std::uint32_t pendingValue = ui->pendingValue_;
    bool pendingFlag = ui->pendingFlag_;
    self->CSaveLoadWindow::SaveGame();
    bool closed = ui->pendingFlag_ && ui->pendingValue_ == 0x100002;
    bool untouched = ui->pendingValue_ == pendingValue && ui->pendingFlag_ == pendingFlag;
    const char* problem = nullptr;
    if (!valid || !closed) {
        if (!untouched) problem = "pending value changed";
    } else {
        std::vector<std::uint8_t> info;
        if (!ReadSaveFile(folder, "info.sav", &info, 8))
            problem = "no info.sav";
        else if (info[0] != 0x11 || info[1] != 0x01 || info[2] || info[3] || info[4] != 3 || info[5] || info[6] || info[7])
            problem = "info.sav header";
        if (!problem && self->dialog_) {
            std::vector<std::uint8_t> shot;
            if (!ReadSaveFile(folder, "shot.sav", &shot, kShotBytes)) problem = "no shot.sav";
            else problem = CheckOurs(HolderOf(self->dialog_), shot);
        }
    }
    ei::verify::Check("CSaveLoadWindow::SaveGame", true, problem == nullptr, "selected %d folder %s closed %d: %s", selected,
                      folder.c_str(), closed, problem ? problem : "ok");
}
EI_VERIFY(0x00655550, SaveGameVerify, 0, s_unused);

// 00653A50: the entry's title (FillEntries for entries without one; ui_saveload: the autosave
// "savea"). The original runs first on the real entry, its title is kept, then ours; the titles
// must be the same text.
static void(__fastcall* s_makeTitle)(SaveEntry*, void*, int);
static void __fastcall MakeTitleVerify(SaveEntry* self, void*, int unused) {
    s_makeTitle(self, nullptr, unused);
    std::string expected = self->title_;
    self->SaveEntry::MakeTitle(unused);
    ei::verify::Check("SaveEntry::MakeTitle", true, expected == self->title_, "folder %s: \"%s\" vs \"%s\"",
                      self->folder_, expected.c_str(), self->title_);
}
EI_VERIFY(0x00653A50, MakeTitleVerify, 7, s_makeTitle);

// 006570E0 (the autosaves "savea" / "saveb" and the quick save; every newgame saves "savea"): ours
// only (it saves the game); info.sav (header 0x111, version 3) and a full shot.sav are read back.
// A disc too full for a save leaves nothing, which shows as a mismatch here.
static void __fastcall QuickSaveVerify(const char* slot, char stepFrame) {
    bool client = IsMultiplayerClient() != 0;
    QuickSave(slot, stepFrame);
    const char* problem = nullptr;
    if (!client) {
        std::vector<std::uint8_t> info;
        std::vector<std::uint8_t> shot;
        if (!ReadSaveFile(slot, "info.sav", &info, 8))
            problem = "no info.sav";
        else if (info[0] != 0x11 || info[1] != 0x01 || info[2] || info[3] || info[4] != 3 || info[5] || info[6] || info[7])
            problem = "info.sav header";
        else if (!ReadSaveFile(slot, "shot.sav", &shot, kShotBytes))
            problem = "no shot.sav";
    }
    ei::verify::Check("QuickSave", true, problem == nullptr, "slot %s client %d: %s", slot, client,
                      problem ? problem : "ok");
}
EI_VERIFY(0x006570E0, QuickSaveVerify, 0, s_unused);

// 006575B0 (the quick load; no scenario): ours only. With the folder present (and not a client)
// the game flow's request (+0) is a load (2).
static void __fastcall QuickLoadVerify(const char* slot) {
    bool client = IsMultiplayerClient() != 0;
    std::vector<std::uint8_t> info;
    bool present = ReadSaveFile(slot, "info.sav", &info, 0);
    QuickLoad(slot);
    int request = *reinterpret_cast<const int*>(std::uintptr_t(0x007AFE20));
    ei::verify::Check("QuickLoad", true, client || !present || request == 2, "slot %s request %d", slot, request);
}
EI_VERIFY(0x006575B0, QuickLoadVerify, 0, s_unused);

// --- A-H14 ----------------------------------------------------------------------------------------
// 00653BD0 / 00653CD0 (a save's zone / allod title; MakeTitle and RefreshList, every load window
// with a save in it: ui_screens' Load row): side by side (7 stolen bytes: PUSH -1; PUSH imm32), the
// original's text into the caller's CString, ours into a local; same text.
using EntryTitleFn = mfc::CString*(__fastcall*)(const SaveEntry*, void*, mfc::CString*);
static EntryTitleFn s_zoneTitle;
static EntryTitleFn s_allodTitle;
static void CompareTitles(const char* name, const mfc::CString& expected, const mfc::CString& actual, const SaveEntry* e) {
    ei::verify::Check(name, true, std::strcmp(expected, actual) == 0, "folder %s: \"%s\" vs \"%s\"", e->folder_,
                      static_cast<const char*>(expected), static_cast<const char*>(actual));
}
static mfc::CString* __fastcall ZoneTitleVerify(const SaveEntry* self, void*, mfc::CString* out) {
    s_zoneTitle(self, nullptr, out);
    CompareTitles("SaveEntry::ZoneTitle", *out, self->ZoneTitle(), self);
    return out;
}
static mfc::CString* __fastcall AllodTitleVerify(const SaveEntry* self, void*, mfc::CString* out) {
    s_allodTitle(self, nullptr, out);
    CompareTitles("SaveEntry::AllodTitle", *out, self->AllodTitle(), self);
    return out;
}
EI_VERIFY(0x00653BD0, ZoneTitleVerify, 7, s_zoneTitle);
EI_VERIFY(0x00653CD0, AllodTitleVerify, 7, s_allodTitle);

// 006577B0 (the list's selection takes its title; save windows): ours only. A valid selection's
// title is the name, the caret at its end, not typed.
static void __fastcall TakeTitleVerify(CSaveLoadWindow* self) {
    self->TakeSelectedTitle();
    int selected = self->selected_;
    bool ok = true;
    if (selected >= 0 && selected < self->EntryCount()) {
        const char* title = self->entriesBegin_[selected].title_;
        ok = std::strcmp(self->name_, title) == 0 && self->cursor_ == static_cast<int>(std::strlen(title)) &&
             !self->nameTyped_;
    }
    ei::verify::Check("CSaveLoadWindow::TakeSelectedTitle", true, ok, "selected %d", selected);
}
EI_VERIFY(0x006577B0, TakeTitleVerify, 0, s_unused);

// 00655290 / 00655380 (OK / Delete; ui_screens' save and load rows): ours only. A message box goes
// on top when one is asked for.
static void __fastcall ConfirmVerify(CSaveLoadWindow* self) {
    int selected = self->selected_;
    bool save = self->isSave_;
    self->CSaveLoadWindow::Confirm();
    ei::verify::Check("CSaveLoadWindow::Confirm", true, true, "selected %d save %d", selected, save ? 1 : 0);
}
static void __fastcall AskDeleteVerify(CSaveLoadWindow* self) {
    int selected = self->selected_;
    self->CSaveLoadWindow::AskDelete();
    ei::verify::Check("CSaveLoadWindow::AskDelete", true, true, "selected %d", selected);
}
EI_VERIFY(0x00655290, ConfirmVerify, 0, s_unused);
EI_VERIFY(0x00655380, AskDeleteVerify, 0, s_unused);

// 00655A10 (a load window's OK): ours only. A valid, readable save makes the game flow's request
// a load (2).
static void __fastcall LoadSelectedVerify(CSaveLoadWindow* self) {
    int selected = self->selected_;
    bool loads = selected >= 0 && selected < self->EntryCount() && !self->entriesBegin_[selected].broken_;
    self->CSaveLoadWindow::LoadSelected();
    int request = *reinterpret_cast<const int*>(std::uintptr_t(0x007AFE20));
    ei::verify::Check("CSaveLoadWindow::LoadSelected", true, !loads || request == 2, "selected %d request %d", selected,
                      request);
}
EI_VERIFY(0x00655A10, LoadSelectedVerify, 0, s_unused);

// 00655B20 (the delete box's yes): ours only. The folder is gone (unless a file in it could not be
// deleted) and the selection is within the refilled list.
static void __fastcall DeleteSaveVerify(CSaveLoadWindow* self) {
    int selected = self->selected_;
    std::string folder;
    if (selected >= 0 && selected < self->EntryCount()) folder = self->entriesBegin_[selected].folder_;
    self->CSaveLoadWindow::DeleteSave();
    bool ok = true;
    if (!folder.empty()) {
        char path[MAX_PATH];
        std::snprintf(path, sizeof(path), "%ssaves\\%s", *reinterpret_cast<char* const*>(std::uintptr_t(0x007AF460)),
                      folder.c_str());
        ok = GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES && self->selected_ < self->EntryCount();
    }
    ei::verify::Check("CSaveLoadWindow::DeleteSave", true, ok, "folder %s selected %d of %d", folder.c_str(),
                      self->selected_, self->EntryCount());
}
EI_VERIFY(0x00655B20, DeleteSaveVerify, 0, s_unused);
