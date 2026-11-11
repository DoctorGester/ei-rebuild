// The briefing / base screen's main virtuals (BriefingScreen, ui_briefing.h): close, message
// box answers, a unit leaving the party, game over, the "briefing" command, the chat window and the
// saved state; at the end the "pers <name>" texts. See docs/notes/ui.md, "Briefing / base screen".
#include "game/ui/ui_briefing.h"
#include "game/ui/ui_camp.h"

#include "game/world/camera.h"
#include "game/combat/combat.h"
#include "engine/core/core_maths.h"
#include "engine/core/core_streams.h"
#include "engine/core/core_transform.h"
#include "game/app/game_flow.h"
#include "game/app/singletons.h"
#include "game/items/item_db.h"
#include "lib/vecmath.h"
#include "game/map/map_database.h"
#include "game/app/options.h"
#include "game/players/party.h"
#include "game/players/player.h"
#include "engine/data/text_manager.h"
#include "engine/core/text_split.h"
#include "game/ui/ui.h"
#include "game/ui/ui_mesh.h"
#include "game/ui/ui_saveload.h"
#include "game/ui/ui_message_box.h"
#include "game/units/unit.h"
#include "game/ai/unit_ai.h"
#include "game/units/unit_client_models.h"
#include "game/world/world.h"
#include "game/sound/sound.h"
#include "game/terrain/terrain.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/intro_sort.h"
#include "lib/str.h"
#include "lib/text_case.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <new>
#include <string>
#include <vector>

namespace {

constexpr std::uint32_t kLeaveAnswer = 0x90001;
constexpr std::uint32_t kLoadWindowClosed = 0x100001;

constexpr const char* kZoneTopicFormat = "b.%s";

constexpr const char* kPersonPattern = "pers *";
constexpr int kPersonPrefixLength = 5;                                               // "pers "

} // namespace

void BriefingScreen::CloseScreen() {
    options::g_options.borderFogDistance = savedEdgeFade;
    g_clientWorld.sounds->StopAll(true);
    g_clientWorld.terrain->StopAmbience();
    std::uint32_t& cameraFlags = g_camera.flags;
    cameraFlags = (cameraFlags & ~(Camera::kBounded | Camera::kAlternate)) | Camera::kFree;
    g_interface.briefing = nullptr;
}

void BriefingScreen::MessageResult(std::uint32_t result) {
    switch (result) {
    case kLeaveZoneYes:
        g_gameFlow.LeaveThroughExit(exit);
        break;
    case kGameOverLoad: {
        g_interface.PushWindow(std::make_unique<SaveLoadWindow>(false, false), WindowBackdrop::FirstWindow);
        break;
    }
    case kGameOverQuit:
    case kLeaveAnswer:
    case kLoadWindowClosed:
        g_interface.closeAll = true;
        g_gameFlow.DropSessions();
        g_gameFlow.CloseSessions();
        break;
    default:
        break;
    }
    if (IsMultiplayerClient()) PlayersChanged(1);
}

void BriefingScreen::UnitJoined(Unit* unit) {
    Party* player = LocalPlayerData();
    int count = lib::Count(player->members);
    if (count == 0) return;
    int place = 0;
    int record = 0;
    for (; record < count; ++record) {
        int unitId = static_cast<int>(player->members[record].unitId);
        if (unit->id == unitId) break;
        if (place < std::ssize(party) && party[place]->id == unitId) ++place;
    }
    if (record < count) {
        // Insert `unit` at `place` (the slot is zeroed, then written); the
        // original then looks the unit up in the server world without using the result.
        const int size = lib::Count(party);
        if (place >= size) {
            party.resize(place + 1);
            party[place] = unit;
        } else {
            party.insert(party.begin() + place, unit);
        }
    } else {
        party.push_back(unit);
    }
    if (unit->id != static_cast<int>(player->members[0].unitId)) return;

    leader = unit;
    g_clientWorld.Select(unit);
    g_camera.SetTarget(unit);
    MapRecord* zone = g_mapDatabase.FindMap(LocalPlayerZone());
    std::string prefix = str::Format(kZoneTopicFormat, zone->name);
    const auto open = OpenTopicNames(prefix);
    if (open.empty()) return;
    topic = open.front()[2];
    briefingStarted = true;
    StartDialogue(true, true);
}

