// The save/load and options windows opened from the Esc menu: their mouse handlers, the slide-in
// drawing and update, and the small virtuals. See ui.h and docs/notes/ui.md.
#include "model.h"
#include "options.h"
#include <bit>
#include "ui.h"
#include "ui_screens.h"
#include "sound.h"
#include "texture.h"

#include "lib/gameapi.h"
#include <cstdint>

#include <memory>
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

constexpr int kSettingVolumeA = 0;     // \ volumes, applied at once
constexpr int kSettingVolumeB = 1;     //  |
constexpr int kSettingVolumeC = 2;     // /
constexpr int kSettingGammaFirst = 14; // the display's three gamma values (settings 14-16)
constexpr int kSettingGammaLast = 16;
constexpr int kSettingCount = 26;
constexpr float kGammaScale = 0.02f;   // value * 0.02 - 1
constexpr float kSlideSeconds = 0.5f;

} // namespace

// --- Save/load window --------------------------------------------------------------------------

void CSaveLoadWindow::TakeSelectedTitle() {
    int selected = selected_;
    if (selected < 0 || static_cast<unsigned>(selected) >= static_cast<unsigned>(EntryCount())) return;
    nameTyped_ = false;
    name_ = entries_[selected].title_;
    cursor_ = name_.size();
}

