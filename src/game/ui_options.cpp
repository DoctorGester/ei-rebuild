// The options window's settings and key bindings and its slider's layout
// (ui.h: OptionsWindow, UiSlider; docs/notes/ui.md "Objectives, save/load, options").
#include "ui.h"

#include "app.h"
#include "texture.h"
#include "text_manager.h"
#include "lib/vecmath.h"
#include "world.h"

#include "lib/crt.h"
#include "lib/rect.h"
#include <cstddef>
#include <cstring>
#include <string>
#include "game_singletons.h"
#include "options.h"
#include "settings.h"
#include "lib/str.h"
#include "render/render.h"
#include "platform/display_sizes.h"
#include "platform/platform.h"

#include <algorithm>
#include <cstdint>
#include <new>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

// --- Helpers -----------------------------------------------------------------------------------

namespace {

constexpr const char* kTextOptionPrefix = "option_";
constexpr const char* kTextActionPrefix = "action_";
constexpr const char* kTextTipOption = "tip option_%s";
constexpr const char* kTextTipAction = "tip action_%s";
constexpr const char* kTextOptionOff = "option_off";
constexpr const char* kTextOptionOn = "option_on";
constexpr const char* kTextDifficultyHard = "difficulty_hard";
constexpr const char* kTextDifficultyEasy = "difficulty_easy";
constexpr const char* kTextKeySeparator = "  |  ";    // after each key name
constexpr const char* kTextKeyTrim = " |";         // trimmed from the right

constexpr int kSliderKind = 0;
constexpr int kSpinKind = 1;
constexpr int kResolutionKind = 2;
constexpr int kChoiceKind = 3;

constexpr std::uint32_t kColourDim = 0xA0A0A0;
constexpr std::uint32_t kColourWaiting = 0x0000FF;   // a binding waiting for its key (red)

constexpr float kFromStep = 50.0f;   // value = setting * 50
constexpr float kToStep = 0.02f;     // setting = value * 0.02
constexpr float kScrollFrom = 4.0f; // setting 15's value
constexpr float kScrollTo = 0.25f;   // setting 15 from it
constexpr float kThumbLift = 0.1f;  // the thumb above the track
constexpr float kQuarterTurn = /*007C8210*/ 1.5707964f;   // a static initialiser: pi * 0.5

// By OptionId. kOptionShowTutorial: help windows (Rebirth's ForceShowTutorial patches its store).
constexpr OptionDefinition kOptionDefinitions[kOptionDefinitionCount] = {
    {"volume_sfx", 0, 100, 1, 0},
    {"volume_stream", 0, 100, 1, 1},
    {"volume_voice", 0, 100, 1, 2},
    {"power_kbd", 0, 100, 2, 0},
    {"power_mouse", 0, 100, 2, 1},
    {"scroll_border", 0, 10, 2, 2},
    {"rubber_select", 0, 10, 2, 3},
    {"shadow_other", 1, 2, 0, 0},   // the one shadows switch, with the original's "all shadows" texts
    {"select_type", 1, 2, 0, 2},
    {"show_path", 1, 2, 0, 3},
    {"brightness", 0, 100, 0, 6},
    {"gamma", 0, 100, 0, 7},
    {"show_flying_hp", 1, 2, 0, 4},
    {"show_tutorial", 1, 2, 10, 11},
    {"autosave", 1, 2, 3, 11},
    {"tooltip_time", 0, 20, 2, 4},
    {"switch_filters", 1, 2, 3, 12},
    {"camera_reverse_x", 1, 2, 2, 6},
    {"camera_reverse_y", 1, 2, 2, 7},
    {"reverse_stereo", 1, 2, 1, 4},
    {"difficulty", 1, 2, 3, 0},   // its spin control says hard / easy
    {"resolution", 2, 0, 0, 9},   // ours from here: the rendering resolution (app::ChooseResolution)
    {"display_mode", 3, 0, 0, 8},
    {"vsync", 1, 2, 0, 10},
    {"msaa", 3, 0, 0, 11},
    {"anisotropy", 3, 0, 0, 12},
    {"perf_overlay", 1, 2, 0, 13},
};

// A choice row's steps (kind 3): each one's text key (else the text itself) and the value it stands for, ascending.
struct Choice {
    const char* text = nullptr;
    int value = 0;
};
constexpr Choice kDisplayModes[] = {{"option_window", 0}, {"option_full_screen", 1}};
constexpr Choice kMsaaLevels[] = {{kTextOptionOff, 1}, {"2x", 2}, {"4x", 4}, {"8x", 8}};
constexpr Choice kAnisotropyLevels[] = {{kTextOptionOff, 1}, {"2x", 2}, {"4x", 4}, {"8x", 8}, {"16x", 16}};

std::span<const Choice> ChoicesOf(int setting) {
    if (setting == kOptionMsaa) return kMsaaLevels;
    if (setting == kOptionAnisotropy) return kAnisotropyLevels;
    return kDisplayModes;
}

// The step of the last choice at or below `value` (the first when none is).
int ChoiceIndex(std::span<const Choice> choices, int value) {
    int index = 0;
    for (int i = 0; i < static_cast<int>(choices.size()); ++i)
        if (choices[i].value <= value) index = i;
    return index;
}

// The texts of what the rebuild adds to the options, which the game's texts lack: in Russian where the game's are
// (the video group's title holds letters beyond ASCII), else in English. A text the game's files have wins.
struct AddedText {
    const char* key = nullptr;
    const char* russian = nullptr;
    const char* english = nullptr;
};
constexpr AddedText kAddedTexts[] = {
    {"option_resolution", "Разрешение", "Resolution"},
    {"tip option_resolution", "Размер изображения игры", "The size of the game's picture"},
    {"option_display_mode", "Режим экрана", "Display mode"},
    {"tip option_display_mode", "Весь экран: окно без рамки на весь монитор, режим монитора не меняется. В окне: окно с "
     "рамкой размером с разрешение",
     "Full screen: a borderless window over the whole monitor, the display mode never changes. Window: a window of "
     "the resolution's size"},
    {"option_window", "В окне", "Window"},
    {"option_full_screen", "Весь экран", "Full screen"},
    {"option_vsync", "Вертикальная синхронизация", "Vertical sync"},
    {"tip option_vsync", "Каждый кадр ждёт обновления монитора: без разрывов изображения",
     "Each frame waits for the monitor's refresh: no tearing"},
    {"option_msaa", "Сглаживание", "Antialiasing"},
    {"tip option_msaa", "Гладкие края моделей и земли (мультисэмплинг)",
     "Smoother edges on models and the ground (multisampling)"},
    {"option_anisotropy", "Анизотропная фильтрация", "Anisotropic filtering"},
    {"tip option_anisotropy", "Чёткие текстуры на поверхностях под углом, например на земле вдали",
     "Sharper textures on surfaces seen at a slant, such as the ground into the distance"},
    {"option_perf_overlay", "Счётчик производительности", "Performance overlay"},
    {"tip option_perf_overlay", "Кадры в секунду, время кадра и число отрисовок поверх игры",
     "Frames per second, frame times and draw counts over the game"},
};

std::optional<std::string> FindAddedText(std::string_view key) {
    for (const AddedText& added : kAddedTexts) {
        if (key != added.key) continue;
        const std::string title = g_clientWorld.texts->GetString("option_group_video");
        const bool russian = std::any_of(title.begin(), title.end(), [](char c) { return (c & 0x80) != 0; });
        return russian ? added.russian : added.english;
    }
    return std::nullopt;
}

// The screen's size (the rendering resolution).
Size ScreenSize() {
    const Rect& screen = g_sceneRenderer.device->screen;
    return Size{screen.right, screen.bottom};
}

// The text `key` up to the next '#', else an added text, or the key itself without one.
std::string TipText(const std::string& key) {
    std::string tip = key;
    if (g_clientWorld.texts->TextManager::Open(key.c_str())) tip = g_clientWorld.texts->ReadToHash();
    else if (std::optional<std::string> added = FindAddedText(key)) tip = *added;
    return tip;
}

// The text `prefix` + `name`, else an added text, or the name itself without one.
std::string ShownName(const char* prefix, const char* name) {
    const std::string key = std::string(prefix) + name;
    if (std::optional<std::string> text = g_clientWorld.texts->FindString(key)) return *text;
    return FindAddedText(key).value_or(name);
}

void RebuildKeysText(OptionBinding& binding) {
    binding.keysText.clear();
    if (binding.key) {
        binding.keysText += binding.key->name;
        binding.keysText += kTextKeySeparator;
    }
    str::TrimRight(binding.keysText, kTextKeyTrim);
}

void TurnQuarter(UiMesh* sprite) {
    Vector3 axis = {0.0f, 0.0f, 1.0f};
    sprite->localRotation = Quaternion::AxisAngle(axis, kQuarterTurn);
}

} // namespace

