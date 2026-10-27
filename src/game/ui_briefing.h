#pragma once

// The base / briefing screen (interface screen 3; vtables input / main, 0x2A4
// bytes, Create, Update, Draw in ui_container_draws.cpp). It is the
// screen of a "brief" zone (map.txt type brief, campaign mode 3: the bases bz1g ..): the party
// walks around the base, a click on a person opens the person's topics, a topic runs its
// dialogue (phrases, then rewards). See docs/notes/ui.md, "Briefing / base screen".
// Dialogue states: 0 walking, 1 choosing a topic, 2 the dialogue starting (people still
// walking to their places), 3 phrases, 4 rewards.

#include "console.h"
#include "maths.h"
#include "object_tables.h"
#include "ui.h"
#include "ui_camp.h"
#include "world.h"

#include "lib/mfc.h"
#include "lib/stlp.h"

#include <cstddef>
#include <cstdint>

class CEngineObject;
class CUnit;

// A person walking to the place a dialogue gives them (a node of the STLport list at
// CBriefingScreen +0x240; Update moves them and removes the ones done).
struct BriefingMover {
    BriefingMover* next;
    BriefingMover* prev;
    Vector3 target;
    Vector3 direction;               // the facing at the target
    int state;                       // kDone, kWalking or kTurning
    CUnit* person;                   // the client unit
    bool waited;                     // the dialogue waits for this one
    std::uint8_t unused_29[3];
    float time;                      // seconds on the way (Update gives up after 30)

    static constexpr int kDone = 0;
    static constexpr int kWalking = 1;
    static constexpr int kTurning = 2;
};
static_assert(offsetof(BriefingMover, person) == 0x24);
static_assert(offsetof(BriefingMover, time) == 0x2C);

// A person taking part in the dialogue (CBriefingScreen +0x294, an STLport vector of 12-byte
// entries): the client unit, its name as the dialogue's commands give it, and a flag.
struct BriefingSpeaker {
    CUnit* unit;
    mfc::CString name;
    bool spareFlag;                  // false when added; only copied with the entry (erase), never tested
    std::uint8_t unused_09[3];
};
static_assert(sizeof(BriefingSpeaker) == 0xC);

// An item shown in the dialogue ("#show <name>" for what is not a unit): a node of the
// STLport list at CBriefingScreen +0x278 (0x18 bytes, node pool). The meshes are the
// window's scene objects; the item's texture is one of its referenced objects.
struct BriefingShownItem {
    BriefingShownItem* next;
    BriefingShownItem* prev;
    mfc::CString name;
    CUiMesh* item;
    CEngineObject* back;
    CEngineObject* frame;
};
static_assert(sizeof(BriefingShownItem) == 0x18);

// The local player's name (its zone): copy-constructs it into raw storage, destroyed at
// the end of the scope.
class BriefingPlayerName {
public:
    BriefingPlayerName() { LocalPlayerName(&name_); }
    BriefingPlayerName(const BriefingPlayerName&) = delete;
    BriefingPlayerName& operator=(const BriefingPlayerName&) = delete;
    operator const char*() const { return name_; }

private:
    mfc::CString name_{mfc::CString::kUnset};
};

// A copy of the local player's variables whose names start with a prefix (into an
// STLport hash_map<CString, float> of 100 buckets), walked in the table's order and
// freed (ui_briefing.cpp).
class BriefingVariables {
public:
    explicit BriefingVariables(const char* prefix);
    ~BriefingVariables();
    BriefingVariables(const BriefingVariables&) = delete;
    BriefingVariables& operator=(const BriefingVariables&) = delete;

    NamedFloatTable::Node* First() const;
    NamedFloatTable::Node* Next(NamedFloatTable::Node* node) const;

private:
    NamedFloatTable table_;
};

class CRefObject;

