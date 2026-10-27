// The save/load and options windows opened from the Esc menu: their mouse handlers, the slide-in
// drawing and update, and the small virtuals. See ui.h and docs/notes/ui.md.
#include "model.h"
#include "options.h"
#include <bit>
#include "ui.h"
#include "ui_screens.h"
#include "sound.h"

#include "ei/hook.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"

#include <cstdint>
#include <memory>
#include "game_data.h"
#include "game_singletons.h"
#include "lib/nan_audit.h"

namespace {
constexpr const char* kSoundCancel = "buttons\\messbox\\cancel.wav";
constexpr const char* kSoundOk = "buttons\\messbox\\ok.wav";
constexpr const char* kSoundSelect = "buttons\\save\\select.wav";

// Pending values a window hands the interface when it closes (CInterface3D::SetPendingValue):
// the low word 1 closes the top window, the high word is the result its parent receives.
constexpr std::uint32_t kCloseSaveLoad = 0x100001;
constexpr std::uint32_t kCloseSaveLoadLoad = 0x100002;
constexpr std::uint32_t kCloseOptions = 0x110001;

// Results of the save/load window's message boxes.
constexpr int kResultDelete = 0x10001;
constexpr int kResultOverwrite = 0x20001;
constexpr int kResultLoad = 0x30001;

// The windows slide in: the model's y goes from 700 to its resting place with an ease-in-out
// curve over the slide (0..1). x87, unrounded until the float argument is stored.
double Ease(float slide) {
    EI_NAN_AUDIT(slide);
    if (slide < 0.5f) {
        double s = slide;
        double e = s * s;
        return e + e;
    }
    double r = 1.0f - static_cast<double>(slide);
    double e = r * r;
    e = e + e;
    return 1.0f - e;
}

} // namespace

// --- Helpers -----------------------------------------------------------------------------------


// --- Save/load window --------------------------------------------------------------------------

void CSaveLoadWindow::TakeSelectedTitle() {
    int selected = selected_;
    if (selected < 0 || static_cast<unsigned>(selected) >= static_cast<unsigned>(EntryCount())) return;
    nameTyped_ = false;
    name_ = entries_.begin()[selected].title_;
    cursor_ = name_.GetLength();
}
EI_REWRITTEN(0x006577B0, CSaveLoadWindow::TakeSelectedTitle);

void CSaveLoadWindow::OnMouseDown(int buttons, int x, int y) {
    if (sliding_) return;
    int region = RegionAt(x, y);
    switch (region) {
    case 0:
        Confirm();
        break;
    case 1:
        interface_->PlaySound(kSoundCancel, 0);
        interface_->SetPendingValue(kCloseSaveLoad);
        break;
    case 2:
        AskDelete();
        break;
    default:
        if (region >= 3) {
            unsigned index = static_cast<unsigned>(firstVisible_ + region - 3);
            if (index < static_cast<unsigned>(EntryCount())) {
                interface_->PlaySound(kSoundSelect, 0);
                if (static_cast<int>(index) != selected_) {
                    selected_ = static_cast<int>(index);
                    TakeSelectedTitle();
                }
                RefreshListVanilla();
                ShowPicture();
            }
        }
        break;
    }
    CUiContainer::LeftDown(buttons, x, y);
}

void CSaveLoadWindow::OnDoubleClick(int buttons, int x, int y) {
    if (sliding_) return;
    int region = RegionAt(x, y);
    if (region >= 3) {
        unsigned index = static_cast<unsigned>(firstVisible_ + region - 3);
        if (index < static_cast<unsigned>(EntryCount())) {
            if (static_cast<int>(index) != selected_) {
                selected_ = static_cast<int>(index);
                TakeSelectedTitle();
            }
            Confirm();
        }
    }
    CUiContainer::LeftDouble(buttons, x, y);
}

