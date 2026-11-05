#pragma once

// The options window: its settings, the key actions table and its rows.

#include "game/ui.h"
#include "game/ui_drop_list.h"
#include "game/ui_scrollbar.h"
#include "game/ui_spin.h"

#include "lib/enum_array.h"

// The options window's settings, in the order of OptionsWindow::settings and kOptionDefinitions.
enum class OptionId : int {
    SfxVolume,
    StreamVolume,
    VoiceVolume,
    KeyboardPower,
    MousePower,
    ScrollBorder,
    RubberSelect,
    Shadows,
    SelectType,
    ShowPath,
    Brightness,
    Gamma,
    ShowFlyingHp,
    ShowTutorial,
    Autosave,
    TooltipTime,
    SwitchFilters,
    CameraReverseX,
    CameraReverseY,
    ReverseStereo,
    Difficulty,
    Resolution,
    DisplayMode,
    Vsync,
    Msaa,
    Anisotropy,
    PerfOverlay,
    Count,
};

struct OptionDefinition {
    const char* name = nullptr;      // "option_" + name is its text key (else the name is shown)
    int kind = 0;                    // 0 a slider, 1 an on / off spin control, 2 the resolutions' drop-down list, 3 a
                                     // spin control over a choice list (ui_options.cpp's)
    int maximum = 0;                 // the slider's range
    int group = 0;                   // the option group (0-10) it is shown in
    int row = 0;                     // its row there
};

constexpr int kOptionDefinitionCount = static_cast<int>(OptionId::Count);

struct ActionDefinition {
    int id = 0;                      // the action (the key code in the key table)
    const char* name = nullptr;      // "action" + name is its text key
    int group = 0;
    int row = 0;
};

constexpr int kActionDefinitionCount = 59;

// The key actions (a table of 60; the first is a blank id 0): the key bindings file's names and the options
// window's rows (group, row).
extern const ActionDefinition kActionDefinitions[60];

// The rows of the shown group point at them.
struct OptionSetting {
    int value = 0;
    UiScrollbar* bar = nullptr;     // a UiSlider, or null
    UiSpin* spin = nullptr;         // or null
    UiDropList* list = nullptr;     // the resolutions' drop-down list, or null
    std::string text;               // the shown name
};

// A key binding row of the options window.
struct OptionBinding {
    const ActionDefinition* action = nullptr;
    const UiKeyName* key = nullptr;     // the key bound to the action, or null
    std::string name;               // the action's shown name
    std::string keysText;           // the keys' names
};

// A row of the options window: a key binding or a setting.
struct OptionRow {
    OptionBinding* binding = nullptr;
    OptionSetting* setting = nullptr;
};

// The options window: OK, cancel, the option groups' buttons (100..250 x 156..420) and the rows of the shown group
// right of them.
class OptionsWindow : public UiContainer {
public:
    static constexpr int kGroups = 11;
    static constexpr int kRows = 14;

    OptionsWindow(bool newSliding) : sliding(newSliding) {}
    void DrawSprites(SceneRenderer* pass) override;                      // slides in
    void DrawSurfaces() override;                            // not while sliding
    void CloseScreen() override {}
    const char* HelpTopic() override;
    void OnEvent(UiEvent& event) override;
    void LeftDown(UiEvent& event);
    void LeftDouble(UiEvent& event);
    void MessageResult(std::uint32_t result) override;
    // Applies the settings whose bar or spin control changed (volumes, the display's gamma at once).
    void Update() override;

    // The group buttons' tints and the highlight on the selected row; PaintRows (surface 0's paint) draws the group
    // titles and the shown group's rows (a binding waiting for its key in red).
    void RefreshRows();
    void PaintRows(UiSurface& surface) const;
    // Clamped to 0-10; the first filled row selected (or row 0), the key wait cleared.
    void ShowGroup(int group);
    // OK: the key table and every setting into the game's option globals (the difficulty only outside a
    // multiplayer client); a shadow switch (settings 7-9) turned off drops those shadows; another display mode or
    // resolution is chosen (app::ChooseFullScreen, app::ChooseResolution), the renderer's switches set; the
    // settings file saved.
    void Apply();
    // A slider (kind 0), an on / off spin control (kind 1), the resolutions' drop-down list (kind 2) or a choice spin
    // control (kind 3) per setting.
    void BuildSettings();
    void LoadSettings();
    // The keys' text is the first key name of a scan code bound to the action.
    void BuildBindings();

    // Binds pressedKey to the selected row's action, taking it from the action it was bound to. Nothing
    // without a pressed key or a binding row.
    void CommitBinding();
    // Cancel: puts back the volumes and the display settings the window started with.
    static void RestoreSettings();
    void CreateScreen() override;

    // Esc cancels, Enter starts a key binding, steps a spin row or opens a list row, Up / Down select rows, PgUp /
    // PgDn switch groups, Left / Right move a bar or step a spin or list row back or on. While a binding waits, the
    // next key is bound (Esc gives up); a key bound to another action asks first. An open list takes every key.
    void KeyDown(UiEvent& event);

    signed char keyOfScan[0x200] = {};  // the key code bound to each scan code
    int selectedRow = 0;
    int group = 0;                   // the option group shown
    std::vector<OptionBinding> bindings;
    lib::EnumArray<OptionSetting, OptionId, static_cast<std::size_t>(OptionId::Count)> settings;
    OptionRow rows[kRows];
    bool waitingForKey = false;      // a key binding row waits for the new key
    bool sliding = false;
    float slide = 0.0f;
    std::vector<std::string> groupNames;    // the option groups' titles
    bool shadowsChanged = false;     // the shadows switch (setting 7) changed
    const UiKeyName* pressedKey = nullptr;  // the key pressed for a binding row
    std::vector<Size> resolutions;   // the resolution setting's sizes, the screen's among them

    // The regions, in their creation order.
    UiRegion* okButton = nullptr;
    UiRegion* cancelButton = nullptr;
    UiRegion* groupButtons[kGroups] = {};
    UiRegion* rowAreas[kRows] = {};
    UiMesh* highlight = nullptr;           // on the selected row
    UiMesh* groupIcons[kGroups] = {};      // the groups' buttons, lit for the group shown
};
