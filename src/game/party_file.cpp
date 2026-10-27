// The multiplayer heroes' files (<folder>\<n>.mp): one party per file, saved (with its variables)
// into a memory stream and written as a save file (magic 0x114). See party.h, ui_screens.h
// (MpHeroRecord) and docs/notes/items_spells.md ("Party records and the party object").
#include "party.h"
#include "game_strings.h"

#include "core_streams.h"
#include "net_link.h"
#include "ui_screens.h"
#include "world.h"

#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"

#include "ei/hook.h"
#include "game_data.h"
#include "lib/str.h"

#include <cstring>
#include <memory>
#include <string>

namespace {

constexpr unsigned kPartyFileMagic = 0x114;

const char* TempFileName() { return "temp.mp"; }

mfc::CString InFolder(const char* folder, const std::string& name) { return mfc::CString(folder) + name; }
mfc::CString TempFile(const char* folder) { return mfc::CString(folder) + TempFileName(); }

// The file stream and the memory stream the originals build on the stack (the memory stream torn down first).
struct LocalStreams {
    netlink::FileStream file;
    CMemoryStream data;
};

// Whether a file name is "<digits>.<anything>": a '.' and only digits before the last one.
bool Numbered(const std::string& name) {
    int dot = str::ReverseFind(name, '.');
    if (dot == -1) return false;
    const char* text = name.c_str();
    for (int i = 0; i < dot; ++i)
        if (!gamecrt::isdigit(static_cast<signed char>(text[i]))) return false;
    return true;
}

} // namespace

void MpHeroRecord::WriteFile(const char* folder) {
    if (!Numbered(file.GetString())) {
        gameapi::DeleteFileA(InFolder(folder, file.GetString()));
        return;
    }
    {
        std::string temp = TempFile(folder).GetString();
        gameapi::MoveFileA(InFolder(folder, file.GetString()), temp.c_str());
    }
    {
        LocalStreams streams;
        streams.file.Open(InFolder(folder, file.GetString()), netlink::FileStream::kOpenWrite, 0);
        Party()->SaveParty(&streams.data, 1);
        SaveFileWrite(&streams.file, kPartyFileMagic, kSaveVersion, &streams.data);
    }
    gameapi::DeleteFileA(TempFile(folder));
}
EI_REWRITTEN(0x00662AC0, MpHeroRecord::WriteFile);

bool MpHeroRecord::ReadFile(const char* folder) {
    LocalStreams streams;
    streams.file.Open(InFolder(folder, file.GetString()), netlink::FileStream::kOpenRead, 1);
    if (!SaveFileRead(&streams.data, kPartyFileMagic, kSaveVersion, kSaveOldestVersion, &streams.file)) return false;
    Party()->LoadParty(&streams.data, 1);
    return streams.data.ok_ != 0;
}
EI_REWRITTEN(0x00662DD0, MpHeroRecord::ReadFile);

void SavedParties::SaveAll() {
    if (gamecrt::mbscmp(folder_, game::EmptyText()) == 0)
        return;
    ClearSaveFolder(folder_);
    // The count is read again after each write.
    for (unsigned i = 0; i < static_cast<unsigned>(entries_.size()); ++i) entries_[i].WriteFile(folder_);
}
EI_REWRITTEN(0x006632B0, SavedParties::SaveAll);

SavedParties* SavedParties::Construct() {
    std::construct_at(&folder_);
    entries_.ConstructEmpty();
    outsideGame_.Construct();
    return this;
}
EI_REWRITTEN(0x0065F580, SavedParties::Construct);

void SavedParties::Destruct() {
    outsideGame_.Destruct();
    for (MpHeroRecord& entry : entries_) entry.Destruct();
    entries_.FreeStorage();
    std::destroy_at(&folder_);
}
EI_REWRITTEN(0x0065F2D0, SavedParties::Destruct);

// The saved parties, empty at the start: built by a static initialiser in the list's order and destroyed by
// the exit function it registers (docs/notes/standalone.md, "The static initialisers' objects").
constinit lib::StaticObject<SavedParties> g_savedParties = /*007C82A0*/ {};

namespace {
void __cdecl ConstructSavedParties() {
    g_savedParties->Construct();
    libcrt::AtExit(ei::Code<void(__cdecl*)()>(0x0065F2C0));
}
void __cdecl DestroySavedParties() { g_savedParties->Destruct(); }
} // namespace

EI_REPLACE(0x0065F2A0, ConstructSavedParties);
EI_REPLACE(0x0065F2C0, DestroySavedParties);
