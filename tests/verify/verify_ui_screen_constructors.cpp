// Checks for the window constructors (ui_screen_constructors.cpp). Ours only (a constructor
// cannot run twice on the same memory): afterwards the vtables, the fields each one sets and its
// result must be as the original leaves them. Reached: the message box by every question (the
// Esc menu's quit box in ui_screens, the save window's overwrite, the disconnect box); the
// objectives window by TAB (ui_hud); the portraits panel by every battle screen's Create.
#include "verify.h"

#include "../../src/game/ui.h"

#include <cstdint>
#include <cstring>

namespace {

bool Is(const void* p, std::uintptr_t address) { return reinterpret_cast<std::uintptr_t>(p) == address; }
const char* Str(const void* field) { return *static_cast<const char* const*>(field); }

} // namespace

// CMessageBox::Construct (00634650).
static CMessageBox*(__fastcall* s_messageBox)(CMessageBox*, void*, CInterface3D*, const char*, const char*, std::uint32_t,
                                               std::uint32_t, std::uint32_t);
static CMessageBox* __fastcall MessageBoxVerify(CMessageBox* self, void*, CInterface3D* ui, const char* title,
                                                const char* text, std::uint32_t result, std::uint32_t flags,
                                                std::uint32_t format) {
    CMessageBox* r = self->CMessageBox::Construct(ui, title, text, result, flags, format);
    bool edit = (flags & 4) != 0;
    bool ok = r == self && Is(self->vtable_, 0x00742348) && Is(self->inputVtable_, 0x00742314) && self->interface_ == ui &&
              self->result_ == result && self->flags_ == flags && self->format_ == format && self->title_ &&
              self->text_ && (edit ? self->editText_ == text && *self->text_ == 0 : self->editText_ == nullptr);
    ei::verify::Check("CMessageBox::Construct", true, ok, "\"%.30s\" \"%.30s\" flags %X: \"%.40s\" / \"%.40s\"",
                      title ? title : "", text ? text : "", flags, self->title_ ? self->title_ : "",
                      self->text_ ? self->text_ : "");
    return r;
}
EI_VERIFY(0x00634650, MessageBoxVerify, 0, s_messageBox);

// CObjectivesWindow::Construct (0063B5D0).
static CObjectivesWindow*(__fastcall* s_objectives)(CObjectivesWindow*, void*, CInterface3D*);
static CObjectivesWindow* __fastcall ObjectivesVerify(CObjectivesWindow* self, void*, CInterface3D* ui) {
    CObjectivesWindow* r = self->CObjectivesWindow::Construct(ui);
    bool ok = r == self && Is(self->vtable_, 0x007411BC) && Is(self->inputVtable_, 0x00741188) && self->interface_ == ui &&
              !self->zones_.Start() && !self->zones_.Finish() && !self->zones_.EndOfStorage() && *Str(&self->zoneName_) == 0 &&
              !self->exitDistances_ && *Str(&self->shownZone_) == 0 && Is(self->exits_.vtable, 0x00741264) &&
              !self->exits_.data && self->exits_.size == 0 && self->exits_.maxSize == 0 && self->exits_.growBy == 0 &&
              !self->quests_.data && self->quests_.size == 0 && !self->questPlaces_.Start() && !self->questPlaces_.Finish() &&
              !self->questPlaces_.EndOfStorage() && !self->objectiveTexts_.data && self->objectiveTexts_.size == 0;
    ei::verify::Check("CObjectivesWindow::Construct", true, ok, "window %p", self);
    return r;
}
EI_VERIFY(0x0063B5D0, ObjectivesVerify, 0, s_objectives);

// CHudPortraits::Construct (0063FE70): also exactly one scene object, the marker sprite.
static CHudPortraits*(__fastcall* s_portraits)(CHudPortraits*, void*, CInterface3D*, CBattleScreen*);
static CHudPortraits* __fastcall PortraitsVerify(CHudPortraits* self, void*, CInterface3D* ui, CBattleScreen* owner) {
    CHudPortraits* r = self->CHudPortraits::Construct(ui, owner);
    bool ok = r == self && Is(self->vtable_, 0x00742A18) && Is(self->inputVtable_, 0x007429E4) && self->interface_ == ui &&
              self->owner_ == owner && self->count_ == 0 && !self->slots_ && !self->slotsEnd_ && !self->slotsCapacity_ &&
              self->rect_.left == 250 && self->rect_.top == 440 && self->rect_.right == 550 && self->rect_.bottom == 600 &&
              self->sceneObjects_.size == 1 && self->sceneObjects_.data[0] != nullptr;
    ei::verify::Check("CHudPortraits::Construct", true, ok, "objects %d", self->sceneObjects_.size);
    return r;
}
EI_VERIFY(0x0063FE70, PortraitsVerify, 0, s_portraits);

// 0063FF70: ours only (a destructor body). Afterwards the window base holds no arrays (005FE930
// ran). Reached when the battle screen closes (ui_camp: the camp and the global map replace it).
static void(__fastcall* s_portraitsDestroy)(CHudPortraits*);
static void __fastcall PortraitsDestroyVerify(CHudPortraits* self) {
    int slots = static_cast<int>(self->slotsEnd_ - self->slots_);
    self->DestroyBody();
    bool ok = !self->sceneObjects_.data && !self->surfaces_.data && !self->regions_.data && !self->models_.data;
    ei::verify::Check("CHudPortraits::DestroyBody", true, ok, "slots %d", slots);
}
EI_VERIFY(0x0063FF70, PortraitsDestroyVerify, 0, s_portraitsDestroy);
