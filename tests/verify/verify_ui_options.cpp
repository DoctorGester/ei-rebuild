// Checks for the options window's settings and bindings and its slider (ui_options.cpp), the
// scrolling text's DrawLines (ui_scroll_text.cpp) and the edit field's keys (ui_edit_field.cpp).
//
// DR-H16; see docs/notes/drafts/DR-H16.md.
//
// Scenarios: ui_hud, ui_main_menu and ui_container_input open the options window (its Build runs
// BuildSettings, LoadSettings, BuildBindings and ShowGroup(0), which draws the rows; ui_hud also
// picks the second group); the slider's SetRect runs from BuildSettings. Apply runs only on OK
// (no scenario presses it). DrawLines runs whenever the battle screen's message log is drawn
// (ui_hud). The edit field's keys need a message box with an edit field (none yet).
#include "verify.h"

#include "../../src/game/ui.h"

#include <cstring>

namespace {
void* s_unused;

CUiMesh* SceneSprite(CUiWindow* window, int index) { return reinterpret_cast<CUiMesh*>(window->sceneObjects_.data[index]); }
} // namespace

// --- Options window: ours only ------------------------------------------------------------------

// 00651350 (draws): the highlight sprite shows exactly when a row 0-13 is selected, the shown
// group's button at full tint and the others at half.
static void __fastcall RefreshRowsVerify(COptionsWindow* self, void*) {
    self->COptionsWindow::RefreshRows();
    bool ok = (SceneSprite(self, 2)->visible_ != 0) == (self->selectedRow_ >= 0 && self->selectedRow_ < 14);
    for (int group = 0; group < 11 && ok; ++group)
        ok = SceneSprite(self, 3 + group)->tint_[0] == (group == self->group_ ? 1.0f : 0.5f);
    ei::verify::Check("COptionsWindow::RefreshRows", true, ok, "group %d row %d", self->group_, self->selectedRow_);
}
EI_VERIFY(0x00651350, RefreshRowsVerify, 0, s_unused);

// 00651D70 (tips, enabled flags, drawing): the group clamped, every filled row from a definition
// of the group at that row, the shown rows' controls enabled and the others disabled, a row
// selected, no key wait.
static void __fastcall ShowGroupVerify(COptionsWindow* self, void*, int group) {
    self->COptionsWindow::ShowGroup(group);
    int expected = group < 0 ? 0 : group > 10 ? 10 : group;
    bool ok = self->group_ == expected && !self->waitingForKey_ && self->selectedRow_ >= 0 && self->selectedRow_ < 14;
    for (int row = 0; row < 14 && ok; ++row) {
        if (OptionSetting* setting = self->rows_[row].setting_)
            ok = setting->definition_->group_ == expected && setting->definition_->row_ == row;
        if (OptionBinding* binding = self->rows_[row].binding_)
            ok = ok && binding->action_->group_ == expected && binding->action_->row_ == row;
    }
    for (int i = 0; i < kOptionDefinitionCount && ok; ++i) {
        const OptionSetting& setting = self->settings_[i];
        bool shown = setting.definition_->group_ == expected;
        if (setting.bar_) ok = (setting.bar_->enabled_ != 0) == shown;
        if (setting.spin_) ok = ok && (setting.spin_->enabled_ != 0) == shown;
    }
    ei::verify::Check("COptionsWindow::ShowGroup", true, ok, "group %d->%d row %d", group, self->group_,
                      self->selectedRow_);
}
EI_VERIFY(0x00651D70, ShowGroupVerify, 0, s_unused);

