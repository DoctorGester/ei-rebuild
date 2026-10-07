// The options window's settings and key bindings (00651350..006524A0) and its slider's layout
// (0065B8B0). See ui.h (COptionsWindow, CUiSlider) and docs/notes/drafts/DR-H16.md.
//
// DR-H16 draft: written without a build. What was not checked is listed in the notes file.
#include "ui.h"

#include "item_db_text.h"
#include "maths.h"
#include "world.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "lib/stlp.h"
#include "originals_engine.h"
#include "originals_ui.h"
#include "originals_world.h"

#include <cstdint>
#include <cstring>
#include <new>

// --- Forwarders --------------------------------------------------------------------------------

__declspec(naked) CUiSlider* CUiSlider::Construct(CInterface3D*, RECT, float, int, float) EI_ORIGINAL(0x0065BC30)
__declspec(naked) CUiSpin* CUiSpin::Construct(CInterface3D*, RECT, const char*) EI_ORIGINAL(0x0065BCD0)
__declspec(naked) void CUiSpin::AddText(const char*) EI_ORIGINAL(0x0065BE70)

__declspec(naked) void COptionsWindow::CallRefreshRows() EI_ORIGINAL(0x00651350)
__declspec(naked) void COptionsWindow::CallShowGroup(int) EI_ORIGINAL(0x00651D70)
__declspec(naked) void COptionsWindow::CallApply() EI_ORIGINAL(0x006524A0)
__declspec(naked) void COptionsWindow::CallBuildSettings() EI_ORIGINAL(0x00651940)
__declspec(naked) void COptionsWindow::CallLoadSettings() EI_ORIGINAL(0x00652200)
__declspec(naked) void COptionsWindow::CallBuildBindings() EI_ORIGINAL(0x00651600)

