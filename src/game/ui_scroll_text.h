#pragma once

// The scrolling text: tokens, laid out lines and the window with its scroll bar.

#include "ui.h"
#include "ui_scrollbar.h"

// A piece of a scrolling text: a format change (newline, font, colour) or a word (text, width).
enum class TokenKind : std::uint8_t { Format, Word };

struct TextToken {
    TokenKind kind = TokenKind::Word;
    bool newline = false;            // a newline token, or a font change after the first token
    int font = 0;                    // interface font
    std::uint32_t colour = 0;
    std::string text;
    int width = 0;                   // its width in the token's font
};

// A laid out line of a scrolling text: the fields PlaceWords reads.
struct UiTextLine {
    int height = 0;                  // pixels
    int firstToken = 0;              // index in the token array
};

// Tokenised text with a scroll bar at its right edge: the battle screen's message log and the objectives window's
// quest log.
class UiScrollText : public UiWindow {
public:
    // The bar scrolls 20 lines per second; justified (align 2).
    UiScrollText(Interface3D* ui, const char* text, Rect rect, float depth);
    // Scrolled to the end first, then kept that far from the end.
    void AddText(std::string_view text);
    // Scrolls to the top.
    void SetText(std::string_view text);
    // The text and its bar over `rect`.
    void SetRect(const Rect* rect);
    // The bar's: its held arrows scroll, its input.
    void Update() override;
    void OnEvent(UiEvent& event) override;
    // Places the words again when the bar's position moved; the bar's sprites.
    void DrawSprites(SceneRenderer* pass) override;
    // The lines stay until the next layout.
    void Clear();
    // Words apart by a third of the line's height (the font in effect at the line's start); a newline format
    // token ends the line and sets the next lines' font. Then the scroll bar's maximum.
    void Layout();
    // One format token before each run of newline, BEL C<rgb> and BEL F<digit> tokens.
    void Tokenize(const std::string& text);
    // The token at `pos` (spaces and tabs skipped): a BEL F / BEL C format (3 / 5 characters), a newline, or
    // up to the next space, tab, BEL or newline. Returns the position after it, or -1 at the end (`out`
    // untouched). `this` is not read.
    int NextToken(const std::string& text, int pos, std::string* out);
    // The words of the lines shown from `scroll` pixels down into shownWords. A line is justified when align is
    // not 1, it is not the last, not ended by a newline, and of two or more tokens.
    void PlaceWords(int scroll);
    // The surface's paint: shownWords.
    void PaintWords(UiSurface& surface) const;

    UiScrollbar bar;                 // its position is the pixels scrolled
    Rect textRect;                   // the text's rectangle (surface coordinates)
    std::vector<TextToken> tokens;
    std::vector<UiTextLine> lines;  // the laid out lines
    int align = 0;                   // 1: left aligned (else justified)
    // A word as PlaceWords placed it: its rectangle (surface layout units), font and colour.
    struct ShownWord {
        std::string text;
        Rect rect;
        int font = 0;
        std::uint32_t colour = 0;
    };
    std::vector<ShownWord> shownWords;

private:
    // The tail of SetRect (the text 14 pixels narrower for the bar).
    void PlaceText(const Rect& rect);
};
