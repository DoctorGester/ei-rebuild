// The options window's settings and key bindings (00651350..006524A0) and its slider's layout
// (0065B8B0). See ui.h (COptionsWindow, CUiSlider) and docs/notes/ui.md ("Objectives, save/load, options").
#include "record_vectors.h"
#include "ui.h"

#include "item_db_text.h"
#include "maths.h"
#include "world.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/nan_audit.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "lib/stlp.h"
#include "originals_engine.h"
#include "originals_ui.h"
#include "originals_world.h"
#include "game_data.h"
#include "game_singletons.h"
#include "options.h"

#include <cstdint>
#include <cstring>
#include <new>

// --- Forwarders --------------------------------------------------------------------------------

__declspec(naked) void COptionsWindow::CallApply() EI_ORIGINAL(0x006524A0)

namespace {

// Strings of the exe.
const char* TextOptionPrefix() { return "option_"; }
const char* TextActionPrefix() { return "action_"; }
const char* TextTipOption() { return "tip option_%s"; }
const char* TextTipAction() { return "tip action_%s"; }
const char* TextOptionOff() { return "option_off"; }
const char* TextOptionOn() { return "option_on"; }
const char* TextOffDefault() { return "OFF"; }
const char* TextOnDefault() { return "ON"; }
const char* TextDifficultyHard() { return "difficulty_hard"; }
const char* TextDifficultyEasy() { return "difficulty_easy"; }
const char* TextKeySeparator() { return "  |  "; }    // after each key name
const char* TextKeyTrim() { return " |"; }         // trimmed from the right

constexpr int kOptionGroups = 11;
constexpr int kOptionRows = 14;
constexpr int kFirstRowRegion = 13;     // regions 13-26: the rows
constexpr int kHighlightSprite = 2;     // scene object 2: the selected row's highlight
constexpr int kFirstGroupSprite = 3;    // scene objects 3-13: the group buttons
constexpr int kDifficultySetting = 25;  // its spin control says hard / easy
constexpr int kSliderKind = 0;
constexpr int kSpinKind = 1;

constexpr COLORREF kColourText = 0xA7D7E4;
constexpr COLORREF kColourDim = 0xA0A0A0;
constexpr COLORREF kColourWaiting = 0x0000FF;   // a binding waiting for its key (red)

// Float constants read from the exe's memory as the original does (rule 9), all single precision.
float ConstOne() { return 1.0f /*0073B84C*/; }        // 1.0
float ConstFromStep() { return 50.0f /*0074023C*/; }   // 50.0: value = setting * 50
float ConstToStep() { return 0.02f /*00742B80*/; }     // 0.02: setting = value * 0.02
float ConstScrollFrom() { return 4.0f /*0073BAA0*/; } // 4.0: setting 20's value
float ConstScrollTo() { return 0.25f /*0073C3B0*/; }   // 0.25: setting 20 from it
float ConstHalf() { return 0.5f /*0073B86C*/; }       // 0.5: the slider's centre
float ConstThumbLift() { return 0.1f /*0073B850*/; }  // 0.1: the thumb above the track
// The slider sprites' turn about z (a float global set at start-up, not in the file).
float QuarterTurn() { return ei::GameData<const float>(0x007C8210); }

// The game's option globals the window loads (00652200) and stores (006524A0), by setting.
int& OptVolume0() { return options::TheOptions()->audioSfx_; }       // setting 0 (the sound system's volume A)
int& OptVolume1() { return options::TheOptions()->audioStreaming_; }       // setting 1
int& OptVolume2() { return options::TheOptions()->audioSpeech_; }       // setting 2
float& OptSpeedA() { return options::TheOptions()->cameraScroll_.powerKeyboard; }    // setting 3 (three more copies on store)
float& OptSpeedA2() { return options::TheOptions()->cameraPitch_.powerKeyboard; }
float& OptSpeedA3() { return options::TheOptions()->cameraZoom_.powerKeyboard; }
float& OptSpeedA4() { return options::TheOptions()->cameraRotate_.powerKeyboard; }
float& OptSpeedB() { return options::TheOptions()->cameraScroll_.powerMouse; }    // setting 4 (three more copies on store)
float& OptSpeedB2() { return options::TheOptions()->cameraPitch_.powerMouse; }
float& OptSpeedB3() { return options::TheOptions()->cameraZoom_.powerMouse; }
float& OptSpeedB4() { return options::TheOptions()->cameraRotate_.powerMouse; }
int& Opt5() { return options::TheOptions()->cameraBorderScrollArea_; }             // setting 5
int& Opt6Tenths() { return options::TheOptions()->cameraFrameSelectionArea_; }       // setting 6 * 10
// Settings 7-9, the shadow switches (map_object_shadow.cpp): units, other objects, class 0x52.
int& OptShadowUnits() { return options::TheOptions()->unitShadow_; }
int& OptShadowBuildings() { return options::TheOptions()->buildingShadow_; }
int& OptShadowFlora() { return options::TheOptions()->floraShadow_; }
int& Opt10() { return options::TheOptions()->enableBloodprints_; }
int& Opt11() { return options::TheOptions()->enableFootprints_; }
int& Opt12() { return options::TheOptions()->selectionType_; }
int& Opt13() { return options::TheOptions()->drawPath_; }
float& OptGamma14() { return options::TheOptions()->brightness_; }   // settings 14-16 (setting * 0.02 - 1)
float& OptGamma15() { return options::TheOptions()->contrast_; }
float& OptGamma16() { return options::TheOptions()->gamma_; }
int& Opt17() { return options::TheOptions()->drawFlyingHp_; }
// Setting 18: help windows (Rebirth's ForceShowTutorial patches its store at 00652663).
int& OptHelp18() { return options::TheOptions()->showTutorial_; }
int& Opt19() { return options::TheOptions()->autosave_; }
float& Opt20() { return options::TheOptions()->toolTipTime_; }        // setting 20
int& Opt21() { return options::TheOptions()->switchFilters_; }
float& OptSign22() { return options::TheOptions()->cameraFlipX_; }    // setting 22 (-1 for 1, else +1)
float& OptSign23() { return options::TheOptions()->cameraFlipY_; }    // setting 23 (likewise)
// The settings of the options window (the exe's table of 26), by the window's rows.
constexpr OptionDefinition kOptionDefinitions[26] = /*00787430*/ {
    {0, "volume_sfx", 0, 100, 1, 0},
    {1, "volume_stream", 0, 100, 1, 1},
    {2, "volume_voice", 0, 100, 1, 2},
    {3, "power_kbd", 0, 100, 2, 0},
    {4, "power_mouse", 0, 100, 2, 1},
    {5, "scroll_border", 0, 10, 2, 2},
    {6, "rubber_select", 0, 10, 2, 3},
    {7, "shadow_units", 1, 2, 0, 0},
    {8, "shadow_buildings", 1, 2, 0, 1},
    {9, "shadow_flora", 1, 2, 0, 2},
    {10, "marks", 1, 2, 0, 4},
    {11, "footprints", 1, 2, 0, 5},
    {12, "select_type", 1, 2, 0, 6},
    {13, "show_path", 1, 2, 0, 7},
    {14, "brightness", 0, 100, 0, 10},
    {15, "contrast", 0, 100, 0, 11},
    {16, "gamma", 0, 100, 0, 12},
    {17, "show_flying_hp", 1, 2, 0, 8},
    {18, "show_tutorial", 1, 2, 10, 11},
    {19, "autosave", 1, 2, 3, 11},
    {20, "tooltip_time", 0, 20, 2, 4},
    {21, "switch_filters", 1, 2, 3, 12},
    {22, "camera_reverse_x", 1, 2, 2, 6},
    {23, "camera_reverse_y", 1, 2, 2, 7},
    {24, "reverse_stereo", 1, 2, 1, 4},
    {25, "difficulty", 1, 2, 3, 0},
};

int& Opt24() { return options::TheOptions()->reverseStereo_; }
int& OptDifficultyCopy() { return options::TheOptions()->difficultyStored_; } // setting 25 (with GameDifficulty 0079BEC0)

CUiMesh* SceneSprite(CUiWindow* window, int index) { return window->SceneMesh(index); }

// A row text into the surface (005FDE20, Rebirth-patched: through its address).
void DrawRowText(CUiSurface* surface, const char* text, RECT* rect, int font, COLORREF colour, int format) {
    SurfaceDrawText(surface, nullptr, text, rect, font, colour, format, 1);
}

// The text key `key` read up to its '#' into `text` (unchanged when there is no such text).
void ReadTip(mfc::CString* text) {
    if (TextsOpenSection(TheTextManager(), nullptr, *text) != 0) TheTextManager()->ReadToHash(text, 1);
}

// The shown name: the text of prefix + name, else the name itself.
void ShownName(mfc::CString* out, const char* prefix, const char* name) {
    if (!TheTextManager()->GetString(mfc::CString(prefix) + name, out)) *out = name;
}

// A blank binding row appended as the original's push_back of a temporary does: in place while
// there is room, else through the instantiation's own _M_insert_overflow. (The original's
// temporary leaves the action pointer unset; it is written right after.)
void AppendBlankBinding(stlp::vector<OptionBinding>& bindings) {
    OptionBinding* at = bindings.Finish();
    if (at != bindings.EndOfStorage()) {
        new (at) OptionBinding{};
        bindings.Finish() = at + 1;
        return;
    }
    OptionBinding blank{};
    bindings.InsertOverflow<recordvec::OptionBindingElement>(at, blank, 1);
    // `blank` has no key storage; its strings go with it.
}
} // namespace