namespace {
// 00652930 (RET 0xC): the binding vector's _M_insert_overflow(pos, value, count): new storage,
// the old rows copied (00652FF0) and destroyed (006537E0), `count` copies of `value` at `pos`.
__declspec(naked) void __fastcall BindingsInsertOverflow(stlp::vector<OptionBinding>* /*bindings*/, void*,
                                                         OptionBinding* /*pos*/, const OptionBinding* /*value*/,
                                                         unsigned /*count*/) EI_ORIGINAL(0x00652930)
// 0056D7A0 (thiscall on the client world 007AF5B8, plain RET): every object of its list at +0xB8
// for which 0051E200 is false gets 0051E280 (what the options' display mode change runs).
__declspec(naked) void __fastcall ClientWorldRefreshObjectsOriginal(void* /*world*/) EI_ORIGINAL(0x0056D7A0)

const char* GameText(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }

// Strings of the exe.
constexpr std::uintptr_t kTextOptionPrefix = 0x007948C8;   // "option_"
constexpr std::uintptr_t kTextActionPrefix = 0x007948A0;   // "action_"
constexpr std::uintptr_t kTextTipOption = 0x007948E0;      // "tip option_%s"
constexpr std::uintptr_t kTextTipAction = 0x007948D0;      // "tip action_%s"
constexpr std::uintptr_t kTextOptionOff = 0x007948B8;      // "option_off"
constexpr std::uintptr_t kTextOptionOn = 0x007948A8;       // "option_on"
constexpr std::uintptr_t kTextOffDefault = 0x007948C4;     // (not a Ghidra string; 3 characters at most)
constexpr std::uintptr_t kTextOnDefault = 0x007948B4;      // (likewise)
constexpr std::uintptr_t kTextDifficultyHard = 0x0079427C; // "difficulty_hard"
constexpr std::uintptr_t kTextDifficultyEasy = 0x0079428C; // "difficulty_easy"
constexpr std::uintptr_t kTextKeySeparator = 0x00794830;   // appended after each key name (not checked)
constexpr std::uintptr_t kTextKeyTrim = 0x0079482C;        // trimmed from the right (not checked)

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

// Read from the exe's memory as the original does (rule 9).
float Constant(std::uintptr_t address) { return *reinterpret_cast<const float*>(address); }
constexpr std::uintptr_t kConstOne = 0x0073B84C;          // the add / compare constant (1.0 by its use, not read)
constexpr std::uintptr_t kConstFromStep = 0x0074023C;     // value = setting / step (50 by its use)
constexpr std::uintptr_t kConstToStep = 0x00742B80;       // setting * 0.02 (ui_menus.cpp's kGammaScale)
constexpr std::uintptr_t kConstScrollFrom = 0x0073BAA0;   // setting 20 from its global
constexpr std::uintptr_t kConstScrollTo = 0x0073C3B0;     // setting 20 to its global
constexpr std::uintptr_t kConstHalf = 0x0073B86C;         // the slider's centre (0.5 by its use)
constexpr std::uintptr_t kConstThumbLift = 0x0073B850;    // the thumb's depth below the track's
constexpr std::uintptr_t kQuarterTurn = 0x007C8210;       // the slider sprites' turn about z (a float global)

// The game's option globals the window loads (00652200) and stores (006524A0), by setting.
template <typename T>
T& OptionGlobal(std::uintptr_t address) {
    return *reinterpret_cast<T*>(address);
}
constexpr std::uintptr_t kOptVolume0 = 0x0079BCC8;        // setting 0 (the sound system's volume A)
constexpr std::uintptr_t kOptVolume1 = 0x0079BCC4;        // setting 1
constexpr std::uintptr_t kOptVolume2 = 0x0079BCCC;        // setting 2
constexpr std::uintptr_t kOptSpeedA = 0x0079BDC0;         // setting 3 (float; three more copies on store)
constexpr std::uintptr_t kOptSpeedA2 = 0x0079BDD8;
constexpr std::uintptr_t kOptSpeedA3 = 0x0079BDE4;
constexpr std::uintptr_t kOptSpeedA4 = 0x0079BDCC;
constexpr std::uintptr_t kOptSpeedB = 0x0079BDBC;         // setting 4 (float; three more copies on store)
constexpr std::uintptr_t kOptSpeedB2 = 0x0079BDD4;
constexpr std::uintptr_t kOptSpeedB3 = 0x0079BDE0;
constexpr std::uintptr_t kOptSpeedB4 = 0x0079BDC8;
constexpr std::uintptr_t kOpt5 = 0x0079BDAC;              // setting 5
constexpr std::uintptr_t kOpt6Tenths = 0x0079BDB0;        // setting 6 * 10
constexpr std::uintptr_t kOptDisplay7 = 0x0079BD14;       // settings 7-9: the display mode
constexpr std::uintptr_t kOptDisplay8 = 0x0079BD18;
constexpr std::uintptr_t kOptDisplay9 = 0x0079BD1C;
constexpr std::uintptr_t kOpt10 = 0x0079BD50;
constexpr std::uintptr_t kOpt11 = 0x0079BD4C;
constexpr std::uintptr_t kOpt12 = 0x0079BE64;
constexpr std::uintptr_t kOpt13 = 0x0079BE70;
constexpr std::uintptr_t kOptGamma14 = 0x0079BCFC;        // settings 14-16 (float: setting * 0.02 - 1)
constexpr std::uintptr_t kOptGamma15 = 0x0079BD00;
constexpr std::uintptr_t kOptGamma16 = 0x0079BD04;
constexpr std::uintptr_t kOpt17 = 0x0079BE74;
constexpr std::uintptr_t kOptHelp18 = 0x0079BE78;         // setting 18: help windows (Rebirth's ForceShowTutorial
                                                          // patches its store at 00652663)
constexpr std::uintptr_t kOpt19 = 0x0079BE7C;
constexpr std::uintptr_t kOpt20 = 0x0079BE80;             // setting 20 (float)
constexpr std::uintptr_t kOpt21 = 0x0079BE84;
constexpr std::uintptr_t kOptSign22 = 0x0079BD64;         // setting 22 (float: -1 for 1, else +1)
constexpr std::uintptr_t kOptSign23 = 0x0079BD68;         // setting 23 (likewise)
constexpr std::uintptr_t kOpt24 = 0x0079BD08;
constexpr std::uintptr_t kOptDifficultyCopy = 0x0079BEC4; // setting 25 (with GameDifficulty 0079BEC0)

CUiMesh* SceneSprite(CUiWindow* window, int index) { return reinterpret_cast<CUiMesh*>(window->sceneObjects_.data[index]); }

// A row text into the surface (005FDE20, Rebirth-patched: through its address).
void DrawRowText(CUiSurface* surface, const char* text, RECT* rect, int font, COLORREF colour, int format) {
    SurfaceDrawText(surface, nullptr, text, rect, font, colour, format, 1);
}

// The text key `key` read up to its '#' into `text` (unchanged when there is no such text).
void ReadTip(mfc::CString* text) {
    if (TextsOpenSection(GameTexts(), nullptr, *text) != 0) GameTexts()->ReadToHash(text, 1);
}

// The shown name: the text of prefix + name, else the name itself.
void ShownName(mfc::CString* out, std::uintptr_t prefix, const char* name) {
    if (!GameTexts()->GetString(mfc::CString(GameText(prefix)) + name, out)) *out = name;
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
    BindingsInsertOverflow(&bindings, nullptr, at, &blank, 1);
    // `blank` has no key storage; its strings go with it.
}
} // namespace