void BriefingScreen::UnitLeft(Unit* unit) {
    if (unit == leader) leader = nullptr;
    const auto member =
        std::find_if(party.begin(), party.end(), [unit](const Unit* other) { return other->id == unit->id; });
    if (member != party.end()) party.erase(member);
}

void BriefingScreen::GameOver(bool) {
    g_interface.PlayInterfaceSound("buttons\\gameover.wav");
    g_interface.PushWindow(std::make_unique<MessageWindow>("game_over", "game_over_msg", MessageQuestion::Second, MessageButtons::OkCancel), WindowBackdrop::FirstWindow);
}

void BriefingScreen::ShowBriefing(std::string_view name) {
    topic = std::string(name);
    StartDialogue(true, true);
}

void BriefingScreen::PlayersChanged(int show) {
    if (IsMultiplayerClient() && show && chat) chat->ShowPlayers();
}

void BriefingScreen::SaveState() {
    int zero = 0;
    g_saveStream.WriteValue(zero);
}

// --- Topics ------------------------------------------------------------------------------------------

// The briefing / base screen's topics: a person's topics from the "b." variables and a topic's
// title (BriefingScreen, ui_briefing.h). See docs/notes/ui.md, "Briefing / base screen".

namespace {

constexpr const char* kConstructorTopic = "constr";
constexpr const char* kBriefingSection = "briefing ";

constexpr int kConstructorPrefixLength = 6;
constexpr const char* kZoneVariablePrefix = "z.";

} // namespace

void StopTalking(Unit* speaker) {
    UnitClientSpecific* model = speaker->ClientController();
    model->blinks = 0;
    model->BlinkOpen(true);
}

void StartSpeakerAnimation(Unit* unit, SkillAnimation number) {
    auto* serverUnit = g_serverWorld.FindUnit(unit->id);
    if (!serverUnit) return;
    auto* controller = serverUnit->ServerController();
    controller->StopAction();
    controller->actionDone = SpeakerAnimationDone;
    if (FigureAnimation* animation = unit->AsObject3D()->FindSkillAnimation(number))
        controller->PlayAnimation(animation->name);
}

void ReleaseSpeaker(Unit* unit) {
    auto* serverUnit = g_serverWorld.FindUnit(unit->id);
    if (!serverUnit) return;
    serverUnit->ShowHelmet(true);
    g_serverWorld.SetUnitHeight(unit->id, 0.0f);
    auto* controller = serverUnit->ServerController();
    controller->actionDone = nullptr;
    controller->StopAction();
}

bool ZoneVisited(std::string_view zone) {
    std::string variable = kZoneVariablePrefix;
    variable += zone;
    return PlayerZeroVariable(variable) == 1.0f;
}

std::vector<std::vector<std::string>> OpenTopicNames(std::string_view prefix) {
    std::vector<std::vector<std::string>> open;
    for (auto& node : PlayerZeroVariables(prefix)) {
        if (node.second != 1.0f) continue;
        std::vector<std::string> parts = text::Split(node.first, '.');
        if (parts.size() == kTopicNameParts) open.push_back(std::move(parts));
    }
    return open;
}

