// Checks for the vanilla bodies of the battle screen's four item panels (A-I22,
// src/game/ui_hud_build.cpp): the constructors 0063BAB0, 0063C800, 0063E2A0, 0063F300, the
// functions that add a slot's sprite 0063BCC0, 0063CAE0, 0063E4B0, 0063F500 and the weapon bar's
// highlight 0063C050. Rebirth patches all nine, so the installer hooks them in the vanilla launch
// only and these wrappers run there.
//
// 0063C050: side by side (it places one sprite and one region; both are put back between the
//   runs). Reached whenever a unit with weapons is shown or a weapon slot is picked: ui_hud,
//   ui_battle_commands.
// The constructors: ours only (they allocate the regions and the labels). Every battle screen
//   made: ui_hud and every scenario that enters a map.
// The Add functions: ours only (each adds a sprite and copies a picture into the panel's
//   texture). The panels' SetUnit calls them for the unit shown: ui_hud (weapons, actions),
//   ui_quick_items (items), ui_battle_spells (spells).
#include "verify.h"

#include "../../src/game/ui.h"
#include "../../src/lib/mfc.h"

#include <cstdint>
#include <cstring>

namespace {
void* s_unused;

constexpr std::uint32_t kPlainFlag = 0x100;
constexpr std::uint32_t kWhite = 0xFFFFFFFF;
constexpr std::uint32_t kColourOff = 0xFF000000;
constexpr float kTexel = 1.0f / 256.0f;

CUiMesh* Sprite(CUiWindow* window, int index) { return reinterpret_cast<CUiMesh*>(window->sceneObjects_.data[index]); }
int LabelCount(void** labels) { return reinterpret_cast<mfc::CStringArray*>(labels)->size; }
bool SameRect(const RECT& rect, int left, int top, int right, int bottom) {
    return rect.left == left && rect.top == top && rect.right == right && rect.bottom == bottom;
}

// What the weapon bar's highlight writes.
struct SlotState {
    Vector3 position;
    float scale[3];
    std::uint32_t colour;
    RECT bounds;
};
SlotState Capture(CUiMesh* sprite, CUiRegion* region) {
    return {sprite->position_, {sprite->ScaleX(), sprite->ScaleY(), sprite->ScaleZ()}, sprite->colour_, region->bounds_};
}
void Restore(CUiMesh* sprite, CUiRegion* region, const SlotState& state) {
    sprite->position_ = state.position;
    sprite->ScaleX() = state.scale[0];
    sprite->ScaleY() = state.scale[1];
    sprite->ScaleZ() = state.scale[2];
    sprite->colour_ = state.colour;
    region->bounds_ = state.bounds;
}

// The texture offsets the Add functions give the n-th 64 x 64 picture of a panel whose pictures
// start at column `firstColumn`.
void CheckIcon(const char* name, CUiMesh* sprite, int n, int firstColumn) {
    float u = static_cast<float>(firstColumn + (n / 4) * 0x40) * kTexel;
    float v = static_cast<float>((n % 4) * 0x40 + 0x40) * kTexel;
    ei::verify::CheckNear(name, u, sprite->uOffset_, "u of picture %d", n);
    ei::verify::CheckNear(name, v, sprite->vOffset_, "v of picture %d", n);
    ei::verify::Check(name, true, sprite->plainColour_ == kWhite && (sprite->flags_ & kPlainFlag) != 0, "plain, picture %d",
                      n);
}
} // namespace

// --- Weapon bar ------------------------------------------------------------------------------------------------

static CHudWeaponBar* __fastcall WeaponBarConstructVerify(CHudWeaponBar* self, void*, CInterface3D* ui,
                                                          CBattleScreen* owner) {
    CHudWeaponBar* result = self->Construct(ui, owner);
    bool ok = result == self && self->owner_ == owner && self->interface_ == ui && self->regions_.size == 4 &&
              self->sceneObjects_.size == 0 && self->count_ == 0 && self->selected_ == -1 && !self->unit_ &&
              LabelCount(&self->labelsVtable_) == 4 && SameRect(self->rect_, 80, 520, 240, 600);
    for (int i = 0; ok && i < 4; ++i) ok = self->regions_.data[i]->tip_ != nullptr;
    ei::verify::Check("CHudWeaponBar::Construct", true, ok, "regions %d sprites %d labels %d", self->regions_.size,
                      self->sceneObjects_.size, LabelCount(&self->labelsVtable_));
    return result;
}
EI_VERIFY(0x0063BAB0, WeaponBarConstructVerify, 0, s_unused);

