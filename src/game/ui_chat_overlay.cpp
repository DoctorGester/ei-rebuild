// The in-game chat overlay: its constructor, per-frame update and redraw, the chat line's opening and its
// keys. Its destructor is in ui_window_delete.cpp. docs/notes/ui.md, section 3.
#include "asset_text.h"
#include "console.h"
#include "net_classes.h"
#include "net_messages.h"
#include "ui.h"
#include "ui_gdi.h"
#include "ui_screens.h"
#include "world.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/stlp.h"
#include "game_data.h"
#include "lib/str.h"

#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>
namespace {
constexpr int kLineLifetime = 20000;   // ms
constexpr int kLeft = 10;
constexpr int kTop = 10;
constexpr int kLineHeight = 16;
// The typed line's surface.
constexpr int kTypedLeft = 10, kTypedTop = 0x23D, kTypedRight = 0x316, kTypedBottom = 0x24E, kTypedFormat = 4;
// The typed line: at most 200 characters and narrower than 780 pixels in font 1.
constexpr int kMaxTyped = 200;
constexpr int kMaxTypedWidth = 0x30C;
constexpr int kTypedFont = 1;
// A chat message stays on the client's timers list for 60 s (the float's bits).
constexpr std::uint32_t kSayLifetime = 0x42700000;

int TextWidth(CInterface3D* ui, const std::string& text, int font) {
    SIZE size;
    ui->TextExtent(&size, text, font);
    return size.cx;
}


// A chat line for the server (SayMessage) carrying `text`, kept 60 s on the client's timers.
void SendSay(const std::string& text) {
    auto* say = net::NewCounted<net::SayMessage>();
    // A failed allocation is not checked from here on.
    say->text_ = text;
    net::GameClient()->AddTimed(say, kSayLifetime);
}
} // namespace

// --- a chat line added ---------------------------------------------------------------------------

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

// The character at `p` as it is compared: a lead byte shifted left by 8 (sign-extended)
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

void DrawPiece(CUiSurface* surface, std::string_view text, int x, COLORREF colour) {
    RECT rect = {x, 0, kLineDrawRight, kLineSurfaceHeight};
    surface->DrawTextIn(text, &rect, kLineFont, colour, kLineDrawFormat, 1);
}
} // namespace

