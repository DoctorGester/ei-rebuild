// Checks for the objectives window (ui_objectives.cpp). Both functions build parts of the
// window (sprites, regions, textures, child windows), so ours run alone and the wrapper checks
// the structure they leave: the 21 fixed sprites and 9 fixed regions plus one marker sprite and
// one region per exit that leads somewhere, and the exit list matching the zone's exits.
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

bool PageMatches(CObjectivesWindow* w, int expected) {
    return w->exitCount_ == expected && w->regions_.size_ == 9 + expected &&
           w->sceneObjects_.size_ == 0x15 + expected;
}
} // namespace

static void __fastcall ObjectivesBuildVerify(CObjectivesWindow* self, void*) {
    int texts = *reinterpret_cast<const int*>(self->objectiveTexts_ + 8);   // CStringArray size
    self->CObjectivesWindow::Build();
    CInterface3D* ui = self->interface_;
    bool closed = ui->pendingFlag_ && ui->pendingValue_ == 0;
    bool ok = *reinterpret_cast<const int*>(self->objectiveTexts_ + 8) == texts + 3 && self->bar_ != nullptr;
    int expected = -2;
    if (ok && !closed) {
        expected = ExpectedExits(self->shownZone_);
        ok = expected >= 0 && PageMatches(self, expected);
    }
    ei::verify::Check("CObjectivesWindow::Build", true, ok, "zone %s exits %d/%d regions %d sprites %d",
                      self->shownZone_ ? self->shownZone_ : "?", expected, self->exitCount_, self->regions_.size_,
                      self->sceneObjects_.size_);
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
    ei::verify::Check("CObjectivesWindow::ShowZone", true, ok, "exits %d/%d regions %d sprites %d", expected,
                      self->exitCount_, self->regions_.size_, self->sceneObjects_.size_);
}
EI_VERIFY(0x0064CC10, ObjectivesShowZoneVerify, 0, s_unused);
