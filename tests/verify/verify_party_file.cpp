// Checks for the multiplayer heroes' party files (party_file.cpp). They move, write and delete
// files, so ours runs alone and the wrapper checks the files left behind (through the game's own
// imports, so the harness's overlay sees the same files).
// Scenarios: ui_hero_delete (a new hero's statistics page writes <n>.mp: 00617A50 -> 00662AC0;
// the list's reload 00662FB0 reads each file, 00662DD0); ui_hero_screen (leaving the hero screen
// saves the heroes, 00615850 -> 006632B0, when there are any).
#include "verify.h"

#include "../../src/game/party.h"
#include "../../src/game/ui_screens.h"
#include "../../src/game/world.h"

#include "../../src/lib/gameapi.h"
#include "../../src/lib/mfc.h"

#include <cstring>

namespace {
void* s_unused;

bool Exists(const char* path) { return gameapi::GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES; }

bool Numbered(const mfc::CString& name) {
    const char* text = name;
    const char* dot = std::strrchr(text, '.');
    if (!dot) return false;
    for (const char* p = text; p < dot; ++p)
        if (*p < '0' || *p > '9') return false;
    return true;
}
} // namespace

// 00662AC0 (stolen 0): a numbered file exists afterwards and temp.mp does not; any other name's
// file does not exist.
static void __fastcall WriteFileVerify(MpHeroRecord* self, void*, const char* folder) {
    self->WriteFile(folder);
    mfc::CString path = mfc::CString(folder) + self->file;
    mfc::CString temp = mfc::CString(folder) + "temp.mp";
    bool numbered = Numbered(self->file);
    bool ok = numbered ? Exists(path) && !Exists(temp) : !Exists(path);
    ei::verify::Check("MpHeroRecord::WriteFile", true, ok, "%s (%s)", static_cast<const char*>(path),
                      numbered ? "written" : "deleted");
}
EI_VERIFY(0x00662AC0, WriteFileVerify, 0, s_unused);

// 00662DD0 (stolen 0): false for a file that does not exist; a true result has the party's
// item manager made (LoadParty creates the party when it has none).
static bool __fastcall ReadFileVerify(MpHeroRecord* self, void*, const char* folder) {
    mfc::CString path = mfc::CString(folder) + self->file;
    bool existed = Exists(path);
    bool result = self->ReadFile(folder);
    bool ok = existed ? !result || self->Party()->itemManager_ != nullptr : !result;
    ei::verify::Check("MpHeroRecord::ReadFile", true, ok, "%s: %d", static_cast<const char*>(path), result ? 1 : 0);
    return result;
}
EI_VERIFY(0x00662DD0, ReadFileVerify, 0, s_unused);

// 006632B0 (stolen 0): with a folder, it exists afterwards and every numbered entry's file is in
// it.
static void __fastcall SaveAllVerify(SavedParties* self, void*) {
    self->SaveAll();
    if (!*self->folder_) return;
    bool ok = Exists(self->folder_);
    auto* begin = reinterpret_cast<MpHeroRecord*>(self->entries_);
    int count = static_cast<int>(reinterpret_cast<MpHeroRecord*>(self->finish_) - begin);
    for (int i = 0; ok && i < count; ++i)
        if (Numbered(begin[i].file)) ok = Exists(mfc::CString(self->folder_) + begin[i].file);
    ei::verify::Check("SavedParties::SaveAll", true, ok, "%s: %d heroes", self->folder_, count);
}
EI_VERIFY(0x006632B0, SaveAllVerify, 0, s_unused);

// A-H17. 0065F2D0 (stolen 0; the static destructor at exit, so every scenario that quits through
// WinMain): ours only; it only frees, so the check only counts the entries it destroyed.
static void __fastcall DestructVerify(SavedParties* self, void*) {
    auto* begin = reinterpret_cast<MpHeroRecord*>(self->entries_);
    int count = static_cast<int>(reinterpret_cast<MpHeroRecord*>(self->finish_) - begin);
    self->Destruct();
    ei::verify::Check("SavedParties::Destruct", true, count >= 0, "%d heroes", count);
}
EI_VERIFY(0x0065F2D0, DestructVerify, 0, s_unused);
