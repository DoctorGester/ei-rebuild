// The multiplayer heroes' files (<folder>\<n>.mp): one party per file, saved (with its variables)
// into a memory stream and written as a save file (magic 0x114). See party.h, ui_screens.h
// (MpHeroRecord) and docs/notes/items_spells.md ("Party records and the party object").
#include "party.h"
#include "game_strings.h"

#include "core_streams.h"
#include "net_link.h"
#include "originals_gameplay.h"
#include "ui_screens.h"
#include "world.h"

#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"

#include "ei/hook.h"
#include "game_data.h"

#include <cstring>

namespace {

constexpr unsigned kPartyFileMagic = 0x114;

int SaveVersion() { return ei::GameData<const int>(0x00790260); }
int NewestReadVersion() { return ei::GameData<const int>(0x00790268); }
const char* TempFileName() { return "temp.mp"; }

mfc::CString InFolder(const char* folder, const mfc::CString& name) { return mfc::CString(folder) + name; }
mfc::CString TempFile(const char* folder) { return mfc::CString(folder) + TempFileName(); }

// The file stream and the memory stream the originals build on the stack (inlined constructors
// and destructors: the memory stream torn down first).
struct LocalStreams {
    netlink::FileStream file;
    CMemoryStream data;
    LocalStreams() {
        std::memset(static_cast<void*>(&file), 0, sizeof(file));
        std::memset(static_cast<void*>(&data), 0, sizeof(data));
        file.Construct();
        data.Construct();
    }
    ~LocalStreams() {
        data.Destruct();
        file.Destruct();
    }
    LocalStreams(const LocalStreams&) = delete;
    LocalStreams& operator=(const LocalStreams&) = delete;
};

// Whether a file name is "<digits>.<anything>": a '.' and only digits before the last one.
bool Numbered(const mfc::CString& name) {
    int dot = name.ReverseFind('.');
    if (dot == -1) return false;
    const char* text = name;
    for (int i = 0; i < dot; ++i)
        if (!gamecrt::isdigit(static_cast<signed char>(text[i]))) return false;
    return true;
}

} // namespace

void MpHeroRecord::WriteFile(const char* folder) {
    if (!Numbered(file)) {
        gameapi::DeleteFileA(InFolder(folder, file));
        return;
    }
    {
        mfc::CString temp = TempFile(folder);
        gameapi::MoveFileA(InFolder(folder, file), temp);
    }
    {
        LocalStreams streams;
        streams.file.Open(InFolder(folder, file), netlink::FileStream::kOpenWrite, 0);
        Party()->SaveParty(&streams.data, 1);
        SaveFileWrite(&streams.file, kPartyFileMagic, SaveVersion(), &streams.data);
    }
    gameapi::DeleteFileA(TempFile(folder));
}
EI_REPLACE_METHOD(0x00662AC0, MpHeroRecord::WriteFile);

bool MpHeroRecord::ReadFile(const char* folder) {
    LocalStreams streams;
    streams.file.Open(InFolder(folder, file), netlink::FileStream::kOpenRead, 1);
    if (!SaveFileRead(&streams.data, kPartyFileMagic, SaveVersion(), NewestReadVersion(), &streams.file)) return false;
    Party()->LoadParty(&streams.data, 1);
    return streams.data.ok_ != 0;
}
EI_REPLACE_METHOD(0x00662DD0, MpHeroRecord::ReadFile);

void SavedParties::SaveAll() {
    if (gamecrt::mbscmp(folder_, game::EmptyText()) == 0)
        return;
    ClearSaveFolder(folder_);
    // The entries are MpHeroRecords (world_game.cpp declares the same 0x74-byte records as
    // SavedPartyEntry); the count is read again after each write.
    auto* begin = reinterpret_cast<MpHeroRecord*>(entries_);
    for (unsigned i = 0; i < static_cast<unsigned>(reinterpret_cast<MpHeroRecord*>(finish_) - begin); ++i)
        begin[i].WriteFile(folder_);
}

SavedParties* SavedParties::Construct() {
    new (&folder_) mfc::CString();
    entries_ = nullptr;
    finish_ = nullptr;
    eos_ = nullptr;
    reinterpret_cast<MpHeroRecord*>(outsideGame_)->Construct();
    return this;
}
EI_REPLACE_METHOD(0x0065F580, SavedParties::Construct);

void SavedParties::Destruct() {
    reinterpret_cast<MpHeroRecord*>(outsideGame_)->Destruct();
    auto* begin = reinterpret_cast<MpHeroRecord*>(entries_);
    auto* end = reinterpret_cast<MpHeroRecord*>(finish_);
    for (MpHeroRecord* entry = begin; entry != end; ++entry) entry->Destruct();
    if (begin != nullptr)
        stlp::deallocate(begin, static_cast<std::size_t>(reinterpret_cast<MpHeroRecord*>(eos_) - begin) * sizeof(MpHeroRecord));
    folder_.~CString();
}
EI_REPLACE_METHOD(0x0065F2D0, SavedParties::Destruct);
