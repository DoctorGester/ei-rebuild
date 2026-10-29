// The Esc menu's save/load and options windows: their key handlers and the
// save/load window's selection helpers. See ui.h and docs/notes/ui.md.
// Both key handlers live on the window's input side. They pass the key to the base handler first, set the
// input side's handled flag for the keys they use, and ignore keys while the window slides in.
#include "asset_text.h"
#include "ui.h"
#include "game_strings.h"
#include "ui_screens.h"

#include "console.h"
#include "text_manager.h"

#include <cstdint>
#include <string>
#include "lib/text_case.h"

#include "game_singletons.h"
#include "world.h"
#include "lib/str.h"

#include <cstring>

namespace {
// Virtual-key codes.
constexpr int kKeyBack = 0x08, kKeyEnter = 0x0D, kKeyEsc = 0x1B, kKeyPageUp = 0x21, kKeyPageDown = 0x22;
constexpr int kKeyEnd = 0x23, kKeyHome = 0x24, kKeyLeft = 0x25, kKeyUp = 0x26, kKeyRight = 0x27, kKeyDown = 0x28;
constexpr int kKeyDelete = 0x2E, kKeyTab = 0x09;
constexpr int kMaxNameLength = 60;       // characters of a typed save name
constexpr int kMaxNameWidth = 0xB2;      // its width limit in pixels (font 1)
constexpr int kScanEsc = 1;              // the scan code that gives up a key binding


constexpr std::uint32_t kCloseSaveLoad = 0x100001;
constexpr std::uint32_t kCloseOptions = 0x110001;
constexpr int kListRows = 16;            // visible rows of the save list
constexpr int kOptionRows = 14;

// The end of the key name table (88 entries of 0x48 bytes).
const UiKeyName* KeyNamesEnd() { return g_keyNames + kUiKeyNameCount; }

} // namespace

// --- Save/load window ----------------------------------------------------------------------------

bool SaveLoadWindow::IsReservedSave(int index) {
    const std::string& folder = entries[index].folder;
    if (text::CompareIgnoringCase(folder, game::menukeytexts::QuickSave()) == 0) return true;
    if (text::CompareIgnoringCase(folder, game::menukeytexts::AutoSaveA()) == 0) return true;
    return text::CompareIgnoringCase(folder, game::menukeytexts::AutoSaveB()) == 0;
}

void SaveLoadWindow::ShowSelection() {
    if (selected < 0) selected = 0;
    int count = EntryCount();
    if (!(static_cast<unsigned>(selected) < static_cast<unsigned>(count))) selected = count - 1;
    int currentSelected = selected;
    if (!(firstVisible + kListRows > currentSelected)) firstVisible = currentSelected - (kListRows - 1);
    if (firstVisible > currentSelected) firstVisible = currentSelected;
    scrollbar->SetPosition(firstVisible);
    firstVisible = scrollbar->TakePosition();
    TakeSelectedTitle();
    RefreshListVanilla();
    ShowPicture();
}

void SaveLoadWindow::KeyDown(int key, int repeat, int modifiers) {
    if (sliding) return;
    UiContainer::KeyDown(key, repeat, modifiers);
    int step = 0;
    switch (key) {
    case kKeyEnter:
        Confirm();
        handled = true;
        break;
    case kKeyEsc:
        ui->PlaySound(game::CancelSoundText());
        ui->SetPendingValue(kCloseSaveLoad);
        handled = true;
        break;
    case kKeyPageUp: step = -kListRows; break;
    case kKeyPageDown: step = kListRows; break;
    case kKeyUp: step = -1; break;
    case kKeyDown: step = 1; break;
    default: break;
    }
    if (step != 0) {
        ui->PlaySound(game::SelectSoundText());
        selected += step;
        ShowSelection();
        handled = true;
    }
    if (handled || !isSave) return;

    // Editing the name. The first edit of an ordinary save starts from its title (Backspace and
    // Delete from an empty name); the quick and automatic saves keep their names.
    int currentSelected = selected;
    bool editable = currentSelected >= 0 && static_cast<unsigned>(currentSelected) < static_cast<unsigned>(EntryCount()) &&
                    !IsReservedSave(currentSelected) && !nameTyped;
    auto& currentName = name;
    switch (key) {
    case kKeyBack:
        if (editable) {
            nameTyped = true;
            currentName.clear();
            cursor = 0;
        }
        if (cursor > 0) {    // by character, as Delete and the cursor's steps
            const int previous = text::PreviousCharacter(currentName, cursor);
            currentName = str::Left(currentName, previous) + str::Mid(currentName, cursor);
            cursor = previous;
        }
        break;
    case kKeyDelete:
        if (editable) {
            nameTyped = true;
            currentName.clear();
            cursor = 0;
        }
        if (cursor < static_cast<int>(currentName.size()))
            currentName = str::Left(currentName, cursor) + str::Mid(currentName, text::NextCharacter(currentName, cursor));
        break;
    case kKeyEnd:
    case kKeyHome:
    case kKeyLeft:
    case kKeyRight:
        if (editable) {
            nameTyped = true;
            currentName = entries[selected].title;
            cursor = currentName.size();
        }
        if (key == kKeyEnd)
            cursor = currentName.size();
        else if (key == kKeyHome)
            cursor = 0;
        else if (key == kKeyLeft)
            cursor = text::PreviousCharacter(currentName, cursor);
        else
            cursor = text::NextCharacter(currentName, cursor);
        break;
    default:
        return;
    }
    RefreshListVanilla();
    handled = true;
}

