#pragma once

// The battle screen's HUD panels: weapon bar, quick items, spell and action columns, portraits, map, unit panel,
// clock, movement panel and top panel, and PanelSlide for the side panels. The battle screen (ui_battle.h) owns a
// row of panel windows and forwards mouse events to the panel whose rectangle holds the point. Coordinates are the
// 800x600 layout (Rebirth moves some panels).

#include "ui.h"

class HudQuickItems;
class HudSpellColumn;
class HudActionColumn;
class HudWeaponBar;
class HudPortraits;
class HudUnitPanel;

inline constexpr float kHudFullTurn = /*007C8140*/ 6.2831855f;
inline constexpr float kHudQuarterTurn = /*007C8144*/ 1.5707964f;
inline constexpr Quaternion kHudRestRotation = /*007C8150*/ {1.0f, 0.0f, 0.0f, 0.0f};

// Weapon bar (four 40x80 slots at (80..240, 520..600)).
class HudWeaponBar : public UiWindow {
public:
    void OnEvent(UiEvent& event) override;
    void LeftUp(UiEvent& event);     // selects a weapon
    // Every frame: the highlight follows the unit's weapon.
    void Update() override;
    void DrawSprites(SceneRenderer* pass) override;
    // Rebirth-patched (WideScreen: the bar at the screen's left edge, from 80 - shift with
    // more regions up to x 240).
    HudWeaponBar(BattleScreen* owner);
    // Weapon `slot` (its button or key), unless it is the one shown or not in use: highlighted, the weapon
    // sound, and the unit's weapon set.
    void Choose(int slot);
    void Highlight(int index, bool on);
    // The next slot's sprite: `image` copied into the bar's texture, the model `model` turned to face the
    // screen (a plain rectangle without one); shown not highlighted.
    void AddWeapon(std::string_view model, std::string_view image);
    // A sprite (Rebirth-patched) and a tooltip per weapon, then the unit's current weapon highlighted.
    void SetUnit(Unit* unit);

    BattleScreen* owner = nullptr;
    int count = 0;                   // slots in use
    int selected = 0;
    Unit* unit = nullptr;
    std::vector<std::string> labels;    // the tooltip's labels
};

// Quick items (2 rows of four 50x50 slots at (520..720, 500..600)).
class HudQuickItems : public UiWindow {
public:
    void OnEvent(UiEvent& event) override;
    void LeftDown(UiEvent& event);
    void MouseMove(UiEvent& event);
    // A unit's quick item (bottom row) is picked or, with a double click, used at once; a player's item
    // (top row) starts mode 6, or cancels when it is the highlighted one.
    void LeftUp(UiEvent& event);
    void DrawSprites(SceneRenderer* pass) override;
    // Every frame: the tooltips of the slots that show an item.
    void Update() override;
    // The item's name and, for a wand (category 5) or a scroll (8) with a spell, its numbers after labels.
    std::string ItemTip(unsigned index);
    // The unit's quick item `slot` (a click on it or its key): a wand or scroll becomes the pending action
    // (3 on a unit for a spell with flag 0x10000000, else 4 at a point); the highlighted slot again, or an
    // empty one, drops it.
    void Pick(int slot);
    // The unit's wand or scroll in `slot` used at once (a double click, or the key handler's Ctrl / Alt), as
    // HudSpellColumn::ClickAlternate.
    void UseNow(int slot);
    // With `on` also selected = slot.
    void Highlight(int slot, bool on);
    // Every slot off; selected = -1.
    void ClearHighlights();
    // The entry shown in slot `slot` (a region's index; -1: empty, or no region).
    int RegionItem(int slot) const { return slot < 0 ? -1 : regionItem[slot]; }

    // Rebirth-patched (WideScreen: the panel at the screen's right edge).
    HudQuickItems(BattleScreen* owner);
    // The next sprite (at most eight, else null): `image` copied into the panel's texture, the model `model`
    // turned over (a plain rectangle without one).
    UiMesh* AddItem(std::string_view model, std::string_view image);
    // The unit's four quick items in the bottom row (sprites Rebirth-patched); for the local player's unit
    // up to four of the player's ItemKind::Quest items in the top row.
    void SetUnit(Unit* unit);

