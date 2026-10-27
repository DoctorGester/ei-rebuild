#pragma once

// The base / briefing screen (interface screen 3). It is the
// screen of a "brief" zone (map.txt type brief, campaign mode 3: the bases bz1g ..): the party walks around
// the base, a click on a person opens the person's topics, a topic runs its dialogue (phrases, then
// rewards). See docs/notes/ui.md, "Briefing / base screen". Dialogue states: 0 walking, 1 choosing a topic,
// 2 the dialogue starting (people still walking to their places), 3 phrases, 4 rewards.

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
#include <string>

// A person walking to the place a dialogue gives them.
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

// A person taking part in the dialogue.
struct BriefingSpeaker {
    CUnit* unit;                     // the client unit
    std::string name;               // as the dialogue's commands give it
    bool spareFlag;                  // false when added; only copied with the entry (erase), never tested
    std::uint8_t unused_09[3];
};

// An item shown in the dialogue ("#show <name>" for what is not a unit). The meshes are the window's scene
// objects; the item's texture is one of its referenced objects.
struct BriefingShownItem {
    BriefingShownItem* next;
    BriefingShownItem* prev;
    std::string name;
    CUiMesh* item;
    CEngineObject* back;
    CEngineObject* frame;
};


// A copy of the local player's variables whose names start with a prefix (100 buckets), walked in the table's
// order.
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

// A network client's player list on the base screen: the players' faces along the top right, a surface under
// them with each one's status and offer. Multiplayer only.
class CChatWindow : public CUiWindow {
public:
    explicit CChatWindow(CInterface3D* ui);
    ~CChatWindow() override;
    // Every fourth face first makes a 256 x 256 texture; `texture` is copied into the newest texture's quarter
    // (count % 4) and a sprite of `model` shows it, or without the model a plain rectangle.
    void AddPortrait(const char* model, const char* texture);
    // The client's players (only while its binding is live; its own player first), each with a face from its
    // unit prototype ("infa<model>me<hair+1>face", "face<m|f><skin, 2 digits>") and its name as the region's
    // tip. The sex letter carries over from the previous player when the model is neither "unhuma" nor
    // "unhufe".
    void ShowPlayers();
    // While the client has a live trade it asks for screen 9 (the exchange); otherwise each listed player's
    // status (state 0..4: texts 0, 1, 2, 3, 3) and "we offer" or "they offer".
    void Update() override;
    // With the active window's player pick 1 / 2 the player's serial goes to the server as command 4 / 5 and
    // the pick ends; otherwise, unless it is our own player, our offer to it is dropped when it is its partner,
    // else one is started.
    void OnLButtonDown(int buttons, int x, int y);
    void OnMouseMove(int buttons, int x, int y);
    int LeftDown(int b, int x, int y) override { return OnLButtonDown(b, x, y), 0; }
    int MouseMove(int b, int x, int y) override { return OnMouseMove(b, x, y), 0; }