// --- 00651350 ------------------------------------------------------------------------------------

void COptionsWindow::RefreshRows() {
    CUiSurface* surface = surfaces_.data[0];
    SurfaceGetDC(surface);
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
    SurfaceReleaseDC(surface);
}
EI_REPLACE_METHOD(0x00651350, COptionsWindow::RefreshRows);

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
        const OptionDefinition& definition = OptionDefinitions()[i];
        if (definition.group_ != group) continue;
        rows_[definition.row_].setting_ = &settings_[i];
        tip.Format(GameText(kTextTipOption), definition.name_);
        ReadTip(&tip);
        regions_.data[kFirstRowRegion + definition.row_]->SetTipString(tip, 1, 0);
    }
    for (int i = 0; i < kActionDefinitionCount; ++i) {
        const ActionDefinition& action = ActionDefinitions()[i];
        if (action.group_ != group) continue;
        rows_[action.row_].binding_ = &bindings_[i];
        tip.Format(GameText(kTextTipAction), action.name_);
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
        CallRefreshRows();
    }
    if (selectedRow_ == -1) selectedRow_ = 0;
    waitingForKey_ = false;
    CallRefreshRows();
}
EI_REPLACE_METHOD(0x00651D70, COptionsWindow::ShowGroup);

// --- 00651940 ------------------------------------------------------------------------------------

void COptionsWindow::BuildSettings() {
    settings_.clear();   // 00652F80 copy and the 00653800 destructors, inlined by the original
    for (int i = 0; i < kOptionDefinitionCount; ++i) {
        // The original's temporary has only its string constructed; its other fields are copied
        // as the stack left them and all written below for both kinds.
        OptionSetting blank{};
        settings_.push_back(blank);
        OptionSetting& setting = settings_.back();
        const OptionDefinition* definition = &OptionDefinitions()[i];
        setting.value_ = 0;
        setting.definition_ = definition;
        ShownName(&setting.text_, kTextOptionPrefix, definition->name_);

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
            text = GameText(kTextOffDefault);
            GameTexts()->GetString(GameText(kTextOptionOff), &text);
            if (i == kDifficultySetting) GameTexts()->GetString(GameText(kTextDifficultyHard), &text);
            void* memory = gamecrt::operator_new(sizeof(CUiSpin));
            setting.spin_ = memory ? static_cast<CUiSpin*>(memory)->Construct(interface_, RECT{530, top + 160, 690, top + 176},
                                                                             text)
                                   : nullptr;
            text = GameText(kTextOnDefault);
            GameTexts()->GetString(GameText(kTextOptionOn), &text);
            if (i == kDifficultySetting) GameTexts()->GetString(GameText(kTextDifficultyEasy), &text);
            setting.spin_->AddText(text);
            AddChild(setting.spin_);
            setting.bar_ = nullptr;
        }
    }
}
EI_REPLACE_METHOD(0x00651940, COptionsWindow::BuildSettings);

// --- 00651600 ------------------------------------------------------------------------------------