    struct Entry {
        bool own = false;            // one of the unit's quick items (else the player's)
        int id = 0;                  // the item's id
        Item* item = nullptr;
        UiMesh* sprite = nullptr;
    };

    BattleScreen* owner = nullptr;
    int shownCount = 0;              // sprites added (the shown entries)
    int selected = 0;
    Unit* unit = nullptr;
    std::vector<Entry> entries;
    int regionItem[8] = {};          // per slot (region) its entry, or -1
    std::vector<std::string> labels;    // the tooltip's labels
};

// Spell column on the right (760..800 x 190..510).
class HudSpellColumn : public UiWindow {
public:
    void OnEvent(UiEvent& event) override;
    void LeftUp(UiEvent& event);
    void DrawSprites(SceneRenderer* pass) override;
    // Spell `index` (its button or key) becomes the pending action: 1 on a unit for a spell with flag
    // 0x10000000, else 2 at a point; the highlighted one again drops it.
    void Click(int index);
    // A double click (or the key handler's Ctrl / Alt) casts at once: on the unit itself, or an attack spell
    // on the nearest unit it sees (none: the "nomagic" sound). The pending action is dropped after.
    void ClickAlternate(int index);
    // selected = index either way.
    void Highlight(int index, bool on);
    // selected = -1.
    void ClearHighlights();
    // Rebirth-patched (WideScreen: the column at the screen's right edge).
    HudSpellColumn(BattleScreen* owner);
    // The next button at the bottom, the ones so far moved up a step; selected = its index.
    void AddSpell(std::string_view model, std::string_view image);
    // A sprite (Rebirth-patched) and a tooltip per spell slot that holds one.
    void SetUnit(Unit* unit);

    BattleScreen* owner = nullptr;
    int count = 0;                   // sprites added
    int selected = 0;
    Unit* unit = nullptr;
    std::vector<std::string> labels;    // the tooltip's labels
};

// Action column on the left (six 40x40 buttons at 0..40 x 270..510).
class HudActionColumn : public UiWindow {
public:
    void OnEvent(UiEvent& event) override;
    void LeftUp(UiEvent& event);
    void DrawSprites(SceneRenderer* pass) override;
    // Action button `index` (a click or its key) becomes the pending action: 8 (an order on an object) for
    // action 0 (science), else 7 (follow); the highlighted one again drops it.
    void Select(int index);
    void Highlight(int index, bool on);
    void ClearHighlights();
    // Rebirth-patched (WideScreen: the column at the screen's left edge).
    HudActionColumn(BattleScreen* owner);
    // The next button, above the ones so far; selected = its index.
    void AddAction(std::string_view model, std::string_view image, int action);
    // With `extra` first a "skill0011" button; with a unit the science button.
    void SetUnit(Unit* unit, bool extra);

    BattleScreen* owner = nullptr;
    int count = 0;                   // buttons added
    int selected = 0;
    Unit* unit = nullptr;
    std::vector<std::uint32_t> actions;            // the buttons' actions (0 science, -1 follow)
    std::vector<std::string> labels;    // the tooltip's labels
};

