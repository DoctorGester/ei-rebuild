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
    int currentSelected = selected;
    if (currentSelected < 0 || static_cast<unsigned>(currentSelected) >= static_cast<unsigned>(EntryCount())) return;
    nameTyped = false;
    name = entries[currentSelected].title;
    cursor = name.size();
}

void CSaveLoadWindow::LeftDown(int buttons, int x, int y) {
    if (sliding) return;
    int region = RegionAt(x, y);
    switch (region) {
    case 0:
        Confirm();
        break;
    case 1:
        ui->PlaySound(kSoundCancel);
        ui->SetPendingValue(kCloseSaveLoad);
        break;
    case 2:
        AskDelete();
        break;
    default:
        if (region >= 3) {
            unsigned index = static_cast<unsigned>(firstVisible + region - 3);
            if (index < static_cast<unsigned>(EntryCount())) {
                ui->PlaySound(kSoundSelect);
                if (static_cast<int>(index) != selected) {
                    selected = static_cast<int>(index);
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
    if (sliding) return;
    int region = RegionAt(x, y);
    if (region >= 3) {
        unsigned index = static_cast<unsigned>(firstVisible + region - 3);
        if (index < static_cast<unsigned>(EntryCount())) {
            if (static_cast<int>(index) != selected) {
                selected = static_cast<int>(index);
                TakeSelectedTitle();
            }
            Confirm();
        }
    }
    CUiContainer::LeftDouble(buttons, x, y);
}

void CSaveLoadWindow::Draw(CSceneRenderer* pass) {
    if (!sliding) {
        surfaces[0]->Show();
        models[0]->SetScreenPosition(400.0f, isSave ? 620.0f : 550.0f, 4.8f);
        CUiContainer::Draw(pass);
        return;
    }
    surfaces[0]->Hide(false);
    double e = Ease(slide);
    CUiModel* model = models[0];
    if (!model) return;
    double y = isSave ? (620.0f - 700.0f) * e + 700.0f : (550.0f - 700.0f) * e + 700.0f;
    model->SetScreenPosition(400.0f, static_cast<float>(y), 4.8f);
    model->Draw(pass);
}

void CSaveLoadWindow::Update() {
    CUiContainer::Update();
    if (sliding) {
        double slideAt = FrameTime() / 0.5f + slide;
        slide = static_cast<float>(slideAt);
        if (slideAt > 1.0f) {
            sliding = false;
            ui->OpenHelp(false);
        }
    }
    if (scrollbar->TakeMoved()) {
        firstVisible = scrollbar->Position();
        RefreshListVanilla();
    }
}

void CSaveLoadWindow::CloseScreen() {
    if (dialog) dialog->Release();
    dialog = nullptr;
}

void CSaveLoadWindow::MessageResult(int result) {
    if (result == kResultDelete)
        DeleteSave();
    else if (result == kResultOverwrite)
        SaveGame();
    else if (result == kResultLoad)
        ui->SetPendingValue(kCloseSaveLoadLoad);
}

// --- Options window ----------------------------------------------------------------------------

void COptionsWindow::RestoreSettings() {
    // The values the window saved when it opened.
    g_clientWorld.sounds->SetEffectsVolume(options::g_options.audioSfx);
    g_clientWorld.sounds->SetMusicVolume(options::g_options.audioStreaming);
    g_clientWorld.sounds->SetSpeechVolume(options::g_options.audioSpeech);
    // The scene renderer's gamma.
    g_sceneRenderer.SetGamma(options::g_options.brightness, options::g_options.contrast,
                              options::g_options.gamma);
}

void COptionsWindow::LeftDown(int buttons, int x, int y) {
    if (sliding) return;
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
        ui->PlaySound(sound);
        ui->SetPendingValue(kCloseOptions);
    } else {
        if (region >= 2 && region < 13) {
            ui->PlaySound(kSoundSelect);
            waitingForKey = false;
            ShowGroup(region - 2);
        }
        if (region >= 13) {
            OptionRow& row = RegionRow(region);
            if (row.binding || row.setting) {
                ui->PlaySound(kSoundSelect);
                selectedRow = region - 13;
                waitingForKey = false;
                RefreshRows();
            }
        }
    }
    CUiContainer::LeftDown(buttons, x, y);
}

void COptionsWindow::LeftDouble(int buttons, int x, int y) {
    if (sliding) return;
    int region = RegionAt(x, y);
    if (region >= 13) {
        OptionRow& row = RegionRow(region);
        if (row.binding) {
            ui->PlaySound(kSoundSelect);
            selectedRow = region - 13;
            waitingForKey = true;
            RefreshRows();
        } else if (row.setting && row.setting->spin) {
            const int value = row.setting->spin->TakeValue();
            RegionRow(region).setting->spin->SetValue(value + 1);
            ui->PlaySound(kSoundOk);
            RefreshRows();
        }
    }
    CUiContainer::LeftDouble(buttons, x, y);
}

void COptionsWindow::Draw(CSceneRenderer* pass) {
    if (!sliding) {
        surfaces[0]->Show();
        models[0]->SetScreenPosition(400.0f, 480.0f, 4.8f);
        CUiContainer::Draw(pass);
        return;
    }
    surfaces[0]->Hide(false);
    double e = Ease(slide);
    CUiModel* model = models[0];
    if (!model) return;
    double y = (480.0f - 700.0f) * e + 700.0f;
    model->SetScreenPosition(400.0f, static_cast<float>(y), 4.8f);
    model->Draw(pass);
}

void COptionsWindow::Redraw() {
    if (!sliding) CUiContainer::Redraw();
}

void COptionsWindow::MessageResult(int result) {
    if (result == 0x10001) CommitBinding();
    RefreshRows();
}

const char* COptionsWindow::HelpTopic() {
    // "options" unless a key binding waits for its key.
    return waitingForKey ? nullptr : "options";
}

// The settings the window applies while it is open (their ids, the first int of the definition).

void COptionsWindow::Update() {
    CUiContainer::Update();
    if (sliding) {
        double slideAt = FrameTime() / kSlideSeconds + static_cast<double>(slide);
        slide = static_cast<float>(slideAt);
        if (slideAt > 1.0f) {
            sliding = false;
            ui->OpenHelp(false);
        }
    }
    for (int i = 0; i < kSettingCount; ++i) {
        if (settings[i].bar && settings[i].bar->TakeMoved()) {
            const int value = settings[i].bar->Position();
            settings[i].value = value;
            unsigned definitionId = static_cast<unsigned>(settings[i].definition->id);
            if (definitionId == kSettingVolumeA) {
                g_clientWorld.sounds->SetEffectsVolume(value);
            } else if (definitionId == kSettingVolumeB) {
                g_clientWorld.sounds->SetMusicVolume(value);
            } else if (definitionId == kSettingVolumeC) {
                g_clientWorld.sounds->SetSpeechVolume(value);
            } else if (definitionId >= kSettingGammaFirst && definitionId <= kSettingGammaLast) {
                const OptionSetting* gamma = settings.data() + kSettingGammaFirst;
                float r = static_cast<float>(static_cast<double>(gamma[0].value) * kGammaScale - 1.0f);
                float g = static_cast<float>(static_cast<double>(gamma[1].value) * kGammaScale - 1.0f);
                float b = static_cast<float>(static_cast<double>(gamma[2].value) * kGammaScale - 1.0f);
                g_sceneRenderer.SetGamma(r, g, b);
            }
        }
        if (settings[i].spin && settings[i].spin->TakeChanged()) {
            const int value = settings[i].spin->Value();
            settings[i].value = value;
            int definitionId = settings[i].definition->id;
            if (definitionId >= 7) {
                if (definitionId <= 9)
                    shadowsChanged = true;
                else if (definitionId == 0x18)   // the sound system's pan switch
                    g_clientWorld.sounds->panDirect = value != 0;
            }
        }
    }
}

// --- Scroll bar --------------------------------------------------------------------------------

bool CUiScrollbar::TakeMoved() {
    if (!moved) return false;
    moved = false;
    return true;
}

int CUiScrollbar::TakePosition() {
    moved = false;
    return position;
}

void CUiScrollbar::Update() {
    if (state == 2 || state == 3) {
        // The container's FrameTime does not use the window.
        double step = CUiContainer::FrameTime() * speed * 16.0f;
        if (state == 2) step = -step;
        accumulated = static_cast<float>(step + accumulated);
    }
    int steps = 0;
    if (accumulated > 0.0f) {    // zero, negative and NaN take the other branch
        if (accumulated < 1.0f) return;
        // Whole steps; subtracting 1 is exact, so the register precision does not matter.
        double rest = accumulated;
        do {
            rest -= 1.0f;
            ++steps;
        } while (rest >= 1.0f);
        accumulated = static_cast<float>(rest);
    } else {
        if (accumulated > -1.0f) return;
        EI_NAN_AUDIT(accumulated);
        double rest;
        do {
            rest = accumulated + 1.0;
            --steps;
            accumulated = static_cast<float>(rest);
        } while (rest <= -1.0f);   // ends on a NaN (the original's did not)
    }
    if (steps != 0) SetPosition(position + steps);
}

void CUiScrollbar::LeftDown(int, int x, int y) {
    POINT point{x, y};
    ui->ScalePointFromScreenEntry(&point);
    if (gameapi::PtInRect(&thumb, point)) {
        CaptureMouse();
        state = 1;
        grabX = point.x - (thumb.left + thumb.right) / 2;
        grabY = point.y - (thumb.top + thumb.bottom) / 2;
        handled = true;
        return;
    }
    int region = RegionAt(x, y);
    if (region != 0 && region != 1) return;
    CaptureMouse();
    state = region == 0 ? 2 : 3;
    accumulated = 0.0f;
    handled = true;
}

void CUiScrollbar::LeftUp(int, int, int) {
    state = 0;
    ui->mouseCapture = nullptr;
    accumulated = 0.0f;
    handled = true;
}

// --- Tooltip -----------------------------------------------------------------------------------

void CUiTooltip::Draw(CSceneRenderer* pass) {
    if (!Shown()) return;
    DrawBase(pass);
}

void CUiTooltip::Redraw() {
    if (Shown()) CUiWindow::Redraw();
}