// A network client's player list on the base screen: the players' faces along the top right (0x38 pixels
// each from x 0x2E8 leftwards, a region each), a surface under them with each one's status and
// offer, and the status texts ("lmp_status_zone" .. "lmp_exch_they_offer"). Multiplayer only.
class CChatWindow : public CUiWindow {
public:
    explicit CChatWindow(CInterface3D* ui);
    ~CChatWindow() override;
    // A face: every fourth one first makes a 256 x 256 texture (kind 0x20);
    // `texture` is copied into the newest texture's quarter (count % 4: left / right, top / bottom)
    // and a sprite of `model` placed at the face's centre (depth 7, scale 0.3, turned pi * 1.05
    // about x), or without the model a plain 7 / 100 rectangle 0..60 high; the sprite shows the
    // quarter (+0xCC / +0xD0 in 1/256) of that texture; a region 0..0x50 high over the face.
    void AddPortrait(const char* model, const char* texture);
    // Clears the sprites, regions and
    // referenced objects; lists the client's players (only while its binding is live; its own
    // player first) with a reference each, counts them, then gives each a face from its
    // unit prototype (+0x18: "infa<model>me<hair+1>face", "face<m|f><skin, 2 digits>"; the sex
    // letter carries over from the previous player when the model is neither "unhuma" nor
    // "unhufe") and its name as the region's tip.
    void ShowPlayers();
    // While the client has a live trade it asks for screen 9 (the
    // exchange); otherwise surface 0 is cleared and each listed player gets its status (state
    // 0..4: texts 0, 1, 2, 3, 3) on the first line and "we offer" (our offer's partner) or "they
    // offer" (an offer from it) on the second, white, 0x38 pixels per player from x 0x118 leftwards.
    void Update() override;
    // Input side (`this` the window): a click on a face (the event
    // marked used): with the active window's player pick 1 / 2 the
    // player's serial goes to the server as command 4 / 5 ("%d") and the pick ends; otherwise the
    // on_off sound and, unless it is our own player, our offer to it is dropped when it is its
    // partner, else one is started: over a face while a
    // pick is on, the cursor's shape 0.
    void OnLButtonDown(int buttons, int x, int y);
    void OnMouseMove(int buttons, int x, int y);
    int LeftDown(int b, int x, int y) override { return OnLButtonDown(b, x, y), 0; }
    int MouseMove(int b, int x, int y) override { return OnMouseMove(b, x, y), 0; }

    int listed_;                              // players listed
    int portraits_;                           // faces added
    stlp::vector<CRefObject*> players_;       // counted PlayerMessage records
    mfc::CStringArray texts_;
};
static_assert(offsetof(CChatWindow, listed_) == 0x148);
static_assert(offsetof(CChatWindow, players_) == 0x150);
static_assert(offsetof(CChatWindow, texts_) == 0x15C);
static_assert(sizeof(CChatWindow) == 0x170);