// A portrait's state, one per portrait: the last hit points seen, the face (rest orientation, current
// orientation and a random wobble), the face picture and model, and the slot's network player.
struct HudPortraitSlot {
    int lastHitPoints = 0;           // lowest hit points seen (a drop flashes the portrait)
    float flash = 0.0f;              // 1 after a hit, fading out
    Quaternion restA{1.0f, 0.0f, 0.0f, 0.0f};      // the face's rest orientation is restA * restB
    Quaternion current{1.0f, 0.0f, 0.0f, 0.0f};    // the face's orientation, eased towards the rest one
    Quaternion restB{1.0f, 0.0f, 0.0f, 0.0f};
    int wobbleKind = 0;              // 0 none, 1 nod (about x), 2 shake (y), 3 turn to target
    float wobbleTime = 0.0f;         // starts negative (a delay)
    std::string faceName;
    int faceState = 0;               // picture shown (0 normal, 2 hit, 3 low mana, ...)
    int faceMode = 0;                // 1 / 2: a timed picture, 0 none
    float faceTimer = 0.0f;
    std::string modelPrefix;        // the face model is prefix + build + suffix,
    std::string modelSuffix;        // build "th", "me" or "fa"
    float build = 0.0f;              // the unit's build the model was chosen for
    Quaternion wobbleTarget{1.0f, 0.0f, 0.0f, 0.0f};
    float wobbleSpeed = 0.0f;
    bool hasPlayer = false;          // a multiplayer player without a unit here (no bars)
    int playerId = 0;                // that player's serial, else -1
    // The portrait's sprites, in their drawing order: the face model (or picture), the health and mana bars,
    // the frame (shown while the unit is selected) and the hit flash.
    UiMesh* face = nullptr;
    UiMesh* healthBar = nullptr;
    UiMesh* manaBar = nullptr;
    UiMesh* frame = nullptr;
    UiMesh* hitFlash = nullptr;
};

// Party portraits (250..550 x 440..600).
class HudPortraits : public UiWindow {
public:
    void OnEvent(UiEvent& event) override;
    void LeftDown(UiEvent& event);
    void LeftUp(UiEvent& event);     // selects the unit
    void MouseMove(UiEvent& event);   // shows the unit / sets the cursor
    // Every frame: the bars, the hit flash, the face picture and model, the wobble and the selection marker.
    void Update() override;
    // Face picture `state` (the face name plus "a" 1, "c" 2, "b" 3; the plain name without such an image)
    // into quarter index % 4 of texture objects[index / 4 + 1].
    void ShowFace(int index, int state);
    // One portrait per party unit and, in a multiplayer client, one per player without a live unit.
    void Rebuild(bool wide);
    // 56 wide (multiplayer: 40, or 56 with `wide` for the first). `hasPlayer` marks a player's portrait (half
    // tint, no bars).
    void AddPortrait(const std::string& model, std::string_view picture, bool wide, bool hasPlayer);
    // Portrait `index` starts wobble `kind` (1 nod, 2 shake) after a short random delay.
    void Wobble(int index, int kind);

    HudPortraits(BattleScreen* owner);

    BattleScreen* owner = nullptr;
    int count = 0;
    std::vector<HudPortraitSlot> slots;    // one per portrait; region i is portrait i
    UiMesh* marker = nullptr;              // the camera target's marker, above its portrait
};

// A side panel's slide (the map's, the unit panel's, the top panel's): the sling button or its key flips `direction` and
// starts it, every frame's Step moves `t` by the frame's time and stops it at either end.
struct PanelSlide {
    // `t` moves by the frame's time * direction / 0.3 and stays within 0..1; `moving` ends at either end.
    void Step();
    // The panel's offset in pixels: `travel` eased (2 t^2 up to half way, 1 - 2 (1 - t)^2 after), truncated.
    int Offset() const;
    // The sling button (or the map's key): the sling sound, then the slide turned round and started.
    void Toggle();

    float t = 0.0f;                  // 0..1: how far the panel has slid
    float direction = 0.0f;          // +1 / -1
    int travel = 0;                  // full slide distance in pixels
    bool moving = false;             // set when the sling button (or its key) starts the slide
};

