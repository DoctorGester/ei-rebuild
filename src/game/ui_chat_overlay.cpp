// The in-game chat overlay: its constructor, per-frame slots (vtable 007400DC: 24 update, 22
// redraw), the chat line's opening and its keys. Its destructor is in ui_window_delete.cpp.
// docs/notes/ui.md, section 3.
#include "console.h"
#include "net_classes.h"
#include "net_messages.h"
#include "ui.h"
#include "ui_gdi.h"
#include "ui_screens.h"
#include "world.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/stlp.h"
#include "originals_library.h"
#include "originals_ui.h"
#include "game_data.h"

#include <cstring>
#include <new>
namespace {
constexpr int kLineLifetime = 20000;   // ms
constexpr int kLeft = 10;
constexpr int kTop = 10;
constexpr int kLineHeight = 16;
// The typed line's surface.
constexpr int kTypedLeft = 10, kTypedTop = 0x23D, kTypedRight = 0x316, kTypedBottom = 0x24E, kTypedFormat = 4;
void** ChatOverlayVtable() { return ei::GamePointer<void*>(0x007400DC); }
void** ChatOverlayInputVtable() { return ei::GamePointer<void*>(0x007400A8); }
// The typed line: at most 200 characters and narrower than 780 pixels in font 1.
constexpr int kMaxTyped = 200;
constexpr int kMaxTypedWidth = 0x30C;
constexpr int kTypedFont = 1;
// A chat message stays on the client's timers list for 60 s (the float's bits).
constexpr std::uint32_t kSayLifetime = 0x42700000;

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


// A chat line for the server (SayMessage) carrying `text`, kept 60 s on the client's timers.
void SendSay(const char* text) {
    auto* say = net::NewMessageMemory<net::SayMessage>();
    if (say) {
        ei::SetVtable(say, net::kMessageVtable);
        say->version_ = 1;
        stlp::StringElement::Default(&say->text_);
        ei::SetVtable(say, net::SayMessage::kVtableAddress);
    }
    // As the original: a failed allocation is not checked from here on.
    say->text_.Assign(text);   // inlined by the original: overwrite, then cut or append
    net::GameClient()->AddTimed(say, kSayLifetime);
}
} // namespace

// --- 005E2A30: a chat line added --------------------------------------------------------------------

namespace {
constexpr int kMaxLines = 12;
constexpr int kLineWidth = 0x30C;
constexpr int kLineFont = 1;
constexpr int kLineSurfaceHeight = 0x11, kLineSurfaceFormat = 5, kLineSurfacePad = 3;
constexpr int kLineDrawRight = 0x30F;
constexpr UINT kLineDrawFormat = 0x800;   // DT_NOPREFIX
constexpr COLORREF kWhite = 0xFFFFFF;
const char* const kColourMark = "^";
const char* const kDefaultColour = "7";

// The character at `p` as the original compares it: a lead byte shifted left by 8 (sign-extended)
// plus the next byte (sign-extended), otherwise the byte itself.
int CharAt(const char* p) {
    int value = 0;
    const char* q = p;
    if (libcrt::Ismbblead(static_cast<unsigned char>(*p))) {
        value = static_cast<signed char>(*p) << 8;
        q = p + 1;
    }
    return value + static_cast<signed char>(*q);
}

// The colour of "^<digit>": bit 0 blue, bit 1 green, bit 2 red (COLORREF 0x00BBGGRR).
COLORREF MarkColour(char digit) {
    int bits = static_cast<signed char>(digit) - '0';
    return ((bits & 1) ? 0xFF0000u : 0u) | ((bits & 2) ? 0xFF00u : 0u) | ((bits & 4) ? 0xFFu : 0u);
}

void DrawPiece(CUiSurface* surface, const char* text, int x, COLORREF colour) {
    RECT rect = {x, 0, kLineDrawRight, kLineSurfaceHeight};
    SurfaceDrawText(surface, nullptr, text, &rect, kLineFont, colour, kLineDrawFormat, 1);
}
} // namespace

