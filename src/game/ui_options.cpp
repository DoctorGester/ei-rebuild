// The options window's settings and key bindings and its slider's layout
// (ui.h: OptionsWindow, UiSlider; docs/notes/ui.md "Objectives, save/load, options").
#include "ui.h"

#include "text_manager.h"
#include "maths.h"
#include "world.h"

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

constexpr const char* kTextOptionPrefix = "option_";
constexpr const char* kTextActionPrefix = "action_";
constexpr const char* kTextTipOption = "tip option_%s";
constexpr const char* kTextTipAction = "tip action_%s";
constexpr const char* kTextOptionOff = "option_off";
constexpr const char* kTextOptionOn = "option_on";
constexpr const char* kTextOffDefault = "OFF";
constexpr const char* kTextOnDefault = "ON";
constexpr const char* kTextDifficultyHard = "difficulty_hard";
constexpr const char* kTextDifficultyEasy = "difficulty_easy";
constexpr const char* kTextKeySeparator = "  |  ";    // after each key name
constexpr const char* kTextKeyTrim = " |";         // trimmed from the right

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

constexpr float kFromStep = 50.0f;   // value = setting * 50
constexpr float kToStep = 0.02f;     // setting = value * 0.02
constexpr float kScrollFrom = 4.0f; // setting 20's value
constexpr float kScrollTo = 0.25f;   // setting 20 from it
constexpr float kThumbLift = 0.1f;  // the thumb above the track
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

// The text `key` up to the next '#', or the key itself without one.
std::string TipText(const std::string& key) {
    std::string tip = key;
    if (g_clientWorld.texts->TextManager::Open(key.c_str())) tip = g_clientWorld.texts->ReadToHash();
    return tip;
}

// The text `prefix` + `name`, or the name itself without one.
std::string ShownName(const char* prefix, const char* name) {
    return g_clientWorld.texts->FindString(std::string(prefix) + name).value_or(name);
}

void RebuildKeysText(OptionBinding& binding) {
    binding.keysText.clear();
    for (std::size_t k = 0; k < binding.keys.size(); ++k) {
        binding.keysText += binding.keys[k]->name;
        binding.keysText += kTextKeySeparator;
    }
    str::TrimRight(binding.keysText, kTextKeyTrim);
}

void TurnQuarter(UiMesh* sprite) {
    Vector3 axis = {0.0f, 0.0f, 1.0f};
    Quaternion turn;
    sprite->localRotation = *turn.SetAxisAngle(axis, kQuarterTurn);
}

} // namespace

void OptionsWindow::RefreshRows() {
    UiSurface* surface = surfaces[0];
    surface->Clear();
    SceneMesh(kHighlightSprite)->visible = false;

    RECT title = {10, 60, 140, 80};
    for (int groupIndex = 0; groupIndex < kOptionGroups; ++groupIndex) {
        UiMesh* button = SceneMesh(kFirstGroupSprite + groupIndex);
        float tint = groupIndex == group ? 1.0f : 0.5f;
        button->tint[0] = tint;
        button->tint[1] = tint;
        button->tint[2] = tint;
        surface->DrawTextIn(groupNames[groupIndex], &title, 1, groupIndex == group ? kColourText : kColourDim, 1, 1);
        gameapi::OffsetRect(&title, 0, 24);
    }
    RECT heading = {0, 10, 600, 34};
    surface->DrawTextIn(groupNames[group], &heading, 2, kColourText, 1, 1);

    RECT name = {160, 60, 420, 80};
    RECT keys = {430, 60, 590, 80};
    for (int row = 0, y = 168; y < 504; ++row, y += 24) {
        if (row == selectedRow) {
            UiMesh* highlight = SceneMesh(kHighlightSprite);
            highlight->visible = true;
            highlight->Place(471.0f, static_cast<float>(y), 3.0f);
        }
        if (OptionBinding* binding = rows[row].binding) {
            COLORREF colour = row == selectedRow && waitingForKey ? kColourWaiting : kColourText;
            surface->DrawTextIn(binding->name, &name, 1, colour, 0, 1);
            surface->DrawTextIn(binding->keysText, &keys, 1, colour, 0, 1);
        } else if (OptionSetting* setting = rows[row].setting) {
            surface->DrawTextIn(setting->text, &name, 1, kColourText, 0, 1);
        }
        gameapi::OffsetRect(&name, 0, 24);
        gameapi::OffsetRect(&keys, 0, 24);
    }
}