void CChatOverlay::AddLine(const char* text, std::uint32_t) {
    using Node = stlp::list<ChatLine>::Node;
    int before = 0;
    for (Node* node = lines_.First(); node != lines_.End(); node = node->next) ++before;
    std::vector<std::string> plain;       // the lines without colour codes
    std::vector<std::string> formatted;   // the same lines with them
    std::string source = text;
    std::string ch, word, wordFormatted, line, lineFormatted;
    bool mark = false;
    // With EI_UTF8 a character is its whole UTF-8 sequence, so a word wider than a line is broken
    // between characters and measured as text.
    for (const char* p = source.c_str(); CharAt(p) != 0; p = text::kUtf8 ? p + text::CharacterLength(p) : gamecrt::mbsinc(p)) {
        ch.assign(1, p[0]);
        if constexpr (text::kUtf8) {
            for (int k = 1; k < text::CharacterLength(p); ++k) ch += p[k];
        } else {
            if (libcrt::Isleadbyte(static_cast<signed char>(*p))) ch += p[1];
        }
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
            word.clear();
            wordFormatted += ch;
            lineFormatted += wordFormatted;
            wordFormatted.clear();
        } else if (c == '^') {
            wordFormatted += ch;
            mark = true;
        } else {
            if (TextWidth(interface_, line + word + ch, kLineFont) > kLineWidth) {
                if (line.size() != 0) {
                    plain.push_back(line);
                    formatted.push_back(lineFormatted);
                }
                line.clear();
                lineFormatted.clear();
                if (TextWidth(interface_, word + ch, kLineFont) > kLineWidth) {
                    if (word.size() != 0) {
                        plain.push_back(word);
                        formatted.push_back(wordFormatted);
                    }
                    word.clear();
                    wordFormatted.clear();
                }
            }
            word += ch;
            wordFormatted += ch;
        }
    }
    if (mark) wordFormatted += kDefaultColour;
    if (line.size() != 0 || word.size() != 0) {
        plain.push_back(line + word);
        formatted.push_back(lineFormatted + wordFormatted);
    }
    const int count = static_cast<int>(plain.size());

    // The oldest lines go (list::erase, inlined as Update does).
    for (int i = 0; i < count + before - kMaxLines; ++i) {
        Node* first = lines_.First();
        first->prev->next = first->next;
        first->next->prev = first->prev;
        first->data.Destroy();
        stlp::deallocate(first, sizeof(Node));
    }

    std::uint32_t now = gameapi::timeGetTime();
    COLORREF colour = kWhite;
    for (int i = 0; i < count; ++i) {
        // insert(end()) of an empty line.
        auto* node = stlp::allocate_as<Node>();
        std::construct_at(&node->data.text);
        std::construct_at(&node->data.name);
        node->data.time = 0;
        node->data.surface = nullptr;
        Node* head = lines_.head;
        node->next = head;
        node->prev = head->prev;
        head->prev->next = node;
        head->prev = node;

        node->data.name = plain[i];
        node->data.time = now;
        const int width = TextWidth(interface_, plain[i], kLineFont) + kLineSurfacePad;
        CUiSurface* surface = new CUiSurface(interface_, 0, 0, width, kLineSurfaceHeight, kLineSurfaceFormat, 1);
        UiSurfaceGetDC(surface);
        surface->Clear();
        const std::string& codes = formatted[i];
        std::string drawn;
        std::string piece;
        int x = 0;
        int start = 0;
        for (int at = str::Find(codes, kColourMark, 0); at != -1; at = str::Find(codes, kColourMark, start)) {
            piece = str::Mid(codes, start, at - start);
            DrawPiece(surface, piece, x, colour);
            drawn += piece;
            x = TextWidth(interface_, drawn, kLineFont);
            colour = MarkColour(codes[at + 1]);
            start = at + 2;
        }
        piece = str::Mid(codes, start);
        if (piece.size() != 0) DrawPiece(surface, piece, x, colour);
        UiSurfaceReleaseDC(surface);
        node->data.surface = surface;
    }
}
EI_REWRITTEN(0x005E2A30, CChatOverlay::AddLine);

// --- opening the chat line, clearing the lines ---------------------------------------------------

void CChatOverlay::SetOpen(bool open) {
    chatOpen_ = open;
    if (open) {
        typed_.clear();
        caret_ = 0;
    }
    DrawTyped();
}
EI_REWRITTEN(0x005E36C0, CChatOverlay::SetOpen);

void CChatOverlay::ClearLines() {
    lines_.ClearWith([](ChatLine* line) { line->Destroy(); });
}
EI_REWRITTEN(0x005E3930, CChatOverlay::ClearLines);

// --- the typed line drawn (Rebirth-patched: WideScreen reproduced) -------------------------------

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
    if (caret_ > static_cast<int>(typed_.size())) caret_ = typed_.size();
    if (caret_ < 0) caret_ = 0;
    CUiSurface* surface = surfaces_.data[0];
    mfc::impl::CDC* dc = UiSurfaceGetDC(surface);
    surface->Clear();
    RECT rect{0, 0, kTypedTextRight, kTypedTextBottom};
    surface->DrawTextIn(typed_, &rect, kTypedFont, kTypedColour, DT_NOPREFIX, 1);
    int width = TextWidth(interface_, str::Left(typed_, caret_).c_str(), kTypedFont);
    if (width < kMaxTypedWidth) {
        mfc::impl::CPen pen(PS_SOLID, 1, kTypedColour);
        mfc::impl::CGdiObject* oldPen = dc->SelectObject(&pen);
        POINT top{width, 0};
        POINT bottom{width, kCaretBottom};
        // WideScreen: without the margin.
        interface_->CallScalePointToScreenNoMargin(&top);
        interface_->CallScalePointToScreenNoMargin(&bottom);
        dc->MoveTo(top.x, top.y);
        dc->LineTo(bottom.x, bottom.y);
        dc->SelectObject(oldPen);
    }
    UiSurfaceReleaseDC(surface);
}
EI_REWRITTEN(0x005E3700, CChatOverlay::DrawTyped);

// --- the chat line's keys -------------------------------------------------------------------------

