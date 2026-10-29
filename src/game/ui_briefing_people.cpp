// The briefing / base screen's dialogue people and items (BriefingScreen, ui_briefing.h): a
// person walking to a place and a dialogue's "#show" line. See
// docs/notes/ui.md, "Briefing / base screen".
#include "world_glue.h"
#include "combat.h"
#include "player_messages.h"
#include "ui_briefing.h"

#include "core_maths.h"
#include "core_transform.h"
#include "item_db.h"
#include "maths.h"
#include "text_split.h"
#include "unit.h"
#include "unit_ai.h"
#include "world.h"

#include "lib/nan_audit.h"
#include "game_singletons.h"
#include "lib/str.h"
#include "lib/text_case.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <new>
#include <string>

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


void BriefingScreen::MovePerson(CUnit* unit, const Vector3& place, const Vector3& direction, bool waited,
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
    movers.push_front(BriefingMover{target, direction, BriefingMover::kWalking, unit, waited, 0.0f});
    controller->brain.Walk(target, run);
}

void BriefingScreen::ShowItem(std::string_view name, int index) {
    auto* unit = GameFindClientUnit(TextKeyFromName(name));
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
    CTexture* texture = CreateTexture(0x100, 0x100, TextureKind::Blank1555);
    CopyImage(texture, image, 0x40, 0x40, 0x40, 0x40, 0, 0xC0);
    item->texture = texture;
    item->Place(x, y, kItemDepth);
    Quaternion turn;
    turn.SetAxisAngle(Vector3{1.0f, 0.0f, 0.0f}, kTurnedOver);
    item->localRotation = turn;

    shown.push_front(BriefingShownItem{std::string(name), item, back, frame});
}
