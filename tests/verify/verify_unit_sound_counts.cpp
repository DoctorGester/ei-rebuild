// Checks for unit_sound_counts.cpp (A-U4).
// - CountSoundFiles (0053A8E0): ours runs, every model's counts are copied, then the original runs
//   again on the same database (the names are already lower-case and the count vectors already
//   sized, so it only recounts); the counts are compared. Differential.
// Reached by: the client world's creation (00563710) when a game starts (every scenario that
// starts a game), and the database rebuild 0057DEE0 (multiplayer hero screens).
#include "verify.h"

#include "../../src/game/unit_db.h"

#include <cstring>
#include <vector>

// 0053A8E0: PUSH -1; PUSH 0071F2A0 (7 bytes).
static void(__fastcall* s_countSoundFiles)(UnitDatabase*, void*, void*);
static void __fastcall CountSoundFilesVerify(UnitDatabase* self, void*, void* sounds) {
    self->CountSoundFiles(sounds);
    std::vector<std::uint32_t> ours;
    for (UnitModelRecord* m = self->models.begin; m != self->models.end; ++m)
        ours.insert(ours.end(), m->soundCounts, m->soundCountsEnd);
    s_countSoundFiles(self, nullptr, sounds);
    std::vector<std::uint32_t> original;
    for (UnitModelRecord* m = self->models.begin; m != self->models.end; ++m)
        original.insert(original.end(), m->soundCounts, m->soundCountsEnd);
    std::size_t first = 0;
    while (first < ours.size() && first < original.size() && ours[first] == original[first]) ++first;
    ei::verify::Check("UnitDatabase::CountSoundFiles", original.size(), ours.size(), "first difference at %u",
                      static_cast<unsigned>(first));
    ei::verify::Check("UnitDatabase::CountSoundFiles", true, ours == original, "first difference at %u",
                      static_cast<unsigned>(first));
}
EI_VERIFY(0x0053A8E0, CountSoundFilesVerify, 7, s_countSoundFiles);