void CChatOverlay::AddLine(const char* text, std::uint32_t) {
    using Node = stlp::list<ChatLine>::Node;
    int before = 0;
    for (Node* node = lines_.First(); node != lines_.End(); node = node->next) ++before;
    console::CStringArray plain;       // the lines without colour codes
    console::CStringArray formatted;   // the same lines with them
    mfc::CString source(text);
    mfc::CString ch, word, wordFormatted, line, lineFormatted;
    bool mark = false;
    for (const char* p = source; CharAt(p) != 0; p = MbsInc(p)) {
        console::Ops(ch).AssignChar(p[0]);
        if (libcrt::Isleadbyte(static_cast<signed char>(*p))) console::Ops(ch).AppendChar(p[1]);
        int c = CharAt(p);
        if (mark) {
            if (static_cast<unsigned>(c) >= '0' && static_cast<unsigned>(c) < '8')
                wordFormatted += ch;
            else
                wordFormatted += kDefaultColour;
            mark = false;
        } else if (c == ' ') {
            word += ch;
            line += word;
            word.Empty();
            wordFormatted += ch;
            lineFormatted += wordFormatted;
            wordFormatted.Empty();
        } else if (c == '^') {
            wordFormatted += ch;
            mark = true;
        } else {
            if (TextWidth(interface_, line + word + ch, kLineFont) > kLineWidth) {
                if (line.GetLength() != 0) {
                    plain.SetAtGrow(plain.GetSize(), line);
                    formatted.SetAtGrow(formatted.GetSize(), lineFormatted);
                }
                line.Empty();
                lineFormatted.Empty();
                if (TextWidth(interface_, word + ch, kLineFont) > kLineWidth) {
                    if (word.GetLength() != 0) {
                        plain.SetAtGrow(plain.GetSize(), word);
                        formatted.SetAtGrow(formatted.GetSize(), wordFormatted);
                    }
                    word.Empty();
                    wordFormatted.Empty();
                }
            }
            word += ch;
            wordFormatted += ch;
        }
    }
    if (mark) wordFormatted += kDefaultColour;
    if (line.GetLength() != 0 || word.GetLength() != 0) {
        plain.SetAtGrow(plain.GetSize(), line + word);
        formatted.SetAtGrow(formatted.GetSize(), lineFormatted + wordFormatted);
    }

    // The oldest lines go (list::erase, 005E3AD0, inlined as Update does).
    for (int i = 0; i < plain.GetSize() + before - kMaxLines; ++i) {
        Node* first = lines_.First();
        first->prev->next = first->next;
        first->next->prev = first->prev;
        first->data.Destroy();
        stlp::deallocate(first, sizeof(Node));
    }

    std::uint32_t now = gameapi::timeGetTime();
    COLORREF colour = kWhite;
    for (int i = 0; i < plain.GetSize(); ++i) {
        // insert(end()) of an empty line (005E3A10).
        auto* node = static_cast<Node*>(stlp::allocate(sizeof(Node)));
        new (&node->data.text) mfc::CString;
        new (&node->data.name) mfc::CString;
        node->data.time = 0;
        node->data.surface = nullptr;
        Node* head = lines_.head;
        node->next = head;
        node->prev = head->prev;
        head->prev->next = node;
        head->prev = node;

        node->data.name = plain[i];
        node->data.time = now;
        void* memory = gamecrt::operator_new(sizeof(CUiSurface));
        CUiSurface* surface =
            memory ? static_cast<CUiSurface*>(memory)->Construct(
                         interface_, 0, 0, TextWidth(interface_, plain[i], kLineFont) + kLineSurfacePad,
                         kLineSurfaceHeight, kLineSurfaceFormat, 1)
                   : nullptr;
        UiSurfaceGetDC(surface);
        SurfaceClearOriginal(surface);
        const mfc::CString& codes = formatted[i];
        mfc::CString drawn;
        mfc::CString piece;
        int x = 0;
        int start = 0;
        for (int at = console::Ops(codes).Find(kColourMark, 0); at != -1;
             at = console::Ops(codes).Find(kColourMark, start)) {
            piece = console::Ops(codes).Mid(start, at - start);
            DrawPiece(surface, piece, x, colour);
            drawn += piece;
            x = TextWidth(interface_, drawn, kLineFont);
            colour = MarkColour(static_cast<const char*>(codes)[at + 1]);
            start = at + 2;
        }
        piece = console::Ops(codes).Mid(start);
        if (piece.GetLength() != 0) DrawPiece(surface, piece, x, colour);
        UiSurfaceReleaseDC(surface);
        node->data.surface = surface;
    }
}
EI_REPLACE_METHOD(0x005E2A30, CChatOverlay::AddLine);

