#pragma once

// The saved multiplayer parties (saves\*.mp) and the hero kept outside a running game.

#include "game/players/party.h"

#include <string>
#include <vector>

// A multiplayer hero record (the saved parties' entries below, the hero screens' records): its party
// (Party) and file name.
struct MpHeroRecord {
    // A copy copies the party (Party's copy constructor and Assign, which read the source non-const).
    MpHeroRecord();
    MpHeroRecord(const MpHeroRecord& other);
    MpHeroRecord& operator=(const MpHeroRecord& other);
    ~MpHeroRecord();
    // Writes the party to `folder` + file: a file name with no '.' or with anything but digits before
    // the last one is deleted instead. Otherwise the old file is moved to `folder` + "temp.mp", the
    // party saved (with its variables) into a memory stream and that written as a save file (magic
    // 0x114, the save version), then the temporary file deleted.
    void WriteFile(const std::string& folder);
    // Reads the party from `folder` + file (magic 0x114, the versions kSaveOldestVersion..kSaveVersion):
    // false when the file does not read, else the party loaded (with its variables) and the memory
    // stream's ok flag.
    bool ReadFile(const std::string& folder);
    Party* GetParty() { return &party; }

    Party party;
    std::string file;
};

// The saved multiplayer parties.
struct SavedParties {
    // Rebuilds the list from the folder's "*.mp" files: each is read into party records; a loaded one
    // has its variables emptied and, with a record's field == 1, is appended. Rebirth's
    // PersistentGlobalVars keeps the "gvar." variables.
    void Reload();
    void SaveAll();
    void Select(const MpHeroRecord* hero);
    // Room for one more record the way the original's vector grew: doubled, the old records copied before the
    // new one is made. A record's copy loads its party again, which draws object ids and random numbers.
    void ReserveOne();
    int Count() const { return static_cast<int>(entries.size()); }
    MpHeroRecord* Hero(int index) { return &entries[index]; }

    // Ours. Whether `hero` is in the running game: the hero played (outsideGame's file) or a companion.
    bool InUse(const MpHeroRecord* hero) const;
    // The entry of saved hero file `file`, null for none.
    MpHeroRecord* Entry(const std::string& file);
    // The party's records after the first are companions: each goes back to the entry of its file (the first
    // companion file whose hero has the record's name and prototype; the entry's record becomes a copy) and that
    // file is written; then the party keeps its first record only. False (nothing written) for a party of one.
    bool ReturnCompanions(Party* party);
    // The party's record of companion file `file` goes back to that file as above (written); its unit id, -1 when the
    // party holds no record of it.
    int ReturnCompanion(const Party& party, const std::string& file);
    // Each companion's record (the files of `companions` in order) is appended to the party, a hero alone: a copy of
    // its file's, following the hero, its unit id the next after the hero's.
    void JoinCompanions(Party* party);

    std::string folder;
    std::vector<MpHeroRecord> entries;
    MpHeroRecord outsideGame;
    // Ours: the files of the heroes picked as companions in the multiplayer base (the companions window), in the order
    // picked: party members in the base (JoinCompanions), asked for in each network zone
    // (PlayerClient::RestoreCompanions); emptied when a multiplayer game begins.
    std::vector<std::string> companions;

private:
    // The entry of each of the party's records after the first (record i's at i - 1): the first companion file not
    // taken by an earlier record whose hero has the record's name and prototype, null for none.
    std::vector<MpHeroRecord*> CompanionEntries(const Party& party);
};
extern SavedParties g_savedParties;
// The party of the record kept outside a running game (the multiplayer hero).
inline Party* GlobalParty() { return g_savedParties.outsideGame.GetParty(); }