class CBriefingScreen : public CUiContainer {
public:
    explicit CBriefingScreen(CInterface3D* ui);
    BOOL Draw(int pass) override;   // (ui_container_draws.cpp)
    void CloseScreen() override { Close(); }
    void MessageResult(int result) override { OnMessageResult(static_cast<std::uint32_t>(result)); }
    void UnitJoined(CUnit* unit) override { OnUnitJoined(unit); }
    void UnitLeft(CUnit* unit) override { OnUnitLeft(unit); }
    void GameOver(int won) override { OnGameOver(won); }
    void ShowBriefing(const char* name) override { OnBriefing(name); }
    bool ReadyToStopPolling() override { return true; }
    void PlayersChanged(int show) override { ShowChat(static_cast<char>(show)); }
    void PlayerList(int mode) override { SetChatActive(mode); }
    int LeftDown(int b, int x, int y) override { return OnLeftDown(b, x, y), 0; }                 // input 2
    int MouseMove(int b, int x, int y) override { return OnMouseMove(b, x, y), 0; }               // input 4
    int LeftDouble(int b, int x, int y) override { return OnLeftDouble(b, x, y), 0; }             // input 7
    int KeyDown(int k, int r, int f) override { return OnKeyDown(k, r, f), 0; }                   // input 9
    // Main virtuals (ui_briefing.cpp).
    // The screen closes: the map edge fade back to the value
    // Create saved, the client glue's (1) /, the camera free again (flag 0x40,
    // flags 0x100 / 0x200 cleared).
    void Close();
    // A message box's answer: 0x10001 leaves through exit_;
    // 0x20001 (game over: load) opens the load window (built inline as the battle screen does);
    // 0x20002, 0x90001, 0x100001 close every window, drop the sessions and leave the game. A
    // network client then shows its chat window again (slot 36 with 1).
    void OnMessageResult(std::uint32_t result);
    // `unit` left the party: no longer the leader, taken out of party_
    // (by id; the rest moved down).
    void OnUnitLeft(CUnit* unit);
    // `unit` joined the party: it goes into party_ after the members
    // whose unit records (the local player's, LocalPlayerData +0x30) come before its record, or
    // last without one. The first record's unit becomes the leader: the client world controls it,
    // the camera follows it, and the first "b.<zone>..." variable equal to 1 with three parts
    // starts its topic (the third part) at once (briefingStarted_; with 1, 1).
    void OnUnitJoined(CUnit* unit);
    // The game is lost: the game-over sound
    // and the box "game_over" / "game_over_msg" (result 0x20000) on top.
    void OnGameOver(int unused);
    // The interface command "briefing <name>": dialogue
    // `name` starts at once.
    void OnBriefing(const char* name);
    // A network client shows its chat window when `show`.
    void ShowChat(char show);
    // The screen's part of a save game: one zero dword.
    void SaveState() override;
    // Builds the screen: the four backdrop sprites,
    // surfaces 0 (topics) and 1 (a person's label), topicBar_ and text_ as children, the people
    // of the base (people_), the zone's music and camera limits, the screen's own inputs (a UiInertInput
    // and the view's) and a network client's chat window. A saved game's screen part is
    // dropped (the save stream resized to 0).
    void CreateScreen() override;
    // Per frame: markPhase_, the children, everything
    // hidden but by state; the movers walk, turn or give up after 30 s (put at their place) and the
    // done ones go; then by state: 0 / 1 the people with open topics are marked (dialogue event 1,
    // the others 2) and the leader's pending exit is left once it can be; 1 the topic
    // list shown; 2 the phrases start once no waited mover walks; 3 the next phrase when the
    // voice ends or phraseTime_ runs out; 4 the speaker stops blinking then.
    void Update() override;
    // Update's part for states 0 and 1: every person of people_ that has a client unit is marked
    // (dialogue event 1) when a "b.<person>.<topic>" variable is 1 for a topic that is not
    // "constr...", else unmarked (event 2).
    void MarkPeople();
    // Update's part for states 0 and 1: with exitPending_, a party and exit_'s zone not "none" nor
    // visited ("z.<zone>" not 1), the party leaves through exit_ once allows it.
    void LeaveIfExitOpen();

    // Input side (window + 0x94; ui_briefing_input.cpp).
    // Left button down: with the chat window active (chatActive_) it gets the
    // event; otherwise the container's handler and, unless a child used it, by state:
    // 0 a person under the cursor (an idle one, controller command 0 or 0xB and next
    // command 9) opens its topics (OpenTopics), else the leader walks to the picked ground point
    // (not when noWalk_); 1 a topic row starts that topic; 3 the next phrase
    // 4 the rewards are taken.
    void OnLeftDown(int buttons, int x, int y);
    // Double click: as the container's; a person who is not one of
    // the moving ones, or with the dialogue running (state not 0), is handled as a click (input
    // slot 2); on the ground the leader runs there.
    void OnLeftDouble(int buttons, int x, int y);
    // Mouse move: with the chat window active, cursor 0xD and the event to it;
    // otherwise cursor 0, the container's handler, then by state: 1 the hovered topic
    // row (redrawn when it changes), 3 cursor 0xE, 4 cursor 0. Walking (state 0, right button up):
    // over a person who can talk (as OnLeftDown; on a network client not the leader) its topics
    // are looked up (cursor 0xE when it has any) and its "pers" text, when it has one,
    // is shown in surface 1 at the mouse (-125, -20); over a zone exit, cursor 0xF when the zone
    // of exit_ (not of the exit found: kept) is not "none" and its "z.<zone>" variable is not 1.
    void OnMouseMove(int buttons, int x, int y);
    // Keys: with the chat window active only Esc,
    // which gives the input back. Otherwise the container's handler, then Backspace /
    // Enter go to a network client's chat line; Enter (single player) and Esc skip the phrases
    // (with 1 until the state changes) or take the rewards; Esc also opens the escape
    // menu (state 0), ends the start or the topic choice (1); Space the
    // next phrase. Other keys while walking by their action: 4 the objectives window
    // when a quest of this allod ("q.<quest>.<zone>.." = 1) is active, 0xB the camera to the
    // leader, '5' / '6' quick save / load "saveq". Every handled key sets handled_.
    void OnKeyDown(int key, int repeat, int flags);

