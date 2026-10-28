// The options window's settings and key bindings and its slider's layout
// (ui.h: COptionsWindow, CUiSlider; docs/notes/ui.md "Objectives, save/load, options").
#include "ui.h"

#include "item_db_text.h"
#include "maths.h"
#include "world.h"

#include "ei/x87.h"
#include "lib/nan_audit.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include <cstddef>
#include <cstring>
#include <string>
#include "game_singletons.h"
#include "options.h"
#include "lib/str.h"

#include <cstdint>
#include <new>

// --- Helpers -----------------------------------------------------------------------------------

namespace {

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

float ConstOne() { return 1.0f /*0073B84C*/; }
float ConstFromStep() { return 50.0f /*0074023C*/; }   // value = setting * 50
float ConstToStep() { return 0.02f /*00742B80*/; }     // setting = value * 0.02
float ConstScrollFrom() { return 4.0f /*0073BAA0*/; } // setting 20's value
float ConstScrollTo() { return 0.25f /*0073C3B0*/; }   // setting 20 from it
float ConstHalf() { return 0.5f /*0073B86C*/; }       // the slider's centre
float ConstThumbLift() { return 0.1f /*0073B850*/; }  // the thumb above the track
constexpr float kQuarterTurn = /*007C8210*/ 1.5707964f;   // a static initialiser: pi * 0.5

// Setting 18: help windows (Rebirth's ForceShowTutorial patches its store).
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

CUiMesh* SceneSprite(CUiWindow* window, int index) { return window->SceneMesh(index); }

void DrawRowText(CUiSurface* surface, std::string_view text, RECT* rect, int font, COLORREF colour, int format) {
    surface->DrawTextIn(text, rect, font, colour, format, 1);
}

void ReadTip(std::string* text) {
    if (g_clientWorld->texts_->CTextManager::Open(text->c_str()) != 0) g_clientWorld->texts_->ReadToHash(text, 1);
}

void ShownName(std::string* out, const char* prefix, const char* name) {
    std::string text;
    if (g_clientWorld->texts_->GetString((std::string(prefix) + name).c_str(), &text))
        *out = text;
    else
        *out = name;
}

void RebuildKeysText(OptionBinding& binding) {
    binding.keysText_.clear();
    for (std::size_t k = 0; k < binding.keys_.size(); ++k) {
        binding.keysText_ += binding.keys_[k]->name_;
        binding.keysText_ += TextKeySeparator();
    }
    str::TrimRight(binding.keysText_, TextKeyTrim());
}

void TurnQuarter(CUiMesh* sprite) {
    Vector3 axis = {0.0f, 0.0f, 1.0f};
    Quaternion turn;
    sprite->localRotation_ = *turn.SetAxisAngle(axis, kQuarterTurn);
}

} // namespace

void COptionsWindow::RefreshRows() {
    CUiSurface* surface = surfaces_[0];
    UiSurfaceGetDC(surface);
    surface->Clear();
    SceneSprite(this, kHighlightSprite)->visible_ = 0;

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

void COptionsWindow::ShowGroup(int group) {
    std::string tip;
    if (group < 0)
        group = 0;
    else if (group >= kOptionGroups)
        group = kOptionGroups - 1;
    group_ = group;

    for (int row = 0; row < kOptionRows; ++row) {
        rows_[row].binding_ = nullptr;
        rows_[row].setting_ = nullptr;
        regions_[kFirstRowRegion + row]->SetTip(0, 1, 0);
    }
    for (int i = 0; i < kOptionDefinitionCount; ++i) {
        const OptionDefinition& definition = kOptionDefinitions[i];
        if (definition.group_ != group) continue;
        rows_[definition.row_].setting_ = &settings_[i];
        tip = str::Format(TextTipOption(), definition.name_);
        ReadTip(&tip);
        regions_[kFirstRowRegion + definition.row_]->SetTipString(tip, 1, 0);
    }
    for (int i = 0; i < kActionDefinitionCount; ++i) {
        const ActionDefinition& action = kActionDefinitions[i + 1];
        if (action.group_ != group) continue;
        rows_[action.row_].binding_ = &bindings_[i];
        tip = str::Format(TextTipAction(), action.name_);
        ReadTip(&tip);
        regions_[kFirstRowRegion + action.row_]->SetTipString(tip, 1, 0);
    }

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

void COptionsWindow::BuildSettings() {
    settings_.clear();   // copy and destructors inlined
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
            auto* slider = new CUiSlider(interface_, RECT{0, 0, 100, 100}, 3.0f, definition->maximum_, 1.0f);
            setting.bar_ = slider;
            RECT rect = {530, top + 163, 680, top + 173};
            slider->SetRect(&rect);   // (a failed allocation is called through, as in the original)
            AddChild(slider);
            setting.spin_ = nullptr;
        } else if (definition->kind_ == kSpinKind) {
            std::string text;
            text = TextOffDefault();
            g_clientWorld->texts_->GetString(TextOptionOff(), &text);
            if (i == kDifficultySetting) g_clientWorld->texts_->GetString(TextDifficultyHard(), &text);
            setting.spin_ = new CUiSpin(interface_, RECT{530, top + 160, 690, top + 176}, text.c_str());
            text = TextOnDefault();
            g_clientWorld->texts_->GetString(TextOptionOn(), &text);
            if (i == kDifficultySetting) g_clientWorld->texts_->GetString(TextDifficultyEasy(), &text);
            setting.spin_->AddText(text);
            AddChild(setting.spin_);
            setting.bar_ = nullptr;
        }
    }
}

