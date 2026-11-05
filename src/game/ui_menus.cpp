// The save/load and options windows opened from the Esc menu: their mouse handlers, the slide-in
// drawing and update, and the small virtuals. See ui.h and docs/notes/ui.md.
#include "model.h"
#include "options.h"
#include <bit>
#include "ui.h"
#include "ui_screens.h"
#include "sound.h"
#include "texture.h"

#include "lib/containers.h"
#include "lib/rect.h"
#include <cstdint>

#include <memory>
#include "game_singletons.h"
#include "world.h"

namespace {
// Pending values a window hands the interface when it closes (Interface3D::SetPendingValue):
// the low word 1 closes the top window, the high word is the result its parent receives.
constexpr std::uint32_t kCloseSaveLoad = 0x100001;
constexpr std::uint32_t kCloseSaveLoadLoad = 0x100002;
constexpr std::uint32_t kCloseOptions = 0x110001;

// Results of the save/load window's message boxes.
constexpr std::uint32_t kResultDelete = BoxResult(MessageQuestion::First, MessageAnswer::Ok);
constexpr std::uint32_t kResultOverwrite = BoxResult(MessageQuestion::Second, MessageAnswer::Ok);
constexpr std::uint32_t kResultNoSpace = BoxResult(MessageQuestion::Third, MessageAnswer::Ok);

// The windows slide in: the model's y goes from 700 to its resting place with an ease-in-out
// curve over the slide (0..1).
float Ease(float slide) {
    if (slide < 0.5f) {
        float s = slide;
        float e = s * s;
        return e + e;
    }
    float r = 1.0f - slide;
    float e = r * r;
    e += e;
    return 1.0f - e;
}

constexpr float kGammaScale = 0.02f;   // value * 0.02 - 1
constexpr float kSlideSeconds = 0.5f;

} // namespace

// --- Save/load window --------------------------------------------------------------------------

void SaveLoadWindow::TakeSelectedTitle() {
    int currentSelected = selected;
    if (currentSelected < 0 || static_cast<unsigned>(currentSelected) >= static_cast<unsigned>(EntryCount())) return;
    nameTyped = false;
    name = entries[currentSelected].title;
    cursor = lib::Count(name);
}

void SaveLoadWindow::LeftDown(UiEvent& event) {
    if (sliding) return;
    UiRegion* region = RegionUnder(event.at);
    const int row = RegionIndex(rows, region);
    if (region == okButton) {
        Confirm();
    } else if (region == cancelButton) {
        ui->PlayInterfaceSound(kMessageBoxCancelSound);
        ui->SetPendingValue(kCloseSaveLoad);
    } else if (region == deleteButton) {
        AskDelete();
    } else if (row >= 0) {
        unsigned index = static_cast<unsigned>(firstVisible + row);
        if (index < static_cast<unsigned>(EntryCount())) {
            ui->PlayInterfaceSound(kSaveSelectSound);
            if (static_cast<int>(index) != selected) {
                selected = static_cast<int>(index);
                TakeSelectedTitle();
            }
            RefreshListVanilla();
            ShowPicture();
        }
    }
    UiContainer::OnEvent(event);
}

void SaveLoadWindow::LeftDouble(UiEvent& event) {
    if (sliding) return;
    const int row = RegionIndex(rows, RegionUnder(event.at));
    if (row >= 0) {
        unsigned index = static_cast<unsigned>(firstVisible + row);
        if (index < static_cast<unsigned>(EntryCount())) {
            if (static_cast<int>(index) != selected) {
                selected = static_cast<int>(index);
                TakeSelectedTitle();
            }
            Confirm();
        }
    }
    UiContainer::OnEvent(event);
}

void SaveLoadWindow::DrawSprites(SceneRenderer* pass) {
    if (!sliding) {
        surfaces[0]->Show();
        models[0]->SetScreenPosition(400.0f, isSave ? 620.0f : 550.0f, 4.8f);
        UiContainer::DrawSprites(pass);
        return;
    }
    surfaces[0]->Hide();
    float e = Ease(slide);
    UiModel* model = models[0];
    if (!model) return;
    float y = isSave ? (620.0f - 700.0f) * e + 700.0f : (550.0f - 700.0f) * e + 700.0f;
    model->SetScreenPosition(400.0f, y, 4.8f);
    model->Draw(pass);
}

