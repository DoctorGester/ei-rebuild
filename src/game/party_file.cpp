// The multiplayer heroes' files (<folder>\<n>.mp): one party per file, saved (with its variables)
// into a memory stream and written as a save file (magic 0x114). See party.h, ui_screens.h
// (MpHeroRecord) and docs/notes/items_spells.md ("Party records and the party object").
#include "party.h"
#include "game_strings.h"

#include "core_streams.h"
#include "net_link.h"
#include "ui_screens.h"
#include "world.h"

#include "lib/gameapi.h"
#include <memory>
#include <string>
#include "lib/text_case.h"

#include "lib/str.h"

#include <cstdlib>
#include <cstring>

namespace {

constexpr unsigned kPartyFileMagic = 0x114;

const char* TempFileName() { return "temp.mp"; }

std::string InFolder(const std::string& folder, const std::string& name) { return folder + name; }
std::string TempFile(const std::string& folder) { return folder + TempFileName(); }

// The file stream and the memory stream the originals build on the stack (the memory stream torn down first).
struct LocalStreams {
    netlink::FileStream file;
    CMemoryStream data;
};

// Whether a file name is "<digits>.<anything>": a '.' and only digits before the last one.
bool Numbered(const std::string& name) {
    int dot = str::ReverseFind(name, '.');
    if (dot == -1) return false;
    for (int i = 0; i < dot; ++i)
        if (!text::IsDigit(static_cast<signed char>(name[i]))) return false;
    return true;
}

void DestroySavedParties() { g_savedParties.Destroy(); }

} // namespace

void MpHeroRecord::WriteFile(const std::string& folder) {
    if (!Numbered(file)) {
        gameapi::DeleteFileA(InFolder(folder, file).c_str());
        return;
    }
    {
        std::string temp = TempFile(folder);
        gameapi::MoveFileA(InFolder(folder, file).c_str(), temp.c_str());
    }
    {
        LocalStreams streams;
        streams.file.Open(InFolder(folder, file), netlink::FileStream::kOpenWrite, 0);
        Party()->SaveParty(&streams.data, 1);
        SaveFileWrite(&streams.file, kPartyFileMagic, kSaveVersion, &streams.data);
    }
    gameapi::DeleteFileA(TempFile(folder).c_str());
}

bool MpHeroRecord::ReadFile(const std::string& folder) {
    LocalStreams streams;
    streams.file.Open(InFolder(folder, file), netlink::FileStream::kOpenRead, 1);
    if (!SaveFileRead(&streams.data, kPartyFileMagic, kSaveVersion, kSaveOldestVersion, &streams.file)) return false;
    Party()->LoadParty(&streams.data, 1);
    return streams.data.ok_ != 0;
}

void SavedParties::SaveAll() {
    if (str::Compare(folder_, game::kEmptyText) == 0)
        return;
    ClearSaveFolder(folder_);
    // The count is read again after each write.
    for (unsigned i = 0; i < static_cast<unsigned>(entries_.size()); ++i) entries_[i].WriteFile(folder_);
}

void SavedParties::ReserveOne() {
    if (entries_.size() == entries_.capacity()) entries_.reserve(entries_.empty() ? 1 : 2 * entries_.size());
}

// The saved parties, empty at the start: built by a static initialiser in the list's order and destroyed by
// the exit function it registers (docs/notes/standalone.md, "The static initialisers' objects").
constinit lib::StaticObject<SavedParties> g_savedParties = /*007C82A0*/ {};

void ConstructSavedParties() {
    g_savedParties.Construct();
    std::atexit(DestroySavedParties);
}