// 00651940 (allocates controls): 26 settings in definition order, a slider for kind 0 and a spin
// control for kind 1 (each a new child), value 0.
static void __fastcall BuildSettingsVerify(COptionsWindow* self, void*) {
    int children = self->children_.size;
    self->COptionsWindow::BuildSettings();
    bool ok = self->settings_.size() == static_cast<std::size_t>(kOptionDefinitionCount);
    int controls = 0;
    for (int i = 0; i < kOptionDefinitionCount && ok; ++i) {
        const OptionSetting& setting = self->settings_[i];
        const OptionDefinition* definition = &OptionDefinitions()[i];
        ok = setting.definition_ == definition && setting.value_ == 0;
        if (definition->kind_ == 0) ok = ok && setting.bar_ && !setting.spin_;
        else if (definition->kind_ == 1) ok = ok && setting.spin_ && !setting.bar_;
        if (setting.bar_ || setting.spin_) ++controls;
    }
    ok = ok && self->children_.size == children + controls;
    ei::verify::Check("COptionsWindow::BuildSettings", true, ok, "settings %d children %d->%d",
                      static_cast<int>(self->settings_.size()), children, self->children_.size);
}
EI_VERIFY(0x00651940, BuildSettingsVerify, 0, s_unused);

// 00651600 (allocates): 59 rows in definition order, at most one key each, and the keys' text
// empty exactly when there is no key.
static void __fastcall BuildBindingsVerify(COptionsWindow* self, void*) {
    self->COptionsWindow::BuildBindings();
    bool ok = self->bindings_.size() == static_cast<std::size_t>(kActionDefinitionCount);
    for (int i = 0; i < kActionDefinitionCount && ok; ++i) {
        const OptionBinding& binding = self->bindings_[i];
        ok = binding.action_ == &ActionDefinitions()[i] && binding.keys_.size() <= 1 &&
             (binding.keys_.size() == 0) == (binding.keysText_.GetLength() == 0) &&
             (binding.keys_.size() == 0 || self->keyOfScan_[binding.keys_[0]->scan_] == binding.action_->id_);
    }
    ei::verify::Check("COptionsWindow::BuildBindings", true, ok, "rows %d", static_cast<int>(self->bindings_.size()));
}
EI_VERIFY(0x00651600, BuildBindingsVerify, 0, s_unused);

// 006524A0 (writes the game's option globals; ours only): the key table is the window's copy and,
// outside a multiplayer client, the difficulty is setting 25's value. ui_main_menu presses OK once.
static void __fastcall ApplyVerify(COptionsWindow* self, void*) {
    int difficulty = GameDifficulty();
    self->COptionsWindow::Apply();
    int setting = self->settings_[25].value_;
    bool ok = std::memcmp(KeyActions(), self->keyOfScan_, sizeof(self->keyOfScan_)) == 0 &&
              (GameDifficulty() == setting || GameDifficulty() == difficulty);
    ei::verify::Check("COptionsWindow::Apply", true, ok, "display %d difficulty %d->%d", self->displayModeChanged_,
                      difficulty, GameDifficulty());
}
EI_VERIFY(0x006524A0, ApplyVerify, 0, s_unused);

// --- 00652200: side by side ------------------------------------------------------------------------

// 00652200: PUSH EBX; MOV EBX,ECX; PUSH ESI; PUSH EDI (5 bytes). The original first (it sets the
// controls too; ours then sets them to the same values), its values kept, then ours; the values
// and the key table compared.
static void(__fastcall* s_loadSettings)(COptionsWindow*, void*);
static void __fastcall LoadSettingsVerify(COptionsWindow* self, void*) {
    s_loadSettings(self, nullptr);
    int expected[kOptionDefinitionCount];
    for (int i = 0; i < kOptionDefinitionCount; ++i) expected[i] = self->settings_[i].value_;
    signed char keys[sizeof(self->keyOfScan_)];
    std::memcpy(keys, self->keyOfScan_, sizeof(keys));
    for (int i = 0; i < kOptionDefinitionCount; ++i) self->settings_[i].value_ = -12345;
    std::memset(self->keyOfScan_, 0x55, sizeof(self->keyOfScan_));
    self->COptionsWindow::LoadSettings();
    int bad = -1;
    for (int i = 0; i < kOptionDefinitionCount && bad < 0; ++i)
        if (self->settings_[i].value_ != expected[i]) bad = i;
    bool keysSame = std::memcmp(keys, self->keyOfScan_, sizeof(keys)) == 0;
    ei::verify::Check("COptionsWindow::LoadSettings", true, bad < 0 && keysSame, "setting %d (%d/%d) keys %d", bad,
                      bad < 0 ? 0 : expected[bad], bad < 0 ? 0 : self->settings_[bad].value_, keysSame);
}
EI_VERIFY(0x00652200, LoadSettingsVerify, 5, s_loadSettings);