// --- 00651350 ------------------------------------------------------------------------------------

void COptionsWindow::RefreshRows() {
    CUiSurface* surface = surfaces_.data[0];
    UiSurfaceGetDC(surface);
    SurfaceClearOriginal(surface);
    SceneSprite(this, kHighlightSprite)->visible_ = 0;

    // The group buttons' titles down the left; the shown group's button at full tint.
    RECT title = {10, 60, 140, 80};
    for (int group = 0; group < kOptionGroups; ++group) {
        CUiMesh* button = SceneSprite(this, kFirstGroupSprite + group);
        float tint = group == group_ ? 1.0f : 0.5f;
        button->tint_[0] = tint;
        button->tint_[1] = tint;
        button->tint_[2] = tint;
        DrawRowText(surface, groupNames_[group], &title, 1, group == group_ ? kColourText : kColourDim, 1);
        gameapi::OffsetRect(&title, 0, 24);
    }
    RECT heading = {0, 10, 600, 34};
    DrawRowText(surface, groupNames_[group_], &heading, 2, kColourText, 1);

    // The rows: a binding's name and keys, or a setting's name.
    RECT name = {160, 60, 420, 80};
    RECT keys = {430, 60, 590, 80};
    for (int row = 0, y = 168; y < 504; ++row, y += 24) {
        if (row == selectedRow_) {
            CUiMesh* highlight = SceneSprite(this, kHighlightSprite);
            highlight->visible_ = 1;
            highlight->Place(471.0f, static_cast<float>(y), 3.0f);
        }
        if (OptionBinding* binding = rows_[row].binding_) {
            COLORREF colour = row == selectedRow_ && waitingForKey_ ? kColourWaiting : kColourText;
            DrawRowText(surface, binding->name_, &name, 1, colour, 0);
            DrawRowText(surface, binding->keysText_, &keys, 1, colour, 0);
        } else if (OptionSetting* setting = rows_[row].setting_) {
            DrawRowText(surface, setting->text_, &name, 1, kColourText, 0);
        }
        gameapi::OffsetRect(&name, 0, 24);
        gameapi::OffsetRect(&keys, 0, 24);
    }
    UiSurfaceReleaseDC(surface);
}
EI_REWRITTEN(0x00651350, COptionsWindow::RefreshRows);