void COptionsWindow::BuildBindings() {
    bindings_.clear();

    for (int i = 0; i < kActionDefinitionCount; ++i) {
        const ActionDefinition* action = &kActionDefinitions[i + 1];
        OptionBinding& binding = bindings_.emplace_back();
        binding.action_ = action;
        ShownName(&binding.name_, TextActionPrefix(), action->name_);

        binding.keys_.clear();
        for (int scan = 0; scan < kKeyActionCount; ++scan) {
            if (keyOfScan_[scan] != action->id_) continue;
            for (int k = 0; k < kUiKeyNameCount; ++k) {
                const UiKeyName* name = &g_keyNames[k];
                if (name->scan_ == scan && binding.keys_.size() < 1) binding.keys_.push_back(name);
            }
        }
        binding.keysText_.clear();
        for (std::size_t k = 0; k < binding.keys_.size(); ++k) {
            binding.keysText_ += binding.keys_[k]->name_;
            binding.keysText_ += TextKeySeparator();
        }
        str::TrimRight(binding.keysText_, TextKeyTrim());
    }
}

void COptionsWindow::CommitBinding() {
    if (!pressedKey_) return;
    OptionBinding* binding = rows_[selectedRow_].binding_;
    if (!binding) return;
    const int boundAction = keyOfScan_[pressedKey_->scan_];
    OptionBinding* other = nullptr;
    for (OptionBinding& b : bindings_) {
        if (b.action_->id_ == boundAction) {
            other = &b;
            break;
        }
    }
    if (binding != other) {
        if (binding->keys_.size() != 0) {
            for (const UiKeyName* key : binding->keys_) keyOfScan_[key->scan_] = 0;
            binding->keys_.clear();   // erase(begin, end): copies nothing
        }
        binding->keys_.push_back(pressedKey_);   // on overflow
        RebuildKeysText(*binding);
        if (other) {
            for (auto key = other->keys_.begin(); key != other->keys_.end(); ++key) {
                if (*key != pressedKey_) continue;
                other->keys_.erase(key);
                RebuildKeysText(*other);
                break;
            }
        }
    }
    keyOfScan_[pressedKey_->scan_] = static_cast<signed char>(binding->action_->id_);
}

void COptionsWindow::LoadSettings() {
    std::memcpy(keyOfScan_, g_keyActions, sizeof(keyOfScan_));
    OptionSetting* s = settings_.data();
    const double fromStep = ConstFromStep();
    const float one = ConstOne();
    s[0].value_ = options::g_options->audioSfx_;
    s[1].value_ = options::g_options->audioStreaming_;
    s[2].value_ = options::g_options->audioSpeech_;
    s[24].value_ = options::g_options->reverseStereo_;
    // _ftol: truncation.
    s[3].value_ = static_cast<int>(options::g_options->cameraScroll_.powerKeyboard * fromStep);
    s[4].value_ = static_cast<int>(options::g_options->cameraScroll_.powerMouse * fromStep);
    // Equal gives 0.
    float sign22 = options::g_options->cameraFlipX_;
    float sign23 = options::g_options->cameraFlipY_;
    EI_NAN_AUDIT(sign22, sign23);
    s[22].value_ = sign22 == one ? 0 : 1;
    s[23].value_ = sign23 == one ? 0 : 1;
    s[5].value_ = options::g_options->cameraBorderScrollArea_;
    s[6].value_ = options::g_options->cameraFrameSelectionArea_ / 10;
    s[20].value_ = static_cast<int>(options::g_options->toolTipTime_ * static_cast<double>(ConstScrollFrom()));
    s[7].value_ = options::g_options->unitShadow_;
    s[8].value_ = options::g_options->buildingShadow_;
    s[9].value_ = options::g_options->floraShadow_;
    s[10].value_ = options::g_options->enableBloodprints_;
    s[11].value_ = options::g_options->enableFootprints_;
    s[12].value_ = options::g_options->selectionType_;
    s[13].value_ = options::g_options->drawPath_;
    s[17].value_ = options::g_options->drawFlyingHp_;
    s[18].value_ = options::g_options->showTutorial_;
    s[19].value_ = options::g_options->autosave_;
    s[21].value_ = options::g_options->switchFilters_;
    s[14].value_ = static_cast<int>((static_cast<double>(options::g_options->brightness_) + one) * fromStep);
    s[15].value_ = static_cast<int>((static_cast<double>(options::g_options->contrast_) + one) * fromStep);
    s[16].value_ = static_cast<int>((static_cast<double>(options::g_options->gamma_) + one) * fromStep);
    s[25].value_ = options::g_options->difficulty_;

    for (int i = 0; i < kOptionDefinitionCount; ++i) {
        if (s[i].bar_)
            s[i].bar_->SetPosition(s[i].value_);
        else if (s[i].spin_)
            s[i].spin_->SetValue(s[i].value_);
    }
}

