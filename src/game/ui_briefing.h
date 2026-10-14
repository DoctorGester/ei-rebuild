#pragma once

// The base / briefing screen (interface screen 3; vtables 00740578 input / 007405AC main, 0x2A4
// bytes, Create 006024B0, Update 00604130, Draw 00602BF0 in ui_container_draws.cpp). It is the
// screen of a "brief" zone (map.txt type brief, campaign mode 3: the bases bz1g ..): the party
// walks around the base, a click on a person opens the person's topics, a topic runs its
// dialogue (phrases, then rewards). See docs/notes/ui.md, "Briefing / base screen".
//
// Dialogue states (+0x16C): 0 walking, 1 choosing a topic, 2 the dialogue starting (people still
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
    float target[3];                 // +0x08
    float direction[3];              // +0x14 the facing at the target
    int state;                       // +0x20 kDone, kWalking or kTurning
    CEngineObject* person;           // +0x24 the client object
    bool waited;                     // +0x28 the dialogue waits for this one
    std::uint8_t unknown_29[3];
    float time;                      // +0x2C seconds on the way (Update gives up after 30)

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
    mfc::CString name;               // +0x04
    bool unknown_08;
    std::uint8_t unknown_09[3];
};
static_assert(sizeof(BriefingSpeaker) == 0xC);

// An item shown in the dialogue ("#show <name>" for what is not a unit, 00605DE0): a node of the
// STLport list at CBriefingScreen +0x278 (0x18 bytes, node pool 0079B22C). The meshes are the
// window's scene objects; the item's texture (+0x280) is one of its referenced objects.
struct BriefingShownItem {
    BriefingShownItem* next;
    BriefingShownItem* prev;
    mfc::CString name;               // +0x08
    CUiMesh* item;                   // +0x0C
    CEngineObject* back;             // +0x10
    CEngineObject* frame;            // +0x14
};
static_assert(sizeof(BriefingShownItem) == 0x18);

// A briefing's record (0x6C bytes; the quest database's briefings, by topic in the hash_map at
// 007C87D4, read by quest_database.cpp): its rewards as NextPhrase lists them.
struct BriefingRecord {
    using NameList = stlp::vector<mfc::CString>;
    std::uint8_t unknown_00[0x0C];
    float experience;                // +0x0C
    float gold;                      // +0x10
    NameList giveItems;              // +0x14 item names
    NameList removeItems;            // +0x20
    NameList giveQuests;             // +0x2C variables "a.b.<quest>"
    NameList completeQuests;         // +0x38
    NameList availableZones;         // +0x44 variables raised to 1 (CWorldServer::CompleteBriefing)
    NameList completeZones;          // +0x50 variables set to 2
    std::uint8_t unknown_5C[0x68 - 0x5C];   // +0x5C a string
    int specials;                    // +0x68 "brief_get_specials_<n>" when above 0
};
static_assert(offsetof(BriefingRecord, giveItems) == 0x14);
static_assert(offsetof(BriefingRecord, completeQuests) == 0x38);
static_assert(offsetof(BriefingRecord, completeZones) == 0x50);
static_assert(sizeof(BriefingRecord) == 0x6C);

// The local player's name (its zone): 00578A90 copy-constructs it into raw storage, destroyed at
// the end of the scope.
class BriefingPlayerName {
public:
    BriefingPlayerName() { LocalPlayerName(reinterpret_cast<mfc::CString*>(raw_)); }
    ~BriefingPlayerName() { reinterpret_cast<mfc::CString*>(raw_)->~CString(); }
    BriefingPlayerName(const BriefingPlayerName&) = delete;
    BriefingPlayerName& operator=(const BriefingPlayerName&) = delete;
    operator const char*() const { return *reinterpret_cast<const mfc::CString*>(raw_); }

private:
    alignas(4) std::uint8_t raw_[4];
};

// A copy of the local player's variables whose names start with a prefix (00578C40 into an
// STLport hash_map<CString, float> of 100 buckets, 00581790), walked in the table's order and
// freed as the original's inlined destructor does (ui_briefing.cpp).
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

