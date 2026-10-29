#pragma once

// The base / briefing screen (interface screen 3). It is the
// screen of a "brief" zone (map.txt type brief, campaign mode 3: the bases bz1g ..): the party walks around
// the base, a click on a person opens the person's topics, a topic runs its dialogue (phrases, then
// rewards). See docs/notes/ui.md, "Briefing / base screen". Dialogue states: 0 walking, 1 choosing a topic,
// 2 the dialogue starting (people still walking to their places), 3 phrases, 4 rewards.

#include "maths.h"
#include "object_tables.h"
#include "ui_camp.h"

#include <cstdint>
#include <string>

#include <cstddef>
#include <vector>

// A person walking to the place a dialogue gives them.
struct BriefingMover {
    Vector3 target;
    Vector3 direction;               // the facing at the target
    int state;                       // kDone, kWalking or kTurning
    CUnit* person;                   // the client unit
    bool waited;                     // the dialogue waits for this one
    float time;                      // seconds on the way (Update gives up after 30)

    static constexpr int kDone = 0;
    static constexpr int kWalking = 1;
    static constexpr int kTurning = 2;
};

// A person taking part in the dialogue.
struct BriefingSpeaker {
    CUnit* unit;                     // the client unit
    std::string name;               // as the dialogue's commands give it
};

// An item shown in the dialogue ("#show <name>" for what is not a unit). The meshes are the window's scene
// objects; the item's texture is one of its referenced objects.
struct BriefingShownItem {
    std::string name;
    UiMesh* item;
    EngineObject* back;
    EngineObject* frame;
};

// A network client's player list on the base screen: the players' faces along the top right, a surface under
// them with each one's status and offer. Multiplayer only.
class ChatWindow : public UiWindow {
public:
    explicit ChatWindow(Interface3D* ui);
    // Every fourth face first makes a 256 x 256 texture; `texture` is copied into the newest texture's quarter
    // (count % 4) and a sprite of `model` shows it, or without the model a plain rectangle.
    void AddPortrait(const std::string& model, std::string_view texture);
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
    void LeftDown(int buttons, int x, int y) override;
    void MouseMove(int buttons, int x, int y) override;

    int listed = 0;
    int portraits = 0;
    std::vector<HeldRef<RefObject>> players;    // PlayerMessage records
    std::vector<std::string> texts;
};

class BriefingScreen : public UiContainer {
public:
    explicit BriefingScreen(Interface3D* ui);
    void Draw(SceneRenderer* pass) override;
    // The camera free again (flag 0x40, flags 0x100 / 0x200 cleared).
    void CloseScreen() override;
    bool ReadyToStopPolling() override { return true; }
    // A network client's chat window shows the players again (not for 0).
    void PlayersChanged(int show) override;
    // The chat window's pick mode (chatActive).
    void PlayerList(int mode) override { chatActive = mode; }
    // 0x10001 leaves through exit; 0x20001 (game over: load) opens the load window; 0x20002, 0x90001,
    // 0x100001 close every window, drop the sessions and leave the game. A network client then shows its chat
    // window again.
    void MessageResult(int result) override;
    void UnitLeft(CUnit* unit) override;
    // `unit` goes into party after the members whose unit records (the local player's) come before its record,
    // or last without one. The first record's unit becomes the leader, and the first "b.<zone>..." variable
    // equal to 1 with three parts starts its topic (the third part) at once.
    void UnitJoined(CUnit* unit) override;
    void GameOver(bool) override;
    // The interface command "briefing <name>": dialogue `name` starts at once.
    void ShowBriefing(std::string_view name) override;
    // One zero dword.
    void SaveState() override;
    // A saved game's screen part is dropped.
    void CreateScreen() override;
    // The movers walk, turn or give up after 30 s (put at their place); then by state: 0 / 1 the people with
    // open topics are marked and the leader's pending exit is left once it can be; 2 the phrases start once no
    // waited mover walks; 3 the next phrase when the voice ends or phraseTime runs out.
    void Update() override;
    // Every person with a client unit is marked (dialogue event 1) when a "b.<person>.<topic>" variable is 1
    // for a topic that is not "constr...", else unmarked (event 2).
    void MarkPeople();
    // Not when exit's zone is "none" or visited ("z.<zone>" not 1).
    void LeaveIfExitOpen();

    // With the chat window active it gets the event; otherwise by state: 0 an idle person under the cursor
    // (controller command 0 or 0xB and next command 9) opens its topics, else the leader walks to the picked
    // ground point (not when noWalk); 1 a topic row starts that topic; 3 the next phrase; 4 the rewards are
    // taken.
    void LeftDown(int buttons, int x, int y) override;
    // A person who is not one of the moving ones, or with the dialogue running, is handled as a click; on the
    // ground the leader runs there.
    void LeftDouble(int buttons, int x, int y) override;
    // See docs/notes/ui.md, "Behaviour moved out of header comments".
    void MouseMove(int buttons, int x, int y) override;
    // With the chat window active only Esc, which gives the input back. See docs/notes/ui.md, "Behaviour moved
    // out of header comments".
    void KeyDown(int key, int repeat, int flags) override;