    int listed_;
    int portraits_;
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
    BOOL Draw(CSceneRenderer* pass) override;
    void CloseScreen() override { Close(); }
    void MessageResult(int result) override { OnMessageResult(static_cast<std::uint32_t>(result)); }
    void UnitJoined(CUnit* unit) override { OnUnitJoined(unit); }
    void UnitLeft(CUnit* unit) override { OnUnitLeft(unit); }
    void GameOver(int won) override { OnGameOver(won); }
    void ShowBriefing(const char* name) override { OnBriefing(name); }
    bool ReadyToStopPolling() override { return true; }
    void PlayersChanged(int show) override { ShowChat(static_cast<char>(show)); }
    void PlayerList(int mode) override { SetChatActive(mode); }
    int LeftDown(int b, int x, int y) override { return OnLeftDown(b, x, y), 0; }
    int MouseMove(int b, int x, int y) override { return OnMouseMove(b, x, y), 0; }
    int LeftDouble(int b, int x, int y) override { return OnLeftDouble(b, x, y), 0; }
    int KeyDown(int k, int r, int f) override { return OnKeyDown(k, r, f), 0; }
    // The camera free again (flag 0x40, flags 0x100 / 0x200 cleared).
    void Close();
    // 0x10001 leaves through exit_; 0x20001 (game over: load) opens the load window; 0x20002, 0x90001,
    // 0x100001 close every window, drop the sessions and leave the game. A network client then shows its chat
    // window again.
    void OnMessageResult(std::uint32_t result);
    void OnUnitLeft(CUnit* unit);
    // `unit` goes into party_ after the members whose unit records (the local player's) come before its record,
    // or last without one. The first record's unit becomes the leader, and the first "b.<zone>..." variable
    // equal to 1 with three parts starts its topic (the third part) at once.
    void OnUnitJoined(CUnit* unit);
    void OnGameOver(int unused);
    // The interface command "briefing <name>": dialogue `name` starts at once.
    void OnBriefing(const char* name);
    void ShowChat(char show);
    // One zero dword.
    void SaveState() override;
    // A saved game's screen part is dropped.
    void CreateScreen() override;
    // The movers walk, turn or give up after 30 s (put at their place); then by state: 0 / 1 the people with
    // open topics are marked and the leader's pending exit is left once it can be; 2 the phrases start once no
    // waited mover walks; 3 the next phrase when the voice ends or phraseTime_ runs out.
    void Update() override;
    // Every person with a client unit is marked (dialogue event 1) when a "b.<person>.<topic>" variable is 1
    // for a topic that is not "constr...", else unmarked (event 2).
    void MarkPeople();
    // Not when exit_'s zone is "none" or visited ("z.<zone>" not 1).
    void LeaveIfExitOpen();

    // With the chat window active it gets the event; otherwise by state: 0 an idle person under the cursor
    // (controller command 0 or 0xB and next command 9) opens its topics, else the leader walks to the picked
    // ground point (not when noWalk_); 1 a topic row starts that topic; 3 the next phrase; 4 the rewards are
    // taken.
    void OnLeftDown(int buttons, int x, int y);
    // A person who is not one of the moving ones, or with the dialogue running, is handled as a click; on the
    // ground the leader runs there.
    void OnLeftDouble(int buttons, int x, int y);
    // See docs/notes/ui.md, "Behaviour moved out of header comments".
    void OnMouseMove(int buttons, int x, int y);
    // With the chat window active only Esc, which gives the input back. See docs/notes/ui.md, "Behaviour moved
    // out of header comments".
    void OnKeyDown(int key, int repeat, int flags);

    // None leaves state 0; otherwise the leader stops (unless noWalk_), "goodbye" is appended, state 1.
    void OpenTopics(CEngineObject* person);
    // "topic_prefix" text + " " + the topic's title, white for hoveredTopic_, else 0xA7D7E4.
    void DrawTopics();
    // Every "b.<person>.<topic>" variable equal to 1 (bit compare), in the variable table's order (a network
    // client sorts them), and last the last "constr..." one found.
    void FindTopics(int personId);
    // The first line of the text "briefing <topic>" (lowercased; "constr..." topics share "briefing constr"),
    // empty without one. `out` is constructed here.
    mfc::CString* TopicTitle(mfc::CString* out, const char* topic);

    void LoadPersonTexts();

    // Without a text "briefing <topic_>" back to state 0. Otherwise the server hears dialogue event 3, the
    // camera is kept (savedCamera_, lookAt_), state 3.
    void StartPhrases();
    // Talking: 3 for a third speaker, else a random one of 1, 6, 7, 8 for a human ("unhuma") and 1 for others;
    // listening a random one of 2, 0xA, 0xC, 0xD for a human and 2 for others (skill animations).
    void PlaySpeakerAnimation(CUnit* unit, bool talking, bool third);
    // From the unit's eyes around its facing turned by `angle`, `distance` away and `height` above; without a
    // unit the middle of the first two places, looked at across them. Sent to the server as message 0x0F.
    void ViewSpeaker(CUnit* unit, float angle, float distance, float height);
    // "#camera n" (1-12, others nothing): views of both (1, 5, 9), the leader (2, 6, 10), the person (3, 7, 11)
    // or the third speaker (4, 8, 12) from various angles; the other speakers are first sunk to height -100 in
    // 2, 3, 6, 7 and 12.
    void ViewPreset(int preset);
    // "#hide <name>": the server hears event 2; its server unit gets flag 0x40000000 back. The third speaker
    // walks to thirdPlace_ and is forgotten. A name that is no unit removes the shown item of that name.
    void HideSpeaker(const char* name, int immediate, int time);

