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

// 00652000 (ours only: rebuilds the rows' key vectors and texts): afterwards the key table binds
// the pressed key to the selected row's action; when the key belonged to another row, the
// selected row holds just the pressed key, its old keys are unbound, its text is the key's name,
// and the first row of the key's old action lost the key. Scenario: ui_options_bind (a free key,
// then one bound to another action, confirmed in the message box).
static void __fastcall CommitBindingVerify(COptionsWindow* self, void*) {
    const UiKeyName* pressed = self->pressedKey_;
    OptionBinding* binding = pressed ? self->rows_[self->selectedRow_].binding_ : nullptr;
    if (!binding) {
        self->COptionsWindow::CommitBinding();
        return;
    }
    OptionBinding* other = nullptr;
    for (OptionBinding* b = self->bindings_.begin(); b != self->bindings_.end() && !other; ++b)
        if (b->action_->id_ == self->keyOfScan_[pressed->scan_]) other = b;
    const UiKeyName* oldKeys[8] = {};
    int oldCount = 0;
    for (const UiKeyName* key : binding->keys_)
        if (oldCount < 8) oldKeys[oldCount++] = key;
    int otherHad = 0;
    if (other)
        for (const UiKeyName* key : other->keys_) otherHad += key == pressed;

    self->COptionsWindow::CommitBinding();

    bool ok = self->keyOfScan_[pressed->scan_] == static_cast<signed char>(binding->action_->id_);
    if (binding != other) {
        // The text: the name with trailing spaces and bars trimmed.
        std::size_t length = std::strlen(pressed->name_);
        while (length > 0 && (pressed->name_[length - 1] == ' ' || pressed->name_[length - 1] == '|')) --length;
        ok = ok && binding->keys_.size() == 1 && binding->keys_[0] == pressed &&
             static_cast<std::size_t>(binding->keysText_.GetLength()) == length &&
             std::strncmp(binding->keysText_, pressed->name_, length) == 0;
        for (int i = 0; i < oldCount; ++i)
            if (oldKeys[i] != pressed && oldKeys[i]->scan_ != pressed->scan_) ok = ok && self->keyOfScan_[oldKeys[i]->scan_] == 0;
        if (other && otherHad > 0) {
            int has = 0;
            for (const UiKeyName* key : other->keys_) has += key == pressed;
            ok = ok && has == otherHad - 1;
        }
    }
    ei::verify::Check("COptionsWindow::CommitBinding", true, ok, "key %s action %d other %d old %d", pressed->name_,
                      binding->action_->id_, other ? other->action_->id_ : -1, oldCount);
}
EI_VERIFY(0x00652000, CommitBindingVerify, 0, s_unused);

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

// 0065C930 (A-H19): MOV EAX,FS:[0] (6 bytes). Side by side as OnKeyDown. No scenario types into an
// edit field (the message box with flag 4 and the multiplayer screens have one).
static void(__fastcall* s_editChar)(void*, void*, int, int, int);
static void __fastcall EditCharVerify(void* input, void*, int key, int repeat, int flags) {
    CUiEditField* w = EditFromInput(input);
    EditState before = TakeEditState(w);
    s_editChar(input, nullptr, key, repeat, flags);
    EditState expected = TakeEditState(w);
    PutEditState(w, before);
    w->CUiEditField::OnChar(key, repeat, flags);
    EditState actual = TakeEditState(w);
    ei::verify::Check("CUiEditField::OnChar", true, SameEditState(expected, actual), "key %X caret %d->%d/%d", key,
                      before.caret, expected.caret, actual.caret);
}
EI_VERIFY(0x0065C930, EditCharVerify, 6, s_editChar);

// 0065C010 (A-H19; ours only, it allocates): the fields, one surface over the rectangle, focused.
static void* s_editUnused;
static CUiEditField* __fastcall EditConstructVerify(CUiEditField* self, void*, CInterface3D* ui, RECT rect) {
    self->CUiEditField::Construct(ui, rect);
    bool ok = self->vtable_ == reinterpret_cast<void**>(std::uintptr_t(0x00742DF4)) && self->charClasses_ == 0xF &&
              self->font_ == 1 && self->maxLength_ == 50 && self->focused_ && self->surfaces_.size == 1 &&
              std::memcmp(&self->area_, &rect, sizeof(rect)) == 0 && self->Text().GetLength() == 0;
    ei::verify::Check("CUiEditField::Construct", true, ok, "(%d,%d,%d,%d)", rect.left, rect.top, rect.right, rect.bottom);
    return self;
}
EI_VERIFY(0x0065C010, EditConstructVerify, 0, s_editUnused);

