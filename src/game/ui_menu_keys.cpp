// The Esc menu's save/load and options windows: their key handlers and the
// save/load window's selection helpers. See ui.h and docs/notes/ui.md.
// Both key handlers pass the key to the container first, mark the event used for the keys they use, and
// ignore keys while the window slides in.
#include "asset_text.h"
#include "ui.h"
#include "ui_screens.h"

#include "console.h"
#include "text_manager.h"

#include <cstdint>
#include <memory>
#include <string>
#include "lib/containers.h"
#include "lib/text_case.h"

#include "game_singletons.h"
#include "world.h"
#include "lib/str.h"

#include <cstring>

namespace {
constexpr int kMaxNameLength = 60;       // characters of a typed save name
constexpr int kMaxNameWidth = 0xB2;      // its width limit in pixels (font 1)
constexpr int kScanEsc = 1;              // the scan code that gives up a key binding


constexpr std::uint32_t kCloseSaveLoad = 0x100001;
constexpr std::uint32_t kCloseOptions = 0x110001;
constexpr int kListRows = 16;            // visible rows of the save list

// The end of the key name table (88 entries of 0x48 bytes).
const UiKeyName* KeyNamesEnd() { return g_keyNames + kUiKeyNameCount; }

} // namespace

// --- Save/load window ----------------------------------------------------------------------------

bool SaveLoadWindow::IsReservedSave(int index) {
    const std::string& folder = entries[index].folder;
    if (text::CompareIgnoringCase(folder, "saveq") == 0) return true;
    if (text::CompareIgnoringCase(folder, "savea") == 0) return true;
    return text::CompareIgnoringCase(folder, "saveb") == 0;
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

void SaveLoadWindow::OnEvent(UiEvent& event) {
    switch (event.kind) {
    case UiEventKind::LeftDown: LeftDown(event); break;
    case UiEventKind::LeftDouble: LeftDouble(event); break;
    case UiEventKind::KeyDown: KeyDown(event); break;
    case UiEventKind::Char: Char(event); break;
    default: UiContainer::OnEvent(event); break;
    }
}

void SaveLoadWindow::KeyDown(UiEvent& event) {
    if (sliding) return;
    UiContainer::OnEvent(event);
    int step = 0;
    switch (event.key) {
    case platform::Key::Return:
        Confirm();
        event.handled = true;
        break;
    case platform::Key::Escape:
        ui->PlayInterfaceSound(kMessageBoxCancelSound);
        ui->SetPendingValue(kCloseSaveLoad);
        event.handled = true;
        break;
    case platform::Key::PageUp: step = -kListRows; break;
    case platform::Key::PageDown: step = kListRows; break;
    case platform::Key::Up: step = -1; break;
    case platform::Key::Down: step = 1; break;
    default: break;
    }
    if (step != 0) {
        ui->PlayInterfaceSound(kSaveSelectSound);
        selected += step;
        ShowSelection();
        event.handled = true;
    }
    if (event.handled || !isSave) return;

    // Editing the name. The first edit of an ordinary save starts from its title (Backspace and
    // Delete from an empty name); the quick and automatic saves keep their names.
    int currentSelected = selected;
    bool editable = currentSelected >= 0 && static_cast<unsigned>(currentSelected) < static_cast<unsigned>(EntryCount()) &&
                    !IsReservedSave(currentSelected) && !nameTyped;
    auto& currentName = name;
    switch (event.key) {
    case platform::Key::Backspace:
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
    case platform::Key::Delete:
        if (editable) {
            nameTyped = true;
            currentName.clear();
            cursor = 0;
        }
        if (cursor < static_cast<int>(currentName.size()))
            currentName = str::Left(currentName, cursor) + str::Mid(currentName, text::NextCharacter(currentName, cursor));
        break;
    case platform::Key::End:
    case platform::Key::Home:
    case platform::Key::Left:
    case platform::Key::Right:
        if (editable) {
            nameTyped = true;
            currentName = entries[selected].title;
            cursor = lib::Count(currentName);
        }
        if (event.key == platform::Key::End)
            cursor = lib::Count(currentName);
        else if (event.key == platform::Key::Home)
            cursor = 0;
        else if (event.key == platform::Key::Left)
            cursor = text::PreviousCharacter(currentName, cursor);
        else
            cursor = text::NextCharacter(currentName, cursor);
        break;
    default:
        return;
    }
    RefreshListVanilla();
    event.handled = true;
}

void SaveLoadWindow::Char(UiEvent& event) {
    if (!isSave) return;
    switch (event.character) {
    case '\b':
    case '\t':
    case '\r':
    case '\x1B':
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
                typed = text::Typed(head, event.character) + tail;   // the character's UTF-8 bytes
            }
            if (ui->TextExtent(typed, 1).w < kMaxNameWidth) {
                currentName = typed;
                cursor += text::TypedLength(event.character);
            }
        }
        RefreshListVanilla();
        break;
    }
    }
    UiContainer::OnEvent(event);
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
            ui->PlayInterfaceSound(kMessageBoxCancelSound);
            return;
        }
        waitingForKey = false;
        OptionBinding* binding = rows[selectedRow].binding;
        if (!binding) {
            RefreshRows();
            return;
        }
        ui->PlayInterfaceSound(kMessageBoxOkSound);
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
            auto box = std::make_unique<MessageWindow>(ui, "key_already_mapped", "key_already_mapped_msg", MessageQuestion::First, MessageButtons::OkCancel);
            std::string format;
            std::string message;
            format = g_clientWorld.texts->GetString("key_already_mapped_msg");
            message = str::Format(format, pressed->name, other->name);
            box->text = message.c_str();
            ui->PushWindow(std::move(box), WindowBackdrop::FirstWindow);
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
        ui->PlayInterfaceSound(kMessageBoxOkSound);
        RefreshRows();
        event.handled = true;
        return;
    }
    case platform::Key::Escape:
        RestoreSettings();
        ui->PlayInterfaceSound(kMessageBoxCancelSound);
        ui->SetPendingValue(kCloseOptions);
        break;
    case platform::Key::PageUp:
    case platform::Key::PageDown:
        ui->PlayInterfaceSound(kSaveSelectSound);
        ShowGroup(event.key == platform::Key::PageUp ? group - 1 : group + 1);
        break;
    case platform::Key::Left:
    case platform::Key::Right: {
        // The row's bar moves one step, or its spin control steps back or on; the event is not marked used.
        OptionSetting* setting = rows[selectedRow].setting;
        if (!setting) return;
        if (setting->list) {
            setting->list->Step(event.key == platform::Key::Left ? -1 : 1);
            ui->PlayInterfaceSound(kMessageBoxOkSound);
            return;
        }
        if (setting->spin) {
            setting->spin->Step(event.key == platform::Key::Left ? -1 : 1);
            ui->PlayInterfaceSound(kMessageBoxOkSound);
            return;
        }
        if (!setting->bar) return;
        const int position = setting->bar->TakePosition();
        rows[selectedRow].setting->bar->SetPosition(event.key == platform::Key::Left ? position - 1 : position + 1);
        return;
    }
    case platform::Key::Up:
        ui->PlayInterfaceSound(kSaveSelectSound);
        for (int i = selectedRow - 1; i >= 0; --i) {
            if (rows[i].binding || rows[i].setting) {
                selectedRow = i;
                RefreshRows();
                break;
            }
        }
        break;
    case platform::Key::Down:
        ui->PlayInterfaceSound(kSaveSelectSound);
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
