#pragma once

// The objectives window.

#include "game/ui/ui.h"
#include "game/ui/ui_scroll_text.h"
#include "game/ui/ui_scrollbar.h"

// An exit shown on the objectives window's zone picture.
struct ObjectiveExit {
    bool flag = false;               // a known distance (global map), or the zone variable is not 1
    int exit = 0;                    // index in the zone's exit list
    float distance = 0.0f;
    float x = 0.0f;                  // centre of the exit rectangle (world units)
    float y = 0.0f;
    float mapX = 0.0f;               // on the picture (layout)
    float mapY = 0.0f;
};

// The objectives window (TAB in a zone opens it for the current zone). Left: the quests of the zone (rows),
// the log; right: the zone picture with its exits; the arrows switch between zones when it lists several.
class ObjectivesWindow : public UiContainer {
public:
    using UiContainer::UiContainer;
    // The alpha test's reference Create replaced put back.
    void CloseScreen() override;
    void CreateScreen() override;
    // Picture, exits, quests and description.
    void ShowZone(std::string_view zone);
    // When the selected quest changed: its description and a marker and region per place of the quest.
    void ShowQuest();
    // Up to five rows from firstRow: their marks and the selection's marker, their titles into questTitles.
    void DrawQuestList();
    // Surface 0's and surface 1's paints: questTitles; the zone's title and description.
    void PaintQuestList(UiSurface& surface) const;
    void PaintZone(UiSurface& surface) const;
    void OnEvent(UiEvent& event) override;
    // OK (with two buttons the time to the nearest exit passes and that zone is requested), cancel, the previous /
    // next listed zone, a quest row or an exit.
    void LeftDown(UiEvent& event);
    // Enter as OK, Esc closes, PgUp / PgDn / Up / Down move the selected quest by 5 / 1. A key
    // left unused that is bound to the objectives action closes a window with one button.
    void KeyDown(UiEvent& event);
    // A multiplayer client takes them from the quest database's record, otherwise from map.txt's "#quest
    // <name>" entry (the second line after it, its words as x y pairs).
    void LoadQuestPlaces(std::string_view name);
    // Exit `index` becomes the nearest (its marker 1.2 times markerScale); the blink restarts.
    void SelectExit(int index);
    // Keeps the selected quest within the five shown rows.
    void RefreshQuests();
    // Every frame: the markers' blink, alphas (cos(2.5 t) * 0.3 + 0.5) * 255 and (cos(5 t) * 0.4 + 0.6) *
    // 255.
    void Update() override;
    // `exitDistances` may be null. The help topic is "quest_global_map" with two buttons, else
    // "quest_mission".
    void Setup(std::string_view zone, std::map<int, float>* exitDistances, bool twoButtons, bool listZones);
    const char* HelpTopic() override;

    std::vector<std::string> zones;    // the zones listed
    std::string zoneName;           // the zone it was opened for
    bool twoButtons = false;         // OK on the left and a second button (else OK centred)
    std::map<int, float>* exitDistances = nullptr;    // exit -> distance, or null
    bool listZones = false;          // list the zones of the zone's allod with quests
    int zoneIndex = 0;
    std::string shownZone;
    std::vector<ObjectiveExit> exits;
    int nearestExit = 0;
    int selectedQuest = 0;           // row of the quest named by the current-quest global
    int shownQuest = 0;              // the row ShowQuest last showed (-1: none)
    std::vector<std::string> quests;
    UiScrollText* questText = nullptr;   // the selected quest's text column (a child)
    UiScrollbar* bar = nullptr;
    int firstRow = 0;                // the first quest row shown (the bar's position)
    float markerScale = 0.0f;
    float blinkTime = 0.0f;          // seconds since the exit was selected (the markers' blink)
    std::vector<Vector2> questPlaces;    // the selected quest's places
    std::vector<std::string> objectiveTexts;    // obj_0..obj_2
    std::vector<std::string> questTitles;    // the shown rows' titles (DrawQuestList)
    std::string zoneTitle;          // the last zone with a text: its title and description (ShowZone)
    std::string zoneText;
    std::uint8_t savedAlphaRef = 0;

    static constexpr int kVisibleRows = 5;
    UiRegion* okButton = nullptr;
    UiRegion* cancelButton = nullptr;        // the second button (no tip with one button)
    UiRegion* previousZoneButton = nullptr;
    UiRegion* nextZoneButton = nullptr;
    UiRegion* rows[kVisibleRows] = {};       // the quest rows, from firstRow
    UiMesh* okIcon = nullptr;
    UiMesh* selectionMarker = nullptr;       // on the selected quest's row
    UiMesh* zonePicture = nullptr;           // ShowZone's picture
    UiMesh* bottomShade = nullptr;           // the strip under the buttons; selecting an exit hides it
    UiMesh* bottomFrame = nullptr;
    UiMesh* previousZoneArrow = nullptr;
    UiMesh* nextZoneArrow = nullptr;
    UiMesh* questMarks[kVisibleRows] = {};   // a shown row's done / failed mark
    // ShowZone's exits and ShowQuest's places: a marker and a region each, made after the window's own.
    std::vector<UiMesh*> exitMarkers;
    std::vector<UiRegion*> exitAreas;
    std::vector<UiMesh*> placeMarkers;
    std::vector<UiRegion*> placeAreas;
};