void CChatOverlay::OnKeyDown(int key, int, int) {
    if (!chatOpen_) return;
    handled_ = true;
    switch (key) {
    case VK_BACK: {   // by character, as Delete and the caret's steps
        if (caret_ <= 0) break;
        const int previous = text::PreviousCharacter(typed_, caret_);
        typed_ = str::Left(typed_, previous) + str::Mid(typed_, caret_);
        caret_ = previous;
        break;
    }
    case VK_RETURN:
        if (typed_.size() != 0 && (IsMultiplayerClient() & 0xFF)) SendSay(typed_);
        chatOpen_ = false;
        break;
    case VK_ESCAPE:
        chatOpen_ = false;
        break;
    case VK_END:
        caret_ = typed_.size();
        break;
    case VK_HOME:
        caret_ = 0;
        break;
    case VK_LEFT:
        caret_ = text::PreviousCharacter(typed_, caret_);
        break;
    case VK_RIGHT:
        caret_ = text::NextCharacter(typed_, caret_);
        break;
    case VK_DELETE:
        if (caret_ >= static_cast<int>(typed_.size())) break;
        typed_ = str::Left(typed_, caret_) + str::Mid(typed_, text::NextCharacter(typed_, caret_));
        break;
    default:
        return;
    }
    DrawTyped();
}

void CChatOverlay::OnChar(int key, int, int) {
    if (!chatOpen_) return;
    if (key == VK_BACK || key == VK_TAB || key == VK_RETURN || key == VK_ESCAPE) return;
    std::string line = (text::Typed(str::Left(typed_, caret_).c_str(), key) + str::Mid(typed_, caret_)).GetString();
    // The line also stays within kMaxTyped bytes (what 200 one-byte characters took).
    // That is what the sent SayMessage fits in: a client's update carries 220 bytes, and a message larger
    // than that would never be counted in one. Without EI_UTF8 it follows from the character count.
    if (!interface_->ctrlDown_ && text::CharacterCount(typed_) < kMaxTyped && static_cast<int>(line.size()) <= kMaxTyped &&
        TextWidth(interface_, line, kTypedFont) < kMaxTypedWidth) {
        typed_ = line;
        caret_ += text::TypedLength(key);
    }
    DrawTyped();
}
EI_REWRITTEN(0x005E3230, CChatOverlay::OnKeyDown);
EI_REWRITTEN(0x005E3550, CChatOverlay::OnChar);

// --- the constructor -----------------------------------------------------------------------------

// Its own redraw and update; the input side's keys and characters.
CChatOverlay::CChatOverlay(CInterface3D* ui) : CUiWindow(ui) {
    using Node = stlp::list<ChatLine>::Node;
    auto* sentinel = stlp::allocate_as<Node>();
    sentinel->next = sentinel;
    sentinel->prev = sentinel;
    lines_.head = sentinel;
    AddSurface(new CUiSurface(interface_, kTypedLeft, kTypedTop, kTypedRight, kTypedBottom, kTypedFormat, 1));
    chatOpen_ = false;
    caret_ = 0;
    DrawTyped();
}
EI_REWRITTEN_SPECIAL(0x005E26F0, CChatOverlay::CChatOverlay);

void CChatOverlay::Update() {
    std::uint32_t now = gameapi::timeGetTime();
    using Node = stlp::list<ChatLine>::Node;
    Node* head = lines_.head;
    for (Node* node = head->next; node != head;) {
        Node* next = node->next;
        if (static_cast<int>(now - node->data.time) > kLineLifetime) {
            // list::erase: unlinked, destroyed, the node back to the pool.
            node->prev->next = next;
            next->prev = node->prev;
            node->data.Destroy();
            stlp::deallocate(node, sizeof(Node));
        }
        node = next;
    }
}
EI_REWRITTEN(0x005E2990, CChatOverlay::Update);

void CChatOverlay::Redraw() {
    int y = kTop;
    for (auto* node = lines_.First(); node != lines_.End(); node = node->next) {
        node->data.surface->MoveTo(kLeft, y);
        node->data.surface->Blit();
        y += kLineHeight;
    }
    RedrawBase();
}
EI_REWRITTEN(0x005E29E0, CChatOverlay::Redraw);