void COptionsWindow::BuildBindings() {
    // clear(): 00652E30 copy and the 006537E0 destructors, inlined by the original.
    for (OptionBinding& binding : bindings_) binding.Destroy();
    bindings_.Finish() = bindings_.Start();

    for (int i = 0; i < kActionDefinitionCount; ++i) {
        const ActionDefinition* action = &ActionDefinitions()[i];
        AppendBlankBinding(bindings_);
        OptionBinding& binding = bindings_.back();
        binding.action_ = action;
        ShownName(&binding.name_, kTextActionPrefix, action->name_);

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
            binding.keysText_ += GameText(kTextKeySeparator);
        }
        binding.keysText_.TrimRight(GameText(kTextKeyTrim));
    }
}
EI_REPLACE_METHOD(0x00651600, COptionsWindow::BuildBindings);

// --- 00652200 ------------------------------------------------------------------------------------

void COptionsWindow::LoadSettings() {
    std::memcpy(keyOfScan_, KeyActions(), sizeof(keyOfScan_));
    OptionSetting* s = settings_.begin();
    const double fromStep = Constant(kConstFromStep);
    const float one = Constant(kConstOne);
    s[0].value_ = OptionGlobal<int>(kOptVolume0);
    s[1].value_ = OptionGlobal<int>(kOptVolume1);
    s[2].value_ = OptionGlobal<int>(kOptVolume2);
    s[24].value_ = OptionGlobal<int>(kOpt24);
    // _ftol: truncation.
    s[3].value_ = static_cast<int>(OptionGlobal<float>(kOptSpeedA) * fromStep);
    s[4].value_ = static_cast<int>(OptionGlobal<float>(kOptSpeedB) * fromStep);
    // FCOMP; TEST AH,0x40: equal (or unordered) gives 0.
    float sign22 = OptionGlobal<float>(kOptSign22);
    float sign23 = OptionGlobal<float>(kOptSign23);
    s[22].value_ = x87::IsNaN(sign22) || sign22 == one ? 0 : 1;
    s[23].value_ = x87::IsNaN(sign23) || sign23 == one ? 0 : 1;
    s[5].value_ = OptionGlobal<int>(kOpt5);
    s[6].value_ = OptionGlobal<int>(kOpt6Tenths) / 10;
    s[20].value_ = static_cast<int>(OptionGlobal<float>(kOpt20) * static_cast<double>(Constant(kConstScrollFrom)));
    s[7].value_ = OptionGlobal<int>(kOptDisplay7);
    s[8].value_ = OptionGlobal<int>(kOptDisplay8);
    s[9].value_ = OptionGlobal<int>(kOptDisplay9);
    s[10].value_ = OptionGlobal<int>(kOpt10);
    s[11].value_ = OptionGlobal<int>(kOpt11);
    s[12].value_ = OptionGlobal<int>(kOpt12);
    s[13].value_ = OptionGlobal<int>(kOpt13);
    s[17].value_ = OptionGlobal<int>(kOpt17);
    s[18].value_ = OptionGlobal<int>(kOptHelp18);
    s[19].value_ = OptionGlobal<int>(kOpt19);
    s[21].value_ = OptionGlobal<int>(kOpt21);
    s[14].value_ = static_cast<int>((static_cast<double>(OptionGlobal<float>(kOptGamma14)) + one) * fromStep);
    s[15].value_ = static_cast<int>((static_cast<double>(OptionGlobal<float>(kOptGamma15)) + one) * fromStep);
    s[16].value_ = static_cast<int>((static_cast<double>(OptionGlobal<float>(kOptGamma16)) + one) * fromStep);
    s[25].value_ = GameDifficulty();

    for (int i = 0; i < kOptionDefinitionCount; ++i) {
        if (s[i].bar_)
            s[i].bar_->CallSetPosition(s[i].value_);
        else if (s[i].spin_)
            s[i].spin_->SetValue(s[i].value_);
    }
}
EI_REPLACE_METHOD(0x00652200, COptionsWindow::LoadSettings);

// --- 006524A0 ------------------------------------------------------------------------------------