void OptionsWindow::ShowGroup(int groupIndex) {
    if (groupIndex < 0)
        groupIndex = 0;
    else if (groupIndex >= kOptionGroups)
        groupIndex = kOptionGroups - 1;
    group = groupIndex;

    for (int row = 0; row < kOptionRows; ++row) {
        rows[row].binding = nullptr;
        rows[row].setting = nullptr;
        regions[kFirstRowRegion + row]->SetTip(0);
    }
    for (int i = 0; i < kOptionDefinitionCount; ++i) {
        const OptionDefinition& definition = kOptionDefinitions[i];
        if (definition.group != groupIndex) continue;
        rows[definition.row].setting = &settings[i];
        regions[kFirstRowRegion + definition.row]->SetTipString(TipText(str::Format(kTextTipOption, definition.name)));
    }
    for (int i = 0; i < kActionDefinitionCount; ++i) {
        const ActionDefinition& action = kActionDefinitions[i + 1];
        if (action.group != groupIndex) continue;
        rows[action.row].binding = &bindings[i];
        regions[kFirstRowRegion + action.row]->SetTipString(TipText(str::Format(kTextTipAction, action.name)));
    }

    for (int i = 0; i < kOptionDefinitionCount; ++i)
        if (settings[i].bar) settings[i].bar->SetEnabled(false);
    for (int i = 0; i < kOptionDefinitionCount; ++i)
        if (settings[i].spin) settings[i].spin->SetEnabled(false);
    for (int row = 0; row < kOptionRows; ++row) {
        OptionSetting* setting = rows[row].setting;
        if (!setting) continue;
        if (setting->bar) setting->bar->SetEnabled(true);
        if (setting->spin) setting->spin->SetEnabled(true);
    }

    selectedRow = -1;
    int row = 0;
    while (row < kOptionRows && !rows[row].binding && !rows[row].setting) ++row;
    if (row < kOptionRows) {
        selectedRow = row;
        RefreshRows();
    }
    if (selectedRow == -1) selectedRow = 0;
    waitingForKey = false;
    RefreshRows();
}

void OptionsWindow::BuildSettings() {
    settings.clear();    // copy and destructors inlined
    for (int i = 0; i < kOptionDefinitionCount; ++i) {
        // The original's temporary has only its string constructed; its other fields are copied
        // as the stack left them and all written below for both kinds.
        OptionSetting blank{};
        settings.push_back(blank);
        OptionSetting& setting = settings.back();
        const OptionDefinition* definition = &kOptionDefinitions[i];
        setting.value = 0;
        setting.definition = definition;
        setting.text = ShownName(kTextOptionPrefix, definition->name);

        int top = definition->row * 24;
        if (definition->kind == kSliderKind) {
            auto* slider = new UiSlider(ui, RECT{0, 0, 100, 100}, 3.0f, definition->maximum, 1.0f);
            setting.bar = slider;
            RECT area = {530, top + 163, 680, top + 173};
            slider->SetRect(&area);   // (a failed allocation is called through, as in the original)
            AddChild(slider);
            setting.spin = nullptr;
        } else if (definition->kind == kSpinKind) {
            std::string text = kTextOffDefault;
            text = g_clientWorld.texts->GetString(kTextOptionOff);
            if (i == kDifficultySetting) text = g_clientWorld.texts->GetString(kTextDifficultyHard);
            setting.spin = new UiSpin(ui, RECT{530, top + 160, 690, top + 176}, text.c_str());
            text = kTextOnDefault;
            text = g_clientWorld.texts->GetString(kTextOptionOn);
            if (i == kDifficultySetting) text = g_clientWorld.texts->GetString(kTextDifficultyEasy);
            setting.spin->AddText(text);
            AddChild(setting.spin);
            setting.bar = nullptr;
        }
    }
}