// Map (615..800 x 0..165).
class HudMap : public UiWindow {
public:
    HudMap(BattleScreen* newOwner) : owner(newOwner) {}
    void OnEvent(UiEvent& event) override;
    void LeftDown(UiEvent& event);   // the view buttons
    // Rebirth-patched (WideScreen: the map's centre at 700 + shift). A held view button is
    // let go; otherwise the sling button starts the slide, the round map and the map's square focus the
    // camera on the map point (inside the world), the north button is FaceNorth.
    void LeftUp(UiEvent& event);
    // Over the sling button the wheel zooms the map by 2.5^(notches / 10).
    void Wheel(UiEvent& event);
    // "Camera north" (its button and key): the click sound, the camera turned north, northTime 0.5.
    void FaceNorth();
    // The view buttons' icons lit by `view`.
    void ShowView();
    // Every frame: the markers mesh of the units, marked objects, zone exits and players.
    void BuildMarkers();
    // Every frame, before BuildMarkers: follows the camera and builds the visible-area mesh clipped to the
    // world, and the world-bounds quad.
    void UpdateView();
    // Rebirth-patched (WideScreen). The parts at the current slide offset.
    void Layout();
    // Rebirth-patched (WideScreen). Every frame: the zoom (2.5 times a second while a view
    // button is held), the view and markers, the slide (0.3 s for the whole way, eased) and the "camera
    // north" time.
    void Update() override;
    // Rebirth-patched (WideScreen: the panel at the screen's right edge). The world's extent
    // and picture come from the local player's zone record (32 by 32 and "battle00" without one); `slid`
    // starts the slide at its far end.
    void Setup(float zoom, bool slid);

    BattleScreen* owner = nullptr;
    Vector2 centre;                  // world point at the map's centre (the camera's)
    Vector2 facing;                  // view direction (normalised by the camera follow)
    float worldWidth = 0.0f;         // the zone's extent (clipping rectangle 0..width)
    float worldHeight = 0.0f;
    float textureSize = 0.0f;        // world units per map texture (texture coordinates)
    float scale = 0.0f;              // world units to map units
    float zoom = 0.0f;               // 1 .. zoomMax (the view buttons change it)
    float zoomMax = 0.0f;
    float halfSide = 0.0f;           // 80: half the map's side in layout units (a click's offset over it)
    std::vector<Vector2> viewPolygon;        // the round map's outline in map units (the disc's corners)
    std::vector<Vector2> visibleArea;        // the outline in world units, clipped to the world
    PanelSlide slide;                // the offset is to the right
    int view = 0;                   // 1 or 2 while a view button is held (zoom in, zoom out), else 0
    float northTime = 0.0f;          // 0.5 when "camera north" turns the camera, then counts down

    // The regions, in their hit-test order (the whole panel's last, unnamed).
    UiRegion* slingButton = nullptr;
    UiRegion* roundMap = nullptr;          // the round map (zoomed)
    UiRegion* mapSquare = nullptr;         // the square around it
    UiRegion* viewButtons[2] = {};         // zoom in, zoom out
    UiRegion* northButton = nullptr;
    // The sprites, in their drawing order.
    UiMesh* bounds = nullptr;              // the world's bounds, in the map's picture
    UiMesh* visibleDisc = nullptr;         // the visible area's disc, in the picture too
    UiMesh* markers = nullptr;             // four texels of one colour each
    UiMesh* mapBack = nullptr;             // the map's and the buttons' plain backgrounds
    UiMesh* buttonsBack = nullptr;
    UiMesh* slingArrow = nullptr;
    UiMesh* viewIcons[2] = {};
    UiMesh* ring = nullptr;                // round the map
    UiMesh* frame = nullptr;
    UiMesh* needle = nullptr;              // the compass needle
    UiMesh* northIcon = nullptr;
};

