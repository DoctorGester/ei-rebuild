// The Esc menu's save/load and options windows: their key handlers (006548A0, 00650E40) and the
// save/load window's selection helpers (00655430, 00657820). See ui.h and docs/notes/ui.md.
//
// Both key handlers live on the window's input side (`this` = window + 0x94, ui_screens.h). They
// pass the key to the base handler first (00601820), set the input side's handled flag for the
// keys they use, and ignore keys while the window slides in.
#include "ui.h"
#include "game_strings.h"
#include "ui_screens.h"

#include "console.h"
#include "item_db_text.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/mfc.h"

#include <cstdint>
#include "originals_library.h"
#include "originals_ui.h"
#include "game_data.h"
#include "game_singletons.h"

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

// The key name table (88 entries of 0x48 bytes).
const UiKeyName* KeyNamesBegin() { return ei::GamePointer<const UiKeyName>(0x007857B0); }
const UiKeyName* KeyNamesEnd() { return ei::GamePointer<const UiKeyName>(0x00787070); }

} // namespace

// --- Save/load window ----------------------------------------------------------------------------

// --- 00657820 ------------------------------------------------------------------------------------

bool CSaveLoadWindow::IsReservedSave(int index) {
    const char* folder = entriesBegin_[index].folder_;
    if (gamecrt::mbsicmp(folder, game::menukeytexts::QuickSave()) == 0) return true;
    if (gamecrt::mbsicmp(folder, game::menukeytexts::AutoSaveA()) == 0) return true;
    return gamecrt::mbsicmp(folder, game::menukeytexts::AutoSaveB()) == 0;
}

// --- 00655430 ------------------------------------------------------------------------------------

void CSaveLoadWindow::ShowSelection() {
    if (selected_ < 0) selected_ = 0;
    int count = EntryCount();
    if (!(static_cast<unsigned>(selected_) < static_cast<unsigned>(count))) selected_ = count - 1;
    int selected = selected_;
    if (!(firstVisible_ + kListRows > selected)) firstVisible_ = selected - (kListRows - 1);
    if (firstVisible_ > selected) firstVisible_ = selected;
    scrollbar_->CallSetPosition(firstVisible_);
    scrollbar_->TakePosition(&firstVisible_);
    TakeSelectedTitle();
    RefreshList();
    CallShowPicture();
}

// --- 006548A0 ------------------------------------------------------------------------------------

void CSaveLoadWindow::OnKeyDown(int key, int repeat, int flags) {
    if (sliding_) return;
    InputOf(this)->OnKeyDown(key, repeat, flags);
    int step = 0;
    switch (key) {
    case kKeyEnter:
        Confirm();
        inputHandled_ = true;
        break;
    case kKeyEsc:
        interface_->PlaySound(game::CancelSoundText(), 0);
        interface_->SetPendingValue(kCloseSaveLoad);
        inputHandled_ = true;
        break;
    case kKeyPageUp: step = -kListRows; break;
    case kKeyPageDown: step = kListRows; break;
    case kKeyUp: step = -1; break;
    case kKeyDown: step = 1; break;
    default: break;
    }
    if (step != 0) {
        interface_->PlaySound(game::SelectSoundText(), 0);
        selected_ += step;
        ShowSelection();
        inputHandled_ = true;
    }
    if (inputHandled_ || !isSave_) return;

    // Editing the name. The first edit of an ordinary save starts from its title (Backspace and
    // Delete from an empty name); the quick and automatic saves keep their names.
    int selected = selected_;
    bool editable = selected >= 0 && static_cast<unsigned>(selected) < static_cast<unsigned>(EntryCount()) &&
                    !IsReservedSave(selected) && !nameTyped_;
    mfc::CString& name = name_;
    switch (key) {
    case kKeyBack:
        if (editable) {
            nameTyped_ = true;
            name.Empty();
            cursor_ = 0;
        }
        if (cursor_ > 0) {
            name = name.Left(cursor_ - 1) + console::Ops(name).Mid(cursor_);
            --cursor_;
        }
        break;
    case kKeyDelete:
        if (editable) {
            nameTyped_ = true;
            name.Empty();
            cursor_ = 0;
        }
        if (cursor_ < name.GetLength()) name = name.Left(cursor_) + console::Ops(name).Mid(cursor_ + 1);
        break;
    case kKeyEnd:
    case kKeyHome:
    case kKeyLeft:
    case kKeyRight:
        if (editable) {
            nameTyped_ = true;
            name = entriesBegin_[selected_].title_;
            cursor_ = name.GetLength();
        }
        if (key == kKeyEnd)
            cursor_ = name.GetLength();
        else if (key == kKeyHome)
            cursor_ = 0;
        else if (key == kKeyLeft)
            --cursor_;
        else
            ++cursor_;
        break;
    default:
        return;
    }
    RefreshList();
    inputHandled_ = true;
}

// --- 00655050 ------------------------------------------------------------------------------------

namespace {
// The width of `text` in interface font `font` (005F8340, through its address: it takes the
// CString by value, so a counted copy's data is handed over and released by the callee).
int TextWidth(CInterface3D* ui, const mfc::CString& text, int font) {
    mfc::CString copy(text);
    char* data;
    std::memcpy(&data, &copy, sizeof(data));
    mfc::CString empty;
    std::memcpy(&copy, &empty, sizeof(data));   // `copy` now owns nothing
    SIZE size;
    InterfaceTextExtentOriginal(ui, nullptr, &size, data, font);
    return size.cx;
}
} // namespace