// --- 00651D70 ------------------------------------------------------------------------------------

void COptionsWindow::ShowGroup(int group) {
    mfc::CString tip;
    if (group < 0)
        group = 0;
    else if (group >= kOptionGroups)
        group = kOptionGroups - 1;
    group_ = group;

    for (int row = 0; row < kOptionRows; ++row) {
        rows_[row].binding_ = nullptr;
        rows_[row].setting_ = nullptr;
        regions_.data[kFirstRowRegion + row]->SetTip(0, 1, 0);
    }
    for (int i = 0; i < kOptionDefinitionCount; ++i) {
        const OptionDefinition& definition = kOptionDefinitions[i];
        if (definition.group_ != group) continue;
        rows_[definition.row_].setting_ = &settings_[i];
        tip.Format(TextTipOption(), definition.name_);
        ReadTip(&tip);
        regions_.data[kFirstRowRegion + definition.row_]->SetTipString(tip, 1, 0);
    }
    for (int i = 0; i < kActionDefinitionCount; ++i) {
        const ActionDefinition& action = kActionDefinitions[i + 1];
        if (action.group_ != group) continue;
        rows_[action.row_].binding_ = &bindings_[i];
        tip.Format(TextTipAction(), action.name_);
        ReadTip(&tip);
        regions_.data[kFirstRowRegion + action.row_]->SetTipString(tip, 1, 0);
    }

    // Every setting's control disabled, then those of the shown rows enabled.
    for (int i = 0; i < kOptionDefinitionCount; ++i)
        if (settings_[i].bar_) settings_[i].bar_->SetEnabled(0, 1);
    for (int i = 0; i < kOptionDefinitionCount; ++i)
        if (settings_[i].spin_) settings_[i].spin_->SetEnabled(0, 1);
    for (int row = 0; row < kOptionRows; ++row) {
        OptionSetting* setting = rows_[row].setting_;
        if (!setting) continue;
        if (setting->bar_) setting->bar_->SetEnabled(1, 1);
        if (setting->spin_) setting->spin_->SetEnabled(1, 1);
    }

    // The first filled row is selected (drawn once for it, and again below).
    selectedRow_ = -1;
    int row = 0;
    while (row < kOptionRows && !rows_[row].binding_ && !rows_[row].setting_) ++row;
    if (row < kOptionRows) {
        selectedRow_ = row;
        RefreshRows();
    }
    if (selectedRow_ == -1) selectedRow_ = 0;
    waitingForKey_ = false;
    RefreshRows();
}
EI_REWRITTEN(0x00651D70, COptionsWindow::ShowGroup);