    // Opens the topics of `person`: none leaves state 0; otherwise
    // the leader stops (unless noWalk_), five topic rows (regions at x 210..570 from y 440, 24
    // high), "goodbye" appended, the scroll bar's range and position 0, the list drawn (6071E0),
    // state 1.
    void OpenTopics(CEngineObject* person);
    // Draws the topic rows into surface 0 (from topicTop_, up to five, at x 15..380,
    // 24 high from y 12): "topic_prefix" text + " " + the topic's title, white for
    // hoveredTopic_, else 0xA7D7E4.
    void DrawTopics();
    // topics_ becomes the topics of the person whose name has the text key `personId`: every
    // "b.<person>.<topic>" variable equal to 1 (bit compare), in the variable table's order (a network client
    // sorts them), and last the last "constr..." one found.
    void FindTopics(int personId);
    // A topic's title into `out` (constructed here, returned): the first line of the text "briefing <topic>"
    // (lowercased; "constr..." topics share "briefing constr"), empty without one.
    mfc::CString* TopicTitle(mfc::CString* out, const char* topic);

    // personTexts_ gets the "pers <name>" texts (LoadPersonTexts).
    void LoadPersonTexts();

    // The dialogue (ui_briefing_dialogue.cpp).
    // The phrases start: the voice still playing stops, the music fades out; without a
    // text "briefing <topic_>" back to state 0. Otherwise the server hears dialogue event 3, the
    // camera position is kept (savedCamera_) with a point one unit along its rotation's z axis
    // (lookAt_), the dialogue text and the text window are emptied, state 3, firstPhrase_, and the
    // first phrase.
    void StartPhrases();
    // `unit` (a client unit) plays a dialogue animation: talking 3 for a third
    // speaker, else a random one of 1, 6, 7, 8 for a human ("unhuma") and 1 for others; listening
    // a random one of 2, 0xA, 0xC, 0xD for a human and 2 for others (skill animations).
    // Its server unit's controller drops its action first and calls when the
    // animation has finished.
    void PlaySpeakerAnimation(CUnit* unit, bool talking, bool third);
    // The camera views `unit`: from its eyes (its place, or its position, plus
    // its facing times the prototype's viewDistance, at viewHeight) around the facing turned by
    // `angle`, `distance` away and `height` above; without a unit the middle of the first two
    // places, looked at across them. Sent to the server as message 0x0F (camera position, point).
    void ViewSpeaker(CUnit* unit, float angle, float distance, float height);
    // Camera preset `preset` of a phrase ("#camera n"; 1-12, others nothing):
    // views of both (1, 5, 9: ViewSpeaker without a unit, at 3, 2.7 or 2.4 times the distance
    // between the first two places), of the leader (2, 6, 10), the person (3, 7, 11) or the third
    // speaker (4, 8, 12) from various angles; the other speakers are first sunk to height -100
    // in 2, 3, 6, 7 and 12.
    void ViewPreset(int preset);
    // "#hide <name>": a dialogue person `name` leaves the dialogue (the server
    // hears event 2; its blink stops; its server unit gets flag 0x40000000 back, height 0, its
    // action dropped) and the speakers' list; the third speaker walks to thirdPlace_
    // (with `immediate` and `time`) and is forgotten. A name that is no unit removes the shown item
    // of that name (its meshes and texture).
    void HideSpeaker(const char* name, int immediate, int time);