BOOL CSaveLoadWindow::Draw(int pass) {
    if (!sliding_) {
        surfaces_.data[0]->Freeze(1, 0);
        models_.data[0]->SetScreenPosition(400.0f, isSave_ ? 620.0f : 550.0f, 4.8f);
        CUiContainer::Draw(pass);
        return 1;
    }
    surfaces_.data[0]->Freeze(0, 0);
    double e = Ease(slide_);
    CUiModel* model = models_.data[0];
    if (!model) return 1;
    double y = isSave_ ? (620.0f - 700.0f) * e + 700.0f : (550.0f - 700.0f) * e + 700.0f;
    model->SetScreenPosition(400.0f, static_cast<float>(y), 4.8f);
    model->Draw(reinterpret_cast<CSceneRenderer*>(pass));
    return 1;
}

void CSaveLoadWindow::Update() {
    UpdateBase();
    if (sliding_) {
        double slide = FrameTime() / 0.5f + slide_;
        slide_ = static_cast<float>(slide);
        if (slide > 1.0f) {
            sliding_ = false;
            interface_->OpenHelp(0);
        }
    }
    int position;
    if (scrollbar_->TakePosition(&position)) {
        firstVisible_ = position;
        RefreshListVanilla();
    }
}

void CSaveLoadWindow::CloseDialog() {
    if (dialog_) dialog_->Release();
    dialog_ = nullptr;
}

void CSaveLoadWindow::OnDialogResult(int result) {
    if (result == kResultDelete)
        DeleteSave();
    else if (result == kResultOverwrite)
        SaveGame();
    else if (result == kResultLoad)
        interface_->SetPendingValue(kCloseSaveLoadLoad);
}

namespace {
template <typename W>
W* FromInput(void* input) {
    return reinterpret_cast<W*>(static_cast<std::uint8_t*>(input) - 0x94);
}

} // namespace

EI_REWRITTEN(0x00654660, CSaveLoadWindow::OnMouseDown);
EI_REWRITTEN(0x006547B0, CSaveLoadWindow::OnDoubleClick);
EI_REWRITTEN(0x006544A0, CSaveLoadWindow::Draw);
EI_REWRITTEN(0x006545E0, CSaveLoadWindow::Update);
EI_REWRITTEN(0x00654430, CSaveLoadWindow::CloseDialog);
EI_REWRITTEN(0x00654450, CSaveLoadWindow::OnDialogResult);

// --- Options window ----------------------------------------------------------------------------

void COptionsWindow::RestoreSettings() {
    // The values the window saved when it opened.
    TheSoundManager()->SetEffectsVolume(options::TheOptions()->audioSfx_);
    TheSoundManager()->SetMusicVolume(options::TheOptions()->audioStreaming_);
    TheSoundManager()->SetSpeechVolume(options::TheOptions()->audioSpeech_);
    // The scene renderer's gamma.
    SceneRenderer()->SetGamma(options::TheOptions()->brightness_, options::TheOptions()->contrast_,
                              options::TheOptions()->gamma_);
}

void COptionsWindow::OnMouseDown(int buttons, int x, int y) {
    if (sliding_) return;
    int region = RegionAt(x, y);
    if (region == 0 || region == 1) {
        const char* sound;
        if (region == 0) {
            Apply();
            sound = kSoundOk;
        } else {
            RestoreSettings();
            sound = kSoundCancel;
        }
        interface_->PlaySound(sound, 0);
        interface_->SetPendingValue(kCloseOptions);
    } else {
        if (region >= 2 && region < 13) {
            interface_->PlaySound(kSoundSelect, 0);
            waitingForKey_ = false;
            ShowGroup(region - 2);
        }
        if (region >= 13) {
            OptionRow& row = RegionRow(region);
            if (row.binding_ || row.setting_) {
                interface_->PlaySound(kSoundSelect, 0);
                selectedRow_ = region - 13;
                waitingForKey_ = false;
                RefreshRows();
            }
        }
    }
    CUiContainer::LeftDown(buttons, x, y);
}

void COptionsWindow::OnDoubleClick(int buttons, int x, int y) {
    if (sliding_) return;
    int region = RegionAt(x, y);
    if (region >= 13) {
        OptionRow& row = RegionRow(region);
        if (row.binding_) {
            interface_->PlaySound(kSoundSelect, 0);
            selectedRow_ = region - 13;
            waitingForKey_ = true;
            RefreshRows();
        } else if (row.setting_ && row.setting_->spin_) {
            int value;
            row.setting_->spin_->GetValue(&value);
            RegionRow(region).setting_->spin_->SetValue(value + 1);
            interface_->PlaySound(kSoundOk, 0);
            RefreshRows();
        }
    }
    CUiContainer::LeftDouble(buttons, x, y);
}

