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
#include "../../src/game/world.h"

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
    int regions = w->regions_.size - 9 - exits;
    int sprites = w->sceneObjects_.size - 0x15 - exits;
    return regions == sprites && (regions == 0 || regions == QuestPlaces(w));
}

bool PageMatches(CObjectivesWindow* w, int expected) { return w->exitCount_ == expected && ExtrasMatch(w, expected); }
} // namespace

static void __fastcall ObjectivesBuildVerify(CObjectivesWindow* self, void*) {
    int texts = *reinterpret_cast<const int*>(self->objectiveTexts_ + 8);   // CStringArray size
    int regionsBefore = self->regions_.size;
    int spritesBefore = self->sceneObjects_.size;
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
                      self->exitCount_, QuestPlaces(self), regionsBefore, self->regions_.size, spritesBefore,
                      self->sceneObjects_.size, pendingBefore, ui->pendingFlag_, ui->pendingValue_);
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
                      expected, self->exitCount_, QuestPlaces(self), self->regions_.size, self->sceneObjects_.size);
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
                      shownBefore, self->shownQuest_, self->selectedQuest_, QuestPlaces(self), self->regions_.size,
                      self->sceneObjects_.size);
}
EI_VERIFY(0x0064DC60, ObjectivesShowQuestVerify, 0, s_unused);

// 0064D9B0 draws into the window's surface: ours only. Afterwards row i's mark (object 16 + i) is
// shown exactly when the row exists and its "q.<zone>.<quest>" state is 2 or 3, and the cursor
// (object 2) exactly when the selected quest is one of the rows.
static void __fastcall ObjectivesDrawQuestListVerify(CObjectivesWindow* self, void*) {
    self->DrawQuestList();
    auto* quests = reinterpret_cast<const mfc::CString*>(*reinterpret_cast<char* const*>(self->quests_ + 4));
    int count = *reinterpret_cast<const int*>(self->quests_ + 8);
    bool ok = true;
    bool cursor = false;
    for (int i = 0; i < 5; ++i) {
        int row = self->firstRow_ + i;
        bool shown = false;
        if (row < count) {
            mfc::CString variable;
            variable.Format("q.%s.%s", self->shownZone_, static_cast<const char*>(quests[row]));
            int state = static_cast<int>(LocalPlayerVariable(variable));
            shown = state == 2 || state == 3;
            cursor = cursor || row == self->selectedQuest_;
        }
        ok = ok && (reinterpret_cast<CUiMesh*>(self->sceneObjects_.data[16 + i])->visible_ != 0) == shown;
    }
    ok = ok && (reinterpret_cast<CUiMesh*>(self->sceneObjects_.data[2])->visible_ != 0) == cursor;
    ei::verify::Check("CObjectivesWindow::DrawQuestList", true, ok, "%d quests from %d, selected %d", count,
                      self->firstRow_, self->selectedQuest_);
}
EI_VERIFY(0x0064D9B0, ObjectivesDrawQuestListVerify, 0, s_unused);

// --- A-H24: SelectExit, RefreshQuests, Update (ours only: they move sprites and the bar) ------------

namespace {
CUiMesh* SpriteOf(const CObjectivesWindow* w, int index) { return reinterpret_cast<CUiMesh*>(w->sceneObjects_.data[index]); }
} // namespace

// 0064E990 (ShowZone and an exit click): the nearest exit and the markers' frames; sprites 4 / 5 hidden.
static void __fastcall ObjectivesSelectExitVerify(CObjectivesWindow* self, void*, int index) {
    self->SelectExit(index);
    bool ok = self->nearestExit_ == index && SpriteOf(self, 4)->visible_ == 0 && SpriteOf(self, 5)->visible_ == 0;
    for (int i = 0; ok && i < self->exitCount_; ++i) {
        float u = SpriteOf(self, 21 + i)->uOffset_;
        ok = i == index || self->exits_[i].flag_ ? u == 0.0f : u == -0.125f;
    }
    if (ok && index >= 0 && index < self->exitCount_) ok = self->blinkTime_ == 0.0f;
    ei::verify::Check("CObjectivesWindow::SelectExit", true, ok, "exit %d of %d", index, self->exitCount_);
}
EI_VERIFY(0x0064E990, ObjectivesSelectExitVerify, 0, s_unused);