void CSaveLoadWindow::LeftDown(int buttons, int x, int y) {
    if (sliding_) return;
    int region = RegionAt(x, y);
    switch (region) {
    case 0:
        Confirm();
        break;
    case 1:
        interface_->PlaySound(kSoundCancel);
        interface_->SetPendingValue(kCloseSaveLoad);
        break;
    case 2:
        AskDelete();
        break;
    default:
        if (region >= 3) {
            unsigned index = static_cast<unsigned>(firstVisible_ + region - 3);
            if (index < static_cast<unsigned>(EntryCount())) {
                interface_->PlaySound(kSoundSelect);
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

void CSaveLoadWindow::LeftDouble(int buttons, int x, int y) {
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

void CSaveLoadWindow::Draw(CSceneRenderer* pass) {
    if (!sliding_) {
        surfaces_[0]->Show();
        models_[0]->SetScreenPosition(400.0f, isSave_ ? 620.0f : 550.0f, 4.8f);
        CUiContainer::Draw(pass);
        return;
    }
    surfaces_[0]->Hide(false);
    double e = Ease(slide_);
    CUiModel* model = models_[0];
    if (!model) return;
    double y = isSave_ ? (620.0f - 700.0f) * e + 700.0f : (550.0f - 700.0f) * e + 700.0f;
    model->SetScreenPosition(400.0f, static_cast<float>(y), 4.8f);
    model->Draw(pass);
}

void CSaveLoadWindow::Update() {
    CUiContainer::Update();
    if (sliding_) {
        double slide = FrameTime() / 0.5f + slide_;
        slide_ = static_cast<float>(slide);
        if (slide > 1.0f) {
            sliding_ = false;
            interface_->OpenHelp(false);
        }
    }
    if (scrollbar_->TakeMoved()) {
        firstVisible_ = scrollbar_->Position();
        RefreshListVanilla();
    }
}

void CSaveLoadWindow::CloseScreen() {
    if (dialog_) dialog_->Release();
    dialog_ = nullptr;
}

void CSaveLoadWindow::MessageResult(int result) {
    if (result == kResultDelete)
        DeleteSave();
    else if (result == kResultOverwrite)
        SaveGame();
    else if (result == kResultLoad)
        interface_->SetPendingValue(kCloseSaveLoadLoad);
}

// --- Options window ----------------------------------------------------------------------------

void COptionsWindow::RestoreSettings() {
    // The values the window saved when it opened.
    g_clientWorld.sounds_->SetEffectsVolume(options::g_options.audioSfx_);
    g_clientWorld.sounds_->SetMusicVolume(options::g_options.audioStreaming_);
    g_clientWorld.sounds_->SetSpeechVolume(options::g_options.audioSpeech_);
    // The scene renderer's gamma.
    g_sceneRenderer.SetGamma(options::g_options.brightness_, options::g_options.contrast_,
                              options::g_options.gamma_);
}

void COptionsWindow::LeftDown(int buttons, int x, int y) {
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
        interface_->PlaySound(sound);
        interface_->SetPendingValue(kCloseOptions);
    } else {
        if (region >= 2 && region < 13) {
            interface_->PlaySound(kSoundSelect);
            waitingForKey_ = false;
            ShowGroup(region - 2);
        }
        if (region >= 13) {
            OptionRow& row = RegionRow(region);
            if (row.binding_ || row.setting_) {
                interface_->PlaySound(kSoundSelect);
                selectedRow_ = region - 13;
                waitingForKey_ = false;
                RefreshRows();
            }
        }
    }
    CUiContainer::LeftDown(buttons, x, y);
}

void COptionsWindow::LeftDouble(int buttons, int x, int y) {
    if (sliding_) return;
    int region = RegionAt(x, y);
    if (region >= 13) {
        OptionRow& row = RegionRow(region);
        if (row.binding_) {
            interface_->PlaySound(kSoundSelect);
            selectedRow_ = region - 13;
            waitingForKey_ = true;
            RefreshRows();
        } else if (row.setting_ && row.setting_->spin_) {
            const int value = row.setting_->spin_->TakeValue();
            RegionRow(region).setting_->spin_->SetValue(value + 1);
            interface_->PlaySound(kSoundOk);
            RefreshRows();
        }
    }
    CUiContainer::LeftDouble(buttons, x, y);
}

void COptionsWindow::Draw(CSceneRenderer* pass) {
    if (!sliding_) {
        surfaces_[0]->Show();
        models_[0]->SetScreenPosition(400.0f, 480.0f, 4.8f);
        CUiContainer::Draw(pass);
        return;
    }
    surfaces_[0]->Hide(false);
    double e = Ease(slide_);
    CUiModel* model = models_[0];
    if (!model) return;
    double y = (480.0f - 700.0f) * e + 700.0f;
    model->SetScreenPosition(400.0f, static_cast<float>(y), 4.8f);
    model->Draw(pass);
}

void COptionsWindow::Redraw() {
    if (!sliding_) CUiContainer::Redraw();
}

void COptionsWindow::MessageResult(int result) {
    if (result == 0x10001) CommitBinding();
    RefreshRows();
}

const char* COptionsWindow::HelpTopic() {
    // "options" unless a key binding waits for its key.
    return waitingForKey_ ? nullptr : "options";
}

// The settings the window applies while it is open (their ids, the first int of the definition).

void COptionsWindow::Update() {
    CUiContainer::Update();
    if (sliding_) {
        double slide = FrameTime() / kSlideSeconds + static_cast<double>(slide_);
        slide_ = static_cast<float>(slide);
        if (slide > 1.0f) {
            sliding_ = false;
            interface_->OpenHelp(false);
        }
    }
    for (int i = 0; i < kSettingCount; ++i) {
        if (settings_[i].bar_ && settings_[i].bar_->TakeMoved()) {
            const int value = settings_[i].bar_->Position();
            settings_[i].value_ = value;
            unsigned id = static_cast<unsigned>(settings_[i].definition_->id_);
            if (id == kSettingVolumeA) {
                g_clientWorld.sounds_->SetEffectsVolume(value);
            } else if (id == kSettingVolumeB) {
                g_clientWorld.sounds_->SetMusicVolume(value);
            } else if (id == kSettingVolumeC) {
                g_clientWorld.sounds_->SetSpeechVolume(value);
            } else if (id >= kSettingGammaFirst && id <= kSettingGammaLast) {
                const OptionSetting* gamma = settings_.data() + kSettingGammaFirst;
                float r = static_cast<float>(static_cast<double>(gamma[0].value_) * kGammaScale - 1.0f);
                float g = static_cast<float>(static_cast<double>(gamma[1].value_) * kGammaScale - 1.0f);
                float b = static_cast<float>(static_cast<double>(gamma[2].value_) * kGammaScale - 1.0f);
                g_sceneRenderer.SetGamma(r, g, b);
            }
        }
        if (settings_[i].spin_ && settings_[i].spin_->TakeChanged()) {
            const int value = settings_[i].spin_->Value();
            settings_[i].value_ = value;
            int id = settings_[i].definition_->id_;
            if (id >= 7) {
                if (id <= 9)
                    shadowsChanged_ = true;
                else if (id == 0x18)   // the sound system's pan switch
                    g_clientWorld.sounds_->panDirect = value != 0;
            }
        }
    }
}

// --- Scroll bar --------------------------------------------------------------------------------

bool CUiScrollbar::TakeMoved() {
    if (!moved_) return false;
    moved_ = false;
    return true;
}

int CUiScrollbar::TakePosition() {
    moved_ = false;
    return position_;
}

void CUiScrollbar::Update() {
    if (state_ == 2 || state_ == 3) {
        // The container's FrameTime does not use the window.
        double step = CUiContainer::FrameTime() * speed_ * 16.0f;
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

void CUiScrollbar::LeftDown(int, int x, int y) {
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

void CUiScrollbar::LeftUp(int, int, int) {
    state_ = 0;
    interface_->mouseCapture_ = nullptr;
    accumulated_ = 0.0f;
    handled_ = true;
}

// --- Tooltip -----------------------------------------------------------------------------------

void CUiTooltip::Draw(CSceneRenderer* pass) {
    if (!Shown()) return;
    DrawBase(pass);
}

void CUiTooltip::Redraw() {
    if (Shown()) CUiWindow::Redraw();
}
