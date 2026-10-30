// The briefing / base screen's main virtuals (BriefingScreen, ui_briefing.h): close, message
// box answers, a unit leaving the party, game over, the "briefing" command, the chat window and the
// saved state. See docs/notes/ui.md, "Briefing / base screen".
#include "ui_briefing.h"

#include "camera.h"
#include "combat.h"
#include "core_maths.h"
#include "core_streams.h"
#include "core_transform.h"
#include "game_singletons.h"
#include "item_db.h"
#include "maths.h"
#include "object_tables.h"
#include "options.h"
#include "party.h"
#include "player_messages.h"
#include "text_manager.h"
#include "text_split.h"
#include "ui.h"
#include "unit.h"
#include "unit_ai.h"
#include "world.h"
#include "world_glue.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/intro_sort.h"
#include "lib/nan_audit.h"
#include "lib/str.h"
#include "lib/text_case.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>

namespace {

constexpr int kLeaveZoneYes = 0x10001;
constexpr int kGameOverLoad = 0x20001;
constexpr int kGameOverQuit = 0x20002;
constexpr int kLeaveAnswer = 0x90001;
constexpr int kLoadWindowClosed = 0x100001;
constexpr int kGameOverResult = 0x20000;

constexpr const char* kZoneTopicFormat = "b.%s";
constexpr std::uint32_t kOneBits = 0x3F800000;   // 1.0f
constexpr int kTopicNameParts = 3;

} // namespace

void BriefingScreen::CloseScreen() {
    options::g_options.borderFogDistance = savedEdgeFade;
    GameStopSounds(1);
    GameStopAmbience();
    std::uint32_t& cameraFlags = g_camera.flags;
    cameraFlags = (cameraFlags & ~(Camera::kBounded | Camera::kAlternate)) | Camera::kFree;
}

void BriefingScreen::MessageResult(int result) {
    switch (result) {
    case kLeaveZoneYes:
        GameLeaveThroughExit(exit);
        break;
    case kGameOverLoad: {
        ui->PushWindow(new SaveLoadWindow(ui, false, false), true, true);
        break;
    }
    case kGameOverQuit:
    case kLeaveAnswer:
    case kLoadWindowClosed:
        ui->closeAll = true;
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
        int size = lib::Count(party);
        if (place >= size) {
            party.resize(place + 1);
        } else {
            party.resize(size + 1);
            std::memmove(&party[place + 1], &party[place], (size - place) * sizeof(party[0]));
        }
        party[place] = unit;
    } else {
        // SetAtGrow(size, unit).
        int size = lib::Count(party);
        party.resize(size + 1);
        party[size] = unit;
    }
    if (unit->id != static_cast<int>(player->members[0].unitId)) return;

    leader = unit;
    g_clientWorld.Select(unit);
    g_camera.SetTarget(unit);
    MapRecord* zone = g_mapDatabase.FindMap(LocalPlayerZone());
    std::string prefix = str::Format(kZoneTopicFormat, zone->name);
    NamedFloatTable variables;
    PlayerZeroVariables(prefix.c_str(), &variables);
    std::vector<std::string> parts;
    for (auto& node : variables) {
        std::uint32_t bits;
        bits = std::bit_cast<std::uint32_t>(node.second);
        if (bits != kOneBits) continue;
        parts = text::Split(node.first, '.');
        if (parts.size() != kTopicNameParts) continue;
        topic = parts[2];
        briefingStarted = true;
        StartDialogue(true, true);
        break;
    }
}

void BriefingScreen::UnitLeft(Unit* unit) {
    if (unit == leader) leader = nullptr;
    int count = lib::Count(party);
    int index = 0;
    while (index < count && party[index]->id != unit->id) ++index;
    if (index >= count) return;
    int after = count - index - 1;
    if (after != 0) std::memmove(&party[index], &party[index + 1], after * sizeof(party[0]));
    party.pop_back();
}

void BriefingScreen::GameOver(bool) {
    ui->PlayInterfaceSound("buttons\\gameover.wav");
    MessageWindow* box = new MessageWindow(ui, "game_over", "game_over_msg", kGameOverResult, 3, 1);
    ui->PushWindow(box, true, true);
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

constexpr const char* kTopicPrefix = "b.";
constexpr const char* kConstructorTopic = "constr";
constexpr const char* kBriefingSection = "briefing ";

constexpr int kConstructorPrefixLength = 6;

} // namespace

void BriefingScreen::FindTopics(int personId) {
    NamedFloatTable variables;
    PlayerZeroVariables(kTopicPrefix, &variables);
    std::vector<std::string> parts;
    std::string name;
    std::string constructor;
    std::vector<std::string> found;
    for (auto& node : variables) {
        if (std::bit_cast<std::uint32_t>(node.second) != kOneBits) continue;
        parts = text::Split(node.first, '.');
        if (parts.size() != kTopicNameParts || TextKeyFromName(parts[1]) != personId) continue;
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
    bool isConstructor;
    {
        std::string start = str::Left(key, kConstructorPrefixLength);
        isConstructor = str::Compare(start, kConstructorTopic) == 0;
    }
    if (isConstructor) key = kConstructorTopic;
    TextManager* texts = g_clientWorld.texts;
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

// A copy of `v`, normalised unless its length is zero.
Vector3 Normalised(const Vector3& v) {
    Vector3 out = v;
    float length = static_cast<float>(std::sqrt(LengthSquaredUnrounded<Sum3::ZXY>(v.x, v.y, v.z)));
    EI_NAN_AUDIT(length);
    if (length == 0.0f) return out;
    float inverse = 1.0f / length;
    out.x = static_cast<float>(v.x * inverse);
    out.y = static_cast<float>(v.y * inverse);
    out.z = static_cast<float>(v.z * inverse);
    return out;
}

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
        Vector3 facing = Normalised(direction);
        serverUnit->SetHeading(&facing.x);
        unit->SetHeading(&facing.x);
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
        if (player->Server()) player->Server()->SendPair19(kEventJoins, unit->id);
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
        back->SetRectangle(20.0f, 100.0f, x - kSlotBack, y - kSlotBack, x + kSlotBack, y + kSlotBack, 0, 0, 0x100, 0x100);
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
    Quaternion turn;
    turn.SetAxisAngle(Vector3{1.0f, 0.0f, 0.0f}, kTurnedOver);
    item->localRotation = turn;

    shown.insert(shown.begin(), BriefingShownItem{std::string(name), item, back, frame});
}
