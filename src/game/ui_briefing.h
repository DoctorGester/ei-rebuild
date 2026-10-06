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
#include "ui.h"
#include "ui_camp.h"

#include "lib/mfc.h"

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
    int state;                       // +0x20 0 done, 1 walking, 2 turning
    CEngineObject* person;           // +0x24 the client object
    bool waited;                     // +0x28 the dialogue waits for this one
    std::uint8_t unknown_29[3];
    float time;                      // +0x2C
};
static_assert(offsetof(BriefingMover, person) == 0x24);
static_assert(offsetof(BriefingMover, time) == 0x2C);

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

    // 00608B60 (RET 4). Opens the topics of `person` (00607360): none leaves state 0; otherwise
    // the leader stops (unless noWalk_), five topic rows (regions at x 210..570 from y 440, 24
    // high), "goodbye" appended, the scroll bar's range and position 0, the list drawn (6071E0),
    // state 1.
    void OpenTopics(CEngineObject* person);
    // 006071E0. Draws the topic rows into surface 0 (from topicTop_, up to five, at x 15..380,
    // 24 high from y 12): "topic_prefix" text + " " + the topic's title (006078C0), white for
    // hoveredTopic_, else 0xA7D7E4.
    void DrawTopics();

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
    CUnit* leader_;                  // +0x1A4 the unit the camera follows
    std::uint8_t unknown_1A8[0x240 - 0x1A8];
    BriefingMover* movers_;          // +0x240 the people walking to their places (list header)
    std::uint8_t unknown_244[0x24C - 0x244];
    float phraseTime_;               // +0x24C time left of a phrase without a voice
    int voice_;                      // +0x250 the phrase's voice (sound id), -1 none
    CUnit* rewardUnit_;              // +0x254
    std::uint8_t unknown_258[4];
    CUiWindow* text_;                // +0x25C the dialogue text (a CUiScrollText)
    CUiScrollbar* topicBar_;         // +0x260 the topic list's scroll bar
    int topicTop_;                   // +0x264 first topic row shown
    float savedEdgeFade_;            // +0x268 the map edge fade (0079BD34) Create replaced
    PersonTextMap personTexts_;      // +0x26C "pers" texts by text key (map<int, CString>)
    std::uint8_t unknown_270[0x27D - 0x270];
    bool noWalk_;                    // +0x27D the zone's map record +0x44: the leader does not walk
    bool unknown_27E;                // +0x27E
    std::uint8_t unknown_27F;
    CUiWindow* chat_;                // +0x280 a network client's chat window (608D20), else null
    std::uint8_t unknown_284[0x2A0 - 0x284];
    int chatActive_;                 // +0x2A0 the chat window takes the input
};
static_assert(offsetof(CBriefingScreen, exit_) == 0x164);
static_assert(offsetof(CBriefingScreen, state_) == 0x16C);
static_assert(offsetof(CBriefingScreen, topics_) == 0x170);
static_assert(offsetof(CBriefingScreen, topic_) == 0x184);
static_assert(offsetof(CBriefingScreen, hoveredTopic_) == 0x188);
static_assert(offsetof(CBriefingScreen, party_) == 0x190);
static_assert(offsetof(CBriefingScreen, leader_) == 0x1A4);
static_assert(offsetof(CBriefingScreen, movers_) == 0x240);
static_assert(offsetof(CBriefingScreen, phraseTime_) == 0x24C);
static_assert(offsetof(CBriefingScreen, text_) == 0x25C);
static_assert(offsetof(CBriefingScreen, topicTop_) == 0x264);
static_assert(offsetof(CBriefingScreen, personTexts_) == 0x26C);
static_assert(offsetof(CBriefingScreen, noWalk_) == 0x27D);
static_assert(offsetof(CBriefingScreen, chat_) == 0x280);
static_assert(offsetof(CBriefingScreen, chatActive_) == 0x2A0);
static_assert(sizeof(CBriefingScreen) == 0x2A4);