// A network client's player list on the base screen (constructor 00608D20, 0x170 bytes, vtables
// 00741478 / 00741444; ui_chat_window.cpp): the players' faces along the top right (0x38 pixels
// each from x 0x2E8 leftwards, a region each), a surface under them with each one's status and
// offer, and the status texts ("lmp_status_zone" .. "lmp_exch_they_offer"). Multiplayer only.
class CChatWindow : public CUiWindow {
public:
    // 00608D20 (RET 4). Constructor: the window base, an empty player vector and text array, the
    // vtables, the rectangle 0x1D0, 0, 800, 0xA0, a surface over 0x1D0..800 x 0x4B..0x69 (format 5),
    // no faces, the six texts from the text table. Returns this.
    CChatWindow* Construct(CInterface3D* ui);
    // 00608ED0 (ui_window_delete.cpp). The destructor body: the texts, each held player released,
    // the vector's storage, then the window's.
    void DestroyBody();
    // 00608EB0 (slot 0, the deleting destructor; ui_window_delete.cpp). DestroyBody, then the
    // memory freed when bit 0 of `flags` is set; returns this.
    CChatWindow* ChatWindowDelete(unsigned flags);
    // 00608F90 (RET 8). A face: every fourth one first makes a 256 x 256 texture (kind 0x20);
    // `texture` is copied into the newest texture's quarter (count % 4: left / right, top / bottom)
    // and a sprite of `model` placed at the face's centre (depth 7, scale 0.3, turned pi * 1.05
    // about x), or without the model a plain 7 / 100 rectangle 0..60 high; the sprite shows the
    // quarter (+0xCC / +0xD0 in 1/256) of that texture; a region 0..0x50 high over the face.
    void AddPortrait(const char* model, const char* texture);
    // 006091C0 (the base screen's slot 36 with 1, 00603090). Clears the sprites, regions and
    // referenced objects; lists the client's players (only while its binding is live; its own
    // player first) with a reference each, counts them (+0x148), then gives each a face from its
    // unit prototype (+0x18: "infa<model>me<hair+1>face", "face<m|f><skin, 2 digits>"; the sex
    // letter carries over from the previous player when the model is neither "unhuma" nor
    // "unhufe") and its name as the region's tip.
    void ShowPlayers();
    // 006095A0 (slot 24). While the client has a live trade (+0x2618) it asks for screen 9 (the
    // exchange); otherwise surface 0 is cleared and each listed player gets its status (state
    // 0..4: texts 0, 1, 2, 3, 3) on the first line and "we offer" (our offer's partner) or "they
    // offer" (an offer from it) on the second, white, 0x38 pixels per player from x 0x118 leftwards.
    void Update();
    // Input side (`this` the window). 006097E0 (slot 2, RET 0xC): a click on a face (the event
    // marked used): with the active window's player pick (CBriefingScreen +0x2A0) 1 / 2 the
    // player's serial goes to the server as command 4 / 5 ("%d") and the pick ends; otherwise the
    // on_off sound and, unless it is our own player, our offer to it is dropped when it is its
    // partner, else one is started (0041E270). 00609900 (slot 4, RET 0xC): over a face while a
    // pick is on, the cursor's shape 0.
    void OnLButtonDown(int buttons, int x, int y);
    void OnMouseMove(int buttons, int x, int y);

    int listed_;                              // +0x148 players listed
    int portraits_;                           // +0x14C faces added
    stlp::vector<CRefObject*> players_;       // +0x150 counted PlayerMessage records
    mfc::CStringArray texts_;                 // +0x15C
};
static_assert(offsetof(CChatWindow, listed_) == 0x148);
static_assert(offsetof(CChatWindow, players_) == 0x150);
static_assert(offsetof(CChatWindow, texts_) == 0x15C);
static_assert(sizeof(CChatWindow) == 0x170);