// Unit panel (0..185 x 0..225).
class HudUnitPanel : public UiWindow {
public:
    HudUnitPanel(BattleScreen* newOwner) : owner(newOwner) {}
    // The unit the panel shows: the hover unit, else the selected one, while the panel is not slid away and the
    // unit's model is attached; else null.
    Unit* PanelUnit() const;
    // The panel surface's paint: PanelUnit's name and the texts of its page (bodyPart): the hit points and mana,
    // the parts' names, the info lines or the effects.
    void PaintTexts(UiSurface& surface);
    // The info page's lines for `unit`: hit points, mana, speed, vision, attack, defence, damage, rate,
    // protection and, for a unit outside the party, its level against the leader's.
    void PaintUnitInfo(UiSurface& surface, Unit* unit);
    // The hit points' and, for a party unit, the mana's bar; PaintHealth their numbers.
    void ShowHealth(Unit* unit);
    void PaintHealth(UiSurface& surface, Unit* unit);
    // The durability bar of each present part in state 3; PaintParts the parts' names.
    void ShowParts(Unit* unit);
    void PaintParts(UiSurface& surface, Unit* unit);
    void OnEvent(UiEvent& event) override;
    void LeftUp(UiEvent& event);
    // The body part buttons lit by bodyPart.
    void ShowBodyPart();
    // The pictures of the first six effects on `unit`; the "more" mark shows when there are more. PaintEffects
    // their names and times left.
    void ShowEffects(Unit* unit);
    void PaintEffects(UiSurface& surface, Unit* unit);
    // Rebirth-patched (WideScreen). The parts at the current slide offset.
    void Layout();
    // Rebirth-patched (WideScreen). Every frame: the slide (as the map's); a dead hover unit
    // is forgotten.
    void Update() override;
    // Rebirth-patched (WideScreen: the panel at the screen's left edge, its rectangle moved with it; addon's own
    // rectangle and "no slide" are not kept, ui_hud_side.cpp). `slid` starts the panel at the far end of its slide.
    void Setup(int bodyPart, bool slid);
    // Rebirth-patched (WideScreen). The hover unit, else the selected one, while the panel
    // is not slid away: its model (not on info page 2) over a depth buffer cleared to 0 around the panel and
    // its bars (the texts are PaintTexts').
    void DrawSprites(SceneRenderer* pass) override;

    BattleScreen* owner = nullptr;
    Unit* shownUnit = nullptr;      // the first selected unit (RefreshSelection), or null
    Unit* hoverUnit = nullptr;      // the unit under the mouse (HoverUnit), or null
    // The model's view for the selected unit [0] and the hover unit [1], from the units' prototypes (the
    // battle screen copies them as dwords).
    float viewHeight[2] = {};        // how far up the panel the model stands, in units of its size
    float viewSize[2] = {};          // the model's size: it is drawn 1 / size (times its animation's ratio)
    std::string strings[2];         // the selected and the hover unit's names
    PanelSlide slide;                // the offset is to the left
    int bodyPart = 0;                // the selected body part button 0-3: the page shown
    int effectIcons[6] = {};         // the spell pictures in the icon texture's six places
    std::vector<std::string> texts; // "infounit_0".."infounit_17": the info lines' labels
    std::vector<std::string> partLabels;    // four: the shown names of "skull", "torso", "leg", "arm"

    // The regions, in their hit-test order (the whole panel's last, unnamed).
    UiRegion* slingButton = nullptr;
    UiRegion* partButtons[4] = {};
    // The sprites, in their drawing order.
    UiMesh* background = nullptr;          // drawn alone, before the model
    UiMesh* edge = nullptr;
    UiMesh* slingArrow = nullptr;
    UiMesh* partIcons[4] = {};
    UiMesh* frame = nullptr;
    UiMesh* heightBar = nullptr;           // a strip of twenty cells down the edge
    UiMesh* moreMark = nullptr;            // more effects than places
    UiMesh* healthBar = nullptr;           // each bar over its frame
    UiMesh* healthFrame = nullptr;
    UiMesh* manaBar = nullptr;
    UiMesh* manaFrame = nullptr;
    UiMesh* bodyBars[6] = {};              // the six body parts' bars, each over its frame
    UiMesh* bodyFrames[6] = {};
    UiMesh* effectSprites[6] = {};         // the effects' pictures, from the panel's icon texture
};

// Game clock (720..800 x 520..600).
class HudClock : public UiWindow {
public:
    void OnEvent(UiEvent& event) override;
    // Rebirth's MPSpeedControl behind its flag. The pause button toggles the pause; the normal and fast
    // buttons set the speed (not on a network client: update period 55 / 27 ms); the objectives button opens
    // the objectives.
    void LeftUp(UiEvent& event);
    // Every frame: the blink and the dial step on (the dial only while the game runs), then Show.
    void Update() override;
    // The face turned to the time of day, the hand and the dial's marks placed; the face and the hand blink while
    // paused.
    void Show();
    // The objectives window of the current zone (one button, no exits, no zone list).
    void OpenObjectives();
    // The pause button and its key: the clock sound, then the server flag 10 flipped (pause / resume).
    void TogglePause();
    // The normal / fast speed (button and keys): the update period 55 / 27 ms and the clock sound.
    void SetSpeed(bool fast);
    // Rebirth-patched (WideScreen: the clock at the screen's right edge).
    void Setup(bool fast);