void BriefingScreen::FindTopics(int personId) {
    std::string name;
    std::string constructor;
    std::vector<std::string> found;
    for (const auto& parts : OpenTopicNames(kTopicPrefix)) {
        if (TextKeyFromName(parts[1]) != personId) continue;
        name = parts[2];
        bool isConstructor;
        {
            std::string start = str::Left(name, kConstructorPrefixLength);
            isConstructor = text::CompareIgnoringCase(start,
                                            kConstructorTopic) == 0;
        }
        if (isConstructor) {
            constructor = name;
            continue;
        }
        found.push_back(name);
    }
    std::vector<std::string>& list = found;
    if (IsMultiplayerClient()) {
        // By the text ignoring case: topics that differ only in case (or reached through two spellings of the
        // person's name) keep the game's sort's order.
        lib::IntroSort(list.data(), list.data() + list.size(), [](const std::string& a, const std::string& b) {
            return text::CompareIgnoringCase(a.data(), b.data()) < 0;
        });
    }
    topics.clear();
    for (auto& entry : list) topics.push_back(entry);
    if (constructor.size() != 0) topics.push_back(constructor);
}

std::string BriefingScreen::TopicTitle(std::string_view topicKey) {
    std::string title;
    std::string key(topicKey);
    str::MakeLower(key);
    if (str::Compare(str::Left(key, kConstructorPrefixLength), kConstructorTopic) == 0) key = kConstructorTopic;
    TextManager* texts = g_clientWorld.texts.get();
    std::string section = kBriefingSection;
    section += key;
    if (texts->TextManager::Open(section)) title = texts->ReadLine(true);
    return title;
}

// --- People ------------------------------------------------------------------------------------------

// The briefing / base screen's dialogue people and items (BriefingScreen, ui_briefing.h): a
// person walking to a place and a dialogue's "#show" line. See
// docs/notes/ui.md, "Briefing / base screen".

namespace {

constexpr float kTurnedOver = 3.1415927f;   // pi
constexpr const char* kItemFigureFormat = "initqu%ditem";
constexpr const char* kItemImageFormat = "quitem%04d";
// The seven speakers' places (800 x 600 layout).
constexpr int kSlotXs[7] = /*0074111C*/ {300, 500, 200, 400, 600, 75, 725};
constexpr int kSlotYs[7] = /*00741138*/ {300, 300, 300, 300, 300, 500, 500};

constexpr int kEventJoins = 1;
constexpr int kSlots = 7;
constexpr float kSlotBack = 60.0f;
constexpr float kSlotFrame = 65.0f;
constexpr float kItemDepth = 12.0f;
constexpr std::uint32_t kSlotBackdrop = 0xA0000000;
constexpr std::uint32_t kTintedItem = 0x82FFFFFF;
constexpr int kTintFrom = 8;             // items from index 8 on are tinted
constexpr std::uint32_t kUnitBlocked = 0x800000;

} // namespace

void BriefingScreen::MovePerson(Unit* unit, const Vector3& place, const Vector3& direction, bool waited,
                                 bool immediately, bool run) {
    auto* serverUnit = g_serverWorld.FindUnit(unit->id);
    if (!serverUnit) return;
    auto* controller = serverUnit->ServerController();
    if (!controller) return;
    Vector3 target{place.x, place.y, serverUnit->localPosition.z};
    if (immediately) {
        g_serverWorld.aiMap.sectorMap.Move(serverUnit, target);
        Vector3 facing = NormalisedUnlessZero(direction);
        serverUnit->SetHeading(facing);
        unit->SetHeading(facing);
        controller->ResyncWithUnit();
        return;
    }
    if (serverUnit->flags & kUnitBlocked) return;
    movers.insert(movers.begin(), BriefingMover{target, direction, BriefingMover::kWalking, unit, waited, 0.0f});
    controller->brain.Walk(target, run);
}

