// The options window (OptionsWindow, ui_options.h): its settings and key bindings (the key actions table), its
// layout, mouse and key handlers, drawing and the slide-in. The key handler passes the key to the container first, marks
// the event used for the keys it uses and ignores keys while the window slides in. See docs/notes/ui.md, section 8.
#include "game/ui/ui_options.h"
#include "game/app/app.h"
#include "game/scene/device.h"
#include "game/app/singletons.h"
#include "game/app/key_bindings.h"
#include "game/scene/scene_renderer.h"
#include "game/models/texture.h"
#include "game/data/options.h"
#include "game/data/settings.h"
#include "game/data/text_manager.h"
#include "game/ui/ui.h"
#include "game/sound/sound.h"
#include "game/ui/ui_drop_list.h"
#include "game/ui/ui_menu_pieces.h"
#include "game/ui/ui_mesh.h"
#include "game/ui/ui_message_box.h"
#include "game/ui/ui_scrollbar.h"
#include "game/ui/ui_spin.h"
#include "game/world/world.h"
#include "lib/crt.h"
#include "lib/rect.h"
#include "lib/str.h"
#include "lib/vecmath.h"
#include "platform/display_sizes.h"
#include "platform/platform.h"
#include "render/render.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

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

// By OptionId. OptionId::ShowTutorial: help windows (Rebirth's ForceShowTutorial patches its store).
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

std::span<const Choice> ChoicesOf(OptionId setting) {
    if (setting == OptionId::Msaa) return kMsaaLevels;
    if (setting == OptionId::Anisotropy) return kAnisotropyLevels;
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
// (GameTextsInRussian), else in English. A text the game's files have wins.
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
    for (const AddedText& added : kAddedTexts)
        if (key == added.key) return GameTextsInRussian() ? added.russian : added.english;
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
    if (g_clientWorld.texts->TextManager::Open(key)) tip = g_clientWorld.texts->ReadToHash();
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

constexpr float kOptionsTilt = /*007C81A0*/ 1.5707964f;

const char* OptionGroupName(int index) {
    static constexpr const char* kNames[13] = /*007947A8*/ {"video",  "sound",  "sens",   "game",    "select",
                                                            "actions", "camera", "items",  "spells",  "cshots",
                                                            "windows", "debug1", "debug2"};
    return kNames[index];
}

constexpr std::uint32_t kCloseOptions = 0x110001;

constexpr float kGammaScale = 0.02f;   // value * 0.02 - 1
constexpr float kSlideSeconds = 0.5f;

constexpr int kScanEsc = 1;              // the scan code that gives up a key binding

// The end of the key name table (88 entries of 0x48 bytes).
const UiKeyName* KeyNamesEnd() { return g_keyNames + kUiKeyNameCount; }
} // namespace

bool GameTextsInRussian() {
    const std::string title = g_clientWorld.texts->GetString("option_group_video");
    return std::any_of(title.begin(), title.end(), [](char c) { return (c & 0x80) != 0; });
}