// PUSH EBX; PUSH ESI; MOV ESI,[ESP+0xC] (6 bytes).
static void(__fastcall* s_weaponHighlight)(CHudWeaponBar*, void*, int, int);
static void __fastcall WeaponHighlightVerify(CHudWeaponBar* self, void*, int index, int on) {
    if (index < 0 || index >= self->count_ || index >= self->sceneObjects_.size || index >= self->regions_.size) {
        // Out of range: nothing to compare (the original does nothing below 0 or from count_ up).
        self->HighlightVanilla(index, on);
        return;
    }
    CUiMesh* sprite = Sprite(self, index);
    CUiRegion* region = self->regions_.data[index];
    SlotState before = Capture(sprite, region);
    s_weaponHighlight(self, nullptr, index, on);
    SlotState expected = Capture(sprite, region);
    Restore(sprite, region, before);
    self->HighlightVanilla(index, on);
    SlotState actual = Capture(sprite, region);
    ei::verify::CheckNear("CHudWeaponBar::HighlightVanilla x", expected.position.x, actual.position.x, "slot %d on %d", index,
                          on);
    ei::verify::CheckNear("CHudWeaponBar::HighlightVanilla y", expected.position.y, actual.position.y, "slot %d on %d", index,
                          on);
    ei::verify::CheckNear("CHudWeaponBar::HighlightVanilla depth", expected.position.z, actual.position.z,
                          "slot %d on %d count %d", index, on, self->count_);
    for (int i = 0; i < 3; ++i)
        ei::verify::CheckNear("CHudWeaponBar::HighlightVanilla scale", expected.scale[i], actual.scale[i],
                              "slot %d on %d count %d", index, on, self->count_);
    ei::verify::Check("CHudWeaponBar::HighlightVanilla colour", expected.colour, actual.colour, "slot %d on %d", index, on);
    ei::verify::Check("CHudWeaponBar::HighlightVanilla region", true,
                      std::memcmp(&expected.bounds, &actual.bounds, sizeof(RECT)) == 0, "slot %d on %d: top %ld / %ld", index,
                      on, expected.bounds.top, actual.bounds.top);
}
EI_VERIFY(0x0063C050, WeaponHighlightVerify, 6, s_weaponHighlight);

static void __fastcall WeaponBarAddVerify(CHudWeaponBar* self, void*, const char* model, const char* image) {
    int count = self->count_;
    int sprites = self->sceneObjects_.size;
    self->AddWeapon(model, image);
    bool ok = self->count_ == count + 1 && self->sceneObjects_.size == sprites + 1;
    ei::verify::Check("CHudWeaponBar::AddWeapon", true, ok, "count %d -> %d, sprites %d -> %d", count, self->count_, sprites,
                      self->sceneObjects_.size);
    if (!ok || count > 3) return;
    CUiMesh* sprite = Sprite(self, sprites);
    float u = static_cast<float>(count >= 2 ? 0x2A : 0) * kTexel;
    float v = static_cast<float>(((count & 1) ? 0x80 : 0) + 0x80) * kTexel;
    ei::verify::CheckNear("CHudWeaponBar::AddWeapon u", u, sprite->uOffset_, "slot %d", count);
    ei::verify::CheckNear("CHudWeaponBar::AddWeapon v", v, sprite->vOffset_, "slot %d", count);
    // Shown not highlighted: black, the region from y 520 down.
    ei::verify::Check("CHudWeaponBar::AddWeapon shown", true,
                      sprite->plainColour_ == kWhite && (sprite->flags_ & kPlainFlag) != 0 && sprite->colour_ == kColourOff,
                      "slot %d colour %08X", count, sprite->colour_);
}
EI_VERIFY(0x0063BCC0, WeaponBarAddVerify, 0, s_unused);

// --- Quick items -----------------------------------------------------------------------------------------------

static CHudQuickItems* __fastcall QuickItemsConstructVerify(CHudQuickItems* self, void*, CInterface3D* ui,
                                                            CBattleScreen* owner) {
    CHudQuickItems* result = self->Construct(ui, owner);
    bool ok = result == self && self->owner_ == owner && self->interface_ == ui && self->regions_.size == 8 &&
              self->sceneObjects_.size == 0 && self->unknown_14C == 0 && self->selected_ == -1 && !self->unit_ &&
              !self->entries_ && self->entryCount_ == 0 && LabelCount(&self->labelsVtable_) == 12 &&
              SameRect(self->rect_, 520, 500, 720, 600);
    for (int i = 0; ok && i < 8; ++i) ok = self->regions_.data[i]->tip_ != nullptr && self->regionItem_[i] == -1;
    ei::verify::Check("CHudQuickItems::Construct", true, ok, "regions %d sprites %d labels %d", self->regions_.size,
                      self->sceneObjects_.size, LabelCount(&self->labelsVtable_));
    return result;
}
EI_VERIFY(0x0063C800, QuickItemsConstructVerify, 0, s_unused);