// --- 00651940 ------------------------------------------------------------------------------------

void COptionsWindow::BuildSettings() {
    settings_.clear();   // 00652F80 copy and the 00653800 destructors, inlined by the original
    for (int i = 0; i < kOptionDefinitionCount; ++i) {
        // The original's temporary has only its string constructed; its other fields are copied
        // as the stack left them and all written below for both kinds.
        OptionSetting blank{};
        settings_.push_back(blank);
        OptionSetting& setting = settings_.back();
        const OptionDefinition* definition = &kOptionDefinitions[i];
        setting.value_ = 0;
        setting.definition_ = definition;
        ShownName(&setting.text_, TextOptionPrefix(), definition->name_);

        int top = definition->row_ * 24;
        if (definition->kind_ == kSliderKind) {
            void* memory = gamecrt::operator_new(sizeof(CUiSlider));
            CUiSlider* slider = memory ? static_cast<CUiSlider*>(memory)->Construct(interface_, RECT{0, 0, 100, 100}, 3.0f,
                                                                                   definition->maximum_, 1.0f)
                                       : nullptr;
            setting.bar_ = slider;
            RECT rect = {530, top + 163, 680, top + 173};
            slider->CallSetRect(&rect);   // (a failed allocation is called through, as in the original)
            AddChild(slider);
            setting.spin_ = nullptr;
        } else if (definition->kind_ == kSpinKind) {
            mfc::CString text;
            text = TextOffDefault();
            TheTextManager()->GetString(TextOptionOff(), &text);
            if (i == kDifficultySetting) TheTextManager()->GetString(TextDifficultyHard(), &text);
            void* memory = gamecrt::operator_new(sizeof(CUiSpin));
            setting.spin_ = memory ? static_cast<CUiSpin*>(memory)->Construct(interface_, RECT{530, top + 160, 690, top + 176},
                                                                             text)
                                   : nullptr;
            text = TextOnDefault();
            TheTextManager()->GetString(TextOptionOn(), &text);
            if (i == kDifficultySetting) TheTextManager()->GetString(TextDifficultyEasy(), &text);
            setting.spin_->AddText(text);
            AddChild(setting.spin_);
            setting.bar_ = nullptr;
        }
    }
}
EI_REWRITTEN(0x00651940, COptionsWindow::BuildSettings);

