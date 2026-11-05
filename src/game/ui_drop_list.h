#pragma once

// The drop-down list (the options' resolution row).

#include "ui.h"
#include "ui_scrollbar.h"

// A drop-down list (ours; the options' resolution row): the row shows the chosen text; a click (or Open) opens the
// list of every text below the row, or above it when only that side has room, with the chosen one marked, the active
// one (under the mouse, or moved to by the keys) highlighted and the game's scroll bar when they don't all fit. A
// click on an entry or Enter chooses the active one; Escape, a click outside or another click on the row closes it
// without a change. While open it is its owner's popup (every input event goes to it) and the owner's children under
// it are not drawn. Drawn in the game's list look: the frame and the shade of the save list, the rows' highlight and
// font. See docs/notes/ui-native.md, "The drop-down list".
class UiDropList : public UiContainer {
public:
    // `row` in layout coordinates.
    UiDropList(Interface3D* ui, UiContainer* owner, Rect row);
    void AddText(std::string_view text);
    // Whether it changed since the last call (the flag cleared).
    bool TakeChanged();
    // 0 when not below the text count.
    void SetValue(int value);
    // The value `by` steps on, wrapping both ways.
    void Step(int by);
    void Open();
    void Close();
    // The first entry shown, and entry `index`'s rectangle (layout; empty when not shown).
    int FirstShown() const;
    Rect EntryRect(int index) const;
    // The entries from `index` on shown (scrolled as little as needed).
    void ScrollTo(int index);

    // A control inside a window, never put on the stack: nothing to create or close.
    void CreateScreen() override {}
    void CloseScreen() override {}
    void Update() override;
    void Deactivate() override;
    void OnEvent(UiEvent& event) override;
    void LeftDown(UiEvent& event);
    void MouseMove(UiEvent& event);
    void Wheel(UiEvent& event);
    void KeyDown(UiEvent& event);

    std::vector<std::string> texts;
    int value = 0;                   // the chosen entry
    bool changed = false;
    int active = 0;                  // the highlighted entry while open
    UiContainer* owner = nullptr;
    Rect row;                        // layout coordinates
    Rect frame;                      // the open list's frame (layout); entries inside its 5-pixel border
    bool open = false;
    int shown = 0;                   // entries shown at once
    UiScrollbar* scroll = nullptr;
    std::vector<UiWindow*> covered;  // the owner's children not drawn while open (under the list)
    UiMesh* rowArrow = nullptr;      // the row's arrow; the open list's shade, marks and frame
    UiMesh* listShade = nullptr;
    UiMesh* chosenMark = nullptr;
    UiMesh* activeMark = nullptr;
    UiMesh* listFrame = nullptr;

private:
    void PaintRow(UiSurface& surface) const;
    void PaintList(UiSurface& surface) const;
    // The entry at screen point `point`, or -1.
    int EntryAt(Point point) const;
    void Choose(int index);
    void SetActive(int index);
    // The marks placed for the scroll position and the active and chosen entries.
    void PlaceMarks();
};