void OptionsWindow::RefreshRows() {
    highlight->visible = false;
    for (int groupIndex = 0; groupIndex < kGroups; ++groupIndex) {
        UiMesh* button = groupIcons[groupIndex];
        float tint = groupIndex == group ? 1.0f : 0.5f;
        button->tint[0] = tint;
        button->tint[1] = tint;
        button->tint[2] = tint;
    }
    for (int row = 0, y = 168; y < 504; ++row, y += 24) {
        if (row == selectedRow) {
            highlight->visible = true;
            highlight->Place(471.0f, static_cast<float>(y), 3.0f);
        }
    }
}

void OptionsWindow::PaintRows(UiSurface& surface) const {
    Rect title = {10, 60, 140, 80};
    for (int groupIndex = 0; groupIndex < kGroups; ++groupIndex) {
        surface.DrawTextIn(groupNames[groupIndex], title, TextStyle{1, groupIndex == group ? kUiTextColour : kColourDim, render::text::kCenter});
        title.Offset(0, 24);
    }
    Rect heading = {0, 10, 600, 34};
    surface.DrawTextIn(groupNames[group], heading, TextStyle{2, kUiTextColour, render::text::kCenter});

    Rect name = {160, 60, 420, 80};
    Rect keys = {430, 60, 590, 80};
    for (int row = 0, y = 168; y < 504; ++row, y += 24) {
        if (const OptionBinding* binding = rows[row].binding) {
            std::uint32_t colour = row == selectedRow && waitingForKey ? kColourWaiting : kUiTextColour;
            surface.DrawTextIn(binding->name, name, TextStyle{1, colour});
            surface.DrawTextIn(binding->keysText, keys, TextStyle{1, colour});
        } else if (const OptionSetting* setting = rows[row].setting) {
            surface.DrawTextIn(setting->text, name, TextStyle{1, kUiTextColour});
        }
        name.Offset(0, 24);
        keys.Offset(0, 24);
    }
}