void CSaveLoadWindow::OnChar(int key, int repeat, int flags) {
    if (!isSave_) return;
    switch (key) {
    case kKeyBack:
    case kKeyTab:
    case kKeyEnter:
    case kKeyEsc:
        break;
    default: {
        int selected = selected_;
        if (selected >= 0 && static_cast<unsigned>(selected) < static_cast<unsigned>(EntryCount()) &&
            !IsReservedSave(selected) && !nameTyped_) {
            nameTyped_ = true;
            name_.Empty();
            cursor_ = 0;
        }
        mfc::CString& name = name_;
        if (!interface_->ctrlDown_ && name.GetLength() < kMaxNameLength) {
            mfc::CString tail = console::Ops(name).Mid(cursor_);
            mfc::CString typed;
            {
                mfc::CString head = name.Left(cursor_);
                typed = mfc::operator+(head, static_cast<char>(key)) + tail;
            }
            if (TextWidth(interface_, typed, 1) < kMaxNameWidth) {
                name = typed;
                ++cursor_;
            }
        }
        RefreshList();
        break;
    }
    }
    InputOf(this)->OnChar(key, repeat, flags);
}

// --- 00650E40 ------------------------------------------------------------------------------------

void COptionsWindow::OnKeyDown(int key, int repeat, int flags) {
    if (sliding_) return;
    InputOf(this)->OnKeyDown(key, repeat, flags);
    if (waitingForKey_) {
        // The key for the selected binding row, by scan code.
        int scan = flags & 0x1FF;
        inputHandled_ = true;
        if (scan == kScanEsc) {
            waitingForKey_ = false;
            RefreshRows();
            return;
        }
        const UiKeyName* pressed = nullptr;
        pressedKey_ = nullptr;
        for (const UiKeyName* k = KeyNamesBegin(); k < KeyNamesEnd(); ++k) {
            if (k->scan_ == scan) {
                pressed = k;
                pressedKey_ = k;
                break;
            }
        }
        if (!pressed) {
            interface_->PlaySound(game::CancelSoundText(), 0);
            return;
        }
        waitingForKey_ = false;
        OptionBinding* binding = rows_[selectedRow_].binding_;
        if (!binding) {
            RefreshRows();
            return;
        }
        interface_->PlaySound(game::OkSoundText(), 0);
        int code = keyOfScan_[pressed->scan_];
        OptionBinding* other = bindings_.begin();
        while (other != bindings_.end() && other->action_->id_ != code) ++other;
        if (other != bindings_.end() && other != nullptr && other != binding) {
            // Already bound to another action: ask (the box's result goes to OnDialogResult).
            void* memory = gamecrt::operator_new(0x180);
            CUiWindow* box = memory ? (CUiWindow*)static_cast<CMessageBox*>(memory)->Construct(interface_, game::menukeytexts::KeyMapped(), game::menukeytexts::KeyMappedText(), 0x10000, 3, 1)
                                    : nullptr;
            mfc::CString format;
            mfc::CString message;
            TheTextManager()->GetString(game::menukeytexts::KeyMappedText(), &format);
            message.Format(format, pressed->name_, static_cast<const char*>(other->name_));
            // The box's text (a null box is written through, as in the original).
            static_cast<CMessageBox*>(box)->text_ = static_cast<const char*>(message);
            interface_->PushWindow(box, 1, 1);
            return;
        }
        CommitBinding();
        RefreshRows();
        return;
    }

    switch (key) {
    case kKeyEnter: {
        OptionRow& row = rows_[selectedRow_];
        if (row.binding_) {
            waitingForKey_ = true;
        } else {
            if (!row.setting_ || !row.setting_->spin_) {
                inputHandled_ = true;
                return;
            }
            int value;
            row.setting_->spin_->GetValue(&value);
            rows_[selectedRow_].setting_->spin_->SetValue(value + 1);
        }
        interface_->PlaySound(game::OkSoundText(), 0);
        RefreshRows();
        inputHandled_ = true;
        return;
    }
    case kKeyEsc:
        RestoreSettings();
        interface_->PlaySound(game::CancelSoundText(), 0);
        interface_->SetPendingValue(kCloseOptions);
        break;
    case kKeyPageUp:
    case kKeyPageDown:
        interface_->PlaySound(game::SelectSoundText(), 0);
        ShowGroup(key == kKeyPageUp ? group_ - 1 : group_ + 1);
        break;
    case kKeyLeft:
    case kKeyRight: {
        // The row's bar moves one step; the handled flag stays as it is.
        OptionSetting* setting = rows_[selectedRow_].setting_;
        if (!setting || !setting->bar_) return;
        int position;
        setting->bar_->TakePosition(&position);
        rows_[selectedRow_].setting_->bar_->CallSetPosition(key == kKeyLeft ? position - 1 : position + 1);
        return;
    }
    case kKeyUp:
        interface_->PlaySound(game::SelectSoundText(), 0);
        for (int i = selectedRow_ - 1; i >= 0; --i) {
            if (rows_[i].binding_ || rows_[i].setting_) {
                selectedRow_ = i;
                RefreshRows();
                break;
            }
        }
        break;
    case kKeyDown:
        interface_->PlaySound(game::SelectSoundText(), 0);
        for (int i = selectedRow_ + 1; i < kOptionRows; ++i) {
            if (rows_[i].binding_ || rows_[i].setting_) {
                selectedRow_ = i;
                RefreshRows();
                break;
            }
        }
        break;
    default:
        return;
    }
    inputHandled_ = true;
}

// --- Entry points --------------------------------------------------------------------------------

EI_REPLACE_METHOD(0x006548A0, InputForward<&CSaveLoadWindow::OnKeyDown>::Call);
EI_REPLACE_METHOD(0x00650E40, InputForward<&COptionsWindow::OnKeyDown>::Call);
EI_REPLACE_METHOD(0x00655050, InputForward<&CSaveLoadWindow::OnChar>::Call);
EI_REWRITTEN(0x00655430, CSaveLoadWindow::ShowSelection);
