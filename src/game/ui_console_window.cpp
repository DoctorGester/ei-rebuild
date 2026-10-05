// The console window's surface, drawing and key input (game.exe 0x005E3FD0-0x005E4D25). The text
// side (lines, history depth, placement) is in console_window.cpp. See docs/notes/ui.md
// ("Console window").
#include "console.h"
#include "ui_gdi.h"

#include "lib/crt.h"
#include "lib/gameapi.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "originals_library.h"
#include "originals_ui.h"

#include <cstring>
#include <string>

#pragma warning(disable : 4740) // naked forwarders: no optimisation to suppress

namespace console {

namespace {

using ui_gdi::GameDC;
using ui_gdi::GamePen;
using ui_gdi::SurfaceDC;

// Colours and the strings the window draws (read from the exe: "> " before the input, "+" for
// the scroll marks).
constexpr COLORREF kBackground = 0x282828;
constexpr COLORREF kSelection = 0x505050;
constexpr COLORREF kLineText = 0xB4B4B4;
constexpr COLORREF kInputText = 0xFFFFFF;
constexpr std::uintptr_t kPromptText = 0x00792DE0;
constexpr std::uintptr_t kMoreText = 0x00787BD8;
constexpr int kLineHeight = 15;
constexpr int kInputHeight = 0x1F;     // the input strip at the bottom
constexpr UINT kDrawLine = 0x800;      // DT_NOPREFIX
constexpr UINT kDrawMark = 0x802;      // DT_NOPREFIX | DT_RIGHT

// Calls between the window's functions go through the original addresses (verify wrappers, and
// any mod patch of the callee, stay in the path).
struct WindowAt : ConsoleWindow {
    void CallDrawLines();   // 005E4800
    void CallDrawInput();   // 005E4A10
};
__declspec(naked) void WindowAt::CallDrawLines() EI_ORIGINAL(0x005E4800)
__declspec(naked) void WindowAt::CallDrawInput() EI_ORIGINAL(0x005E4A10)
WindowAt* At(ConsoleWindow* w) { return static_cast<WindowAt*>(w); }

// CUiSurface: constructor over a layout rectangle (005FCDD0) and destructor body (005FD120).
struct SurfaceAt : SurfaceDC {
    SurfaceAt* Construct(int* ui, const int* rect, int format);
    void ReleaseContents();
};
__declspec(naked) SurfaceAt* SurfaceAt::Construct(int*, const int*, int) EI_ORIGINAL(0x005FCDD0)
__declspec(naked) void SurfaceAt::ReleaseContents() EI_ORIGINAL(0x005FD120)

// CString members not in lib/mfc.h.
struct StrAt : mfc::CString {
    mfc::CString Left(int count) const;   // 006FDAE9
};
__declspec(naked) mfc::CString StrAt::Left(int) const EI_ORIGINAL(0x006FDAE9)
mfc::CString Left(const mfc::CString& s, int count) { return static_cast<const StrAt&>(s).Left(count); }

int Clamp(int value, int high) {
    if (value > high) value = high;
    if (value < 0) value = 0;
    return value;
}

// Width in pixels of the prompt and the first `count` characters of the input.
int PromptWidth(GameDC* dc, const mfc::CString& input, int count) {
    mfc::CString text = mfc::CString(GameText(kPromptText)) + Left(input, count);
    SIZE size;
    gameapi::GetTextExtentPoint32A(dc->hdc_, text, Length(text), &size);
    return size.cx;
}

} // namespace

// 005E3FD0: a shown window gets a surface over its rectangle (format 4) if it has none and is
// drawn; hiding releases the surface.
void ConsoleWindow::Show(bool shown) {
    shown_ = shown;
    if (shown) {
        if (!surface_) {
            void* memory = gamecrt::operator_new(0x38);
            surface_ = memory ? static_cast<SurfaceAt*>(memory)->Construct(screen_, rect_, 4) : nullptr;
        }
        At(this)->CallDrawLines();
        At(this)->CallDrawInput();
        return;
    }
    if (SurfaceAt* surface = static_cast<SurfaceAt*>(surface_)) {
        surface->ReleaseContents();
        gamecrt::operator_delete(surface);
    }
    surface_ = nullptr;
}
EI_REPLACE_METHOD(0x005E3FD0, ConsoleWindow::Show);

// 005E4180: keys while the window is shown. Shift (0x10) and Ctrl (0x11) are remembered; with
// Ctrl, PgUp/PgDn scroll by a page and Home/End go to the first/last page, otherwise PgUp/PgDn
// scroll by a line and Home/End/Left/Right move the caret (Shift keeps the selection's anchor);
// Up/Down step through the entered commands; Delete removes the selection or the next character.
void ConsoleWindow::OnKeyDown(int key, int, int) {
    switch (key) {
    case VK_SHIFT:
        shift_ = true;
        return;
    case VK_CONTROL:
        ctrl_ = true;
        return;
    case VK_PRIOR:
        scroll_ = ctrl_ ? scroll_ + visibleLines_ : scroll_ + 1;
        break;
    case VK_NEXT:
        scroll_ = ctrl_ ? scroll_ - visibleLines_ : scroll_ - 1;
        break;
    case VK_END:
        if (!ctrl_) {
            caret_ = Length(input_);
            if (!shift_) anchor_ = caret_;
            At(this)->CallDrawInput();
            return;
        }
        scroll_ = 0;
        break;
    case VK_HOME:
        if (!ctrl_) {
            caret_ = 0;
            if (!shift_) anchor_ = 0;
            At(this)->CallDrawInput();
            return;
        }
        {
            int first = lines_.GetSize() - visibleLines_;
            scroll_ = first < 0 ? 0 : first;
        }
        break;
    case VK_LEFT:
    case VK_RIGHT:
        caret_ += key == VK_LEFT ? -1 : 1;
        if (!shift_) anchor_ = caret_;
        At(this)->CallDrawInput();
        return;
    case VK_UP:
    case VK_DOWN:
        if (key == VK_UP) {
            if (commandIndex_ < commands_.GetSize()) ++commandIndex_;
        } else if (commandIndex_ >= 0) {
            --commandIndex_;
        }
        if (commandIndex_ >= 0 && commandIndex_ < commands_.GetSize())
            input_ = commands_[commandIndex_];
        else
            input_.Empty();
        caret_ = Length(input_);
        anchor_ = caret_;
        At(this)->CallDrawInput();
        return;
    case VK_DELETE:
        if (anchor_ == caret_) {
            if (caret_ < Length(input_)) {
                mfc::CString rest = Ops(input_).Mid(caret_ + 1);
                input_ = Left(input_, caret_) + rest;
                At(this)->CallDrawInput();
            }
        } else {
            int low = anchor_ < caret_ ? anchor_ : caret_;
            int high = anchor_ > caret_ ? anchor_ : caret_;
            mfc::CString rest = Ops(input_).Mid(high);
            input_ = Left(input_, low) + rest;
            caret_ = low;
            anchor_ = low;
            At(this)->CallDrawInput();
        }
        return;
    default:
        return;
    }
    At(this)->CallDrawLines();
}
EI_REPLACE_METHOD(0x005E4180, ConsoleWindow::OnKeyDown);

// ---- Character input (005E44A0) ------------------------------------------------------------------

namespace {

constexpr int kHistoryDepth = 30;
constexpr std::uintptr_t kNewLine = 0x00784340;   // "\n"

struct TextAt : mfc::CString {
    mfc::CString Mid(int first) const;    // 006FD9B4
    int FindOneOf(const char* set) const; // 00700ECC
};
__declspec(naked) mfc::CString TextAt::Mid(int) const EI_ORIGINAL(0x006FD9B4)
__declspec(naked) int TextAt::FindOneOf(const char*) const EI_ORIGINAL(0x00700ECC)
mfc::CString MidOf(const mfc::CString& s, int first) { return static_cast<const TextAt&>(s).Mid(first); }
// 006FD56E (stdcall). operator+(const CString&, TCHAR).
// 006FED03. CStringArray::InsertAt(index, const CString&, count).
__declspec(naked) void __fastcall InsertString(CStringArray*, void*, int, const mfc::CString*, int) EI_ORIGINAL(0x006FED03)
// 005E4090 / 005DC620, through their addresses.
__declspec(naked) void __fastcall AddTextAt(ConsoleWindow*, void*, const char*) EI_ORIGINAL(0x005E4090)

// Rebirth's clipboard (ImproveConsole): the text as CF_UNICODETEXT, converted with the ANSI code
// page, as addon's helpers do.
void CopyToClipboard(const char* first, const char* last) {
    int count = static_cast<int>(last - first);
    std::wstring wide;
    int n = count > 0 ? MultiByteToWideChar(CP_ACP, 0, first, count, nullptr, 0) : 0;
    if (n > 0) {
        wide.resize(static_cast<std::size_t>(n));
        MultiByteToWideChar(CP_ACP, 0, first, count, &wide[0], n);
    }
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, (wide.size() + 1) * 2);
    if (!memory) return;
    void* locked = GlobalLock(memory);
    if (!locked) {
        GlobalFree(memory);
        return;
    }
    std::memcpy(locked, wide.c_str(), (wide.size() + 1) * 2);
    GlobalUnlock(memory);
    if (!OpenClipboard(nullptr)) {
        GlobalFree(memory);
        return;
    }
    EmptyClipboard();
    SetClipboardData(CF_UNICODETEXT, memory);
    CloseClipboard();
}

std::string ClipboardText() {
    std::wstring wide;
    if (OpenClipboard(nullptr)) {
        if (HANDLE data = GetClipboardData(CF_UNICODETEXT)) {
            if (const auto* text = static_cast<const wchar_t*>(GlobalLock(data))) {
                wide = text;
                GlobalUnlock(data);
            }
        }
        CloseClipboard();
    }
    std::string narrow;
    int count = static_cast<int>(wide.size());
    int n = count > 0 ? WideCharToMultiByte(CP_ACP, 0, wide.c_str(), count, nullptr, 0, nullptr, nullptr) : 0;
    if (n > 0) {
        narrow.resize(static_cast<std::size_t>(n));
        WideCharToMultiByte(CP_ACP, 0, wide.c_str(), count, &narrow[0], n, nullptr, nullptr);
    }
    return narrow;
}

} // namespace

