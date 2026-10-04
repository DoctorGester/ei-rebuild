// Checks for the objectives window (ui_objectives.cpp). Both functions build parts of the
// window (sprites, regions, textures, child windows), so ours run alone and the wrapper checks
// the structure they leave: the 21 fixed sprites and 9 fixed regions, one marker sprite and one
// region per exit that leads somewhere (the exit list matching the zone's exits), then one sprite
// and one region per place of the selected quest. The quest places come from the original's
// ShowQuest (0064DC60, through RefreshQuests), which may also return before adding them (no such quest), leaving the
// list from earlier: so the extra count is either 0 or the list's length.
#include "verify.h"

#include "../../src/game/object_tables.h"
#include "../../src/game/ui.h"

#include <cstring>

namespace {
void* s_unused;

// The exits of the zone `name` that lead somewhere (target not "none"), or -1 without the zone.
int ExpectedExits(const char* name) {
    auto* map = reinterpret_cast<CMapDatabase*>(0x0079BF00)->FindMap(name);
    if (!map) return -1;
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(map);
    const std::uint8_t* exits = *reinterpret_cast<const std::uint8_t* const*>(bytes + 0x4C);
    int count = *reinterpret_cast<const int*>(bytes + 0x50);
    int shown = 0;
    for (int i = 0; i < count; ++i) {
        const char* target = *reinterpret_cast<const char* const*>(exits + i * 0x3C + 0x28);
        if (_stricmp(target, "none") != 0) ++shown;
    }
    return shown;
}

int QuestPlaces(const CObjectivesWindow* w) { return static_cast<int>(w->questPlacesEnd_ - w->questPlacesBegin_); }

// Regions and sprites beyond the fixed ones and the exits' must be the quest places' (or none).
bool ExtrasMatch(const CObjectivesWindow* w, int exits) {
    int regions = w->regions_.size_ - 9 - exits;
    int sprites = w->sceneObjects_.size_ - 0x15 - exits;
    return regions == sprites && (regions == 0 || regions == QuestPlaces(w));
}

bool PageMatches(CObjectivesWindow* w, int expected) { return w->exitCount_ == expected && ExtrasMatch(w, expected); }
} // namespace

static void __fastcall ObjectivesBuildVerify(CObjectivesWindow* self, void*) {
    int texts = *reinterpret_cast<const int*>(self->objectiveTexts_ + 8);   // CStringArray size
    int regionsBefore = self->regions_.size_;
    int spritesBefore = self->sceneObjects_.size_;
    CInterface3D* ui = self->interface_;
    bool pendingBefore = ui->pendingFlag_;
    self->CObjectivesWindow::Build();
    bool closed = ui->pendingFlag_ && ui->pendingValue_ == 0;
    int textsAfter = *reinterpret_cast<const int*>(self->objectiveTexts_ + 8);
    const char* failed = "";
    int expected = -2;
    if (textsAfter != texts + 3) {
        failed = "objective texts";
    } else if (!self->bar_) {
        failed = "scroll bar";
    } else if (!closed) {
        expected = ExpectedExits(self->shownZone_);
        if (expected < 0)
            failed = "zone not in the map database";
        else if (self->exitCount_ != expected)
            failed = "exit count";
        else if (!ExtrasMatch(self, expected))
            failed = "regions/sprites";
    }
    ei::verify::Check("CObjectivesWindow::Build", true, *failed == 0,
                      "%s: zone %s list %d texts %d->%d exits %d/%d quest places %d regions %d->%d sprites %d->%d "
                      "pending %d->%d (value %X)",
                      failed, self->shownZone_ ? self->shownZone_ : "?", self->listZones_, texts, textsAfter, expected,
                      self->exitCount_, QuestPlaces(self), regionsBefore, self->regions_.size_, spritesBefore,
                      self->sceneObjects_.size_, pendingBefore, ui->pendingFlag_, ui->pendingValue_);
}
EI_VERIFY(0x0064BE80, ObjectivesBuildVerify, 0, s_unused);

static void __fastcall ObjectivesShowZoneVerify(CObjectivesWindow* self, void*, const char* zone) {
    char name[128];   // `zone` may be the window's own string, which the call replaces
    strncpy_s(name, zone, _TRUNCATE);
    int expected = ExpectedExits(name);
    self->CObjectivesWindow::ShowZone(zone);
    CInterface3D* ui = self->interface_;
    bool ok = expected < 0 ? ui->pendingFlag_ && ui->pendingValue_ == 0
                           : PageMatches(self, expected) && _stricmp(self->shownZone_, name) == 0;
    ei::verify::Check("CObjectivesWindow::ShowZone", true, ok, "exits %d/%d quest places %d regions %d sprites %d",
                      expected, self->exitCount_, QuestPlaces(self), self->regions_.size_, self->sceneObjects_.size_);
}
EI_VERIFY(0x0064CC10, ObjectivesShowZoneVerify, 0, s_unused);

// 0064DC60 fills the quest log and adds sprites: ours only. Afterwards the shown quest is the
// selected one, and the extra sprites and regions are the quest places' (or none, when it returned
// before them).
static void __fastcall ObjectivesShowQuestVerify(CObjectivesWindow* self, void*) {
    int shownBefore = self->shownQuest_;
    self->CObjectivesWindow::ShowQuest();
    bool ok = self->shownQuest_ == self->selectedQuest_ && ExtrasMatch(self, self->exitCount_);
    ei::verify::Check("CObjectivesWindow::ShowQuest", true, ok, "quest %d->%d (selected %d) places %d regions %d sprites %d",
                      shownBefore, self->shownQuest_, self->selectedQuest_, QuestPlaces(self), self->regions_.size_,
                      self->sceneObjects_.size_);
}
EI_VERIFY(0x0064DC60, ObjectivesShowQuestVerify, 0, s_unused);