// --- 00651600 ------------------------------------------------------------------------------------

void COptionsWindow::BuildBindings() {
    // clear(): 00652E30 copy and the 006537E0 destructors, inlined by the original.
    for (OptionBinding& binding : bindings_) binding.Destroy();
    bindings_.Finish() = bindings_.Start();

    for (int i = 0; i < kActionDefinitionCount; ++i) {
        const ActionDefinition* action = &kActionDefinitions[i + 1];
        AppendBlankBinding(bindings_);
        OptionBinding& binding = bindings_.back();
        binding.action_ = action;
        ShownName(&binding.name_, TextActionPrefix(), action->name_);

        // The first key name of a scan code bound to the action (00652E10 clears the list).
        binding.keys_.clear();
        for (int scan = 0; scan < kKeyActionCount; ++scan) {
            if (keyOfScan_[scan] != action->id_) continue;
            for (int k = 0; k < kUiKeyNameCount; ++k) {
                const UiKeyName* name = &UiKeyNames()[k];
                if (name->scan_ == scan && binding.keys_.size() < 1) binding.keys_.push_back(name);
            }
        }
        binding.keysText_.Empty();
        for (std::size_t k = 0; k < binding.keys_.size(); ++k) {
            binding.keysText_ += binding.keys_[k]->name_;
            binding.keysText_ += TextKeySeparator();
        }
        binding.keysText_.TrimRight(TextKeyTrim());
    }
}
EI_REWRITTEN(0x00651600, COptionsWindow::BuildBindings);

// --- 00652000 ------------------------------------------------------------------------------------

namespace {
// The keys' names, each followed by "  |  ", the last separator trimmed (as BuildBindings).
void RebuildKeysText(OptionBinding& binding) {
    binding.keysText_.Empty();
    for (std::size_t k = 0; k < binding.keys_.size(); ++k) {
        binding.keysText_ += binding.keys_[k]->name_;
        binding.keysText_ += TextKeySeparator();
    }
    binding.keysText_.TrimRight(TextKeyTrim());
}
} // namespace

void COptionsWindow::CommitBinding() {
    if (!pressedKey_) return;
    OptionBinding* binding = rows_[selectedRow_].binding_;
    if (!binding) return;
    // The first binding row of the action the key is bound to now (none for 0 when no action has
    // that id).
    const int boundAction = keyOfScan_[pressedKey_->scan_];
    OptionBinding* other = nullptr;
    for (OptionBinding* b = bindings_.begin(); b != bindings_.end(); ++b) {
        if (b->action_->id_ == boundAction) {
            other = b;
            break;
        }
    }
    if (binding != other) {
        if (binding->keys_.size() != 0) {
            for (const UiKeyName* key : binding->keys_) keyOfScan_[key->scan_] = 0;
            binding->keys_.Finish() = binding->keys_.Start();   // erase(begin, end): 00653150 copies nothing
        }
        binding->keys_.push_back(pressedKey_);   // 006527A0 on overflow
        RebuildKeysText(*binding);
        if (other) {
            for (const UiKeyName** key = other->keys_.begin(); key != other->keys_.end(); ++key) {
                if (*key != pressedKey_) continue;
                other->keys_.erase(key);
                RebuildKeysText(*other);
                break;
            }
        }
    }
    keyOfScan_[pressedKey_->scan_] = static_cast<signed char>(binding->action_->id_);
}
EI_REWRITTEN(0x00652000, COptionsWindow::CommitBinding);