class CBriefingScreen : public CUiContainer {
public:
    // Main vtable slots (ui_briefing.cpp).
    // 00602BB0 (slot 26). The screen closes: the map edge fade (0079BD34) back to the value
    // Create saved, the client glue's 00578920(1) / 00578940, the camera free again (flag 0x40,
    // flags 0x100 / 0x200 cleared).
    void Close();
    // 00606F40 (slot 27, RET 4). A message box's answer: 0x10001 leaves through exit_ (00578AC0);
    // 0x20001 (game over: load) opens the load window (built inline as the battle screen does);
    // 0x20002, 0x90001, 0x100001 close every window, drop the sessions and leave the game. A
    // network client then shows its chat window again (slot 36 with 1).
    void OnMessageResult(std::uint32_t result);
    // 00603010 (slot 30, RET 4). `unit` left the party: no longer the leader, taken out of party_
    // (by id; the rest moved down).
    void OnUnitLeft(CUnit* unit);
    // 00602C90 (slot 29, RET 4). `unit` joined the party: it goes into party_ after the members
    // whose unit records (the local player's, LocalPlayerData +0x30) come before its record, or
    // last without one. The first record's unit becomes the leader: the client world controls it,
    // the camera follows it, and the first "b.<zone>..." variable equal to 1 with three parts
    // starts its topic (the third part) at once (briefingStarted_; 006064E0 with 1, 1).
    void OnUnitJoined(CUnit* unit);
    // 00608C70 (slot 31, RET 4; the argument is not read). The game is lost: the game-over sound
    // and the box "game_over" / "game_over_msg" (result 0x20000) on top.
    void OnGameOver(int unused);
    // 006030C0 (slot 34, RET 4). The interface command "briefing <name>" (005F8070): dialogue
    // `name` starts at once (006064E0 with 1, 1).
    void OnBriefing(const char* name);
    // 00603090 (slot 36, RET 4). A network client shows its chat window (006091C0) when `show`.
    void ShowChat(char show);
    // 00602490 (slot 40). The screen's part of a save game: one zero dword.
    void SaveState();
    // 006024B0 (slot 25; ui_briefing_create.cpp). Builds the screen: the four backdrop sprites,
    // surfaces 0 (topics) and 1 (a person's label), topicBar_ and text_ as children, the people
    // of the base (people_), the zone's music and camera limits, the screen's own inputs (00741154
    // and the view's 005EACB0) and a network client's chat window. A saved game's screen part is
    // dropped (the save stream resized to 0).
    void Create();
    // 00604130 (slot 24; ui_briefing_create.cpp). Per frame: markPhase_, the children, everything
    // hidden but by state; the movers walk, turn or give up after 30 s (put at their place) and the
    // done ones go; then by state: 0 / 1 the people with open topics are marked (dialogue event 1,
    // the others 2) and the leader's pending exit is left once it can be (00578A20); 1 the topic
    // list shown; 2 the phrases start once no waited mover walks; 3 the next phrase when the
    // voice ends or phraseTime_ runs out; 4 the speaker stops blinking then.
    void Update();
    // Update's part for states 0 and 1: every person of people_ that has a client unit is marked
    // (dialogue event 1) when a "b.<person>.<topic>" variable is 1 for a topic that is not
    // "constr...", else unmarked (event 2).
    void MarkPeople();
    // Update's part for states 0 and 1: with exitPending_, a party and exit_'s zone not "none" nor
    // visited ("z.<zone>" not 1), the party leaves through exit_ once 00578A20 allows it.
    void LeaveIfExitOpen();
    // Slot 36 (ShowChat for this screen) through the vtable.
    void VirtualShowChat(int show) {
        using Fn = void(__thiscall*)(CBriefingScreen*, int);
        reinterpret_cast<Fn>(vtable_[36])(this, show);
    }

    // Input side (window + 0x94; ui_briefing_input.cpp).
    // 006030F0 (input 2). Left button down: with the chat window active (chatActive_) it gets the
    // event; otherwise the container's handler (00601110) and, unless a child used it, by state:
    // 0 a person under the cursor (00565D00; an idle one, controller command 0 or 0xB and next
    // command 9) opens its topics (OpenTopics), else the leader walks to the picked ground point
    // (not when noWalk_); 1 a topic row starts that topic (006064E0 with 0, 1); 3 the next phrase
    // (00604C50 with 0); 4 the rewards are taken (00608450).
    void OnLeftDown(int buttons, int x, int y);
    // 00603360 (input 7). Double click: as the container's (00601570); a person who is not one of
    // the moving ones, or with the dialogue running (state not 0), is handled as a click (input
    // slot 2); on the ground the leader runs there.
    void OnLeftDouble(int buttons, int x, int y);
    // 00603D00 (input 4). Mouse move: with the chat window active, cursor 0xD and the event to it;
    // otherwise cursor 0, the container's handler (006012D0), then by state: 1 the hovered topic
    // row (redrawn when it changes), 3 cursor 0xE, 4 cursor 0. Walking (state 0, right button up):
    // over a person who can talk (as OnLeftDown; on a network client not the leader) its topics
    // are looked up (00607360; cursor 0xE when it has any) and its "pers" text, when it has one,
    // is shown in surface 1 at the mouse (-125, -20); over a zone exit, cursor 0xF when the zone
    // of exit_ (not of the exit found: kept) is not "none" and its "z.<zone>" variable is not 1.
    void OnMouseMove(int buttons, int x, int y);
    // 006034A0 (input 9; ui_briefing_keys.cpp). Keys: with the chat window active only Esc,
    // which gives the input back. Otherwise the container's handler (00601820), then Backspace /
    // Enter go to a network client's chat line; Enter (single player) and Esc skip the phrases
    // (00604C50 with 1 until the state changes) or take the rewards; Esc also opens the escape
    // menu (state 0), ends the start (2: 006064E0 with 1, 0) or the topic choice (1); Space the
    // next phrase. Other keys while walking by their action (0079BF70): 4 the objectives window
    // when a quest of this allod ("q.<quest>.<zone>.." = 1) is active, 0xB the camera to the
    // leader, '5' / '6' quick save / load "saveq". Every handled key sets inputHandled_.
    void OnKeyDown(int key, int repeat, int flags);