// 005E44A0: typed characters. Backspace deletes the selection or the character before the caret,
// Enter enters the line (into the history, at most 30, echoed with "> ", then executed), Escape
// clears the line, Tab does nothing; other characters replace the selection, except while Ctrl is
// held. Rebirth's ImproveConsole skips the history for a line equal to the last one and adds
// Ctrl+A / C / X / V (select all, copy, cut, paste: the first line of the clipboard).
void ConsoleWindow::OnChar(int key, int, int) {
    int low = caret_ < anchor_ ? caret_ : anchor_;
    int high = anchor_ > caret_ ? anchor_ : caret_;
    switch (key) {
    case 8: {   // Backspace
        if (anchor_ != caret_) {
            int first = anchor_ < caret_ ? anchor_ : caret_;
            int last = anchor_ > caret_ ? anchor_ : caret_;
            mfc::CString rest = MidOf(input_, last);
            input_ = Left(input_, first) + rest;
            caret_ = first;
            anchor_ = first;
        } else {
            if (caret_ == 0) return;
            mfc::CString rest = MidOf(input_, caret_);
            input_ = Left(input_, caret_ - 1) + rest;
            --caret_;
            anchor_ = caret_;
        }
        At(this)->CallDrawInput();
        return;
    }
    case 9:
        return;
    case 13: {   // Enter
        bool repeated = ei::rebirth::Enabled(ei::rebirth::Patch::ImproveConsole) && commands_.size_ != 0 &&
                        std::strcmp(input_, commands_.data_[0]) == 0;
        if (!repeated) {
            InsertString(&commands_, nullptr, 0, &input_, 1);
            if (commands_.size_ > kHistoryDepth) commands_.RemoveAt(commands_.size_ - 1, 1);
        }
        commandIndex_ = -1;
        input_.Empty();
        caret_ = 0;
        anchor_ = 0;
        At(this)->CallDrawInput();
        AddTextAt(this, nullptr, (mfc::CString(GameText(kPromptText)) + commands_.data_[0]) + GameText(kNewLine));
        ExecuteAt(commands_.data_[0]);
        return;
    }
    case 27:   // Escape
        input_.Empty();
        caret_ = 0;
        anchor_ = 0;
        At(this)->CallDrawInput();
        return;
    default:
        break;
    }
    if (ctrl_ && !shift_ && ei::rebirth::Enabled(ei::rebirth::Patch::ImproveConsole)) {
        char letter = static_cast<char>(static_cast<std::uint8_t>(key) + 0x60);
        if (letter == 'a') {
            anchor_ = 0;
            caret_ = Length(input_);
            At(this)->CallDrawInput();
        } else if ((letter == 'c' || letter == 'x') && low != high) {
            const char* text = input_;
            CopyToClipboard(text + low, text + high);
            if (letter == 'x') {
                mfc::CString rest = MidOf(input_, high);
                mfc::CString head = Left(input_, low);
                input_ = head + rest;
                caret_ = low;
                anchor_ = low;
                At(this)->CallDrawInput();
            }
        } else if (letter == 'v') {
            mfc::CString clip(ClipboardText().c_str());
            int cut = static_cast<const TextAt&>(clip).FindOneOf("\r\n");
            if (cut >= 0) clip = Left(clip, cut);
            mfc::CString rest = MidOf(input_, high);
            mfc::CString head = Left(input_, low);
            input_ = (head + clip) + rest;
            int position = Length(clip) - (anchor_ != caret_ ? 1 : 0) + high;
            caret_ = position;
            anchor_ = position;
            At(this)->CallDrawInput();
        }
    }
    if (ctrl_) return;
    mfc::CString rest = MidOf(input_, high);
    mfc::CString head = Left(input_, low);
    input_ = PlusChar(head, static_cast<char>(key)) + rest;
    caret_ = low + 1;
    anchor_ = low + 1;
    At(this)->CallDrawInput();
}
EI_REPLACE_METHOD(0x005E44A0, ConsoleWindow::OnChar);
EI_REBIRTH_IMPLEMENTS(0x005E44A0, ei::rebirth::Patch::ImproveConsole);

