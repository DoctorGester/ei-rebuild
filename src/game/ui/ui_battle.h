#pragma once

// The battle screen (CreateScreen id 1) and what its next click does. Its HUD panels are in ui_hud.h.

#include "game/ui/ui.h"

class HudQuickItems;
class HudSpellColumn;
class HudActionColumn;
class HudWeaponBar;
class HudPortraits;
class HudUnitPanel;
class HudMap;
class HudClock;
class HudMovePanel;
class HudTopPanel;
class UiScrollText;

// What the next click on the view does (BattleScreen::pendingAction, SelectUnit).
enum class PendingAction : int {
    None = 0,           // select, attack or move
    SpellOnUnit = 1,    // the pending spell
    SpellAtPoint = 2,
    QuickOnUnit = 3,    // the pending quick item
    QuickAtPoint = 4,
    ItemOnObject = 5,   // an item (pendingItem) used on an object
    KeyOnObject = 6,    // one of the player's items (its key in itemUseKey) used on an object
    Follow = 7,         // the action buttons
    Order = 8,          // an order on an object (the science button)
};

// The in-game screen (CreateScreen id 1); its Create builds the HUD panels as children in order: weapon
// bar, quick items, spell column, action column, portraits, unit panel, map, clock, movement panel, message
// log, top panel. See docs/notes/ui.md, section 6.
class BattleScreen : public UiContainer {
public:
    // A click on `unit` (or, without one, at screen point x, y): with party members selected, by the pending
    // action (0 select / attack / move, 1 spell on a unit, 2 and 4 spell on a point, 3 quick item, 5 item
    // and 6 spell on an object, 7 follow, 8 order on an object) or the modifier keys; with nobody selected
    // it selects the clicked member. Rebirth's FixSpellSelfCasting #1 drops the Ctrl flag of a spell on a
    // unit.
    void SelectUnit(Unit* unit, Point at);
    // The unit panel's hover unit, and with party members selected the cursor for what a click would do.
    void HoverUnit(Unit* unit, Point at);
    // Orders the selected party units to the point (`goTo`: with a modifier, else a walk); a point on a zone
    // exit whose "z.<zone>" variable is not 1 is remembered (exitPending).
    void OrderAt(Vector3 point, bool goTo, bool run);
    // The pending action becomes `mode` on `slot` with `item`, the panels' highlights move to `slot` in the
    // panel of that mode. PendingAction::None with slot and item -1 cancels.
    void SetPendingAction(PendingAction mode, int slot, int item);

    // Reads the screen's saved state when a game is being loaded, else remembers the local player's first
    // hero.
    void CreateScreen() override;
    // The panels built again for the window's new shape (they sit at its edges), showing what they showed: the
    // party's portraits, the selection, the pending action, the map's zoom and the message log.
    void LayoutChanged() override;
    // The HUD panels (the children, in their order) for the window's shape, the map at `mapZoom`.
    void BuildPanels(float mapZoom);
    // Rebirth's AddNewConsoleCommands applies "nogui" first.
    void DrawSprites(SceneRenderer* pass) override;
    // Every frame: a dead selected unit empties the panels, a multiplayer client rebuilds the party, the
    // "leave_zone" box for a pending exit, the selection box's marks, the quest sound.
    void Update() override;
    void OnEvent(UiEvent& event) override;
    // A click on the unit under the point, or a drag selecting the party units inside the box. Details:
    // docs/notes/ui.md, "Behaviour moved out of header comments".
    void LeftUp(UiEvent& event);
    // Also every frame from Draw. Details: docs/notes/ui.md, "Behaviour moved out of header comments".
    void MouseMove(UiEvent& event);
    // While a portrait click picks a player the portraits get it; else, when no panel took it, a press on
    // the view starts.
    void LeftDown(UiEvent& event);
    void LeftDouble(UiEvent& event);
    void Deactivate() override;
    // Rebirth's MPSpeedControl behind its flag. While a portrait click picks a player, Esc ends that; on a
    // network client Enter / Backspace go to the chat line. Otherwise Esc drops the pending action, else
    // opens the escape menu; other keys run their action (docs/notes/ui.md, "Behaviour moved out of header
    // comments").
    void KeyDown(UiEvent& event);
    // Releases the order key of the key's action.
    void KeyUp(UiEvent& event);
    // Offered to the map, the unit panel, the message log and the top panel in that order; when none takes
    // it, the container passes it on with those four disabled.
    void Wheel(UiEvent& event);
    // Its GetName when not empty; for a unit whose id is a text key, its "pers" text; otherwise the first
    // line of the text "unit " + the prototype's name with spaces as '_'.
    std::string UnitName(Unit* unit);
    // A "q." player variable changed: a quest line to the top panel's text and the message log; the clock
    // pulses. Details: docs/notes/ui.md, "Behaviour moved out of header comments".
    void QuestVariable(std::string_view name, float value);
    // With exactly one living party unit selected the weapon bar, quick items, spell and action columns show
    // it, otherwise they are emptied; the pending action and highlights are cleared. Details:
    // docs/notes/ui.md, "Behaviour moved out of header comments".
    void RefreshSelection();
    // Nothing on a network client or past the party. With the modifier key the member flips, else it alone
    // is chosen. Details: docs/notes/ui.md, "Behaviour moved out of header comments".
    void ToggleMember(int index);
    // The party indexes of the selected members (flag 0x10), in order.
    std::vector<int> SelectedIndexes() const;
    // Ours: how many members from the party's first a selection box may select: all in a single-player game; on a
    // multiplayer client the local hero and its companions (RebuildClientParty puts them first), never another
    // player's unit.
    int BoxMembers() const;
    // The order kind is the first order key held (6 for none); a member more than five levels below the
    // target speaks (speech event 10).
    void AttackUnit(Unit* target, bool ctrl);
    // The same for a dead `target` (no kind, no levels).
    void OrderDeadUnit(Unit* target, bool ctrl);
    // The pending spell (or with `quick` the quick item) used on `target` by each selected member; the
    // pending action is dropped after.
    void UseOnUnit(Unit* target, bool quick, bool ctrl);
    // The same at the point. For the member that is `unit` itself the point first moves 0.1 behind it, for
    // it and the members after it.
    void UseAtPoint(Vector3 point, Unit* unit, bool quick);
    // The unit's speech line goes to the top panel's text and the message log; the kind Kill shows its timed
    // face, the kinds NoWayToAttack..Injured shake its portrait.
    void Speech(int id, AckKind kind, std::string_view extra);

