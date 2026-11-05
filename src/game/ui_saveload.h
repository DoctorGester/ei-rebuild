#pragma once

// The save / load window and the quick saves.

#include "game/ui.h"
#include "game/ui_scrollbar.h"

// A saved game in the save/load list; the fields after the folder come from the save's info.sav.
struct SaveEntry {
    // "save_quick", "save_auto_enter" / "save_auto_exit" for "saveq", "savea" / "saveb", else the zone's name.
    void MakeTitle();
    // Text "zone " + zone (its first line), else "save_camp"; text "allod " + map, else "Unknown".
    std::string ZoneTitle() const;
    std::string AllodTitle() const;

    std::string title;
    std::string folder;             // under saves\ ("saveq" quick, "savea" / "saveb" automatic, "save<n>")
    std::int32_t savedAt = 0;        // time() when it was saved (info.sav version 2 and up)
    float gameTime = 0.0f;           // the "gtime" variable
    std::string map;                // the map of the zone
    std::string zone;
    bool broken = false;             // no info.sav: titled "Error Save", no picture
};

// The quick and automatic saves (`slot` the save folder's name). Nothing for a multiplayer client or when
// the disc lacks the space a save needs (0 free space: not checked). Writes info.sav (the "gtime" variable,
// the zone's map, the zone, the time, an empty title) and shot.sav (the screen's picture, save_picture.h).
void QuickSave(const char* slot, bool stepFrame);

// Not for a multiplayer client; only when saves\<slot>\ exists.
void QuickLoad(const char* slot);

// The save/load window (opened from the Esc menu).
class SaveLoadWindow : public UiContainer {
public:
    SaveLoadWindow(bool newIsSave, bool newSliding)
        : isSave(newIsSave), sliding(newSliding) {}
    void DrawSprites(SceneRenderer* pass) override;                      // slides in
    void Update() override;
    // The new save's picture released.
    void CloseScreen() override;
    void OnEvent(UiEvent& event) override;
    void LeftDown(UiEvent& event);
    void LeftDouble(UiEvent& event);
    void MessageResult(std::uint32_t result) override;

    // A save window saves into the new save (row 0) or asks "overwrite" (message box 0x20000); a load
    // window loads.
    void Confirm();
    // Not for a save window's new save; asks "delete" (message box 0x10000).
    void AskDelete();
    // Deletes the selected save's files and folder and refills the list.
    void DeleteSave();
    // Not for a broken save.
    void LoadSelected();

    // The caret kept within the name and the selection's marker on its row; PaintList (surface 0's paint) draws the
    // rows, the caret and the selected save's details (Rebirth-patched: WideScreen).
    void RefreshListVanilla();
    void PaintList(UiSurface& surface) const;

    // Saves into the selected entry's folder, then info.sav and shot.sav (newPicture); a message
    // box when the disc is full.
    void SaveGame();
    // The selected save's shot.sav (as `picture`, no larger than shown), or dialog for a save window's new save;
    // hidden for none or a broken save.
    void ShowPicture();
    // Reads saves\save*\info.sav and sorts the list; a save window's new save goes first (newPicture made from the
    // escape menu's copy of the screen, shown as dialog).
    void FillEntries();

    void CreateScreen() override;

    int EntryCount() const { return static_cast<int>(entries.size()); }
    void TakeSelectedTitle();

    // In a save window the edit keys edit the save's name (the first edit of an ordinary save takes over its
    // title, Backspace / Delete starting from an empty name).
    void KeyDown(UiEvent& event);
    // In a save window: the first character for an ordinary save starts an empty name; a name stays under 60
    // characters and 178 pixels (font 1).
    void Char(UiEvent& event);
    // Clamps the selection and scrolls it into view (16 rows).
    void ShowSelection();
    // "saveq", "savea" or "saveb"; no bound check.
    bool IsReservedSave(int index);

    static constexpr int kVisibleRows = 16;
    UiRegion* okButton = nullptr;
    UiRegion* cancelButton = nullptr;
    UiRegion* deleteButton = nullptr;
    UiRegion* rows[kVisibleRows] = {};   // the visible rows, from firstVisible
    UiMesh* pictureSprite = nullptr;     // the selected save's picture
    UiMesh* marker = nullptr;            // the selected row's marker
    std::vector<SaveEntry> entries;
    int selected = 0;
    int firstVisible = 0;            // list scroll position
    bool isSave = false;             // save (not load) window
    bool sliding = false;            // sliding in; input is ignored meanwhile
    float slide = 0.0f;              // 0..1
    UiScrollbar* scrollbar = nullptr;
    // The selected save's picture in layout coordinates (CreateScreen's sprite).
    static constexpr Rect kPictureRect{410, 100, 700, 320};
    Texture* picture = nullptr;     // the selected save's picture (ShowPicture)
    Texture* dialog = nullptr;      // the save window's picture of the game for the new save
    std::vector<std::uint8_t> newPicture;    // the new save's shot.sav bytes (save_picture.h), dialog's picture
    std::string name;               // the save name being edited
    bool nameTyped = false;
    int cursor = 0;                  // caret position in name (bytes, at a character's start: U4)
};