BOOL COptionsWindow::Draw(int pass) {
    if (!sliding_) {
        surfaces_.data[0]->Freeze(1, 0);
        models_.data[0]->SetScreenPosition(400.0f, 480.0f, 4.8f);
        CUiContainer::Draw(pass);
        return 1;
    }
    surfaces_.data[0]->Freeze(0, 0);
    double e = Ease(slide_);
    CUiModel* model = models_.data[0];
    if (!model) return 1;
    double y = (480.0f - 700.0f) * e + 700.0f;
    model->SetScreenPosition(400.0f, static_cast<float>(y), 4.8f);
    model->Draw(reinterpret_cast<CSceneRenderer*>(pass));
    return 1;
}

void COptionsWindow::Redraw() {
    if (!sliding_) RedrawContainer();
}

void COptionsWindow::OnDialogResult(int result) {
    if (result == 0x10001) CommitBinding();
    RefreshRows();
}

const char* COptionsWindow::HelpTopic() {
    // "options" unless a key binding waits for its key.
    return waitingForKey_ ? nullptr : "options";
}

// The settings the window applies while it is open (their ids, the first int of the definition).
namespace {
constexpr int kSettingVolumeA = 0;     // \ volumes, applied at once
constexpr int kSettingVolumeB = 1;     //  |
constexpr int kSettingVolumeC = 2;     // /
constexpr int kSettingGammaFirst = 14; // the display's three gamma values (settings 14-16)
constexpr int kSettingGammaLast = 16;
constexpr int kSettingCount = 26;
constexpr float kGammaScale = 0.02f;   // value * 0.02 - 1
constexpr float kSlideSeconds = 0.5f;
} // namespace

void COptionsWindow::Update() {
    UpdateBase();
    if (sliding_) {
        double slide = FrameTime() / kSlideSeconds + static_cast<double>(slide_);
        slide_ = static_cast<float>(slide);
        if (slide > 1.0f) {
            sliding_ = false;
            interface_->OpenHelp(0);
        }
    }
    for (int i = 0; i < kSettingCount; ++i) {
        int value;
        if (settings_[i].bar_ && settings_[i].bar_->TakePosition(&value)) {
            settings_[i].value_ = value;
            unsigned id = static_cast<unsigned>(settings_[i].definition_->id_);
            if (id == kSettingVolumeA) {
                TheSoundManager()->SetEffectsVolume(value);
            } else if (id == kSettingVolumeB) {
                TheSoundManager()->SetMusicVolume(value);
            } else if (id == kSettingVolumeC) {
                TheSoundManager()->SetSpeechVolume(value);
            } else if (id >= kSettingGammaFirst && id <= kSettingGammaLast) {
                const OptionSetting* gamma = settings_.begin() + kSettingGammaFirst;
                float r = static_cast<float>(static_cast<double>(gamma[0].value_) * kGammaScale - 1.0f);
                float g = static_cast<float>(static_cast<double>(gamma[1].value_) * kGammaScale - 1.0f);
                float b = static_cast<float>(static_cast<double>(gamma[2].value_) * kGammaScale - 1.0f);
                SceneRenderer()->SetGamma(r, g, b);
            }
        }
        if (settings_[i].spin_ && settings_[i].spin_->GetValue(&value)) {
            settings_[i].value_ = value;
            int id = settings_[i].definition_->id_;
            if (id >= 7) {
                if (id <= 9)
                    shadowsChanged_ = true;
                else if (id == 0x18)   // the sound system's pan switch
                    TheSoundManager()->panDirect = value != 0;
            }
        }
    }
}

