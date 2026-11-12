#pragma once

// Ours: the companions window of the multiplayer base (the base screen's companions button or F3): the heroes picked
// as companions (SavedParties::companions), then the player's other saved heroes; the picked one is removed from the
// list or added to it, and its unit leaves or joins the base's party. The list changes only here: the base is a local
// zone, and each network zone only asks for the list again (PlayerClient::RestoreCompanions). See docs/notes/ui.md
// ("Companions window").

#include "game/ui/ui.h"

#include <string>
#include <vector>

// The base screen offers the companions window: a multiplayer game whose host (when this game is the host) takes
// companions. A joined game's host option is unknown here; the zone's start drops the files past its limit.
bool CompanionsOffered();

class CompanionWindow : public UiContainer {
public:
    // Regions: 0 the action (Add or Remove, the picked row's), 1 Close, then one per hero row (2 the first).
    void CreateScreen() override;
    void CloseScreen() override;
    void OnEvent(UiEvent& event) override;

    // The picked hero added to the list while there is room and joined, or the picked companion's record handed back
    // to its file and the companion taken off the list and out of the party; the window closes.
    void Act();
    void Close();

private:
    void Paint(UiSurface& out) const;

    std::vector<std::string> files;   // the heroes shown, by file: the companions, then the entries not in use
    std::vector<std::string> names;   // their names
    int companionRows = 0;            // the first rows that are companions
    int picked = -1;                  // the picked row
    UiRegion* addButton = nullptr;
    UiRegion* closeButton = nullptr;
    std::vector<UiRegion*> rows;
};

// The base screen's companions button: a framed "+" on a shade at the screen's bottom right; a click opens the
// companions window.
class CompanionButton : public UiWindow {
public:
    void OnEvent(UiEvent& event) override;
    // The first frame: the button built.
    void Update() override;

    UiRegion* button = nullptr;
    bool built = false;
};