// --- 005E36C0 / 005E3930: opening the chat line, clearing the lines ----------------------------------

void CChatOverlay::SetOpen(bool open) {
    chatOpen_ = open;
    if (open) {
        typed_.Empty();
        caret_ = 0;
    }
    ChatOverlayDrawTypedOriginal(this);
}
EI_REPLACE_METHOD(0x005E36C0, CChatOverlay::SetOpen);

void CChatOverlay::ClearLines() {
    lines_.ClearWith([](ChatLine* line) { line->Destroy(); });
}
EI_REPLACE_METHOD(0x005E3930, CChatOverlay::ClearLines);

// --- 005E3700: the typed line drawn (Rebirth-patched: WideScreen reproduced) ------------------------------

namespace {
constexpr int kTypedTextRight = 0x30F, kTypedTextBottom = 0x11;
constexpr int kCaretBottom = 0x10;
constexpr COLORREF kTypedColour = 0xFFFFFF;
} // namespace

void CChatOverlay::DrawTyped() {
    if (!chatOpen_) {
        surfaces_.data[0]->Freeze(0, 0);
        return;
    }
    surfaces_.data[0]->Freeze(1, 0);
    if (caret_ > typed_.GetLength()) caret_ = typed_.GetLength();
    if (caret_ < 0) caret_ = 0;
    CUiSurface* surface = surfaces_.data[0];
    ui_gdi::GameDC* dc = UiSurfaceGetDC(surface);
    surface->Clear();
    RECT rect{0, 0, kTypedTextRight, kTypedTextBottom};
    SurfaceDrawText(surface, nullptr, typed_, &rect, kTypedFont, kTypedColour, DT_NOPREFIX, 1);
    int width = TextWidth(interface_, typed_.Left(caret_), kTypedFont);
    if (width < kMaxTypedWidth) {
        ui_gdi::GamePen pen;
        pen.Construct(PS_SOLID, 1, kTypedColour);
        mfc::impl::CGdiObject* oldPen = dc->SelectObject(&pen);
        POINT top{width, 0};
        POINT bottom{width, kCaretBottom};
        // WideScreen (005E3839 / 005E386C: XOR ECX,ECX): without the margin.
        interface_->CallScalePointToScreenNoMargin(&top);
        interface_->CallScalePointToScreenNoMargin(&bottom);
        dc->MoveTo(top.x, top.y);
        dc->LineTo(bottom.x, bottom.y);
        dc->SelectObject(oldPen);
        pen.Destruct();
    }
    UiSurfaceReleaseDC(surface);
}
EI_REPLACE_METHOD(0x005E3700, CChatOverlay::DrawTyped);

// --- 005E3230 / 005E3550: the chat line's keys (input slots 9 and 10) ---------------------------------