void COptionsWindow::Apply() {
    std::memcpy(KeyActionTable(), keyOfScan_, sizeof(keyOfScan_));
    const OptionSetting* s = settings_.begin();
    const double toStep = Constant(kConstToStep);
    const float one = Constant(kConstOne);
    OptionGlobal<int>(kOptVolume0) = s[0].value_;
    OptionGlobal<int>(kOptVolume1) = s[1].value_;
    OptionGlobal<int>(kOptVolume2) = s[2].value_;
    OptionGlobal<int>(kOpt24) = s[24].value_;
    // Each product is rounded once by its float stores.
    float speedA = static_cast<float>(s[3].value_ * toStep);
    float speedB = static_cast<float>(s[4].value_ * toStep);
    OptionGlobal<float>(kOptSpeedA) = speedA;
    OptionGlobal<float>(kOptSpeedA2) = speedA;
    OptionGlobal<float>(kOptSpeedA3) = speedA;
    OptionGlobal<float>(kOptSpeedA4) = speedA;
    OptionGlobal<float>(kOptSpeedB) = speedB;
    OptionGlobal<float>(kOptSpeedB2) = speedB;
    OptionGlobal<float>(kOptSpeedB3) = speedB;
    OptionGlobal<float>(kOptSpeedB4) = speedB;
    OptionGlobal<float>(kOptSign22) = s[22].value_ == 1 ? -1.0f : 1.0f;
    OptionGlobal<float>(kOptSign23) = s[23].value_ == 1 ? -1.0f : 1.0f;
    OptionGlobal<int>(kOpt5) = s[5].value_;
    OptionGlobal<int>(kOpt6Tenths) = s[6].value_ * 10;
    OptionGlobal<float>(kOpt20) = static_cast<float>(s[20].value_ * static_cast<double>(Constant(kConstScrollTo)));
    OptionGlobal<int>(kOptDisplay7) = s[7].value_;
    OptionGlobal<int>(kOptDisplay8) = s[8].value_;
    OptionGlobal<int>(kOptDisplay9) = s[9].value_;
    OptionGlobal<int>(kOpt10) = s[10].value_;
    OptionGlobal<int>(kOpt11) = s[11].value_;
    OptionGlobal<int>(kOpt12) = s[12].value_;
    OptionGlobal<int>(kOpt13) = s[13].value_;
    OptionGlobal<int>(kOpt17) = s[17].value_;
    OptionGlobal<int>(kOptHelp18) = s[18].value_;
    OptionGlobal<int>(kOpt19) = s[19].value_;
    OptionGlobal<int>(kOpt21) = s[21].value_;
    OptionGlobal<float>(kOptGamma14) = static_cast<float>(s[14].value_ * toStep - one);
    OptionGlobal<float>(kOptGamma15) = static_cast<float>(s[15].value_ * toStep - one);
    OptionGlobal<float>(kOptGamma16) = static_cast<float>(s[16].value_ * toStep - one);
    if (!IsMultiplayerClient()) {
        OptionGlobal<int>(kOptDifficultyCopy) = s[25].value_;
        GameDifficulty() = s[25].value_;
    }
    if (displayModeChanged_) ClientWorldRefreshObjectsOriginal(ClientWorldObject());
}
EI_REPLACE_METHOD(0x006524A0, COptionsWindow::Apply);

// --- 0065B8B0: the slider's slot 26 ---------------------------------------------------------------

namespace {
// The slider's sprites lie along x: a quarter turn about z (the angle is the float at 007C8210).
void TurnQuarter(CUiMesh* sprite) {
    Vector3 axis = {0.0f, 0.0f, 1.0f};
    Quaternion turn;
    sprite->orientation_ = *turn.SetAxisAngle(axis, Constant(kQuarterTurn));
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
    track->Place(static_cast<float>((rect->right + rect->left) * static_cast<double>(Constant(kConstHalf))),
                 static_cast<float>(rect->bottom - 5), depth_);
    TurnQuarter(track);

    CUiMesh* thumb = SceneSprite(this, 1);
    thumb->SetRectangle(depth_ - Constant(kConstThumbLift), 100.0f, 0.0f, 0.0f, 10.0f, 30.0f, 0x28, 8, 0x50, 0x78);
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