const ActionDefinition kActionDefinitions[60] = /*00787070*/ {
    {0, "", 0, 0},
    {1, "pause", 3, 4},
    {2, "decel", 3, 3},
    {3, "accel", 3, 2},
    {4, "obj", 3, 6},
    {5, "camera_up", 6, 1},
    {6, "camera_down", 6, 2},
    {7, "camera_left", 6, 3},
    {8, "camera_right", 6, 4},
    {9, "camera_zoom_in", 6, 5},
    {10, "camera_zoom_out", 6, 6},
    {11, "camera_track", 6, 0},
    {12, "camera_norm", 6, 8},
    {13, "camera1", 6, 9},
    {14, "camera2", 6, 10},
    {15, "camera3", 6, 11},
    {16, "camera4", 6, 12},
    {17, "cs_head", 9, 0},
    {18, "cs_body", 9, 1},
    {19, "cs_rleg", 9, 2},
    {20, "cs_lleg", 9, 3},
    {21, "cs_rhand", 9, 4},
    {22, "cs_lhand", 9, 5},
    {23, "crawl", 5, 7},
    {24, "sneak", 5, 6},
    {25, "walk", 5, 5},
    {26, "run", 5, 4},
    {27, "swarm", 5, 2},
    {28, "follow", 5, 0},
    {29, "use_science", 5, 1},
    {30, "spell1", 8, 0},
    {31, "spell2", 8, 1},
    {32, "spell3", 8, 2},
    {33, "spell4", 8, 3},
    {34, "spell5", 8, 4},
    {35, "spell6", 8, 5},
    {36, "spell7", 8, 6},
    {37, "spell8", 8, 7},
    {38, "weapon1", 7, 0},
    {39, "weapon2", 7, 1},
    {40, "weapon3", 7, 2},
    {41, "weapon4", 7, 3},
    {42, "item1", 7, 5},
    {43, "item2", 7, 6},
    {44, "item3", 7, 7},
    {45, "item4", 7, 8},
    {46, "w_info1", 10, 3},
    {47, "w_info2", 10, 4},
    {48, "w_info3", 10, 5},
    {49, "w_info4", 10, 6},
    {50, "w_text1", 10, 0},
    {51, "w_text2", 10, 1},
    {52, "w_minimap", 10, 8},
    {53, "quicksave", 3, 8},
    {54, "quickload", 3, 9},
    {55, "select1", 4, 0},
    {56, "select2", 4, 1},
    {57, "select3", 4, 2},
    {58, "select_all", 4, 4},
    {59, "tutorial_script", 10, 10},
};

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
        surface.DrawTextIn(groupNames[groupIndex], title, TextStyle{1, groupIndex == group ? kUiTextColour : kColourDim, render::text::Format::Center});
        title.Offset(0, 24);
    }
    Rect heading = {0, 10, 600, 34};
    surface.DrawTextIn(groupNames[group], heading, TextStyle{2, kUiTextColour, render::text::Format::Center});

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
    settings = {};
    for (int i = 0; i < kOptionDefinitionCount; ++i) {
        OptionSetting& setting = settings[i];
        const auto optionId = static_cast<OptionId>(i);
        const OptionDefinition* definition = &kOptionDefinitions[i];
        setting.text = ShownName(kTextOptionPrefix, definition->name);

        int top = definition->row * 24;
        if (definition->kind == kSliderKind) {
            auto* slider = AddChild(std::make_unique<UiSlider>(Rect{0, 0, 100, 100}, 3.0f, definition->maximum, 1.0f));
            setting.bar = slider;
            Rect area = {530, top + 163, 680, top + 173};
            slider->SetRect(area);
        } else if (definition->kind == kSpinKind) {
            std::string text = g_clientWorld.texts->GetString(kTextOptionOff);
            if (optionId == OptionId::Difficulty) text = g_clientWorld.texts->GetString(kTextDifficultyHard);
            setting.spin = AddChild(std::make_unique<UiSpin>(Rect{530, top + 160, 690, top + 176}, text));
            text = g_clientWorld.texts->GetString(kTextOptionOn);
            if (optionId == OptionId::Difficulty) text = g_clientWorld.texts->GetString(kTextDifficultyEasy);
            setting.spin->AddText(text);
        } else if (definition->kind == kResolutionKind) {
            resolutions = platform::DisplaySizes(g_sceneRenderer.device->window);
            platform::AddSize(resolutions, ScreenSize());
            setting.list = AddChild(std::make_unique<UiDropList>(this, Rect{530, top + 160, 690, top + 176}));
            for (const Size size : resolutions) setting.list->AddText(str::Format("%d x %d", size.w, size.h));
        } else if (definition->kind == kChoiceKind) {
            const std::span<const Choice> choices = ChoicesOf(optionId);
            setting.spin = AddChild(std::make_unique<UiSpin>(Rect{530, top + 160, 690, top + 176}, ShownName("", choices[0].text)));
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
    auto& s = settings;
    const options::GameOptions& o = options::g_options;
    s[OptionId::SfxVolume].value = o.audioSfx;
    s[OptionId::StreamVolume].value = o.audioStreaming;
    s[OptionId::VoiceVolume].value = o.audioSpeech;
    s[OptionId::ReverseStereo].value = o.reverseStereo;
    // _ftol: truncation.
    s[OptionId::KeyboardPower].value = static_cast<int>(o.cameraScroll.powerKeyboard * kFromStep);
    s[OptionId::MousePower].value = static_cast<int>(o.cameraScroll.powerMouse * kFromStep);
    s[OptionId::CameraReverseX].value = o.cameraFlipX == 1.0f ? 0 : 1;
    s[OptionId::CameraReverseY].value = o.cameraFlipY == 1.0f ? 0 : 1;
    s[OptionId::ScrollBorder].value = o.cameraBorderScrollArea;
    s[OptionId::RubberSelect].value = o.cameraFrameSelectionArea / 10;
    s[OptionId::TooltipTime].value = static_cast<int>(o.toolTipTime * kScrollFrom);
    s[OptionId::Shadows].value = o.shadows;
    s[OptionId::SelectType].value = o.selectionType;
    s[OptionId::ShowPath].value = o.drawPath;
    s[OptionId::ShowFlyingHp].value = o.drawFlyingHp;
    s[OptionId::ShowTutorial].value = o.showTutorial;
    s[OptionId::Autosave].value = o.autosave;
    s[OptionId::SwitchFilters].value = o.switchFilters;
    s[OptionId::Brightness].value = static_cast<int>((o.brightness + 1.0f) * kFromStep);
    s[OptionId::Gamma].value = static_cast<int>((o.gamma + 1.0f) * kFromStep);
    s[OptionId::Difficulty].value = o.difficulty;
    s[OptionId::Resolution].value = static_cast<int>(
        std::find(resolutions.begin(), resolutions.end(), ScreenSize()) - resolutions.begin());
    s[OptionId::DisplayMode].value = g_sceneRenderer.device->fullScreen ? 1 : 0;
    s[OptionId::Vsync].value = render::Vsync() ? 1 : 0;
    s[OptionId::Msaa].value = ChoiceIndex(kMsaaLevels, render::Msaa());
    s[OptionId::Anisotropy].value = ChoiceIndex(kAnisotropyLevels, render::Anisotropy());
    s[OptionId::PerfOverlay].value = render::PerfOverlay() ? 1 : 0;

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
    const auto& s = settings;
    options::g_options.audioSfx = s[OptionId::SfxVolume].value;
    options::g_options.audioStreaming = s[OptionId::StreamVolume].value;
    options::g_options.audioSpeech = s[OptionId::VoiceVolume].value;
    options::g_options.reverseStereo = s[OptionId::ReverseStereo].value;
    float speedA = s[OptionId::KeyboardPower].value * kToStep;
    float speedB = s[OptionId::MousePower].value * kToStep;
    options::g_options.cameraScroll.powerKeyboard = speedA;
    options::g_options.cameraPitch.powerKeyboard = speedA;
    options::g_options.cameraZoom.powerKeyboard = speedA;
    options::g_options.cameraRotate.powerKeyboard = speedA;
    options::g_options.cameraScroll.powerMouse = speedB;
    options::g_options.cameraPitch.powerMouse = speedB;
    options::g_options.cameraZoom.powerMouse = speedB;
    options::g_options.cameraRotate.powerMouse = speedB;
    options::g_options.cameraFlipX = s[OptionId::CameraReverseX].value == 1 ? -1.0f : 1.0f;
    options::g_options.cameraFlipY = s[OptionId::CameraReverseY].value == 1 ? -1.0f : 1.0f;
    options::g_options.cameraBorderScrollArea = s[OptionId::ScrollBorder].value;
    options::g_options.cameraFrameSelectionArea = s[OptionId::RubberSelect].value * 10;
    options::g_options.toolTipTime = s[OptionId::TooltipTime].value * kScrollTo;
    options::g_options.shadows = s[OptionId::Shadows].value;
    options::g_options.selectionType = s[OptionId::SelectType].value;
    options::g_options.drawPath = s[OptionId::ShowPath].value;
    options::g_options.drawFlyingHp = s[OptionId::ShowFlyingHp].value;
    options::g_options.showTutorial = s[OptionId::ShowTutorial].value;
    options::g_options.autosave = s[OptionId::Autosave].value;
    options::g_options.switchFilters = s[OptionId::SwitchFilters].value;
    options::g_options.brightness = s[OptionId::Brightness].value * kToStep - 1.0f;
    options::g_options.gamma = s[OptionId::Gamma].value * kToStep - 1.0f;
    if (!IsMultiplayerClient()) {
        options::g_options.difficultyStored = s[OptionId::Difficulty].value;
        options::g_options.difficulty = s[OptionId::Difficulty].value;
    }
    if (shadowsChanged) g_clientWorld.DropShadows();
    // The display mode first: a window takes the chosen resolution's size after it.
    if (const bool fullScreen = s[OptionId::DisplayMode].value == 1; fullScreen != g_sceneRenderer.device->fullScreen)
        app::ChooseFullScreen(fullScreen);
    if (const Size chosen = resolutions[s[OptionId::Resolution].value]; chosen != ScreenSize())
        app::ChooseResolution(chosen.w, chosen.h);

    // The renderer's switches, each from the next frame; a value the window left as it was is not written (the
    // harness build's renderer ignores the VSync and overlay switches; they are still saved).
    if (const bool vsync = s[OptionId::Vsync].value == 1; vsync != render::Vsync()) {
        render::SetVsync(vsync);
        settings::SetBool(settings::kRenderer, "VSync", vsync);
    }
    if (const int samples = kMsaaLevels[s[OptionId::Msaa].value].value; samples != render::Msaa()) {
        render::SetMsaa(samples);
        settings::SetInt(settings::kRenderer, "Msaa", samples);
    }
    if (const int level = kAnisotropyLevels[s[OptionId::Anisotropy].value].value; level != render::Anisotropy()) {
        render::SetAnisotropy(level);
        settings::SetInt(settings::kRenderer, "Anisotropy", level);
    }
    if (const bool overlay = s[OptionId::PerfOverlay].value == 1; overlay != render::PerfOverlay()) {
        render::SetPerfOverlay(overlay);
        settings::SetBool(settings::kRenderer, "PerfOverlay", overlay);
    }
    options::g_options.Save();
}

// --- Options window ------------------------------------------------------------------

void OptionsWindow::CreateScreen() {
    rect = Rect{0, 0, 800, 600};
    LoadTexture("saveload", TextureKind::Interface);
    CreateTexture(0x100, 0x100, TextureKind::Blank565);

    AddBlankSprite()->SetRectangle(kDialogDepth, kSpriteScale, 213.0f, 526.0f, 287.0f, 574.0f, 0x51, 2, 0x9B, 0x32);
    AddBlankSprite()->SetRectangle(kDialogDepth, kSpriteScale, 513.0f, 526.0f, 587.0f, 574.0f, 0xA0, 2, 0xEA, 0x32);
    okButton = AddRect(this, 213, 526, 287, 574);
    okButton->SetTip(0x111D4);
    cancelButton = AddRect(this, 513, 526, 587, 574);
    cancelButton->SetTip(0x111D5);
    highlight = AddBlankSprite();
    highlight->SetRectangle(kDialogDepth, kSpriteScale, 250.0f, 108.0f, 692.0f, 132.0f, 0, 0, kUiTextureSize, kUiTextureSize);
    highlight->texture = nullptr;
    highlight->flatColour = 0xC8A06800;
    highlight->flags |= FigureMeshFlag::Flat;

    // The option groups, each with its button sprite, tooltip and title.
    std::string text;
    for (int k = 0, top = 156; k < kGroups; ++k, top += 24) {
        groupButtons[k] = AddRect(this, 100, top, 250, top + 24);
        groupIcons[k] = AddBlankSprite();
        groupIcons[k]->SetRectangle(kDialogDepth, kSpriteScale, 110.0f, static_cast<float>(top + 2), 240.0f,
                                    static_cast<float>(top + 21), 0x7A, 0x6C, 0xFC, 0x7F);
        text = std::format("tip option_group_{}", OptionGroupName(k));
        if (g_clientWorld.texts->Open(text) != 0) text = g_clientWorld.texts->ReadToHash();
        groupButtons[k]->SetTipString(text);
        {
            std::string prefix("option_group_");
            std::string key = prefix + OptionGroupName(k);
            text = g_clientWorld.texts->GetString(key);
        }
        groupNames.push_back(text);
    }

    Frame(this, 2.0f, 95.0f, 95.0f, 705.0f, 505.0f);
    Shade(this, 5.0f, 100.0f, 100.0f, 700.0f, 500.0f);
    for (int row = 0, top = 156; row < kRows; ++row, top += 24) rowAreas[row] = AddRect(this, 250, top, 520, top + 24);
    UiSurface* surface = AddSurface(NewSurface(Rect{100, 100, 700, 500}));
    surface->PaintWith([this](UiSurface& out) { PaintRows(out); });

    UiModel* model = AddMenuModel(this, kOptionsTilt);
    model->SetScreenPosition(400.0f, 700.0f, 4.8f);
    model->ShowAllParts(0);
    model->ShowPart("options", 1);
    model->ShowPart("optionslabel", 1);

    if (sliding) slide = 0.0f;
    waitingForKey = false;
    pressedKey = nullptr;
    BuildSettings();
    LoadSettings();
    BuildBindings();
    shadowsChanged = false;
    ShowGroup(0);
    if (!sliding) g_interface.OpenHelp(false);
}

// --- Options window ----------------------------------------------------------------------------

void OptionsWindow::RestoreSettings() {
    // The values the window saved when it opened.
    g_clientWorld.sounds->SetEffectsVolume(options::g_options.audioSfx);
    g_clientWorld.sounds->SetMusicVolume(options::g_options.audioStreaming);
    g_clientWorld.sounds->SetSpeechVolume(options::g_options.audioSpeech);
    // The scene renderer's gamma.
    g_sceneRenderer.SetGamma(options::g_options.brightness, options::g_options.gamma);
}

void OptionsWindow::LeftDown(UiEvent& event) {
    if (sliding) return;
    if (popup) {
        UiContainer::OnEvent(event);
        return;
    }
    const UiRegion* region = RegionUnder(event.at);
    if (region == okButton || region == cancelButton) {
        const char* sound;
        if (region == okButton) {
            Apply();
            sound = kMessageBoxOkSound;
        } else {
            RestoreSettings();
            sound = kMessageBoxCancelSound;
        }
        g_interface.PlayInterfaceSound(sound);
        g_interface.SetPendingValue(kCloseOptions);
    } else {
        if (const int groupIndex = RegionIndex(groupButtons, region); groupIndex >= 0) {
            g_interface.PlayInterfaceSound(kSaveSelectSound);
            waitingForKey = false;
            ShowGroup(groupIndex);
        }
        if (const int rowIndex = RegionIndex(rowAreas, region); rowIndex >= 0) {
            const OptionRow& row = rows[rowIndex];
            if (row.binding || row.setting) {
                g_interface.PlayInterfaceSound(kSaveSelectSound);
                selectedRow = rowIndex;
                waitingForKey = false;
                RefreshRows();
            }
        }
    }
    UiContainer::OnEvent(event);
}

void OptionsWindow::LeftDouble(UiEvent& event) {
    if (sliding) return;
    if (popup) {
        UiContainer::OnEvent(event);
        return;
    }
    if (const int rowIndex = RegionIndex(rowAreas, RegionUnder(event.at)); rowIndex >= 0) {
        const OptionRow& row = rows[rowIndex];
        if (row.binding) {
            g_interface.PlayInterfaceSound(kSaveSelectSound);
            selectedRow = rowIndex;
            waitingForKey = true;
            RefreshRows();
        } else if (row.setting && row.setting->list) {
            g_interface.PlayInterfaceSound(kMessageBoxOkSound);
            row.setting->list->Open();
        } else if (row.setting && row.setting->spin) {
            const int value = row.setting->spin->TakeValue();
            row.setting->spin->SetValue(value + 1);
            g_interface.PlayInterfaceSound(kMessageBoxOkSound);
            RefreshRows();
        }
    }
    UiContainer::OnEvent(event);
}

void OptionsWindow::DrawSprites(SceneRenderer* pass) {
    if (!sliding) {
        surfaces[0]->Show();
        models[0]->SetScreenPosition(400.0f, 480.0f, 4.8f);
        UiContainer::DrawSprites(pass);
        return;
    }
    surfaces[0]->Hide();
    float e = Ease(slide);
    UiModel* model = models[0].get();
    if (!model) return;
    float y = (480.0f - 700.0f) * e + 700.0f;
    model->SetScreenPosition(400.0f, y, 4.8f);
    model->Draw(pass);
}

void OptionsWindow::DrawSurfaces() {
    if (!sliding) UiContainer::DrawSurfaces();
}

void OptionsWindow::MessageResult(std::uint32_t result) {
    if (result == BoxResult(MessageQuestion::First, MessageAnswer::Ok)) CommitBinding();
    RefreshRows();
}

const char* OptionsWindow::HelpTopic() {
    // "options" unless a key binding waits for its key.
    return waitingForKey ? nullptr : "options";
}

// The volumes, the display's brightness and gamma, the shadows and the stereo switch are applied while the window is open.

void OptionsWindow::Update() {
    UiContainer::Update();
    if (sliding) {
        float slideAt = FrameTime() / kSlideSeconds + slide;
        slide = slideAt;
        if (slideAt > 1.0f) {
            sliding = false;
            g_interface.OpenHelp(false);
        }
    }
    for (int i = 0; i < kOptionDefinitionCount; ++i) {
        OptionSetting& setting = settings[i];
        const auto optionId = static_cast<OptionId>(i);
        if (setting.bar && setting.bar->TakeMoved()) {
            const int value = setting.bar->Position();
            setting.value = value;
            if (optionId == OptionId::SfxVolume) {
                g_clientWorld.sounds->SetEffectsVolume(value);
            } else if (optionId == OptionId::StreamVolume) {
                g_clientWorld.sounds->SetMusicVolume(value);
            } else if (optionId == OptionId::VoiceVolume) {
                g_clientWorld.sounds->SetSpeechVolume(value);
            } else if (optionId == OptionId::Brightness || optionId == OptionId::Gamma) {
                float brightness = settings[OptionId::Brightness].value * kGammaScale - 1.0f;
                float gamma = settings[OptionId::Gamma].value * kGammaScale - 1.0f;
                g_sceneRenderer.SetGamma(brightness, gamma);
            }
        }
        if (setting.list && setting.list->TakeChanged()) setting.value = setting.list->value;
        if (setting.spin && setting.spin->TakeChanged()) {
            const int value = setting.spin->Value();
            setting.value = value;
            if (optionId == OptionId::Shadows)
                shadowsChanged = true;
            else if (optionId == OptionId::ReverseStereo)   // the sound system's pan switch
                g_clientWorld.sounds->panDirect = value != 0;
        }
    }
}

void OptionsWindow::OnEvent(UiEvent& event) {
    switch (event.kind) {
    case UiEventKind::LeftDown: LeftDown(event); break;
    case UiEventKind::LeftDouble: LeftDouble(event); break;
    case UiEventKind::KeyDown: KeyDown(event); break;
    default: UiContainer::OnEvent(event); break;
    }
}

void OptionsWindow::KeyDown(UiEvent& event) {
    if (sliding) return;
    const bool listOpen = popup != nullptr;
    UiContainer::OnEvent(event);
    if (listOpen) return;   // the open list took the key (Enter or Escape may have closed it)
    if (waitingForKey) {
        // The key for the selected binding row, by scan code.
        event.handled = true;
        if (event.scan == kScanEsc) {
            waitingForKey = false;
            RefreshRows();
            return;
        }
        const UiKeyName* pressed = nullptr;
        pressedKey = nullptr;
        for (const UiKeyName* k = g_keyNames; k < KeyNamesEnd(); ++k) {
            if (k->scan == event.scan) {
                pressed = k;
                pressedKey = k;
                break;
            }
        }
        if (!pressed) {
            g_interface.PlayInterfaceSound(kMessageBoxCancelSound);
            return;
        }
        waitingForKey = false;
        OptionBinding* binding = rows[selectedRow].binding;
        if (!binding) {
            RefreshRows();
            return;
        }
        g_interface.PlayInterfaceSound(kMessageBoxOkSound);
        int code = keyOfScan[pressed->scan];
        OptionBinding* other = nullptr;
        for (OptionBinding& b : bindings) {
            if (b.action->id == code) {
                other = &b;
                break;
            }
        }
        if (other && other != binding) {
            // Already bound to another action: ask (the box's result goes to MessageResult).
            auto box = std::make_unique<MessageWindow>("key_already_mapped", "key_already_mapped_msg", MessageQuestion::First, MessageButtons::OkCancel);
            std::string format;
            std::string message;
            format = g_clientWorld.texts->GetString("key_already_mapped_msg");
            message = str::Format(format, pressed->name, other->name);
            box->text = std::move(message);
            g_interface.PushWindow(std::move(box), WindowBackdrop::FirstWindow);
            return;
        }
        CommitBinding();
        RefreshRows();
        return;
    }

    switch (event.key) {
    case platform::Key::Return: {
        OptionRow& row = rows[selectedRow];
        if (row.binding) {
            waitingForKey = true;
        } else if (row.setting && row.setting->list) {
            row.setting->list->Open();
        } else {
            if (!row.setting || !row.setting->spin) {
                event.handled = true;
                return;
            }
            const int value = row.setting->spin->TakeValue();
            rows[selectedRow].setting->spin->SetValue(value + 1);
        }
        g_interface.PlayInterfaceSound(kMessageBoxOkSound);
        RefreshRows();
        event.handled = true;
        return;
    }
    case platform::Key::Escape:
        RestoreSettings();
        g_interface.PlayInterfaceSound(kMessageBoxCancelSound);
        g_interface.SetPendingValue(kCloseOptions);
        break;
    case platform::Key::PageUp:
    case platform::Key::PageDown:
        g_interface.PlayInterfaceSound(kSaveSelectSound);
        ShowGroup(event.key == platform::Key::PageUp ? group - 1 : group + 1);
        break;
    case platform::Key::Left:
    case platform::Key::Right: {
        // The row's bar moves one step, or its spin control steps back or on; the event is not marked used.
        OptionSetting* setting = rows[selectedRow].setting;
        if (!setting) return;
        if (setting->list) {
            setting->list->Step(event.key == platform::Key::Left ? -1 : 1);
            g_interface.PlayInterfaceSound(kMessageBoxOkSound);
            return;
        }
        if (setting->spin) {
            setting->spin->Step(event.key == platform::Key::Left ? -1 : 1);
            g_interface.PlayInterfaceSound(kMessageBoxOkSound);
            return;
        }
        if (!setting->bar) return;
        const int position = setting->bar->TakePosition();
        rows[selectedRow].setting->bar->SetPosition(event.key == platform::Key::Left ? position - 1 : position + 1);
        return;
    }
    case platform::Key::Up:
        g_interface.PlayInterfaceSound(kSaveSelectSound);
        for (int i = selectedRow - 1; i >= 0; --i) {
            if (rows[i].binding || rows[i].setting) {
                selectedRow = i;
                RefreshRows();
                break;
            }
        }
        break;
    case platform::Key::Down:
        g_interface.PlayInterfaceSound(kSaveSelectSound);
        for (int i = selectedRow + 1; i < kRows; ++i) {
            if (rows[i].binding || rows[i].setting) {
                selectedRow = i;
                RefreshRows();
                break;
            }
        }
        break;
    default:
        return;
    }
    event.handled = true;
}