// --- 0065C1A0 / 0065C290 / 0065C2D0 (A-H25): the edit field's look, text and focus, side by side ---
// The original on the real field (it redraws the text), what it wrote kept and the field put back,
// then ours. Reached from the multiplayer hero screen and the message box with an edit field (no
// scenario opens either).

namespace {
struct EditLook {
    std::uint32_t charClasses;
    std::uint8_t mode;
    int font;
    int maxLength;
    COLORREF colour;
    COLORREF caretColour;
};
EditLook TakeLook(const CUiEditField* w) {
    return EditLook{w->charClasses_, w->centred_, w->font_, w->maxLength_, w->colour_, w->caretColour_};
}
void PutLook(CUiEditField* w, const EditLook& s) {
    w->charClasses_ = s.charClasses;
    w->centred_ = s.mode;
    w->font_ = s.font;
    w->maxLength_ = s.maxLength;
    w->colour_ = s.colour;
    w->caretColour_ = s.caretColour;
}
bool SameLook(const EditLook& a, const EditLook& b) {
    return a.charClasses == b.charClasses && a.mode == b.mode && a.font == b.font && a.maxLength == b.maxLength &&
           a.colour == b.colour && a.caretColour == b.caretColour;
}
} // namespace

// 0065C1A0: MOV EAX,[ESP+4]; MOV DL,[ESP+8] (8 bytes).
static void(__fastcall* s_editSetLook)(CUiEditField*, void*, std::uint32_t, char, int, int, COLORREF, COLORREF);
static void __fastcall EditSetLookVerify(CUiEditField* self, void*, std::uint32_t charClasses, char mode, int font,
                                         int maxLength, COLORREF colour, COLORREF caretColour) {
    EditLook before = TakeLook(self);
    s_editSetLook(self, nullptr, charClasses, mode, font, maxLength, colour, caretColour);
    EditLook expected = TakeLook(self);
    PutLook(self, before);
    self->CUiEditField::SetLook(charClasses, mode, font, maxLength, colour, caretColour);
    ei::verify::Check("CUiEditField::SetLook", true, SameLook(expected, TakeLook(self)), "classes %X font %d length %d",
                      charClasses, font, maxLength);
}
EI_VERIFY(0x0065C1A0, EditSetLookVerify, 8, s_editSetLook);

// 0065C290: MOV EAX,[ESP+4]; PUSH ESI (5 bytes). `text` may be the field's own text: the kept state
// holds that buffer while both run.
static void(__fastcall* s_editSetText)(CUiEditField*, void*, const char*);
static void __fastcall EditSetTextVerify(CUiEditField* self, void*, const char* text) {
    EditState before = TakeEditState(self);
    s_editSetText(self, nullptr, text);
    EditState expected = TakeEditState(self);
    PutEditState(self, before);
    self->CUiEditField::SetText(text);
    EditState actual = TakeEditState(self);
    ei::verify::Check("CUiEditField::SetText", true, SameEditState(expected, actual), "'%s' caret %d/%d",
                      static_cast<const char*>(expected.text), expected.caret, actual.caret);
}
EI_VERIFY(0x0065C290, EditSetTextVerify, 5, s_editSetText);

// 0065C2D0: MOV AL,[ESP+4]; MOV [ECX+0x179],AL (10 bytes).
static void(__fastcall* s_editSetFocused)(CUiEditField*, void*, bool);
static void __fastcall EditSetFocusedVerify(CUiEditField* self, void*, bool focused) {
    const bool before = self->focused_;
    s_editSetFocused(self, nullptr, focused);
    const bool expected = self->focused_;
    self->focused_ = before;
    self->CUiEditField::SetFocused(focused);
    ei::verify::Check("CUiEditField::SetFocused", expected, self->focused_, "focused %d", focused);
}
EI_VERIFY(0x0065C2D0, EditSetFocusedVerify, 10, s_editSetFocused);

// 0065C2E0 (A-H26; hooked in the vanilla launch only): MOV EAX,FS:[0] (6 bytes). Side by side on
// the real field: both clear the surface and draw the same text and caret, so the second run
// leaves what the first drew; the caret (clamped by the function, uninitialised when the
// constructor draws) is put back in between and compared. The pixels are not compared. Scenarios:
// the screens with an edit field build and fill it (ui_hero_screen's name field, net_lobby).
static void(__fastcall* s_editDraw)(CUiEditField*);
static void __fastcall EditDrawVerify(CUiEditField* self) {
    const int before = self->caret_;
    s_editDraw(self);
    const int expected = self->caret_;
    self->caret_ = before;
    self->CUiEditField::DrawContentsVanilla();
    ei::verify::Check("CUiEditField::DrawContents", expected, self->caret_, "caret %d length %d", before,
                      self->Text().GetLength());
}
EI_VERIFY(0x0065C2E0, EditDrawVerify, 6, s_editDraw);
