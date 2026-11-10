#pragma once

// Ours: the companions window of a network zone (F3 on the battle screen while the host takes companions):
// the player's other saved heroes, one to pick and Add. See docs/notes/ui.md ("Companions window").

#include "game/ui/ui.h"

#include <string>
#include <vector>

class CompanionWindow : public UiContainer {
public:
    // Regions: 0 Add, 1 Close, then one per hero row (2 the first).
    void CreateScreen() override;
    void CloseScreen() override;
    void OnEvent(UiEvent& event) override;

    // The picked hero asked for as a companion (PlayerClient::SendAddCompanion) while there is room; the window
    // closes.
    void Add();
    void Close();

private:
    void Paint(UiSurface& out) const;

    std::vector<std::string> files;   // the heroes shown, by file (SavedParties::entries not in use)
    std::vector<std::string> names;   // their names
    int picked = -1;                  // the picked row
    UiRegion* addButton = nullptr;
    UiRegion* closeButton = nullptr;
    std::vector<UiRegion*> rows;
};
