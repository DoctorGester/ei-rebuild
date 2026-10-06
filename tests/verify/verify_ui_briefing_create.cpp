// Checks for the briefing / base screen's Create and Update (src/game/ui_briefing_create.cpp).
//
// Both are ours only: Create allocates the screen's sprites, surfaces, children and inputs and
// starts the music; Update moves people, talks to the server and may leave the zone. Create:
// four more scene objects and two more surfaces, all hidden; topicBar_ and text_ the next two
// children (when allocated); the state fields; the map edge fade 4 with the old one kept; people_
// only text keys. Update: the state stays 0..4, markPhase_ below 1; from states 0 and 1 (which do
// not add movers) no finished mover is left, and the meshes are hidden but for state 1's topic
// list. A frame that left the zone (the screen flag cleared) is not checked. Scenarios
// zone_base_bz1g and ui_base_dialogue reach both.
#include "verify.h"

#include "../../src/game/object_tables.h"
#include "../../src/game/ui_briefing.h"

namespace {
void* s_unused;

float EdgeFade() { return *reinterpret_cast<const float*>(0x0079BD34); }

bool MeshVisible(const CBriefingScreen* screen, int index) {
    return reinterpret_cast<const CUiMesh*>(screen->sceneObjects_.data[index])->visible_ != 0;
}
} // namespace

static void __fastcall CreateVerify(CBriefingScreen* self, void*) {
    const char* name = "CBriefingScreen::Create";
    int meshes = self->sceneObjects_.size;
    int surfaces = self->surfaces_.size;
    int children = self->children_.size;
    int people = static_cast<int>(self->people_.size());
    float fade = EdgeFade();
    self->Create();

    ei::verify::Check(name, meshes + 4, self->sceneObjects_.size, "scene objects");
    ei::verify::Check(name, surfaces + 2, self->surfaces_.size, "surfaces");
    for (int i = 0; i < self->sceneObjects_.size; ++i)
        ei::verify::Check(name, false, MeshVisible(self, i), "scene object %d shown", i);
    int child = children;
    if (self->topicBar_)
        ei::verify::Check(name, true, self->children_.data[child++] == self->topicBar_, "topic bar child");
    if (self->text_) ei::verify::Check(name, true, self->children_.data[child++] == self->text_, "text child");
    bool fields = self->state_ == 0 && self->exitPending_ && self->exit_ == 0 && !self->leader_ && !self->chatActive_ &&
                  !self->briefingStarted_ && self->voice_ == -1 && !self->speaking_ && self->markPhase_ == 0.0f;
    ei::verify::Check(name, true, fields, "state %d, voice %d", self->state_, self->voice_);
    ei::verify::Check(name, fade, self->savedEdgeFade_, "saved edge fade");
    ei::verify::Check(name, 4.0f, EdgeFade(), "edge fade");
    int added = static_cast<int>(self->people_.size()) - people;
    ei::verify::Check(name, true, added >= 0, "people %d -> %d", people, static_cast<int>(self->people_.size()));
    for (const int* id = self->people_.begin() + people; id < self->people_.end(); ++id)
        ei::verify::Check(name, TRUE, IsTextKey(*id), "person id %d", *id);
}
EI_VERIFY(0x006024B0, CreateVerify, 0, s_unused);

static void __fastcall UpdateVerify(CBriefingScreen* self, void*) {
    const char* name = "CBriefingScreen::Update";
    int before = self->state_;
    self->Update();
    if (!InterfaceHasScreen()) return;
    int state = self->state_;
    ei::verify::Check(name, true, state >= 0 && state <= 4, "state %d -> %d", before, state);
    ei::verify::Check(name, true, !(self->markPhase_ >= 1.0f), "phase %g", self->markPhase_);
    if (before != 0 && before != 1) return;
    for (BriefingMover* node = self->movers_->next; node != self->movers_; node = node->next)
        ei::verify::Check(name, true, node->state != BriefingMover::kDone, "finished mover left");
    if (state != before) return;
    for (int i = 0; i < self->sceneObjects_.size; ++i)
        ei::verify::Check(name, before == 1 && i < 2, MeshVisible(self, i), "state %d, scene object %d", before, i);
}
EI_VERIFY(0x00604130, UpdateVerify, 0, s_unused);