    void CloseScreen() override;
    // Polled every frame by the interface until it answers true. After a new game (hasHero): the camera
    // follows the first party unit and the autosave "savea" when the option is on.
    bool ReadyToStopPolling() override;
    // Items picked up (items, money, crippled): the items and the money to the top panel, which shows them.
    void ItemsPicked(const std::vector<Item*>& items, int money, bool crippled);
    // A network client instead empties the top panel's page-0 text.
    void UnitLeft(Unit* unit) override;
    // The count and ids of the selected party units, the map's zoom, the server flag (a byte), the local
    // player's first value, the top panel's page-0 text with its zero, exitPending (a byte), pendingExit.
    void SaveState() override;
    // With a record of the local player for it, the unit goes after the party units whose records come
    // first and the record's selection byte decides; otherwise it is appended, not selected. A unit among
    // heroes (the save's selection) is taken out and selected. A network client instead empties the top
    // panel's page-0 text.
    void UnitJoined(Unit* unit) override;
    // Also turns the face's rest orientation about -x by kHudQuarterTurn / 7.
    void MarkPortrait(int index);
    // 0x10001 (the "leave_zone" question) leaves through pendingExit; 0x20001 (game over: load) opens the
    // load window; 0x20002, 0x90001 and 0x100001 leave the game.
    void MessageResult(std::uint32_t result) override;
    // Lost (`won` 0): the box "game_over" (result 0x20000); won: "end_of_game" (0xFFFF0000).
    void GameOver(bool won) override;
    // A portrait click picks a player while it is not 0.
    void PlayerList(int mode) override { portraitMode = mode; }
    struct PartyIds {
        // Both id vectors hold the same ids in the same order (the camera's id is not compared).
        bool Equals(const PartyIds& other) const;

        std::vector<int> withUnit;
        int camera = 0;
        std::vector<int> without;
    };

    std::vector<Unit*> party;       // shown as portraits
    // The pending action: what the next click on the view does (SelectUnit), its spell, quick item
    // or action button, and the player's item id (mode 6) or the action (7, 8).
    PendingAction pendingAction = {};
    int pendingSlot = 0;
    int pendingItem = 0;
    // A zone exit OrderAt found under the clicked point; the update asks about leaving while exitPending is
    // set. Saved with the game (version 0x62).
    int pendingExit = 0;
    bool exitPending = false;
    // Key actions 0x11..0x16 held; AttackUnit's order kind is the first one set.
    std::uint8_t orderKeys[6] = {};
    HudWeaponBar* weaponBar = nullptr;
    HudQuickItems* quickItems = nullptr;
    HudSpellColumn* spellColumn = nullptr;
    HudActionColumn* actionColumn = nullptr;
    HudPortraits* portraits = nullptr;
    HudUnitPanel* unitPanel = nullptr;
    HudMap* map = nullptr;
    HudClock* clock = nullptr;
    HudMovePanel* movePanel = nullptr;
    UiScrollText* messageLog = nullptr;
    HudTopPanel* topPanel = nullptr;
    bool viewPressed = false;        // the left button went down on the view
    bool boxSelecting = false;       // a selection box is being dragged
    Point press;                     // where the button went down on the view (screen)
    Rect selectionBox;               // the box
    std::vector<std::uint32_t> heroes;             // ids of the heroes shown (saved with the game)
    std::map<int, std::string> personTexts;       // the "pers" texts by person
    int questSound = 0;              // the quest sound to play (1 quest, 2 part done, 3 done)
    bool hasHero = false;            // a hero id was added (new game)
    PartyIds clientParty;            // the client's party ids last built
    int companionPortraits = 0;      // ours: the local hero's companions, the portraits after the first
    // The key a player's item gives when it starts mode 6; SelectUnit and the hover hand it to the target's
    // AcceptsKey.
    LockKey itemUseKey = {1, 0, 0};
    int portraitMode = 0;            // 1, 2: the next portrait click picks a player for a server command (4, 5)
};
