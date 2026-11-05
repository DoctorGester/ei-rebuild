#pragma once

// The in-game chat overlay.

#include "ui.h"

// A line of the in-game chat overlay: the text, the speaker, a value and the line's own surface.
struct ChatLine {
    std::string text;          // AddLine leaves it empty
    std::string name;          // the line's text without its colour codes
    std::uint32_t time = 0;     // timeGetTime when it was added
    std::unique_ptr<UiSurface> surface;
};

static_assert(std::is_nothrow_move_constructible_v<ChatLine>);

// The in-game chat overlay: the interface's root window, updated after the top window every frame; in a
// multiplayer client it holds the chat line, which takes the keys while it is open.
class ChatOverlay : public UiWindow {
public:
    // Rebirth-patched.
    explicit ChatOverlay(Interface3D* ui);
    // Lines narrower than 780 pixels, "^<digit>" switching the colour (bits 0 / 1 / 2
    // blue / green / red; a missing digit is 7); at most 12 lines remain.
    void AddLine(const char* text);
    // Opening empties the typed line.
    void SetOpen(bool open);
    void ClearLines();
    // Hidden while the chat is closed; clamps the caret.
    void ShowTyped();
    // Rebirth-patched (WideScreen). The typed line's paint: the background, the text and a 1-pixel caret line.
    void PaintTyped(UiSurface& surface) const;
    void OnEvent(UiEvent& event) override;
    // Nothing while the chat is closed. Every key is marked used; Enter sends a non-empty line (a network
    // client: SayMessage kept 60 s) and, like Esc, closes the chat. Char inserts unless Ctrl is down, the
    // line is 200 characters or more, or it would be 780 pixels wide; the character is not marked used.
    void KeyDown(UiEvent& event);
    void Char(UiEvent& event);
    // Lines older than 20 s are erased.
    void Update() override;
    void DrawSurfaces() override;

    std::vector<ChatLine> lines;
    bool chatOpen = false;           // a multiplayer client's chat line is open (it takes the keys)
    std::string typed;
    int caret = 0;                   // in the typed line (a byte offset at a character's start: U4)
};