void SaveLoadWindow::Char(int key, int repeat, int modifiers) {
    if (!isSave) return;
    switch (key) {
    case kKeyBack:
    case kKeyTab:
    case kKeyEnter:
    case kKeyEsc:
        break;
    default: {
        int currentSelected = selected;
        if (currentSelected >= 0 && static_cast<unsigned>(currentSelected) < static_cast<unsigned>(EntryCount()) &&
            !IsReservedSave(currentSelected) && !nameTyped) {
            nameTyped = true;
            name.clear();
            cursor = 0;
        }
        auto& currentName = name;
        if (!ui->ctrlDown && text::CharacterCount(currentName) < kMaxNameLength) {
            std::string tail = str::Mid(currentName, cursor);
            std::string typed;
            {
                std::string head = str::Left(currentName, cursor);
                typed = text::Typed(head, key) + tail;   // the character's UTF-8 bytes
            }
            if (ui->TextExtent(typed, 1).cx < kMaxNameWidth) {
                currentName = typed;
                cursor += text::TypedLength(key);
            }
        }
        RefreshListVanilla();
        break;
    }
    }
    UiContainer::Char(key, repeat, modifiers);
}

void OptionsWindow::KeyDown(int key, int repeat, int modifiers) {
    if (sliding) return;
    UiContainer::KeyDown(key, repeat, modifiers);
    if (waitingForKey) {
        // The key for the selected binding row, by scan code.
        int scan = modifiers & 0x1FF;
        handled = true;
        if (scan == kScanEsc) {
            waitingForKey = false;
            RefreshRows();
            return;
        }
        const UiKeyName* pressed = nullptr;
        pressedKey = nullptr;
        for (const UiKeyName* k = g_keyNames; k < KeyNamesEnd(); ++k) {
            if (k->scan == scan) {
                pressed = k;
                pressedKey = k;
                break;
            }
        }
        if (!pressed) {
            ui->PlaySound(game::CancelSoundText());
            return;
        }
        waitingForKey = false;
        OptionBinding* binding = rows[selectedRow].binding;
        if (!binding) {
            RefreshRows();
            return;
        }
        ui->PlaySound(game::OkSoundText());
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
            MessageWindow* box = new MessageWindow(ui, game::menukeytexts::KeyMapped(), game::menukeytexts::KeyMappedText(), 0x10000, 3, 1);
            std::string format;
            std::string message;
            format = g_clientWorld.texts->GetString(game::menukeytexts::KeyMappedText());
            message = str::Format(format, pressed->name, other->name);
            // The box's text (a null box is written through, like the original).
            box->text = message.c_str();
            ui->PushWindow(box, true, true);
            return;
        }
        CommitBinding();
        RefreshRows();
        return;
    }

    switch (key) {
    case kKeyEnter: {
        OptionRow& row = rows[selectedRow];
        if (row.binding) {
            waitingForKey = true;
        } else {
            if (!row.setting || !row.setting->spin) {
                handled = true;
                return;
            }
            const int value = row.setting->spin->TakeValue();
            rows[selectedRow].setting->spin->SetValue(value + 1);
        }
        ui->PlaySound(game::OkSoundText());
        RefreshRows();
        handled = true;
        return;
    }
    case kKeyEsc:
        RestoreSettings();
        ui->PlaySound(game::CancelSoundText());
        ui->SetPendingValue(kCloseOptions);
        break;
    case kKeyPageUp:
    case kKeyPageDown:
        ui->PlaySound(game::SelectSoundText());
        ShowGroup(key == kKeyPageUp ? group - 1 : group + 1);
        break;
    case kKeyLeft:
    case kKeyRight: {
        // The row's bar moves one step; the handled flag stays as it is.
        OptionSetting* setting = rows[selectedRow].setting;
        if (!setting || !setting->bar) return;
        const int position = setting->bar->TakePosition();
        rows[selectedRow].setting->bar->SetPosition(key == kKeyLeft ? position - 1 : position + 1);
        return;
    }
    case kKeyUp:
        ui->PlaySound(game::SelectSoundText());
        for (int i = selectedRow - 1; i >= 0; --i) {
            if (rows[i].binding || rows[i].setting) {
                selectedRow = i;
                RefreshRows();
                break;
            }
        }
        break;
    case kKeyDown:
        ui->PlaySound(game::SelectSoundText());
        for (int i = selectedRow + 1; i < kOptionRows; ++i) {
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
    handled = true;
}