    // "goodbye" (or no leader) back to state 0; a "constr..." topic opens the camp (screen 5) with constructor N
    // when that class template exists. See docs/notes/ui.md, "Behaviour moved out of header comments".
    void StartDialogue(bool immediate, bool remember);
    // The first "b.*.<topic>" variable (three parts) completes (for player 0). A dialogue that started by
    // itself puts the camera on the leader and frees it, otherwise it goes back to savedCamera_ / lookAt_. See
    // docs/notes/ui.md, "Behaviour moved out of header comments".
    void FinishDialogue();
    // `z` is not read: the server unit's height is kept. Unless `immediate`, a server unit that is blocked
    // (0x800000) does not move.
    void MovePerson(CUnit* unit, float x, float y, float z, float dx, float dy, float dz, bool waited, bool immediate,
                    int run);
    // "#show <name>": a person joins the speakers (the leader's server unit gets flag 0x40000000). Anything
    // else with `index` >= 0 is a loot item. See docs/notes/ui.md, "Behaviour moved out of header comments".
    void ShowItem(const char* name, int index, int unused);
    void ClearShownItems();
    ~CBriefingScreen() override;
    void SetChatActive(int mode);

    // Lines up to the next "#phrase <speaker> [voice]" are carried out ("#nolips", "#show", "#hide",
    // "#animation n", "#camera n [angle distance height]"). After the last phrase: state 4 and the briefing
    // record's rewards. See docs/notes/ui.md, "Behaviour moved out of header comments".
    void NextPhrase(bool skip);

    int exit_;                      // the exit a "leave_zone" answer leaves through
    bool exitPending_;
    std::uint8_t unused_169[3];
    int state_;                      // dialogue state (above)
    console::CStringArray topics_;   // "goodbye" last
    std::string topic_;              // the dialogue running
    int hoveredTopic_;               // or -1
    bool briefingStarted_;           // a dialogue started by itself
    std::uint8_t unused_18D[3];
    mfc::CArray<CUnit*> party_;
    CUnit* leader_;                  // the unit the camera follows (the hero speaker)
    CUnit* person_;                  // the person the dialogue is with
    CUnit* third_;                   // or null
    // [0] person_, [1] leader_, [2] third_.
    Vector3 places_[3];
    Vector3 directions_[3];
    Vector3 savedCamera_;            // the camera position when the phrases started
    Vector3 lookAt_;                 // position + the camera rotation's z axis then
    Vector3 thirdPlace_;             // where the third speaker goes when hidden
    Vector3 thirdDirection_;
    Vector3 personHome_;             // where the person stood when the dialogue started (noWalk_)
    Vector3 personHomeDirection_;
    BriefingMover* movers_;
    bool firstPhrase_;               // the first phrase lasts a second longer
    std::uint8_t unused_245[3];
    std::string dialogueText_;       // shown whole when skipped
    float phraseTime_;               // time left of a phrase without a voice
    int voice_;                      // sound id, -1 none
    CUnit* speaking_;                // or null
    std::uint8_t unused_258[4];
    CUiScrollText* text_;
    CUiScrollbar* topicBar_;
    int topicTop_;                   // first topic row shown
    float savedEdgeFade_;            // the map edge fade Create replaced
    PersonTextMap personTexts_;      // "pers" texts by text key
    BriefingShownItem* shown_;
    bool immediate_;                 // the dialogue's people are placed at once
    bool noWalk_;                    // the zone's map record's cage: the leader does not walk
    bool autosave_;                  // unless loading or back from the camp
    std::uint8_t unused_27F;
    CChatWindow* chat_;              // a network client's only
    stlp::vector<int> people_;       // ids of the local player's objects that are text keys
    float markPhase_;                // kept below 1
    BriefingSpeaker* speakers_;
    BriefingSpeaker* speakersEnd_;
    BriefingSpeaker* speakersCapacity_;
    int chatActive_;                 // the chat window takes the input
};
static_assert(offsetof(CBriefingScreen, exit_) == 0x164);
static_assert(offsetof(CBriefingScreen, state_) == 0x16C);
static_assert(offsetof(CBriefingScreen, topics_) == 0x170);
static_assert(offsetof(CBriefingScreen, topic_) == 0x184);