// --- 00652200 ------------------------------------------------------------------------------------

void COptionsWindow::LoadSettings() {
    std::memcpy(keyOfScan_, KeyActions(), sizeof(keyOfScan_));
    OptionSetting* s = settings_.begin();
    const double fromStep = ConstFromStep();
    const float one = ConstOne();
    s[0].value_ = OptVolume0();
    s[1].value_ = OptVolume1();
    s[2].value_ = OptVolume2();
    s[24].value_ = Opt24();
    // _ftol: truncation.
    s[3].value_ = static_cast<int>(OptSpeedA() * fromStep);
    s[4].value_ = static_cast<int>(OptSpeedB() * fromStep);
    // FCOMP; TEST AH,0x40: equal gives 0.
    float sign22 = OptSign22();
    float sign23 = OptSign23();
    EI_NAN_AUDIT(sign22, sign23);
    s[22].value_ = sign22 == one ? 0 : 1;
    s[23].value_ = sign23 == one ? 0 : 1;
    s[5].value_ = Opt5();
    s[6].value_ = Opt6Tenths() / 10;
    s[20].value_ = static_cast<int>(Opt20() * static_cast<double>(ConstScrollFrom()));
    s[7].value_ = OptShadowUnits();
    s[8].value_ = OptShadowBuildings();
    s[9].value_ = OptShadowFlora();
    s[10].value_ = Opt10();
    s[11].value_ = Opt11();
    s[12].value_ = Opt12();
    s[13].value_ = Opt13();
    s[17].value_ = Opt17();
    s[18].value_ = OptHelp18();
    s[19].value_ = Opt19();
    s[21].value_ = Opt21();
    s[14].value_ = static_cast<int>((static_cast<double>(OptGamma14()) + one) * fromStep);
    s[15].value_ = static_cast<int>((static_cast<double>(OptGamma15()) + one) * fromStep);
    s[16].value_ = static_cast<int>((static_cast<double>(OptGamma16()) + one) * fromStep);
    s[25].value_ = GameDifficulty();

    for (int i = 0; i < kOptionDefinitionCount; ++i) {
        if (s[i].bar_)
            s[i].bar_->CallSetPosition(s[i].value_);
        else if (s[i].spin_)
            s[i].spin_->SetValue(s[i].value_);
    }
}
EI_REWRITTEN(0x00652200, COptionsWindow::LoadSettings);

// --- 006524A0 ------------------------------------------------------------------------------------