    // 00608B60 (RET 4). Opens the topics of `person` (00607360): none leaves state 0; otherwise
    // the leader stops (unless noWalk_), five topic rows (regions at x 210..570 from y 440, 24
    // high), "goodbye" appended, the scroll bar's range and position 0, the list drawn (6071E0),
    // state 1.
    void OpenTopics(CEngineObject* person);
    // 006071E0. Draws the topic rows into surface 0 (from topicTop_, up to five, at x 15..380,
    // 24 high from y 12): "topic_prefix" text + " " + the topic's title (006078C0), white for
    // hoveredTopic_, else 0xA7D7E4.
    void DrawTopics();
    // 00607360 (RET 4; ui_briefing_topics.cpp). topics_ becomes the topics of the person whose
    // name has the text key `personId`: every "b.<person>.<topic>" variable equal to 1 (bit
    // compare), in the variable table's order (a network client sorts them, 0060B330 / 0060B440),
    // and last the last "constr..." one found.
    void FindTopics(int personId);
    // 006078C0 (RET 8; ui_briefing_topics.cpp; the screen is not read). A topic's title into
    // `out` (constructed here, returned): the first line of the text "briefing <topic>"
    // (lowercased; "constr..." topics share "briefing constr"), empty without one.
    mfc::CString* TopicTitle(mfc::CString* out, const char* topic);

    // 00607720 (ui_briefing.cpp). personTexts_ gets the "pers <name>" texts (PersonTextMap::Load).
    void LoadPersonTexts();

    // The dialogue (ui_briefing_dialogue.cpp).
    // 00608210. The phrases start: the voice still playing stops, the music fades out; without a
    // text "briefing <topic_>" back to state 0. Otherwise the server hears dialogue event 3, the
    // camera position is kept (savedCamera_) with a point one unit along its rotation's z axis
    // (lookAt_), the dialogue text and the text window are emptied, state 3, firstPhrase_, and the
    // first phrase (00604C50 with 0).
    void StartPhrases();
    // 00607A60 (RET 0xC). `unit` (a client unit) plays a dialogue animation: talking 3 for a third
    // speaker, else a random one of 1, 6, 7, 8 for a human ("unhuma") and 1 for others; listening
    // a random one of 2, 0xA, 0xC, 0xD for a human and 2 for others (skill animations, 00518FC0).
    // Its server unit's controller drops its action first (00555840) and calls 006023F0 when the
    // animation has finished.
    void PlaySpeakerAnimation(CUnit* unit, bool talking, bool third);
    // 00607BB0 (RET 0x10). The camera views `unit`: from its eyes (its place, or its position, plus
    // its facing times the prototype's viewDistance, at viewHeight) around the facing turned by
    // `angle`, `distance` away and `height` above; without a unit the middle of the first two
    // places, looked at across them. Sent to the server as message 0x0F (camera position, point).
    void ViewSpeaker(CUnit* unit, float angle, float distance, float height);
    // 00607EC0 (RET 4). Camera preset `preset` of a phrase ("#camera n"; 1-12, others nothing):
    // views of both (1, 5, 9: ViewSpeaker without a unit, at 3, 2.7 or 2.4 times the distance
    // between the first two places), of the leader (2, 6, 10), the person (3, 7, 11) or the third
    // speaker (4, 8, 12) from various angles; the other speakers are first sunk to height -100
    // (00578760) in 2, 3, 6, 7 and 12.
    void ViewPreset(int preset);
    // 006062C0 (RET 0xC). "#hide <name>": a dialogue person `name` leaves the dialogue (the server
    // hears event 2; its blink stops; its server unit gets flag 0x40000000 back, height 0, its
    // action dropped) and the speakers' list; the third speaker walks to thirdPlace_ (006089D0
    // with `immediate` and `time`) and is forgotten. A name that is no unit removes the shown item
    // of that name (its meshes and texture).
    void HideSpeaker(const char* name, int immediate, int time);

