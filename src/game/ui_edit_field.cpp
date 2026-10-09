// The message box's edit field: its key handler (0065C610), constructor (0065C010) and character
// handler (0065C930). See ui.h (CUiEditField) and docs/notes/items_spells.md (A-H19).
#include "ui.h"

#include "originals_library.h"
#include "originals_ui.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/mfc.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

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

// --- A-H19: 0065C010 and 0065C930 --------------------------------------------------------------------

namespace {
constexpr std::uintptr_t kEditFieldVtable = std::uintptr_t(0x00742DF4);
constexpr std::uintptr_t kEditFieldInputVtable = std::uintptr_t(0x00742DC0);
constexpr int kSurfaceFormat = 5;
constexpr int kKeyTab = 0x09;
constexpr int kKeyEnter = 0x0D;
constexpr int kKeyEscape = 0x1B;
constexpr std::uint32_t kClassDigit = 1;
constexpr std::uint32_t kClassLetter = 2;      // letters and the space
constexpr std::uint32_t kClassOther = 4;
constexpr std::uint32_t kClassSeparator = 8;   // '.' and ':'

// The width of `text` in interface font `font` (005F8340, Rebirth-patched: through its address; it
// takes the CString by value and releases it, so a copy's data is handed over).
int TextWidth(CInterface3D* ui, const mfc::CString& text, int font) {
    mfc::CString copy(text);
    char* data;
    std::memcpy(&data, &copy, sizeof(data));
    mfc::CString empty;
    std::memcpy(&copy, &empty, sizeof(data));
    SIZE size;
    InterfaceTextExtentOriginal(ui, nullptr, &size, data, font);
    return size.cx;
}
} // namespace

CUiEditField* CUiEditField::Construct(CInterface3D* ui, RECT rect) {
    CUiWindow::Construct(ui);
    new (&Text()) mfc::CString;
    vtable_ = reinterpret_cast<void**>(kEditFieldVtable);
    inputVtable_ = reinterpret_cast<void**>(kEditFieldInputVtable);
    charClasses_ = kClassDigit | kClassLetter | kClassOther | kClassSeparator;
    mode15C_ = 0;
    font_ = 1;
    maxLength_ = 50;
    colour_ = 0xFFFFFF;
    unknown_16C = 0xFF;
    ClearSurfaces();
    area_ = rect;
    void* memory = gamecrt::operator_new(sizeof(CUiSurface));
    AddSurface(memory ? SurfaceConstructOnRectOriginal(memory, nullptr, interface_, &area_, kSurfaceFormat) : nullptr);
    DrawContents();
    focused_ = true;
    return this;
}
EI_REPLACE_METHOD(0x0065C010, CUiEditField::Construct);

void CUiEditField::OnChar(int key, int, int) {
    if (!focused_) return;
    if (key == kKeyBackspace || key == kKeyTab || key == kKeyEnter || key == kKeyEscape) return;
    if (interface_->ctrlDown_) return;
    char c = static_cast<char>(key);
    std::uint32_t kind;
    if (gamecrt::isdigit(key) != 0)
        kind = kClassDigit;
    else if (gamecrt::isalpha(key) != 0 || c == ' ')
        kind = kClassLetter;
    else if (c == '.' || c == ':')
        kind = kClassSeparator;
    else
        kind = kClassOther;
    if ((charClasses_ & kind) == 0) return;
    mfc::CString& text = Text();
    // The first character of a field starts from an empty text.
    if (!editing_) {
        editing_ = true;
        text.Empty();
        caret_ = 0;
        changed_ = true;
    }
    mfc::CString tail = text.Mid(caret_);
    mfc::CString head = text.Left(caret_);
    mfc::CString typed = PlusChar(head, c) + tail;
    if (text.GetLength() < maxLength_ && TextWidth(interface_, typed, font_) < area_.right - area_.left) {
        text = typed;
        changed_ = true;
        ++caret_;
    }
    DrawContents();
    inputHandled_ = true;
}

namespace {
void __fastcall EditFieldChar(void* input, void*, int key, int repeat, int flags) {
    reinterpret_cast<CUiEditField*>(static_cast<std::uint8_t*>(input) - offsetof(CUiWindow, inputVtable_))
        ->OnChar(key, repeat, flags);
}
} // namespace

EI_REPLACE(0x0065C930, EditFieldChar);
