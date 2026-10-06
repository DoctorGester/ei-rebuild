// Checks for the briefing / base screen's dialogue people and items (src/game/ui_briefing_people.cpp).
//
// Both are ours only (server orders, sector map moves, sprites and textures). MovePerson: placed at
// once, no new mover; otherwise at most one new mover, first, with the unit, walking, `waited`,
// time 0 and the target's x and y. ShowItem: a person ends up the last speaker, once; an item name
// is shown at most once, and when it was added it is first with its mesh. Scenario
// ui_base_dialogue (the dialogue's people walk to their places; "#show" lines of Smith's topics).
#include "verify.h"

#include "../../src/game/object_tables.h"
#include "../../src/game/originals_ui.h"
#include "../../src/game/ui_briefing.h"

#include <cstring>

namespace {
void* s_unused;

int CountMovers(const CBriefingScreen* screen) {
    int n = 0;
    for (const BriefingMover* node = screen->movers_->next; node != screen->movers_; node = node->next) ++n;
    return n;
}

int CountShown(const CBriefingScreen* screen, const char* name) {
    int n = 0;
    for (const BriefingShownItem* node = screen->shown_->next; node != screen->shown_; node = node->next)
        if (_stricmp(node->name, name) == 0) ++n;
    return n;
}
} // namespace

static void __fastcall MovePersonVerify(CBriefingScreen* self, void*, CUnit* unit, float x, float y, float z, float dx,
                                        float dy, float dz, bool waited, bool immediate, int run) {
    const char* name = "CBriefingScreen::MovePerson";
    int before = CountMovers(self);
    self->MovePerson(unit, x, y, z, dx, dy, dz, waited, immediate, run);
    int after = CountMovers(self);
    ei::verify::Check(name, true, after == before || (!immediate && after == before + 1), "movers %d -> %d, immediate %d",
                      before, after, immediate);
    if (after != before + 1) return;
    const BriefingMover* node = self->movers_->next;
    bool ok = node->person == reinterpret_cast<CEngineObject*>(unit) && node->state == BriefingMover::kWalking &&
              node->waited == waited && node->time == 0.0f;
    ei::verify::Check(name, true, ok, "new mover state %d", node->state);
    ei::verify::Check(name, x, node->target[0], "target x");
    ei::verify::Check(name, y, node->target[1], "target y");
}
EI_VERIFY(0x006089D0, MovePersonVerify, 0, s_unused);

static void __fastcall ShowItemVerify(CBriefingScreen* self, void*, const char* item, int index, int unused) {
    const char* name = "CBriefingScreen::ShowItem";
    void* unit = GameUnitByIdOriginal(reinterpret_cast<void*>(0x007AFEA5), nullptr, TextKeyFromName(item));
    const BriefingShownItem* first = self->shown_->next;
    self->ShowItem(item, index, unused);
    if (unit) {
        int count = 0;
        for (const BriefingSpeaker* p = self->speakers_; p != self->speakersEnd_; ++p)
            if (p->unit == unit) ++count;
        bool last = self->speakersEnd_ != self->speakers_ && self->speakersEnd_[-1].unit == unit;
        ei::verify::Check(name, true, count == 1 && last, "person %s: %d entries", item, count);
        return;
    }
    ei::verify::Check(name, true, CountShown(self, item) <= 1, "item %s shown more than once", item);
    const BriefingShownItem* now = self->shown_->next;
    if (now != first && now != self->shown_)
        ei::verify::Check(name, true, _stricmp(now->name, item) == 0 && now->item != nullptr, "item %s", item);
}
EI_VERIFY(0x00605DE0, ShowItemVerify, 0, s_unused);
