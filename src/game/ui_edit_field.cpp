// The message box's edit field: its key handler (0065C610). See ui.h (CUiEditField) and
// docs/notes/drafts/DR-H16.md.
#include "ui.h"

#include "ei/hook.h"
#include "lib/mfc.h"

#include <cstddef>
#include <cstdint>

namespace {
constexpr int kKeyBackspace = 0x08;
constexpr int kKeyEnd = 0x23;
constexpr int kKeyHome = 0x24;
constexpr int kKeyLeft = 0x25;
constexpr int kKeyRight = 0x27;
constexpr int kKeyDelete = 0x2E;
} // namespace

// 0065C2E0 (Rebirth-patched): through its address.
__declspec(naked) void CUiEditField::DrawContents() EI_ORIGINAL(0x0065C2E0)

// --- 0065C610 ------------------------------------------------------------------------------------

void CUiEditField::OnKeyDown(int key, int, int) {
    if (!focused_) return;
    mfc::CString& text = Text();
    switch (key) {
    case kKeyBackspace:
    case kKeyDelete:
        // The first edit key of a field starts from an empty text.
        if (!editing_) {
            editing_ = true;
            text.Empty();
            caret_ = 0;
            changed_ = true;
        }
        if (key == kKeyBackspace) {
            if (caret_ > 0) {
                mfc::CString tail = text.Mid(caret_);
                mfc::CString head = text.Left(caret_ - 1);
                text = head + tail;
                --caret_;
                changed_ = true;
            }
        } else if (caret_ < text.GetLength()) {
            mfc::CString tail = text.Mid(caret_ + 1);
            mfc::CString head = text.Left(caret_);
            text = head + tail;
            changed_ = true;
        }
        break;
    case kKeyEnd:
    case kKeyHome:
    case kKeyLeft:
    case kKeyRight:
        // The first edit key of a field keeps the text, the caret at its end.
        if (!editing_) {
            editing_ = true;
            caret_ = text.GetLength();
        }
        if (key == kKeyEnd)
            caret_ = text.GetLength();
        else if (key == kKeyHome)
            caret_ = 0;
        else if (key == kKeyLeft)
            --caret_;
        else
            ++caret_;
        break;
    default:
        return;
    }
    DrawContents();
    inputHandled_ = true;
}

namespace {
void __fastcall EditFieldKeyDown(void* input, void*, int key, int repeat, int flags) {
    reinterpret_cast<CUiEditField*>(static_cast<std::uint8_t*>(input) - offsetof(CUiWindow, inputVtable_))
        ->OnKeyDown(key, repeat, flags);
}
} // namespace

EI_REPLACE(0x0065C610, EditFieldKeyDown);