void CChatOverlay::OnKeyDown(int key, int, int) {
    if (!chatOpen_) return;
    inputHandled_ = true;
    switch (key) {
    case VK_BACK:
        if (caret_ <= 0) break;
        typed_ = typed_.Left(caret_ - 1) + typed_.Mid(caret_);
        --caret_;
        break;
    case VK_RETURN:
        if (typed_.GetLength() != 0 && (IsMultiplayerClient() & 0xFF)) SendSay(typed_);
        chatOpen_ = false;
        break;
    case VK_ESCAPE:
        chatOpen_ = false;
        break;
    case VK_END:
        caret_ = typed_.GetLength();
        break;
    case VK_HOME:
        caret_ = 0;
        break;
    case VK_LEFT:
        --caret_;
        break;
    case VK_RIGHT:
        ++caret_;
        break;
    case VK_DELETE:
        if (caret_ >= typed_.GetLength()) break;
        typed_ = typed_.Left(caret_) + typed_.Mid(caret_ + 1);
        break;
    default:
        return;
    }
    ChatOverlayDrawTypedOriginal(this);
}

void CChatOverlay::OnChar(int key, int, int) {
    if (!chatOpen_) return;
    if (key == VK_BACK || key == VK_TAB || key == VK_RETURN || key == VK_ESCAPE) return;
    mfc::CString line = mfc::operator+(typed_.Left(caret_), static_cast<char>(key)) + typed_.Mid(caret_);
    if (!interface_->ctrlDown_ && typed_.GetLength() < kMaxTyped && TextWidth(interface_, line, kTypedFont) < kMaxTypedWidth) {
        typed_ = line;
        ++caret_;
    }
    ChatOverlayDrawTypedOriginal(this);
}
namespace {
} // namespace
EI_REPLACE_METHOD(0x005E3230, InputForward<&CChatOverlay::OnKeyDown>::Call);
EI_REPLACE_METHOD(0x005E3550, InputForward<&CChatOverlay::OnChar>::Call);

// --- 005E26F0: the constructor ---------------------------------------------------------------------

CChatOverlay* CChatOverlay::Construct(CInterface3D* ui) {
    CUiWindow::Construct(ui);
    using Node = stlp::list<ChatLine>::Node;
    auto* sentinel = static_cast<Node*>(stlp::allocate(sizeof(Node)));
    sentinel->next = sentinel;
    sentinel->prev = sentinel;
    lines_.head = sentinel;
    new (&typed_) mfc::CString;
    vtable_ = ChatOverlayVtable();
    inputVtable_ = ChatOverlayInputVtable();
    void* memory = gamecrt::operator_new(sizeof(CUiSurface));
    AddSurface(memory ? static_cast<CUiSurface*>(memory)->Construct(interface_, kTypedLeft, kTypedTop, kTypedRight,
                                                                    kTypedBottom, kTypedFormat, 1)
                      : nullptr);
    chatOpen_ = false;
    caret_ = 0;
    ChatOverlayDrawTypedOriginal(this);
    return this;
}
EI_REPLACE_METHOD(0x005E26F0, CChatOverlay::Construct);

void CChatOverlay::Update() {
    std::uint32_t now = gameapi::timeGetTime();
    using Node = stlp::list<ChatLine>::Node;
    Node* head = lines_.head;
    for (Node* node = head->next; node != head;) {
        Node* next = node->next;
        if (static_cast<int>(now - node->data.time) > kLineLifetime) {
            // list::erase (005E3AD0): unlinked, destroyed, the node back to the pool.
            node->prev->next = next;
            next->prev = node->prev;
            node->data.Destroy();
            stlp::deallocate(node, sizeof(Node));
        }
        node = next;
    }
}
EI_REPLACE_METHOD(0x005E2990, CChatOverlay::Update);

void CChatOverlay::Redraw() {
    int y = kTop;
    for (auto* node = lines_.First(); node != lines_.End(); node = node->next) {
        node->data.surface->MoveTo(kLeft, y);
        node->data.surface->Blit();
        y += kLineHeight;
    }
    RedrawBase();
}
EI_REPLACE_METHOD(0x005E29E0, CChatOverlay::Redraw);