    float blink = 0.0f;              // 0..1, the pause blink phase
    float dial = 0.0f;               // speed dial angle (wraps at pi / 6)
    bool fast = false;               // fast game speed (copied to Interface3D::fastGame)
    bool highlight = false;          // the objectives button pulses

    // The regions: the pause, normal and fast sectors and the objectives' quarter disc.
    UiRegion* pauseButton = nullptr;
    UiRegion* normalButton = nullptr;
    UiRegion* fastButton = nullptr;
    UiRegion* objectivesButton = nullptr;
    // The sprites, in their drawing order.
    UiMesh* face = nullptr;               // turned to the time of day
    UiMesh* hand = nullptr;
    UiMesh* dialMarks[4] = {};
    UiMesh* objectivesIcon = nullptr;
    UiMesh* rim = nullptr;
};

// Movement panel (0..80 x 520..600): the flag button toggles the units' flag, the four mode buttons set their
// movement mode 0-3.
class HudMovePanel : public UiWindow {
public:
    HudMovePanel(BattleScreen* newOwner) : owner(newOwner) {}
    void OnEvent(UiEvent& event) override;
    void LeftUp(UiEvent& event);
    // Every frame: the selected units' common mode and flag.
    void Update() override;
    // The flag button (and its key): the selected party units' flag becomes 0 when all have 1, else 1.
    void ToggleFlag();
    // A negative mode hides the marker.
    void ShowMode(int mode);
    // A mode button and its keys: the click sound and the mode 0-3 for the selected party units (nothing
    // without the local player's view), then the marker moved there.
    void SetMode(int mode);
    // Rebirth-patched (WideScreen: the panel at the screen's left edge).
    void Build() override;

    BattleScreen* owner = nullptr;
    int mode = 0;                    // the shown movement mode (-1, -2: none, mixed)
    int flag = 0;                    // the selected units' common flag (-1, -2: none, mixed)

    // The regions: the flag's quarter disc in the corner, then the four modes' sectors.
    UiRegion* flagButton = nullptr;
    UiRegion* modeButtons[4] = {};
    // The sprites, in their drawing order.
    UiMesh* marker = nullptr;              // the hand pointing at the mode's button
    UiMesh* modeIcons[4] = {};
    UiMesh* flagIcon = nullptr;            // shows the flag through its texture offset
};

// Top panel (180..620 x 0..100).
class HudTopPanel : public UiWindow {
public:
    HudTopPanel(BattleScreen* newOwner) : owner(newOwner) {}
    void OnEvent(UiEvent& event) override;
    void LeftUp(UiEvent& event);
    // Page `page` into the message log (page 0 scrolled to the end).
    void ShowPage();
    // Every frame: the slide.
    void Update() override;
    // The parts, the message log and the regions at the current slide offset.
    void Layout();
    // Page 1's text: the party's money and the local player's items merged by kind, one per line.
    void ShowItems();
    // Slid in when `shown`.
    void Setup(int page, bool shown);
    // Appends "<combat_picked_up or, when `crippled`, combat_crippled> <item name>" and a newline to page 0's
    // text, and to the message log when page 0 is up.
    void AddPickedUp(Item* item, bool crippled);
    // The same with the money ("format_money"), for money > 0.
    void AddMoney(int money);

    BattleScreen* owner = nullptr;
    PanelSlide slide;                // t 0 (out) .. 1 (in)
    int page = 0;
    std::string pageTexts[2];       // the message log's text of pages 0 and 1

    // The regions, in their hit-test order (the whole panel's last, unnamed).
    UiRegion* slingButton = nullptr;
    UiRegion* pageButtons[2] = {};
    // The sprites, in their drawing order.
    UiMesh* back = nullptr;
    UiMesh* bar = nullptr;                 // the strip along the top
    UiMesh* slingArrow = nullptr;
    UiMesh* pageIcons[2] = {};
    UiMesh* frame = nullptr;
};