// 0064E8B0 (the quest keys): with quests the selection within the list and the first shown row
// within five above it.
static void __fastcall ObjectivesRefreshQuestsVerify(CObjectivesWindow* self, void*) {
    self->RefreshQuests();
    int count = *reinterpret_cast<const int*>(self->quests_ + 8);
    bool ok = count == 0 || (self->selectedQuest_ >= 0 && self->selectedQuest_ < count &&
                             self->firstRow_ <= self->selectedQuest_ && self->selectedQuest_ < self->firstRow_ + 5);
    ei::verify::Check("CObjectivesWindow::RefreshQuests", true, ok, "%d quests, selected %d from %d", count,
                      self->selectedQuest_, self->firstRow_);
}
EI_VERIFY(0x0064E8B0, ObjectivesRefreshQuestsVerify, 0, s_unused);

// 0064E760 (slot 24, every frame while the window is open): every marker and quest place drawn in
// its plain colour, the nearest exit's white, the other exits' grey, the places' red.
static void __fastcall ObjectivesUpdateVerify(CObjectivesWindow* self, void*) {
    self->Update();
    bool ok = true;
    for (int i = 21; ok && i < self->sceneObjects_.size; ++i) {
        const CUiMesh* sprite = SpriteOf(self, i);
        std::uint32_t rgb = sprite->plainColour_ & 0xFFFFFF;
        std::uint32_t expected = i >= 21 + self->exitCount_ ? 0xFF0000u : i - 21 == self->nearestExit_ ? 0xFFFFFFu : 0xC8C8C8u;
        ok = (sprite->flags_ & 0x100) != 0 && rgb == expected;
    }
    ei::verify::Check("CObjectivesWindow::Update", true, ok, "%d sprites, %d exits", self->sceneObjects_.size,
                      self->exitCount_);
}
EI_VERIFY(0x0064E760, ObjectivesUpdateVerify, 0, s_unused);

// --- 0064BE40 / 0064D990 (A-H25) -------------------------------------------------------------------
// Setup (every opening of the window: TAB in a zone, the clock's button, the briefing keys, a zone
// exit) only stores its arguments: the original, then ours from the same members, compared (the
// zone by its text). HelpTopic (F1 over the window) is pure.

// 0064BE40: MOV EAX,[ESP+4]; PUSH ESI (5 bytes).
static void(__fastcall* s_objectivesSetup)(CObjectivesWindow*, void*, const char*, void*, int, int);
static void __fastcall ObjectivesSetupVerify(CObjectivesWindow* self, void*, const char* zone, void* exitDistances,
                                             int twoButtons, int listZones) {
    mfc::CString& name = *reinterpret_cast<mfc::CString*>(&self->zoneName_);
    const mfc::CString nameBefore = name;
    const int twoBefore = self->twoButtons_;
    void* const distancesBefore = self->exitDistances_;
    const int listBefore = self->listZones_;
    s_objectivesSetup(self, nullptr, zone, exitDistances, twoButtons, listZones);
    const mfc::CString expectedName = name;
    const int expectedTwo = self->twoButtons_;
    void* const expectedDistances = self->exitDistances_;
    const int expectedList = self->listZones_;
    name = nameBefore;
    self->twoButtons_ = twoBefore;
    self->exitDistances_ = distancesBefore;
    self->listZones_ = listBefore;
    self->CObjectivesWindow::Setup(zone, exitDistances, twoButtons, listZones);
    const bool ok = std::strcmp(expectedName, name) == 0 && expectedTwo == self->twoButtons_ &&
                    expectedDistances == self->exitDistances_ && expectedList == self->listZones_;
    ei::verify::Check("CObjectivesWindow::Setup", true, ok, "'%s' two %d list %d", zone, twoButtons, listZones);
}
EI_VERIFY(0x0064BE40, ObjectivesSetupVerify, 5, s_objectivesSetup);

// 0064D990: MOV EAX,[ECX+0x174] (6 bytes).
static const char*(__fastcall* s_objectivesHelpTopic)(const CObjectivesWindow*);
static const char* __fastcall ObjectivesHelpTopicVerify(const CObjectivesWindow* self) {
    const char* expected = s_objectivesHelpTopic(self);
    const char* actual = self->CObjectivesWindow::HelpTopic();
    ei::verify::Check("CObjectivesWindow::HelpTopic", true, std::strcmp(expected, actual) == 0, "'%s' / '%s'", expected,
                      actual);
    return expected;
}
EI_VERIFY(0x0064D990, ObjectivesHelpTopicVerify, 6, s_objectivesHelpTopic);
