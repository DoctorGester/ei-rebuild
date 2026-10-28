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
#include "lib/mfc.h"
#include "lib/text_case.h"

#include "game_data.h"
#include "lib/str.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

namespace {

constexpr unsigned kPartyFileMagic = 0x114;

const char* TempFileName() { return "temp.mp"; }

std::string InFolder(const char* folder, const std::string& name) { return std::string(folder) + name; }
std::string TempFile(const char* folder) { return std::string(folder) + TempFileName(); }

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
        if (!text::IsDigit(static_cast<signed char>(text[i]))) return false;
    return true;
}

} // namespace

void MpHeroRecord::WriteFile(const char* folder) {
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

bool MpHeroRecord::ReadFile(const char* folder) {
    LocalStreams streams;
    streams.file.Open(InFolder(folder, file), netlink::FileStream::kOpenRead, 1);
    if (!SaveFileRead(&streams.data, kPartyFileMagic, kSaveVersion, kSaveOldestVersion, &streams.file)) return false;
    Party()->LoadParty(&streams.data, 1);
    return streams.data.ok_ != 0;
}

void SavedParties::SaveAll() {
    if (str::Compare(folder_, game::EmptyText()) == 0)
        return;
    ClearSaveFolder(folder_.c_str());
    // The count is read again after each write.
    for (unsigned i = 0; i < static_cast<unsigned>(entries_.size()); ++i) entries_[i].WriteFile(folder_.c_str());
}

SavedParties* SavedParties::Construct() {
    std::construct_at(&folder_);
    std::construct_at(&entries_);
    std::construct_at(&outsideGame_);
    return this;
}

void SavedParties::ReserveOne() {
    if (entries_.size() == entries_.capacity()) entries_.reserve(entries_.empty() ? 1 : 2 * entries_.size());
}

void SavedParties::Destruct() {
    std::destroy_at(&outsideGame_);
    std::destroy_at(&entries_);
    std::destroy_at(&folder_);
}

// The saved parties, empty at the start: built by a static initialiser in the list's order and destroyed by
// the exit function it registers (docs/notes/standalone.md, "The static initialisers' objects").
constinit lib::StaticObject<SavedParties> g_savedParties = /*007C82A0*/ {};

namespace {
void DestroySavedParties() { g_savedParties->Destruct(); }
} // namespace

void ConstructSavedParties() {
    g_savedParties->Construct();
    std::atexit(DestroySavedParties);
}