void OptionsWindow::ShowGroup(int groupIndex) {
    if (groupIndex < 0)
        groupIndex = 0;
    else if (groupIndex >= kGroups)
        groupIndex = kGroups - 1;
    group = groupIndex;

    for (int row = 0; row < kRows; ++row) {
        rows[row].binding = nullptr;
        rows[row].setting = nullptr;
        rowAreas[row]->SetTip(0);
    }
    for (int i = 0; i < kOptionDefinitionCount; ++i) {
        const OptionDefinition& definition = kOptionDefinitions[i];
        if (definition.group != groupIndex) continue;
        rows[definition.row].setting = &settings[i];
        rowAreas[definition.row]->SetTipString(
            TipText(str::Format(kTextTipOption, definition.name)));
    }
    for (int i = 0; i < kActionDefinitionCount; ++i) {
        const ActionDefinition& action = kActionDefinitions[i + 1];
        if (action.group != groupIndex) continue;
        rows[action.row].binding = &bindings[i];
        rowAreas[action.row]->SetTipString(TipText(str::Format(kTextTipAction, action.name)));
    }

    for (const OptionSetting& setting : settings) {
        if (setting.bar) setting.bar->SetEnabled(false);
        if (setting.spin) setting.spin->SetEnabled(false);
        if (setting.list) setting.list->SetEnabled(false);
    }
    for (int row = 0; row < kRows; ++row) {
        OptionSetting* setting = rows[row].setting;
        if (!setting) continue;
        if (setting->bar) setting->bar->SetEnabled(true);
        if (setting->spin) setting->spin->SetEnabled(true);
        if (setting->list) setting->list->SetEnabled(true);
    }

    int row = 0;
    while (row < kRows && !rows[row].binding && !rows[row].setting) ++row;
    selectedRow = row < kRows ? row : 0;
    waitingForKey = false;
    RefreshRows();
}