// --- 0065B8B0: the slider's layout (ours only; sprites and regions through Rebirth-patched code) ---

static void __fastcall SliderSetRectVerify(CUiSlider* self, void*, const RECT* rect) {
    RECT copy = *rect;
    self->CUiSlider::SetRect(rect);
    bool ok = std::memcmp(&self->rect_, &copy, sizeof(copy)) == 0 && self->track_.left == copy.left + 26 &&
              self->track_.right == copy.right - 26 && self->track_.top == copy.bottom - 10 &&
              self->track_.bottom == copy.bottom;
    ei::verify::Check("CUiSlider::SetRect", true, ok, "(%d,%d,%d,%d)", copy.left, copy.top, copy.right, copy.bottom);
}
EI_VERIFY(0x0065B8B0, SliderSetRectVerify, 0, s_unused);

// --- 0065D150: the scrolling text's lines (ours only; it only draws) ---------------------------------

static void __fastcall DrawLinesVerify(CUiScrollText* self, void*, int scroll) {
    int tokens = self->tokens_.size;
    int lines = self->lines_.size;
    self->CUiScrollText::DrawLines(scroll);
    ei::verify::Check("CUiScrollText::DrawLines", true, self->tokens_.size == tokens && self->lines_.size == lines,
                      "scroll %d lines %d", scroll, lines);
}
EI_VERIFY(0x0065D150, DrawLinesVerify, 0, s_unused);

// --- 0065C610: the edit field's keys, side by side ------------------------------------------------

namespace {
CUiEditField* EditFromInput(void* input) {
    return reinterpret_cast<CUiEditField*>(static_cast<std::uint8_t*>(input) - offsetof(CUiWindow, inputVtable_));
}

struct EditState {
    mfc::CString text;
    int caret;
    bool editing;
    bool changed;
    bool handled;
};
EditState TakeEditState(CUiEditField* w) {
    return EditState{w->Text(), w->caret_, w->editing_, w->changed_, w->inputHandled_};
}
void PutEditState(CUiEditField* w, const EditState& s) {
    w->Text() = s.text;
    w->caret_ = s.caret;
    w->editing_ = s.editing;
    w->changed_ = s.changed;
    w->inputHandled_ = s.handled;
}
bool SameEditState(const EditState& a, const EditState& b) {
    return std::strcmp(a.text, b.text) == 0 && a.caret == b.caret && a.editing == b.editing &&
           a.changed == b.changed && a.handled == b.handled;
}
} // namespace

// 0065C610: MOV EAX,FS:[0] (6 bytes). The original on the real field (it redraws the text), its
// state kept and the field put back, then ours.
static void(__fastcall* s_editKeyDown)(void*, void*, int, int, int);
static void __fastcall EditKeyDownVerify(void* input, void*, int key, int repeat, int flags) {
    CUiEditField* w = EditFromInput(input);
    EditState before = TakeEditState(w);
    s_editKeyDown(input, nullptr, key, repeat, flags);
    EditState expected = TakeEditState(w);
    PutEditState(w, before);
    w->CUiEditField::OnKeyDown(key, repeat, flags);
    EditState actual = TakeEditState(w);
    ei::verify::Check("CUiEditField::OnKeyDown", true, SameEditState(expected, actual), "key %X caret %d->%d/%d", key,
                      before.caret, expected.caret, actual.caret);
}
EI_VERIFY(0x0065C610, EditKeyDownVerify, 6, s_editKeyDown);