// 005E4800: keeps the scroll position within the lines, then (with a surface and a font) fills
// the area above the input strip and draws the visible lines bottom up, 15 pixels apart, with a
// "+" at the top right when there is more above and at the bottom right when scrolled back.
void ConsoleWindow::DrawLines() {
    if (scroll_ > lines_.GetSize() - visibleLines_) scroll_ = lines_.GetSize() - visibleLines_;
    if (scroll_ < 0) scroll_ = 0;
    if (!surface_ || !font_) return;
    SurfaceDC* surface = static_cast<SurfaceDC*>(surface_);
    GameDC* dc = surface->GetDC();
    dc->FillSolidRect(0, 0, width_, height_ - kInputHeight, kBackground);
    void* oldFont = dc->SelectFont(font_);
    dc->SetTextColor(kLineText);
    dc->SetBkMode(TRANSPARENT);
    int line = lines_.GetSize() - scroll_ - 1;
    for (int i = 0, y = 0; i < visibleLines_ && line >= 0; ++i, y += kLineHeight, --line) {
        RECT rect{8, height_ - y - 0x2E, width_ - 0x12, height_ - y - 0x1A};
        const mfc::CString& text = lines_[line];
        dc->DrawText(text, Length(text), &rect, kDrawLine);
    }
    if (scroll_ > 0) {
        mfc::CString mark(GameText(kMoreText));
        RECT rect{width_ - 0x12, height_ - 0x2E, width_ - 8, height_ - kInputHeight};
        dc->DrawText(mark, Length(mark), &rect, kDrawMark);
    }
    if (scroll_ < lines_.GetSize() - visibleLines_) {
        mfc::CString mark(GameText(kMoreText));
        RECT rect{width_ - 0x12, 8, width_ - 8, 0x17};
        dc->DrawText(mark, Length(mark), &rect, kDrawMark);
    }
    dc->SelectFont(oldFont);
    surface->ReleaseDC();
}
EI_REPLACE_METHOD(0x005E4800, ConsoleWindow::DrawLines);