    // None leaves state 0; otherwise the leader stops (unless noWalk), "goodbye" is appended, state 1.
    void OpenTopics(EngineObject* person);
    // "topic_prefix" text + " " + the topic's title, white for hoveredTopic, else 0xA7D7E4.
    void DrawTopics();
    // Every "b.<person>.<topic>" variable equal to 1 (bit compare), in the variable table's order (a network
    // client sorts them), and last the last "constr..." one found.
    void FindTopics(int personId);
    // The first line of the text "briefing <topic>" (lowercased; "constr..." topics share "briefing constr"),
    // empty without one.
    std::string TopicTitle(std::string_view topic);

    // Without a text "briefing <topic>" back to state 0. Otherwise the server hears dialogue event 3, the
    // camera is kept (savedCamera, lookAt), state 3.
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
    // walks (or runs) to thirdPlace and is forgotten. A name that is no unit removes the shown item of that name.
    void HideSpeaker(std::string_view name, bool immediate, bool run);

    // "goodbye" (or no leader) back to state 0; a "constr..." topic opens the camp (screen 5) with constructor N
    // when that class template exists. See docs/notes/ui.md, "Behaviour moved out of header comments".
    void StartDialogue(bool immediate, bool remember);
    // The first "b.*.<topic>" variable (three parts) completes (for player 0). A dialogue that started by
    // itself puts the camera on the leader and frees it, otherwise it goes back to savedCamera / lookAt. See
    // docs/notes/ui.md, "Behaviour moved out of header comments".
    void FinishDialogue();
    // To `place` (its z is not read: the server unit's height is kept), facing `direction`. Unless `immediate`, a
    // server unit that is blocked (0x800000) does not move.
    void MovePerson(CUnit* unit, const Vector3& place, const Vector3& direction, bool waited, bool immediate, bool run);
    // "#show <name>": a person joins the speakers (the leader's server unit gets flag 0x40000000). Anything
    // else with `index` >= 0 is a loot item. See docs/notes/ui.md, "Behaviour moved out of header comments".
    void ShowItem(std::string_view name, int index);
    void ClearShownItems();

    // Lines up to the next "#phrase <speaker> [voice]" are carried out ("#nolips", "#show", "#hide",
    // "#animation n", "#camera n [angle distance height]"). After the last phrase: state 4 and the briefing
    // record's rewards. See docs/notes/ui.md, "Behaviour moved out of header comments".
    void NextPhrase(bool skip);

    int exit = 0;                       // the exit a "leave_zone" answer leaves through
    bool exitPending = false;
    int state = 0;                       // dialogue state (above)
    std::vector<std::string> topics;    // "goodbye" last
    std::string topic;               // the dialogue running
    int hoveredTopic = -1;                // or -1
    bool briefingStarted = false;            // a dialogue started by itself
    std::vector<CUnit*> party;
    CUnit* leader = nullptr;                   // the unit the camera follows (the hero speaker)
    CUnit* person = nullptr;                   // the person the dialogue is with
    CUnit* third = nullptr;                    // or null
    // [0] person, [1] leader, [2] third.
    Vector3 places[3] = {};
    Vector3 directions[3] = {};
    Vector3 savedCamera = {};             // the camera position when the phrases started
    Vector3 lookAt = {};                  // position + the camera rotation's z axis then
    Vector3 thirdPlace = {};              // where the third speaker goes when hidden
    Vector3 thirdDirection = {};
    Vector3 personHome = {};              // where the person stood when the dialogue started (noWalk)
    Vector3 personHomeDirection = {};
    std::vector<BriefingMover> movers;   // the newest first
    bool firstPhrase = false;                // the first phrase lasts a second longer
    std::string dialogueText;        // shown whole when skipped
    float phraseTime = 0.0f;                // time left of a phrase without a voice
    int voice = -1;                       // sound id, -1 none
    CUnit* speaking = nullptr;                 // or null
    UiScrollText* text = nullptr;
    UiScrollbar* topicBar = nullptr;
    int topicTop = 0;                    // first topic row shown
    float savedEdgeFade = 0.0f;             // the map edge fade Create replaced
    std::map<int, std::string> personTexts;       // "pers" texts by text key
    std::vector<BriefingShownItem> shown;   // the newest first
    bool immediate = false;                  // the dialogue's people are placed at once
    bool noWalk = false;                     // the zone's map record's cage: the leader does not walk
    bool autosave = false;                   // unless loading or back from the camp
    ChatWindow* chat = nullptr;               // a network client's only
    std::vector<int> people;        // ids of the local player's objects that are text keys
    float markPhase = 0.0f;                 // kept below 1
    std::vector<BriefingSpeaker> speakers;
    int chatActive;                  // the chat window takes the input
};
// What a speaker's controller calls when its dialogue animation has finished (its actionDone).
void SpeakerAnimationDone(CUnit* serverUnit);
