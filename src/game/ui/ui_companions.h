#pragma once

// Ours: the companions window of a network zone (the HUD's companions button or F3 while the host takes
// companions and the party stands in the multiplayer base zone): the hero's companions, then the player's other
// saved heroes; the picked one is removed or added. And the HUD's button. See docs/notes/ui.md ("Companions window").

#include "game/ui/ui.h"

#include <string>
#include <vector>

// The local client may change its companions now: a multiplayer client whose host takes companions and whose party
// stands in the multiplayer base zone (the server refuses the messages elsewhere). The button and F3 ask this.
bool CompanionsOffered();

class CompanionWindow : public UiContainer {
public:
    // Regions: 0 the action (Add or Remove, the picked row's), 1 Close, then one per hero row (2 the first).
    void CreateScreen() override;
    void CloseScreen() override;
    void OnEvent(UiEvent& event) override;

    // The picked hero asked for as a companion (PlayerClient::SendAddCompanion) while there is room, or the picked
    // companion sent away (SendRemoveCompanion; its file leaving until its last record is written); the window
    // closes.
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

class BattleScreen;

// The HUD's companions button: a framed "+" on a shade right of the portraits while the zone's host takes companions
// (PlayerClient::companionLimit); a click opens the companions window. Placed again whenever the portraits change.
class HudCompanionButton : public UiWindow {
public:
    explicit HudCompanionButton(BattleScreen* newOwner) : owner(newOwner) {}
    void OnEvent(UiEvent& event) override;
    // Every frame: shown or hidden, beside the last portrait.
    void Update() override;

    BattleScreen* owner = nullptr;
    UiRegion* button = nullptr;
    int placedAt = -1;                // the button's left edge, -1 hidden
};
