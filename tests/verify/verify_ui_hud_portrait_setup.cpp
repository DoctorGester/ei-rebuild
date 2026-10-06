// Checks for the portraits' setup (ui_hud_portrait_setup.cpp). All three are stateful (sprites,
// regions, textures and slots are created; a picture is written into a texture), so only ours runs
// and the result is checked:
// - Rebuild (006407E0): one portrait per party unit plus the multiplayer players, five sprites each
//   after the marker, one region each; every party slot holds its unit's hit points, build and
//   player -1. Reached whenever the battle screen sets up its party (every new game, newgame).
// - AddPortrait (00640060, from Rebuild): one more slot, five more sprites, one more region of the
//   portrait's rectangle (y 510..600), the slot's player flag and its orientations.
// - ShowFace (00640670, from the per-frame update when the face picture changes: a hit, low mana;
//   spells_cast): no slot and no referenced object changes.
#include "verify.h"

#include "../../src/game/ui.h"
#include "../../src/game/unit.h"

#include <cstring>
#include <vector>

namespace {
void* s_unused;

int SlotCount(const CHudPortraits* p) { return static_cast<int>(p->slotsEnd_ - p->slots_); }
} // namespace

static void __fastcall PortraitsRebuildVerify(CHudPortraits* self, void*, int wide) {
    self->CHudPortraits::Rebuild(wide);
    const int party = self->owner_->partyCount_;
    bool ok = self->count_ >= party && SlotCount(self) == self->count_ && self->regions_.size == self->count_ &&
              self->sceneObjects_.size == 1 + 5 * self->count_ && self->objects_.size >= 1 + (self->count_ + 3) / 4;
    int bad = -1;
    for (int i = 0; ok && i < party; ++i) {
        const HudPortraitSlot& slot = self->slots_[i];
        const CUnit* unit = self->owner_->party_[i];
        if (slot.lastHitPoints_ != unit->hitPoints || slot.playerId_ != -1 || slot.hasPlayer_ ||
            std::memcmp(&slot.build_, &unit->build, 4) != 0 || slot.faceState_ != 0 || slot.wobbleKind_ != 0) {
            ok = false;
            bad = i;
        }
    }
    ei::verify::Check("CHudPortraits::Rebuild", true, ok, "party %d count %d slots %d regions %d sprites %d slot %d",
                      party, self->count_, SlotCount(self), self->regions_.size, self->sceneObjects_.size, bad);
}
EI_VERIFY(0x006407E0, PortraitsRebuildVerify, 0, s_unused);

static void __fastcall PortraitsAddVerify(CHudPortraits* self, void*, const char* model, const char* picture, int wide,
                                          int hasPlayer) {
    const int count = self->count_;
    const int sprites = self->sceneObjects_.size;
    const int regions = self->regions_.size;
    self->CHudPortraits::AddPortrait(model, picture, wide, hasPlayer);
    bool ok = self->count_ == count + 1 && SlotCount(self) == count + 1 && self->sceneObjects_.size == sprites + 5 &&
              self->regions_.size == regions + 1;
    if (ok) {
        const HudPortraitSlot& slot = self->slots_[count];
        const CUiRegion* region = self->regions_.data[regions];
        ok = slot.hasPlayer_ == (static_cast<char>(hasPlayer) != 0) && slot.wobbleKind_ == 0 &&
             std::memcmp(&slot.current_, &slot.restA_, sizeof(Quaternion)) == 0 && region->bounds_.top == 510 &&
             region->bounds_.bottom == 600 && region->bounds_.right - region->bounds_.left >= 40;
    }
    ei::verify::Check("CHudPortraits::AddPortrait", true, ok, "%s %s count %d -> %d sprites %d -> %d", model, picture,
                      count, self->count_, sprites, self->sceneObjects_.size);
}
EI_VERIFY(0x00640060, PortraitsAddVerify, 0, s_unused);

static void __fastcall PortraitsShowFaceVerify(CHudPortraits* self, void*, int index, int state) {
    const int slots = SlotCount(self);
    std::vector<std::uint8_t> before(reinterpret_cast<std::uint8_t*>(self->slots_),
                                     reinterpret_cast<std::uint8_t*>(self->slotsEnd_));
    const int objects = self->objects_.size;
    self->CHudPortraits::ShowFace(index, state);
    const bool ok = SlotCount(self) == slots && self->objects_.size == objects &&
                    std::memcmp(before.data(), self->slots_, before.size()) == 0;
    ei::verify::Check("CHudPortraits::ShowFace", true, ok, "portrait %d state %d", index, state);
}
EI_VERIFY(0x00640670, PortraitsShowFaceVerify, 0, s_unused);