void COptionsWindow::Apply() {
    std::memcpy(KeyActionTable(), keyOfScan_, sizeof(keyOfScan_));
    const OptionSetting* s = settings_.begin();
    const double toStep = ConstToStep();
    const float one = ConstOne();
    OptVolume0() = s[0].value_;
    OptVolume1() = s[1].value_;
    OptVolume2() = s[2].value_;
    Opt24() = s[24].value_;
    // Each product is rounded once by its float stores.
    float speedA = static_cast<float>(s[3].value_ * toStep);
    float speedB = static_cast<float>(s[4].value_ * toStep);
    OptSpeedA() = speedA;
    OptSpeedA2() = speedA;
    OptSpeedA3() = speedA;
    OptSpeedA4() = speedA;
    OptSpeedB() = speedB;
    OptSpeedB2() = speedB;
    OptSpeedB3() = speedB;
    OptSpeedB4() = speedB;
    OptSign22() = s[22].value_ == 1 ? -1.0f : 1.0f;
    OptSign23() = s[23].value_ == 1 ? -1.0f : 1.0f;
    Opt5() = s[5].value_;
    Opt6Tenths() = s[6].value_ * 10;
    Opt20() = static_cast<float>(s[20].value_ * static_cast<double>(ConstScrollTo()));
    OptShadowUnits() = s[7].value_;
    OptShadowBuildings() = s[8].value_;
    OptShadowFlora() = s[9].value_;
    Opt10() = s[10].value_;
    Opt11() = s[11].value_;
    Opt12() = s[12].value_;
    Opt13() = s[13].value_;
    Opt17() = s[17].value_;
    OptHelp18() = s[18].value_;
    Opt19() = s[19].value_;
    Opt21() = s[21].value_;
    OptGamma14() = static_cast<float>(s[14].value_ * toStep - one);
    OptGamma15() = static_cast<float>(s[15].value_ * toStep - one);
    OptGamma16() = static_cast<float>(s[16].value_ * toStep - one);
    if (!IsMultiplayerClient()) {
        OptDifficultyCopy() = s[25].value_;
        GameDifficulty() = s[25].value_;
    }
    if (shadowsChanged_) TheClientWorld()->DropShadows();
}
EI_REPLACE_METHOD(0x006524A0, COptionsWindow::Apply);

// --- 0065B8B0: the slider's slot 26 ---------------------------------------------------------------

namespace {
// The slider's sprites lie along x: a quarter turn about z (the angle is the float at 007C8210).
void TurnQuarter(CUiMesh* sprite) {
    Vector3 axis = {0.0f, 0.0f, 1.0f};
    Quaternion turn;
    sprite->localRotation_ = *turn.SetAxisAngle(axis, QuarterTurn());
}
} // namespace

void CUiSlider::SetRect(const RECT* rect) {
    rect_ = *rect;
    track_.top = rect->bottom - 10;
    track_.bottom = rect->bottom;
    track_.left = rect->left + 26;
    track_.right = rect->right - 26;

    CUiMesh* track = SceneSprite(this, 0);
    track->SetRectangle(depth_, 100.0f, 0.0f, 0.0f, 6.0f, static_cast<float>(rect->right - rect->left - 8), 8, 4, 0x20,
                        0xFC);
    track->Place(static_cast<float>((rect->right + rect->left) * static_cast<double>(ConstHalf())),
                 static_cast<float>(rect->bottom - 5), depth_);
    TurnQuarter(track);

    CUiMesh* thumb = SceneSprite(this, 1);
    thumb->SetRectangle(depth_ - ConstThumbLift(), 100.0f, 0.0f, 0.0f, 10.0f, 30.0f, 0x28, 8, 0x50, 0x78);
    TurnQuarter(thumb);

    CUiMesh* right = SceneSprite(this, 2);
    right->SetRectangle(depth_, 100.0f, 0.0f, 0.0f, 10.0f, 12.0f, 0x28, 0xB0, 0x50, 0x80);
    right->Place(static_cast<float>(rect->right - 6), static_cast<float>(rect->bottom - 5), depth_);
    TurnQuarter(right);

    CUiMesh* left = SceneSprite(this, 3);
    left->SetRectangle(depth_, 100.0f, 0.0f, 0.0f, 10.0f, 12.0f, 0x28, 0x80, 0x50, 0xB0);
    left->Place(static_cast<float>(rect->left + 6), static_cast<float>(rect->bottom - 5), depth_);
    TurnQuarter(left);

    // Regions (SetRect is Rebirth-patched: through its address).
    regions_.data[0]->CallSetRect(rect->left, rect->bottom - 10, rect->left + 12, rect->bottom);
    regions_.data[1]->CallSetRect(rect->right - 12, rect->bottom - 10, rect->right, rect->bottom);
    regions_.data[2]->CallSetRect(rect->left, rect->top, rect->right, rect->bottom - 10);

    // The position set again (one below first, so slot 25 sees a change).
    int position = position_;
    position_ = position - 1;
    CallSetPosition(position);
}
EI_REPLACE_METHOD(0x0065B8B0, CUiSlider::SetRect);