void SaveLoadWindow::Update() {
    UiContainer::Update();
    if (sliding) {
        float slideAt = FrameTime() / 0.5f + slide;
        slide = slideAt;
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

void SaveLoadWindow::CloseScreen() {
    if (dialog) dialog->Release();
    dialog = nullptr;
    if (picture) picture->Release();
    picture = nullptr;
}

void SaveLoadWindow::MessageResult(std::uint32_t result) {
    if (result == kResultDelete)
        DeleteSave();
    else if (result == kResultOverwrite)
        SaveGame();
    else if (result == kResultNoSpace)
        ui->SetPendingValue(kCloseSaveLoadLoad);
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
        ui->PlayInterfaceSound(sound);
        ui->SetPendingValue(kCloseOptions);
    } else {
        if (const int groupIndex = RegionIndex(groupButtons, region); groupIndex >= 0) {
            ui->PlayInterfaceSound(kSaveSelectSound);
            waitingForKey = false;
            ShowGroup(groupIndex);
        }
        if (const int rowIndex = RegionIndex(rowAreas, region); rowIndex >= 0) {
            const OptionRow& row = rows[rowIndex];
            if (row.binding || row.setting) {
                ui->PlayInterfaceSound(kSaveSelectSound);
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
            ui->PlayInterfaceSound(kSaveSelectSound);
            selectedRow = rowIndex;
            waitingForKey = true;
            RefreshRows();
        } else if (row.setting && row.setting->list) {
            ui->PlayInterfaceSound(kMessageBoxOkSound);
            row.setting->list->Open();
        } else if (row.setting && row.setting->spin) {
            const int value = row.setting->spin->TakeValue();
            row.setting->spin->SetValue(value + 1);
            ui->PlayInterfaceSound(kMessageBoxOkSound);
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
    UiModel* model = models[0];
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
            ui->OpenHelp(false);
        }
    }
    for (int i = 0; i < kOptionCount; ++i) {
        OptionSetting& setting = settings[i];
        if (setting.bar && setting.bar->TakeMoved()) {
            const int value = setting.bar->Position();
            setting.value = value;
            if (i == kOptionSfxVolume) {
                g_clientWorld.sounds->SetEffectsVolume(value);
            } else if (i == kOptionStreamVolume) {
                g_clientWorld.sounds->SetMusicVolume(value);
            } else if (i == kOptionVoiceVolume) {
                g_clientWorld.sounds->SetSpeechVolume(value);
            } else if (i == kOptionBrightness || i == kOptionGamma) {
                float brightness = settings[kOptionBrightness].value * kGammaScale - 1.0f;
                float gamma = settings[kOptionGamma].value * kGammaScale - 1.0f;
                g_sceneRenderer.SetGamma(brightness, gamma);
            }
        }
        if (setting.list && setting.list->TakeChanged()) setting.value = setting.list->value;
        if (setting.spin && setting.spin->TakeChanged()) {
            const int value = setting.spin->Value();
            setting.value = value;
            if (i == kOptionShadows)
                shadowsChanged = true;
            else if (i == kOptionReverseStereo)   // the sound system's pan switch
                g_clientWorld.sounds->panDirect = value != 0;
        }
    }
}

// --- Scroll bar --------------------------------------------------------------------------------

bool UiScrollbar::TakeMoved() {
    if (!moved) return false;
    moved = false;
    return true;
}

int UiScrollbar::TakePosition() {
    moved = false;
    return position;
}

void UiScrollbar::Update() {
    if (state == 2 || state == 3) {
        // The container's FrameTime does not use the window.
        float step = UiContainer::FrameTime() * speed * 16.0f;
        if (state == 2) step = -step;
        accumulated = step + accumulated;
    }
    int steps = 0;
    if (accumulated > 0.0f) {    // zero, negative and NaN take the other branch
        if (accumulated < 1.0f) return;
        // Whole steps.
        float rest = accumulated;
        do {
            rest -= 1.0f;
            ++steps;
        } while (rest >= 1.0f);
        accumulated = rest;
    } else {
        if (accumulated > -1.0f) return;
        float rest;
        do {
            rest = accumulated + 1.0f;
            --steps;
            accumulated = rest;
        } while (rest <= -1.0f);   // ends on a NaN (the original's did not)
    }
    if (steps != 0) SetPosition(position + steps);
}

void UiScrollbar::OnEvent(UiEvent& event) {
    switch (event.kind) {
    case UiEventKind::LeftDown: case UiEventKind::LeftDouble: LeftDown(event); break;
    case UiEventKind::LeftUp: LeftUp(event); break;
    case UiEventKind::MouseMove: MouseMove(event); break;
    case UiEventKind::Wheel: Wheel(event); break;
    default: break;
    }
}

void UiScrollbar::LeftDown(UiEvent& event) {
    // The arrows' region from the screen point (the original tested the point scaled twice: no arrow took a click at
    // any size but 800x600).
    const UiRegion* region = RegionUnder(event.at);
    Point at = event.at;
    ui->ScalePointFromScreen(&at);
    if (thumb.Contains(at)) {
        CaptureMouse();
        state = 1;
        grabX = at.x - (thumb.left + thumb.right) / 2;
        grabY = at.y - (thumb.top + thumb.bottom) / 2;
        event.handled = true;
        return;
    }
    if (region != upButton && region != downButton) return;
    CaptureMouse();
    state = region == upButton ? 2 : 3;
    accumulated = 0.0f;
    event.handled = true;
}

void UiScrollbar::LeftUp(UiEvent& event) {
    state = 0;
    ui->mouseCapture = nullptr;
    accumulated = 0.0f;
    event.handled = true;
}