static void __fastcall QuickItemsAddVerify(CHudQuickItems* self, void*, const char* model, const char* image) {
    int count = self->unknown_14C;
    int sprites = self->sceneObjects_.size;
    self->AddItem(model, image);
    if (count > 7) {
        // Full: nothing is added.
        ei::verify::Check("CHudQuickItems::AddItem full", true,
                          self->unknown_14C == count && self->sceneObjects_.size == sprites, "count %d", count);
        return;
    }
    bool ok = self->unknown_14C == count + 1 && self->sceneObjects_.size == sprites + 1;
    ei::verify::Check("CHudQuickItems::AddItem", true, ok, "count %d -> %d, sprites %d -> %d", count, self->unknown_14C,
                      sprites, self->sceneObjects_.size);
    if (ok) CheckIcon("CHudQuickItems::AddItem picture", Sprite(self, sprites), count, 0x80);
}
EI_VERIFY(0x0063CAE0, QuickItemsAddVerify, 0, s_unused);

// --- Spell column ----------------------------------------------------------------------------------------------

static CHudSpellColumn* __fastcall SpellColumnConstructVerify(CHudSpellColumn* self, void*, CInterface3D* ui,
                                                              CBattleScreen* owner) {
    CHudSpellColumn* result = self->Construct(ui, owner);
    bool ok = result == self && self->owner_ == owner && self->interface_ == ui && self->regions_.size == 0 &&
              self->sceneObjects_.size == 0 && self->count_ == 0 && self->selected_ == -1 && !self->unit_ &&
              LabelCount(&self->labelsVtable_) == 13 && SameRect(self->rect_, 760, 190, 800, 510);
    ei::verify::Check("CHudSpellColumn::Construct", true, ok, "regions %d sprites %d labels %d", self->regions_.size,
                      self->sceneObjects_.size, LabelCount(&self->labelsVtable_));
    return result;
}
EI_VERIFY(0x0063E2A0, SpellColumnConstructVerify, 0, s_unused);

static void __fastcall SpellColumnAddVerify(CHudSpellColumn* self, void*, const char* model, const char* image) {
    int count = self->count_;
    int sprites = self->sceneObjects_.size;
    int regions = self->regions_.size;
    self->AddSpell(model, image);
    bool ok = self->count_ == count + 1 && self->sceneObjects_.size == sprites + 1 && self->regions_.size == regions + 1 &&
              self->selected_ == count;
    ei::verify::Check("CHudSpellColumn::AddSpell", true, ok, "count %d -> %d, sprites %d -> %d, regions %d -> %d", count,
                      self->count_, sprites, self->sceneObjects_.size, regions, self->regions_.size);
    if (!ok) return;
    if (count < 8) CheckIcon("CHudSpellColumn::AddSpell picture", Sprite(self, sprites), count, 0x80);
    if (count < self->sceneObjects_.size)
        ei::verify::Check("CHudSpellColumn::AddSpell colour", kColourOff, Sprite(self, count)->colour_, "button %d", count);
}
EI_VERIFY(0x0063E4B0, SpellColumnAddVerify, 0, s_unused);

// --- Action column ---------------------------------------------------------------------------------------------

static CHudActionColumn* __fastcall ActionColumnConstructVerify(CHudActionColumn* self, void*, CInterface3D* ui,
                                                                CBattleScreen* owner) {
    CHudActionColumn* result = self->Construct(ui, owner);
    bool ok = result == self && self->owner_ == owner && self->interface_ == ui && self->regions_.size == 6 &&
              self->sceneObjects_.size == 0 && self->count_ == 0 && self->selected_ == -1 && !self->unit_ &&
              self->actions_.size == 0 && LabelCount(&self->labelsVtable_) == 1 && SameRect(self->rect_, 0, 250, 40, 510);
    for (int i = 0; ok && i < 6; ++i) ok = self->regions_.data[i]->tip_ != nullptr;
    ei::verify::Check("CHudActionColumn::Construct", true, ok, "regions %d sprites %d labels %d", self->regions_.size,
                      self->sceneObjects_.size, LabelCount(&self->labelsVtable_));
    return result;
}
EI_VERIFY(0x0063F300, ActionColumnConstructVerify, 0, s_unused);

static void __fastcall ActionColumnAddVerify(CHudActionColumn* self, void*, const char* model, const char* image,
                                             int action) {
    int count = self->count_;
    int sprites = self->sceneObjects_.size;
    int actions = self->actions_.size;
    self->AddAction(model, image, action);
    bool ok = self->count_ == count + 1 && self->sceneObjects_.size == sprites + 1 && self->actions_.size == actions + 1 &&
              self->selected_ == count && static_cast<int>(self->actions_.data[actions]) == action;
    ei::verify::Check("CHudActionColumn::AddAction", true, ok, "count %d -> %d, sprites %d -> %d, actions %d -> %d", count,
                      self->count_, sprites, self->sceneObjects_.size, actions, self->actions_.size);
    if (!ok) return;
    if (count < 8) CheckIcon("CHudActionColumn::AddAction picture", Sprite(self, sprites), count, 0);
    if (count < self->sceneObjects_.size)
        ei::verify::Check("CHudActionColumn::AddAction colour", kColourOff, Sprite(self, count)->colour_, "button %d", count);
}
EI_VERIFY(0x0063F500, ActionColumnAddVerify, 0, s_unused);