void COptionsWindow::Apply() {
    std::memcpy(g_keyActions, keyOfScan_, sizeof(keyOfScan_));
    const OptionSetting* s = settings_.data();
    const double toStep = ConstToStep();
    const float one = ConstOne();
    options::g_options->audioSfx_ = s[0].value_;
    options::g_options->audioStreaming_ = s[1].value_;
    options::g_options->audioSpeech_ = s[2].value_;
    options::g_options->reverseStereo_ = s[24].value_;
    // Each product is rounded once by its float stores.
    float speedA = static_cast<float>(s[3].value_ * toStep);
    float speedB = static_cast<float>(s[4].value_ * toStep);
    options::g_options->cameraScroll_.powerKeyboard = speedA;
    options::g_options->cameraPitch_.powerKeyboard = speedA;
    options::g_options->cameraZoom_.powerKeyboard = speedA;
    options::g_options->cameraRotate_.powerKeyboard = speedA;
    options::g_options->cameraScroll_.powerMouse = speedB;
    options::g_options->cameraPitch_.powerMouse = speedB;
    options::g_options->cameraZoom_.powerMouse = speedB;
    options::g_options->cameraRotate_.powerMouse = speedB;
    options::g_options->cameraFlipX_ = s[22].value_ == 1 ? -1.0f : 1.0f;
    options::g_options->cameraFlipY_ = s[23].value_ == 1 ? -1.0f : 1.0f;
    options::g_options->cameraBorderScrollArea_ = s[5].value_;
    options::g_options->cameraFrameSelectionArea_ = s[6].value_ * 10;
    options::g_options->toolTipTime_ = static_cast<float>(s[20].value_ * static_cast<double>(ConstScrollTo()));
    options::g_options->unitShadow_ = s[7].value_;
    options::g_options->buildingShadow_ = s[8].value_;
    options::g_options->floraShadow_ = s[9].value_;
    options::g_options->enableBloodprints_ = s[10].value_;
    options::g_options->enableFootprints_ = s[11].value_;
    options::g_options->selectionType_ = s[12].value_;
    options::g_options->drawPath_ = s[13].value_;
    options::g_options->drawFlyingHp_ = s[17].value_;
    options::g_options->showTutorial_ = s[18].value_;
    options::g_options->autosave_ = s[19].value_;
    options::g_options->switchFilters_ = s[21].value_;
    options::g_options->brightness_ = static_cast<float>(s[14].value_ * toStep - one);
    options::g_options->contrast_ = static_cast<float>(s[15].value_ * toStep - one);
    options::g_options->gamma_ = static_cast<float>(s[16].value_ * toStep - one);
    if (!IsMultiplayerClient()) {
        options::g_options->difficultyStored_ = s[25].value_;
        options::g_options->difficulty_ = s[25].value_;
    }
    if (shadowsChanged_) g_clientWorld->DropShadows();
}

// --- The slider's virtual ------------------------------------------------------------------

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

    regions_[0]->SetRect(rect->left, rect->bottom - 10, rect->left + 12, rect->bottom);
    regions_[1]->SetRect(rect->right - 12, rect->bottom - 10, rect->right, rect->bottom);
    regions_[2]->SetRect(rect->left, rect->top, rect->right, rect->bottom - 10);

    // The position set again (one below first, so the change virtual sees a change).
    int position = position_;
    position_ = position - 1;
    SetPosition(position);
}