    // 006064E0 (RET 8; ui_briefing_start.cpp). Dialogue topic_ starts: "goodbye" (or no leader)
    // back to state 0; a "constr..." topic opens the camp (screen 5) with constructor N (the topic
    // from its eighth character) when that class template exists. Otherwise the "#show" lines of
    // the text "briefing <topic>" name the people: the first one not the leader is person_, a
    // second third_; without one, state 0. The places: the person's (or, with noWalk_, the
    // leader's) position, the other 2.5 + viewDistance along its facing, the third one beside
    // the middle; their facings across, flat and normalised. With `remember` the people's
    // positions are kept first (personHome_, thirdPlace_). A mover of the third person (or of the
    // person, noWalk_) is dropped, its target kept unless it was waited for; then the leader (or,
    // noWalk_, the person) and the third walk to their places (006089D0, waited, `immediate`).
    // `immediate` starts the phrases at once (StartPhrases), else state 2.
    void StartDialogue(bool immediate, bool remember);
    // 00608450 (ui_briefing_start.cpp). The dialogue ends (the rewards taken, or skipped): the voice
    // stops, the music fades in, the shown items go, every speaker leaves (the server hears event 2,
    // its blink stops, its server unit's flag 0x40000000 back, height 0, action dropped) and the
    // list is emptied; the leader and the person back to height 0, the third speaker walks back to
    // thirdPlace_ (and is forgotten), with noWalk_ the person to personHome_. The first "b.*.<topic>"
    // variable (three parts) completes (00592090 for player 0). State 0, the server hears event 0
    // with the leader; a dialogue that started by itself (briefingStarted_) puts the camera on the
    // leader (message 0x04) and frees it, otherwise it goes back to savedCamera_ / lookAt_.
    void FinishDialogue();
    // 006089D0 (RET 0x28; ui_briefing_people.cpp). `unit` (a client unit) goes to (x, y) at its
    // server unit's height (`z` is not read), facing (dx, dy, dz): `immediate` puts the server
    // unit there at once (sector map, both units turned, the controller repositioned); otherwise,
    // unless the server unit is blocked (0x800000), a mover is put first in movers_ (walking,
    // `waited`) and the server unit's brain walks there (`run`).
    void MovePerson(CUnit* unit, float x, float y, float z, float dx, float dy, float dz, bool waited, bool immediate,
                    int run);
    // 00605DE0 (RET 0xC; ui_briefing_people.cpp). "#show <name>": a person `name` joins the
    // speakers (moved to the end; the server hears event 1; the leader's server unit gets flag
    // 0x40000000; its controller's actionDone the speakers' callback 006023F0). Anything else with
    // `index` >= 0 is a loot item ("_" read as " ", lowercased, the loot table's name index): an
    // item of that name shown already goes (006062C0), then slot `index` % 7 (x 300, 500, 200,
    // 400, 600, 75, 725; y 300 or 500) gets a backdrop and frame for the first seven, the item's
    // figure "initqu<n>item" (tinted from index 8) with its image "quitem<n>" in a new texture,
    // turned over about x; it goes first in shown_. The third argument is not read.
    void ShowItem(const char* name, int index, int unused);
    // Removes the shown items' meshes (and the item texture) from the scene and empties shown_
    // (inlined in 00608450 and 00604C50).
    void ClearShownItems();
    // 005F6BA0 (ui_window_delete.cpp). The destructor body: the speakers (names, storage), the
    // people ids' storage, the shown items (names, nodes, sentinel), the "pers" texts (erased,
    // header), the dialogue text, the movers (nodes, sentinel), the party array (vtable 007402AC,
    // storage), the topic and the topics, then the container's.
    void DestroyBody();
    // 005F6B80 (slot 0, the deleting destructor; ui_window_delete.cpp). DestroyBody, then the
    // memory freed when bit 0 of `flags` is set; returns this.
    CBriefingScreen* BriefingDelete(unsigned flags);
    // 005F6B70 (slot 38, RET 4; ui_small_members.cpp). chatActive_ = `mode`.
    void SetChatActive(int mode);