void OptionsWindow::BuildSettings() {
    settings.clear();    // copy and destructors inlined
    for (int i = 0; i < kOptionDefinitionCount; ++i) {
        OptionSetting& setting = settings.emplace_back();
        const OptionDefinition* definition = &kOptionDefinitions[i];
        setting.text = ShownName(kTextOptionPrefix, definition->name);

        int top = definition->row * 24;
        if (definition->kind == kSliderKind) {
            auto* slider = AddChild(std::make_unique<UiSlider>(ui, Rect{0, 0, 100, 100}, 3.0f, definition->maximum, 1.0f));
            setting.bar = slider;
            Rect area = {530, top + 163, 680, top + 173};
            slider->SetRect(&area);
        } else if (definition->kind == kSpinKind) {
            std::string text = g_clientWorld.texts->GetString(kTextOptionOff);
            if (i == kOptionDifficulty) text = g_clientWorld.texts->GetString(kTextDifficultyHard);
            setting.spin = AddChild(std::make_unique<UiSpin>(ui, Rect{530, top + 160, 690, top + 176}, text.c_str()));
            text = g_clientWorld.texts->GetString(kTextOptionOn);
            if (i == kOptionDifficulty) text = g_clientWorld.texts->GetString(kTextDifficultyEasy);
            setting.spin->AddText(text);
        } else if (definition->kind == kResolutionKind) {
            resolutions = platform::DisplaySizes(g_sceneRenderer.device->window);
            platform::AddSize(resolutions, ScreenSize());
            setting.list = AddChild(std::make_unique<UiDropList>(ui, this, Rect{530, top + 160, 690, top + 176}));
            for (const Size size : resolutions) setting.list->AddText(str::Format("%d x %d", size.w, size.h));
        } else if (definition->kind == kChoiceKind) {
            const std::span<const Choice> choices = ChoicesOf(i);
            setting.spin = AddChild(std::make_unique<UiSpin>(ui, Rect{530, top + 160, 690, top + 176}, ShownName("", choices[0].text).c_str()));
            for (const Choice& choice : choices.subspan(1)) setting.spin->AddText(ShownName("", choice.text));
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

        for (int scan = 0; scan < kKeyActionCount; ++scan) {
            if (keyOfScan[scan] != action->id) continue;
            for (int k = 0; k < kUiKeyNameCount; ++k) {
                const UiKeyName* name = &g_keyNames[k];
                if (name->scan == scan && !binding.key) binding.key = name;
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
        if (binding->key) keyOfScan[binding->key->scan] = 0;
        binding->key = pressedKey;
        RebuildKeysText(*binding);
        if (other && other->key == pressedKey) {
            other->key = nullptr;
            RebuildKeysText(*other);
        }
    }
    keyOfScan[pressedKey->scan] = static_cast<signed char>(binding->action->id);
}

void OptionsWindow::LoadSettings() {
    std::memcpy(keyOfScan, g_keyActions, sizeof(keyOfScan));
    OptionSetting* s = settings.data();
    const options::GameOptions& o = options::g_options;
    s[kOptionSfxVolume].value = o.audioSfx;
    s[kOptionStreamVolume].value = o.audioStreaming;
    s[kOptionVoiceVolume].value = o.audioSpeech;
    s[kOptionReverseStereo].value = o.reverseStereo;
    // _ftol: truncation.
    s[kOptionKeyboardPower].value = static_cast<int>(o.cameraScroll.powerKeyboard * kFromStep);
    s[kOptionMousePower].value = static_cast<int>(o.cameraScroll.powerMouse * kFromStep);
    s[kOptionCameraReverseX].value = o.cameraFlipX == 1.0f ? 0 : 1;
    s[kOptionCameraReverseY].value = o.cameraFlipY == 1.0f ? 0 : 1;
    s[kOptionScrollBorder].value = o.cameraBorderScrollArea;
    s[kOptionRubberSelect].value = o.cameraFrameSelectionArea / 10;
    s[kOptionTooltipTime].value = static_cast<int>(o.toolTipTime * kScrollFrom);
    s[kOptionShadows].value = o.shadows;
    s[kOptionSelectType].value = o.selectionType;
    s[kOptionShowPath].value = o.drawPath;
    s[kOptionShowFlyingHp].value = o.drawFlyingHp;
    s[kOptionShowTutorial].value = o.showTutorial;
    s[kOptionAutosave].value = o.autosave;
    s[kOptionSwitchFilters].value = o.switchFilters;
    s[kOptionBrightness].value = static_cast<int>((o.brightness + 1.0f) * kFromStep);
    s[kOptionGamma].value = static_cast<int>((o.gamma + 1.0f) * kFromStep);
    s[kOptionDifficulty].value = o.difficulty;
    s[kOptionResolution].value = static_cast<int>(
        std::find(resolutions.begin(), resolutions.end(), ScreenSize()) - resolutions.begin());
    s[kOptionDisplayMode].value = g_sceneRenderer.device->fullScreen ? 1 : 0;
    s[kOptionVsync].value = render::Vsync() ? 1 : 0;
    s[kOptionMsaa].value = ChoiceIndex(kMsaaLevels, render::Msaa());
    s[kOptionAnisotropy].value = ChoiceIndex(kAnisotropyLevels, render::Anisotropy());
    s[kOptionPerfOverlay].value = render::PerfOverlay() ? 1 : 0;

    for (OptionSetting& setting : settings) {
        if (setting.bar)
            setting.bar->SetPosition(setting.value);
        else if (setting.spin)
            setting.spin->SetValue(setting.value);
        else if (setting.list)
            setting.list->SetValue(setting.value);
    }
}

void OptionsWindow::Apply() {
    std::memcpy(g_keyActions, keyOfScan, sizeof(keyOfScan));
    const OptionSetting* s = settings.data();
    options::g_options.audioSfx = s[kOptionSfxVolume].value;
    options::g_options.audioStreaming = s[kOptionStreamVolume].value;
    options::g_options.audioSpeech = s[kOptionVoiceVolume].value;
    options::g_options.reverseStereo = s[kOptionReverseStereo].value;
    float speedA = s[kOptionKeyboardPower].value * kToStep;
    float speedB = s[kOptionMousePower].value * kToStep;
    options::g_options.cameraScroll.powerKeyboard = speedA;
    options::g_options.cameraPitch.powerKeyboard = speedA;
    options::g_options.cameraZoom.powerKeyboard = speedA;
    options::g_options.cameraRotate.powerKeyboard = speedA;
    options::g_options.cameraScroll.powerMouse = speedB;
    options::g_options.cameraPitch.powerMouse = speedB;
    options::g_options.cameraZoom.powerMouse = speedB;
    options::g_options.cameraRotate.powerMouse = speedB;
    options::g_options.cameraFlipX = s[kOptionCameraReverseX].value == 1 ? -1.0f : 1.0f;
    options::g_options.cameraFlipY = s[kOptionCameraReverseY].value == 1 ? -1.0f : 1.0f;
    options::g_options.cameraBorderScrollArea = s[kOptionScrollBorder].value;
    options::g_options.cameraFrameSelectionArea = s[kOptionRubberSelect].value * 10;
    options::g_options.toolTipTime = s[kOptionTooltipTime].value * kScrollTo;
    options::g_options.shadows = s[kOptionShadows].value;
    options::g_options.selectionType = s[kOptionSelectType].value;
    options::g_options.drawPath = s[kOptionShowPath].value;
    options::g_options.drawFlyingHp = s[kOptionShowFlyingHp].value;
    options::g_options.showTutorial = s[kOptionShowTutorial].value;
    options::g_options.autosave = s[kOptionAutosave].value;
    options::g_options.switchFilters = s[kOptionSwitchFilters].value;
    options::g_options.brightness = s[kOptionBrightness].value * kToStep - 1.0f;
    options::g_options.gamma = s[kOptionGamma].value * kToStep - 1.0f;
    if (!IsMultiplayerClient()) {
        options::g_options.difficultyStored = s[kOptionDifficulty].value;
        options::g_options.difficulty = s[kOptionDifficulty].value;
    }
    if (shadowsChanged) g_clientWorld.DropShadows();
    // The display mode first: a window takes the chosen resolution's size after it.
    if (const bool fullScreen = s[kOptionDisplayMode].value == 1; fullScreen != g_sceneRenderer.device->fullScreen)
        app::ChooseFullScreen(fullScreen);
    if (const Size chosen = resolutions[s[kOptionResolution].value]; chosen != ScreenSize())
        app::ChooseResolution(chosen.w, chosen.h);

    // The renderer's switches, each from the next frame; a value the window left as it was is not written (the
    // harness build's renderer ignores the VSync and overlay switches; they are still saved).
    if (const bool vsync = s[kOptionVsync].value == 1; vsync != render::Vsync()) {
        render::SetVsync(vsync);
        settings::SetBool(settings::kRenderer, "VSync", vsync);
    }
    if (const int samples = kMsaaLevels[s[kOptionMsaa].value].value; samples != render::Msaa()) {
        render::SetMsaa(samples);
        settings::SetInt(settings::kRenderer, "Msaa", samples);
    }
    if (const int level = kAnisotropyLevels[s[kOptionAnisotropy].value].value; level != render::Anisotropy()) {
        render::SetAnisotropy(level);
        settings::SetInt(settings::kRenderer, "Anisotropy", level);
    }
    if (const bool overlay = s[kOptionPerfOverlay].value == 1; overlay != render::PerfOverlay()) {
        render::SetPerfOverlay(overlay);
        settings::SetBool(settings::kRenderer, "PerfOverlay", overlay);
    }
    options::g_options.Save();
}

// --- The slider's layout -------------------------------------------------------------------

void UiSlider::SetRect(const Rect* newRect) {
    rect = *newRect;
    const Rect bar = newRect->BottomStrip(10);   // the arrows at both ends, the track between them
    track = bar.Inset(26, 0);

    trackSprite->SetRectangle(depth, 100.0f, 0.0f, 0.0f, 6.0f, static_cast<float>(newRect->Width() - 8), 8, 4, 0x20,
                              0xFC);
    trackSprite->Place((newRect->right + newRect->left) * 0.5f, static_cast<float>(newRect->bottom - 5), depth);
    TurnQuarter(trackSprite);

    thumbSprite->SetRectangle(depth - kThumbLift, 100.0f, 0.0f, 0.0f, 10.0f, 30.0f, 0x28, 8, 0x50, 0x78);
    TurnQuarter(thumbSprite);

    upArrow->SetRectangle(depth, 100.0f, 0.0f, 0.0f, 10.0f, 12.0f, 0x28, 0xB0, 0x50, 0x80);
    upArrow->Place(static_cast<float>(newRect->right - 6), static_cast<float>(newRect->bottom - 5), depth);
    TurnQuarter(upArrow);

    downArrow->SetRectangle(depth, 100.0f, 0.0f, 0.0f, 10.0f, 12.0f, 0x28, 0x80, 0x50, 0xB0);
    downArrow->Place(static_cast<float>(newRect->left + 6), static_cast<float>(newRect->bottom - 5), depth);
    TurnQuarter(downArrow);

    upButton->SetRect(bar.LeftStrip(12));
    downButton->SetRect(bar.RightStrip(12));
    trackArea->SetRect(newRect->Inset(0, 0, 0, 10));

    // The position set again (one below first, so the change virtual sees a change).
    int currentPosition = position;
    position = currentPosition - 1;
    SetPosition(currentPosition);
}