    // Dialogue topic_ starts: "goodbye" (or no leader)
    // back to state 0; a "constr..." topic opens the camp (screen 5) with constructor N (the topic
    // from its eighth character) when that class template exists. Otherwise the "#show" lines of
    // the text "briefing <topic>" name the people: the first one not the leader is person_, a
    // second third_; without one, state 0. The places: the person's (or, with noWalk_, the
    // leader's) position, the other 2.5 + viewDistance along its facing, the third one beside
    // the middle; their facings across, flat and normalised. With `remember` the people's
    // positions are kept first (personHome_, thirdPlace_). A mover of the third person (or of the
    // person, noWalk_) is dropped, its target kept unless it was waited for; then the leader (or,
    // noWalk_, the person) and the third walk to their places (waited, `immediate`).
    // `immediate` starts the phrases at once (StartPhrases), else state 2.
    void StartDialogue(bool immediate, bool remember);
    // The dialogue ends (the rewards taken, or skipped): the voice
    // stops, the music fades in, the shown items go, every speaker leaves (the server hears event 2,
    // its blink stops, its server unit's flag 0x40000000 back, height 0, action dropped) and the
    // list is emptied; the leader and the person back to height 0, the third speaker walks back to
    // thirdPlace_ (and is forgotten), with noWalk_ the person to personHome_. The first "b.*.<topic>"
    // variable (three parts) completes (for player 0). State 0, the server hears event 0
    // with the leader; a dialogue that started by itself (briefingStarted_) puts the camera on the
    // leader (message 0x04) and frees it, otherwise it goes back to savedCamera_ / lookAt_.
    void FinishDialogue();
    // `unit` (a client unit) goes to (x, y) at its
    // server unit's height (`z` is not read), facing (dx, dy, dz): `immediate` puts the server
    // unit there at once (sector map, both units turned, the controller repositioned); otherwise,
    // unless the server unit is blocked (0x800000), a mover is put first in movers_ (walking,
    // `waited`) and the server unit's brain walks there (`run`).
    void MovePerson(CUnit* unit, float x, float y, float z, float dx, float dy, float dz, bool waited, bool immediate,
                    int run);
    // "#show <name>": a person `name` joins the
    // speakers (moved to the end; the server hears event 1; the leader's server unit gets flag
    // 0x40000000; its controller's actionDone the speakers' callback). Anything else with
    // `index` >= 0 is a loot item ("_" read as " ", lowercased, the loot table's name index): an
    // item of that name shown already goes, then slot `index` % 7 (x 300, 500, 200,
    // 400, 600, 75, 725; y 300 or 500) gets a backdrop and frame for the first seven, the item's
    // figure "initqu<n>item" (tinted from index 8) with its image "quitem<n>" in a new texture,
    // turned over about x; it goes first in shown_. The third argument is not read.
    void ShowItem(const char* name, int index, int unused);
    // Removes the shown items' meshes (and the item texture) from the scene and empties shown_
    void ClearShownItems();
    ~CBriefingScreen() override;
    // chatActive_ = `mode`.
    void SetChatActive(int mode);

    // The next phrase of the current text: its lines up
    // to the next "#phrase <speaker> [voice]" are carried out ("#nolips", "#show", "#hide",
    // "#animation n", "#camera n [angle distance height]"); the phrase (up to the next '#') goes,
    // headed by the speaker's name, into dialogueText_ and (unless `skip`) the text window, with
    // its voice "<t|s>\briefing\<topic>\<n>.mp3" and phraseTime_. After the last phrase: state 4
    // and the briefing record's rewards as lines (experience, gold, items given and lost, quests).
    // The camera then views the phrase's speaker, who talks (an animation, blinking unless
    // "#nolips") while the others listen; skipping to the end shows the whole text at once.
    void NextPhrase(bool skip);