    // 00604C50 (RET 4; ui_briefing_phrases.cpp). The next phrase of the current text: its lines up
    // to the next "#phrase <speaker> [voice]" are carried out ("#nolips", "#show", "#hide",
    // "#animation n", "#camera n [angle distance height]"); the phrase (up to the next '#') goes,
    // headed by the speaker's name, into dialogueText_ and (unless `skip`) the text window, with
    // its voice "<t|s>\briefing\<topic>\<n>.mp3" and phraseTime_. After the last phrase: state 4
    // and the briefing record's rewards as lines (experience, gold, items given and lost, quests).
    // The camera then views the phrase's speaker, who talks (an animation, blinking unless
    // "#nolips") while the others listen; skipping to the end shows the whole text at once.
    void NextPhrase(bool skip);

    int exit_;                      // +0x164 the exit a "leave_zone" answer leaves through
    bool exitPending_;               // +0x168 Create sets it (Update checks the leader's exit)
    std::uint8_t unknown_169[3];
    int state_;                      // +0x16C dialogue state (above)
    console::CStringArray topics_;   // +0x170 the person's topics (names), "goodbye" last
    mfc::CString topic_;             // +0x184 the dialogue running
    int hoveredTopic_;               // +0x188 the topic row under the mouse, or -1
    bool briefingStarted_;           // +0x18C a dialogue started by itself (602C90)
    std::uint8_t unknown_18D[3];
    UiDwordArray party_;             // +0x190 the party's units (CArray, SetSize 005EBDB0)
    CUnit* leader_;                  // +0x1A4 the unit the camera follows (the hero speaker)
    CUnit* person_;                  // +0x1A8 the person the dialogue is with
    CUnit* third_;                   // +0x1AC a third speaker, or null
    // +0x1B0 the speakers' places and +0x1D4 their facings: [0] person_, [1] leader_, [2] third_.
    Vector3 places_[3];
    Vector3 directions_[3];
    Vector3 savedCamera_;            // +0x1F8 the camera position when the phrases started
    Vector3 lookAt_;                 // +0x204 position + the camera rotation's z axis then
    Vector3 thirdPlace_;             // +0x210 where the third speaker goes when hidden (006062C0)
    Vector3 thirdDirection_;         // +0x21C
    Vector3 personHome_;             // +0x228 where the person stood when the dialogue started (noWalk_)
    Vector3 personHomeDirection_;    // +0x234 and its facing
    BriefingMover* movers_;          // +0x240 the people walking to their places (list header)
    bool firstPhrase_;               // +0x244 set when the phrases start (the first one lasts a second longer)
    std::uint8_t unknown_245[3];
    mfc::CString dialogueText_;      // +0x248 the dialogue's text so far (shown whole when skipped)
    float phraseTime_;               // +0x24C time left of a phrase without a voice
    int voice_;                      // +0x250 the phrase's voice (sound id), -1 none
    CUnit* speaking_;                // +0x254 the speaker of the current phrase (NextPhrase), or null
    std::uint8_t unknown_258[4];
    CUiScrollText* text_;            // +0x25C the dialogue text
    CUiScrollbar* topicBar_;         // +0x260 the topic list's scroll bar
    int topicTop_;                   // +0x264 first topic row shown
    float savedEdgeFade_;            // +0x268 the map edge fade (0079BD34) Create replaced
    PersonTextMap personTexts_;      // +0x26C "pers" texts by text key (map<int, CString>)
    std::uint32_t personTextCount_;  // +0x270 the map's node count
    std::uint8_t unknown_274[4];
    BriefingShownItem* shown_;       // +0x278 the items shown (list header)
    bool immediate_;                 // +0x27C the dialogue's people are placed at once (006064E0's first argument)
    bool noWalk_;                    // +0x27D the zone's map record +0x44: the leader does not walk
    bool autosave_;                  // +0x27E Create sets it unless loading or back from the camp
    std::uint8_t unknown_27F;
    CUiWindow* chat_;                // +0x280 a network client's chat window (608D20), else null
    stlp::vector<int> people_;       // +0x284 ids of the local player's objects that are text keys (Create)
    float markPhase_;                // +0x290 Update adds the frame time, kept below 1
    BriefingSpeaker* speakers_;      // +0x294 the people in the dialogue (vector begin, end, capacity)
    BriefingSpeaker* speakersEnd_;   // +0x298
    BriefingSpeaker* speakersCapacity_;   // +0x29C
    int chatActive_;                 // +0x2A0 the chat window takes the input
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