void OptionsWindow::BuildBindings() {
    bindings.clear();

    for (int i = 0; i < kActionDefinitionCount; ++i) {
        const ActionDefinition* action = &kActionDefinitions[i + 1];
        OptionBinding& binding = bindings.emplace_back();
        binding.action = action;
        binding.name = ShownName(kTextActionPrefix, action->name);

        binding.keys.clear();
        for (int scan = 0; scan < kKeyActionCount; ++scan) {
            if (keyOfScan[scan] != action->id) continue;
            for (int k = 0; k < kUiKeyNameCount; ++k) {
                const UiKeyName* name = &g_keyNames[k];
                if (name->scan == scan && binding.keys.size() < 1) binding.keys.push_back(name);
            }
        }
        RebuildKeysText(binding);
    }
}

void OptionsWindow::CommitBinding() {
    if (!pressedKey) return;
    OptionBinding* binding = rows[selectedRow].binding;
    if (!binding) return;
    const int boundAction = keyOfScan[pressedKey->scan];
    OptionBinding* other = nullptr;
    for (OptionBinding& b : bindings) {
        if (b.action->id == boundAction) {
            other = &b;
            break;
        }
    }
    if (binding != other) {
        if (binding->keys.size() != 0) {
            for (const UiKeyName* key : binding->keys) keyOfScan[key->scan] = 0;
            binding->keys.clear();    // erase(begin, end): copies nothing
        }
        binding->keys.push_back(pressedKey);     // on overflow
        RebuildKeysText(*binding);
        if (other) {
            for (auto key = other->keys.begin(); key != other->keys.end(); ++key) {
                if (*key != pressedKey) continue;
                other->keys.erase(key);
                RebuildKeysText(*other);
                break;
            }
        }
    }
    keyOfScan[pressedKey->scan] = static_cast<signed char>(binding->action->id);
}

void OptionsWindow::LoadSettings() {
    std::memcpy(keyOfScan, g_keyActions, sizeof(keyOfScan));
    OptionSetting* s = settings.data();
    const float fromStep = kFromStep;
    const float one = 1.0f;
    s[0].value = options::g_options.audioSfx;
    s[1].value = options::g_options.audioStreaming;
    s[2].value = options::g_options.audioSpeech;
    s[24].value = options::g_options.reverseStereo;
    // _ftol: truncation.
    s[3].value = static_cast<int>(options::g_options.cameraScroll.powerKeyboard * fromStep);
    s[4].value = static_cast<int>(options::g_options.cameraScroll.powerMouse * fromStep);
    // Equal gives 0.
    float sign22 = options::g_options.cameraFlipX;
    float sign23 = options::g_options.cameraFlipY;
    EI_NAN_AUDIT(sign22, sign23);
    s[22].value = sign22 == one ? 0 : 1;
    s[23].value = sign23 == one ? 0 : 1;
    s[5].value = options::g_options.cameraBorderScrollArea;
    s[6].value = options::g_options.cameraFrameSelectionArea / 10;
    s[20].value = static_cast<int>(options::g_options.toolTipTime * static_cast<double>(kScrollFrom));
    s[7].value = options::g_options.unitShadow;
    s[8].value = options::g_options.buildingShadow;
    s[9].value = options::g_options.floraShadow;
    s[10].value = options::g_options.enableBloodprints;
    s[11].value = options::g_options.enableFootprints;
    s[12].value = options::g_options.selectionType;
    s[13].value = options::g_options.drawPath;
    s[17].value = options::g_options.drawFlyingHp;
    s[18].value = options::g_options.showTutorial;
    s[19].value = options::g_options.autosave;
    s[21].value = options::g_options.switchFilters;
    s[14].value = static_cast<int>((static_cast<double>(options::g_options.brightness) + one) * fromStep);
    s[15].value = static_cast<int>((static_cast<double>(options::g_options.contrast) + one) * fromStep);
    s[16].value = static_cast<int>((static_cast<double>(options::g_options.gamma) + one) * fromStep);
    s[25].value = options::g_options.difficulty;

    for (int i = 0; i < kOptionDefinitionCount; ++i) {
        if (s[i].bar)
            s[i].bar->SetPosition(s[i].value);
        else if (s[i].spin)
            s[i].spin->SetValue(s[i].value);
    }
}