    int exit_;                      // the exit a "leave_zone" answer leaves through
    bool exitPending_;               // Create sets it (Update checks the leader's exit)
    std::uint8_t unused_169[3];
    int state_;                      // dialogue state (above)
    console::CStringArray topics_;   // the person's topics (names), "goodbye" last
    mfc::CString topic_;             // the dialogue running
    int hoveredTopic_;               // the topic row under the mouse, or -1
    bool briefingStarted_;           // a dialogue started by itself (602C90)
    std::uint8_t unused_18D[3];
    mfc::CArray<CUnit*> party_;      // the party's units (CArray, SetSize)
    CUnit* leader_;                  // the unit the camera follows (the hero speaker)
    CUnit* person_;                  // the person the dialogue is with
    CUnit* third_;                   // a third speaker, or null
    // +0x1B0 the speakers' places and +0x1D4 their facings: [0] person_, [1] leader_, [2] third_.
    Vector3 places_[3];
    Vector3 directions_[3];
    Vector3 savedCamera_;            // the camera position when the phrases started
    Vector3 lookAt_;                 // position + the camera rotation's z axis then
    Vector3 thirdPlace_;             // where the third speaker goes when hidden
    Vector3 thirdDirection_;
    Vector3 personHome_;             // where the person stood when the dialogue started (noWalk_)
    Vector3 personHomeDirection_;    // and its facing
    BriefingMover* movers_;          // the people walking to their places (list header)
    bool firstPhrase_;               // set when the phrases start (the first one lasts a second longer)
    std::uint8_t unused_245[3];
    mfc::CString dialogueText_;      // the dialogue's text so far (shown whole when skipped)
    float phraseTime_;               // time left of a phrase without a voice
    int voice_;                      // the phrase's voice (sound id), -1 none
    CUnit* speaking_;                // the speaker of the current phrase (NextPhrase), or null
    std::uint8_t unused_258[4];
    CUiScrollText* text_;            // the dialogue text
    CUiScrollbar* topicBar_;         // the topic list's scroll bar
    int topicTop_;                   // first topic row shown
    float savedEdgeFade_;            // the map edge fade Create replaced
    PersonTextMap personTexts_;      // "pers" texts by text key (map<int, CString>)
    BriefingShownItem* shown_;       // the items shown (list header)
    bool immediate_;                 // the dialogue's people are placed at once ('s first argument)
    bool noWalk_;                    // the zone's map record +0x44: the leader does not walk
    bool autosave_;                  // Create sets it unless loading or back from the camp
    std::uint8_t unused_27F;
    CChatWindow* chat_;              // a network client's chat window (608D20), else null
    stlp::vector<int> people_;       // ids of the local player's objects that are text keys (Create)
    float markPhase_;                // Update adds the frame time, kept below 1
    BriefingSpeaker* speakers_;      // the people in the dialogue (vector begin, end, capacity)
    BriefingSpeaker* speakersEnd_;
    BriefingSpeaker* speakersCapacity_;
    int chatActive_;                 // the chat window takes the input
};
static_assert(offsetof(CBriefingScreen, exit_) == 0x164);
static_assert(offsetof(CBriefingScreen, state_) == 0x16C);
static_assert(offsetof(CBriefingScreen, topics_) == 0x170);
static_assert(offsetof(CBriefingScreen, topic_) == 0x184);
static_assert(offsetof(CBriefingScreen, hoveredTopic_) == 0x188);
static_assert(offsetof(CBriefingScreen, party_) == 0x190);
static_assert(offsetof(CBriefingScreen, leader_) == 0x1A4);
static_assert(offsetof(CBriefingScreen, person_) == 0x1A8);
static_assert(offsetof(CBriefingScreen, places_) == 0x1B0);
static_assert(offsetof(CBriefingScreen, directions_) == 0x1D4);
static_assert(offsetof(CBriefingScreen, savedCamera_) == 0x1F8);
static_assert(offsetof(CBriefingScreen, lookAt_) == 0x204);
static_assert(offsetof(CBriefingScreen, movers_) == 0x240);
static_assert(offsetof(CBriefingScreen, firstPhrase_) == 0x244);
static_assert(offsetof(CBriefingScreen, dialogueText_) == 0x248);
static_assert(offsetof(CBriefingScreen, phraseTime_) == 0x24C);
static_assert(offsetof(CBriefingScreen, text_) == 0x25C);
static_assert(offsetof(CBriefingScreen, topicTop_) == 0x264);
static_assert(offsetof(CBriefingScreen, personTexts_) == 0x26C);
static_assert(offsetof(CBriefingScreen, noWalk_) == 0x27D);
static_assert(offsetof(CBriefingScreen, chat_) == 0x280);
static_assert(offsetof(CBriefingScreen, thirdPlace_) == 0x210);
static_assert(offsetof(CBriefingScreen, personHome_) == 0x228);
static_assert(offsetof(CBriefingScreen, personHomeDirection_) == 0x234);
static_assert(offsetof(CBriefingScreen, immediate_) == 0x27C);
static_assert(offsetof(CBriefingScreen, shown_) == 0x278);
static_assert(offsetof(CBriefingScreen, speakers_) == 0x294);
static_assert(offsetof(CBriefingScreen, autosave_) == 0x27E);
static_assert(offsetof(CBriefingScreen, people_) == 0x284);
static_assert(offsetof(CBriefingScreen, markPhase_) == 0x290);
static_assert(offsetof(CBriefingScreen, chatActive_) == 0x2A0);
static_assert(sizeof(CBriefingScreen) == 0x2A4);