void BriefingScreen::ShowItem(std::string_view name, int index) {
    auto* unit = g_clientWorld.FindUnit(TextKeyFromName(name));
    if (unit) {
        // A person joins the speakers (at the end, once).
        auto entry = std::find_if(speakers.begin(), speakers.end(), [unit](const BriefingSpeaker& s) { return s.unit == unit; });
        if (entry != speakers.end()) speakers.erase(entry);
        speakers.push_back(BriefingSpeaker{unit, std::string(name)});
        auto* player = g_serverWorld.GetPlayer(0);
        if (player->Server()) player->Server()->SendUnitControl(kEventJoins, unit->id);
        if (auto* serverUnit = g_serverWorld.FindUnit(unit->id)) {
            if (leader && leader->id == unit->id) serverUnit->ShowHelmet(false);
            serverUnit->ServerController()->actionDone = SpeakerAnimationDone;
        }
        return;
    }
    if (index < 0) return;

    // An item: one shown under this name goes first.
    for (const BriefingShownItem& item : shown) {
        if (text::CompareIgnoringCase(item.name, name) != 0) continue;
        HideSpeaker(name, false, false);
        break;
    }
    std::string key(name);
    text::ReplaceChar(&key, '_', ' ');
    str::MakeLower(key);
    const itemdb::ItemRecordHead* record;
    // The quest item table's (this window calls it the loot table) name index's
    // operator[] (an unknown name gets a null entry).
    record = g_itemDatabase.questItems.Named(key);
    if (!record) return;

    float x = static_cast<float>(kSlotXs[index % kSlots]);
    float y = static_cast<float>(kSlotYs[index % kSlots]);
    UiMesh* back = nullptr;
    UiMesh* frame = nullptr;
    if (index < kSlots) {
        back = AddBlankSprite();
        back->SetRectangle(20.0f, kSpriteScale, x - kSlotBack, y - kSlotBack, x + kSlotBack, y + kSlotBack, 0, 0, kUiTextureSize, kUiTextureSize);
        back->texture = nullptr;
        back->flags |= FigureMeshFlag::Flat;
        back->flatColour = kSlotBackdrop;
        frame = AddBlankSprite();
        frame->SetFrame(20.0f, 100.0f, x - kSlotFrame, y - kSlotFrame, x + kSlotFrame, y + kSlotFrame, 5.0f, 2, 0xFE, 0xB4,
                        0xF9);
    }
    std::string figure;
    figure = str::Format(kItemFigureFormat, record->number);
    UiMesh* item = AddSprite(figure);
    if (!item) {
        if (back) RemoveSceneObject(back);
        if (frame) RemoveSceneObject(frame);
        return;
    }
    if (index >= kTintFrom) {
        item->flatColour = kTintedItem;
        item->flags |= FigureMeshFlag::Flat;
    }
    std::string image;
    image = str::Format(kItemImageFormat, record->number);
    Texture* texture = CreateTexture(0x100, 0x100, TextureKind::Blank1555);
    CopyImage(texture, image, 0x40, 0x40, 0x40, 0x40, 0, 0xC0);
    item->texture = texture;
    item->Place(x, y, kItemDepth);
    Quaternion turn = Quaternion::AxisAngle(Vector3{1.0f, 0.0f, 0.0f}, kTurnedOver);
    item->localRotation = turn;

    shown.insert(shown.begin(), BriefingShownItem{std::string(name), item, back, frame});
}

void BriefingScreen::DrawSprites(SceneRenderer* pass) { DrawScreen(this, pass, true); }

// --- The person texts ---------------------------------------------------------------------------

// The "pers <name>" texts of the battle and briefing screens (PersonTextMap, ui_camp.h): one line
// per person, by the text key of the person's name. Read by the battle screen's setup (inline
// there) and the briefing screen.
void LoadPersonTexts(std::map<int, std::string>* map) {
    TextManager* texts = g_clientWorld.texts.get();
    std::string line;
    for (const std::string& textKey : texts->ListKeys(kPersonPattern)) {
        if (!texts->TextManager::Open(textKey)) continue;
        line = texts->ReadLine(true);
        (*map)[TextKeyFromName(str::Mid(textKey, kPersonPrefixLength))] = line;
    }
}