// 005E4A10: keeps the caret and anchor within the input, then (with a surface and a font) fills
// the input strip, shades the selection (clipped 16 pixels before the right edge), draws "> " and
// the input, and a 2-pixel caret line when it lies left of that limit.
void ConsoleWindow::DrawInput() {
    caret_ = Clamp(caret_, Length(input_));
    anchor_ = Clamp(anchor_, Length(input_));
    if (!surface_ || !font_) return;
    SurfaceDC* surface = static_cast<SurfaceDC*>(surface_);
    GameDC* dc = surface->GetDC();
    void* oldFont = dc->SelectFont(font_);
    dc->FillSolidRect(0, height_ - kInputHeight, width_, kInputHeight, kBackground);
    int caretX = PromptWidth(dc, input_, caret_);
    if (anchor_ != caret_) {
        int anchorX = PromptWidth(dc, input_, anchor_);
        int low = caretX < anchorX ? caretX : anchorX;
        int high = caretX > anchorX ? caretX : anchorX;
        if (high > width_ - 0x10) high = width_ - 0x10;
        if (low < high) dc->FillSolidRect(low + 8, height_ - 0x17, high - low, 0xF, kSelection);
    }
    dc->SetTextColor(kInputText);
    dc->SetBkMode(TRANSPARENT);
    RECT rect{8, height_ - 0x17, width_ - 8, height_ - 3};
    mfc::CString text = mfc::CString(GameText(kPromptText)) + input_;
    dc->DrawText(text, Length(text), &rect, kDrawLine);
    if (caretX < width_ - 0x10) {
        GamePen pen;
        pen.Construct(PS_SOLID, 2, kInputText);
        void* oldPen = dc->SelectPen(&pen);
        dc->MoveTo(caretX + 8, height_ - 0x17);
        dc->LineTo(caretX + 8, height_ - 8);
        dc->SelectPen(oldPen);
        pen.Destruct();
    }
    dc->SelectFont(oldFont);
    surface->ReleaseDC();
}
EI_REPLACE_METHOD(0x005E4A10, ConsoleWindow::DrawInput);

} // namespace console

namespace ui_gdi {
__declspec(naked) void GameDC::FillSolidRect(int, int, int, int, COLORREF) EI_ORIGINAL(0x0070BB04)
__declspec(naked) int GameDC::SetBkMode(int) EI_ORIGINAL(0x007090C2)
__declspec(naked) void* GameDC::SelectPen(void*) EI_ORIGINAL(0x00708F5A)
__declspec(naked) POINT GameDC::MoveTo(int, int) EI_ORIGINAL(0x00709613)
__declspec(naked) BOOL GameDC::LineTo(int, int) EI_ORIGINAL(0x0070965F)
__declspec(naked) GamePen* GamePen::Construct(int, int, COLORREF) EI_ORIGINAL(0x0070A0E2)
__declspec(naked) BOOL GamePen::DeleteObject() EI_ORIGINAL(0x0070A0CC)
__declspec(naked) GameDC* SurfaceDC::GetDC() EI_ORIGINAL(0x005FDC00)
__declspec(naked) void SurfaceDC::ReleaseDC() EI_ORIGINAL(0x005FDC60)
} // namespace ui_gdi