EI_REWRITTEN(0x00650C20, COptionsWindow::OnMouseDown);
EI_REWRITTEN(0x00650D70, COptionsWindow::OnDoubleClick);
EI_REWRITTEN(0x00650920, COptionsWindow::Draw);
EI_REWRITTEN(0x00650A20, COptionsWindow::Redraw);
EI_REWRITTEN(0x006508E0, COptionsWindow::OnDialogResult);
EI_REWRITTEN(0x00650900, COptionsWindow::HelpTopic);
EI_REWRITTEN(0x00650A30, COptionsWindow::Update);

// --- The destructor body ------------------------------------------------------------------

COptionsWindow::~COptionsWindow() {
    groupNames_.Destruct();
    for (OptionSetting& setting : settings_) std::destroy_at(&setting.text_);
    settings_.FreeStorage();
    for (OptionBinding& binding : bindings_) binding.Destroy();
    bindings_.FreeStorage();
}
EI_REWRITTEN_SPECIAL(0x0062B180, COptionsWindow::~COptionsWindow);
EI_REWRITTEN_SPECIAL(0x0062B160, COptionsWindow::~COptionsWindow);
EI_REWRITTEN_SPECIAL(0x0062B770, COptionsWindow::~COptionsWindow);

// --- Scroll bar --------------------------------------------------------------------------------

BOOL CUiScrollbar::TakePosition(int* position) {
    *position = position_;
    if (!moved_) return 0;
    moved_ = false;
    return 1;
}

void CUiScrollbar::Update() {
    if (state_ == 2 || state_ == 3) {
        // The container's FrameTime does not use the window.
        double step = reinterpret_cast<CUiContainer*>(this)->FrameTime() * speed_ * 16.0f;
        if (state_ == 2) step = -step;
        accumulated_ = static_cast<float>(step + accumulated_);
    }
    int steps = 0;
    if (accumulated_ > 0.0f) {   // zero, negative and NaN take the other branch
        if (accumulated_ < 1.0f) return;
        // Whole steps; subtracting 1 is exact, so the register precision does not matter.
        double rest = accumulated_;
        do {
            rest -= 1.0f;
            ++steps;
        } while (rest >= 1.0f);
        accumulated_ = static_cast<float>(rest);
    } else {
        if (accumulated_ > -1.0f) return;
        EI_NAN_AUDIT(accumulated_);
        double rest;
        do {
            rest = accumulated_ + 1.0;
            --steps;
            accumulated_ = static_cast<float>(rest);
        } while (rest <= -1.0f);   // ends on a NaN (the original's did not)
    }
    if (steps != 0) SetPosition(position_ + steps);
}

void CUiScrollbar::OnMouseDown(int, int x, int y) {
    POINT point{x, y};
    interface_->ScalePointFromScreenEntry(&point);
    if (gameapi::PtInRect(&thumb_, point)) {
        CaptureMouse();
        state_ = 1;
        grabX_ = point.x - (thumb_.left + thumb_.right) / 2;
        grabY_ = point.y - (thumb_.top + thumb_.bottom) / 2;
        handled_ = true;
        return;
    }
    int region = RegionAt(x, y);
    if (region != 0 && region != 1) return;
    CaptureMouse();
    state_ = region == 0 ? 2 : 3;
    accumulated_ = 0.0f;
    handled_ = true;
}

void CUiScrollbar::OnMouseUp(int, int, int) {
    state_ = 0;
    interface_->mouseCapture_ = nullptr;
    accumulated_ = 0.0f;
    handled_ = true;
}

EI_REWRITTEN(0x0065B090, CUiScrollbar::TakePosition);
EI_REWRITTEN(0x0065B360, CUiScrollbar::Update);
EI_REWRITTEN(0x0065B240, CUiScrollbar::OnMouseDown);
EI_REWRITTEN(0x0065B340, CUiScrollbar::OnMouseUp);

// --- Tooltip -----------------------------------------------------------------------------------

BOOL CUiTooltip::Draw(int pass) {
    if (!Shown()) return 1;
    return DrawBase(pass);
}

void CUiTooltip::Redraw() {
    if (Shown()) RedrawBase();
}

EI_REWRITTEN(0x0065EA50, CUiTooltip::Draw);
EI_REWRITTEN(0x0065EA80, CUiTooltip::Redraw);