void OptionsWindow::Apply() {
    std::memcpy(g_keyActions, keyOfScan, sizeof(keyOfScan));
    const OptionSetting* s = settings.data();
    const float toStep = kToStep;
    const float one = 1.0f;
    options::g_options.audioSfx = s[0].value;
    options::g_options.audioStreaming = s[1].value;
    options::g_options.audioSpeech = s[2].value;
    options::g_options.reverseStereo = s[24].value;
    // Each product is rounded once by its float stores.
    float speedA = s[3].value * toStep;
    float speedB = s[4].value * toStep;
    options::g_options.cameraScroll.powerKeyboard = speedA;
    options::g_options.cameraPitch.powerKeyboard = speedA;
    options::g_options.cameraZoom.powerKeyboard = speedA;
    options::g_options.cameraRotate.powerKeyboard = speedA;
    options::g_options.cameraScroll.powerMouse = speedB;
    options::g_options.cameraPitch.powerMouse = speedB;
    options::g_options.cameraZoom.powerMouse = speedB;
    options::g_options.cameraRotate.powerMouse = speedB;
    options::g_options.cameraFlipX = s[22].value == 1 ? -1.0f : 1.0f;
    options::g_options.cameraFlipY = s[23].value == 1 ? -1.0f : 1.0f;
    options::g_options.cameraBorderScrollArea = s[5].value;
    options::g_options.cameraFrameSelectionArea = s[6].value * 10;
    options::g_options.toolTipTime = s[20].value * kScrollTo;
    options::g_options.unitShadow = s[7].value;
    options::g_options.buildingShadow = s[8].value;
    options::g_options.floraShadow = s[9].value;
    options::g_options.enableBloodprints = s[10].value;
    options::g_options.enableFootprints = s[11].value;
    options::g_options.selectionType = s[12].value;
    options::g_options.drawPath = s[13].value;
    options::g_options.drawFlyingHp = s[17].value;
    options::g_options.showTutorial = s[18].value;
    options::g_options.autosave = s[19].value;
    options::g_options.switchFilters = s[21].value;
    options::g_options.brightness = s[14].value * toStep - one;
    options::g_options.contrast = s[15].value * toStep - one;
    options::g_options.gamma = s[16].value * toStep - one;
    if (!IsMultiplayerClient()) {
        options::g_options.difficultyStored = s[25].value;
        options::g_options.difficulty = s[25].value;
    }
    if (shadowsChanged) g_clientWorld.DropShadows();
}

// --- The slider's virtual ------------------------------------------------------------------

void UiSlider::SetRect(const RECT* newRect) {
    rect = *newRect;
    track.top = newRect->bottom - 10;
    track.bottom = newRect->bottom;
    track.left = newRect->left + 26;
    track.right = newRect->right - 26;

    UiMesh* trackMesh = SceneMesh(0);
    trackMesh->SetRectangle(depth, 100.0f, 0.0f, 0.0f, 6.0f, static_cast<float>(newRect->right - newRect->left - 8), 8, 4, 0x20,
                        0xFC);
    trackMesh->Place((newRect->right + newRect->left) * 0.5f,
                 static_cast<float>(newRect->bottom - 5), depth);
    TurnQuarter(trackMesh);

    UiMesh* thumbMesh = SceneMesh(1);
    thumbMesh->SetRectangle(depth - kThumbLift, 100.0f, 0.0f, 0.0f, 10.0f, 30.0f, 0x28, 8, 0x50, 0x78);
    TurnQuarter(thumbMesh);

    UiMesh* right = SceneMesh(2);
    right->SetRectangle(depth, 100.0f, 0.0f, 0.0f, 10.0f, 12.0f, 0x28, 0xB0, 0x50, 0x80);
    right->Place(static_cast<float>(newRect->right - 6), static_cast<float>(newRect->bottom - 5), depth);
    TurnQuarter(right);

    UiMesh* left = SceneMesh(3);
    left->SetRectangle(depth, 100.0f, 0.0f, 0.0f, 10.0f, 12.0f, 0x28, 0x80, 0x50, 0xB0);
    left->Place(static_cast<float>(newRect->left + 6), static_cast<float>(newRect->bottom - 5), depth);
    TurnQuarter(left);

    regions[0]->SetRect(newRect->left, newRect->bottom - 10, newRect->left + 12, newRect->bottom);
    regions[1]->SetRect(newRect->right - 12, newRect->bottom - 10, newRect->right, newRect->bottom);
    regions[2]->SetRect(newRect->left, newRect->top, newRect->right, newRect->bottom - 10);

    // The position set again (one below first, so the change virtual sees a change).
    int currentPosition = position;
    position = currentPosition - 1;
    SetPosition(currentPosition);
}
