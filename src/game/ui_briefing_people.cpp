// The briefing / base screen's dialogue people and items (CBriefingScreen, ui_briefing.h): a
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

float TurnedOver() { return 3.1415927f; }   // pi
const char* ItemFigureFormat() { return "initqu%ditem"; }
const char* ItemImageFormat() { return "quitem%04d"; }
// The seven speakers' places (800 x 600 layout).
constexpr int kSlotXs[7] = /*0074111C*/ {300, 500, 200, 400, 600, 75, 725};
constexpr int kSlotYs[7] = /*00741138*/ {300, 300, 300, 300, 300, 500, 500};


constexpr int kEventJoins = 1;
constexpr int kSlots = 7;
constexpr float kSlotBack = 60.0f;
constexpr float kSlotFrame = 65.0f;
constexpr float kItemDepth = 12.0f;
constexpr std::uint32_t kPlainFlag = 0x100;
constexpr std::uint32_t kSlotBackdrop = 0xA0000000;
constexpr std::uint32_t kTintedItem = 0x82FFFFFF;
constexpr int kTintFrom = 8;             // items from index 8 on are tinted
constexpr std::uint32_t kUnitBlocked = 0x800000;

// A copy of `v`, normalised unless its length is zero (FCOM, TEST AH,0x40).
Vector3 Normalised(float x, float y, float z) {
    Vector3 out{x, y, z};
    float length = static_cast<float>(std::sqrt(LengthSquaredUnrounded<Sum3::ZXY>(x, y, z)));
    EI_NAN_AUDIT(length);
    if (length == 0.0f) return out;
    float inverse = 1.0f / length;
    out.x = static_cast<float>(x * inverse);
    out.y = static_cast<float>(y * inverse);
    out.z = static_cast<float>(z * inverse);
    return out;
}

} // namespace


void CBriefingScreen::MovePerson(CUnit* unit, float x, float y, float z, float dx, float dy, float dz, bool waited,
                                 bool immediate, int run) {
    (void)z;   // the server unit's height is used instead
    auto* serverUnit = g_serverWorld->FindUnit(unit->id_);
    if (!serverUnit) return;
    auto* controller = serverUnit->ServerController();
    if (!controller) return;
    Vector3 target{x, y, serverUnit->localPosition_.z};
    if (immediate) {
        g_serverWorld->aiMap_.sectorMap.Move(serverUnit, &target);
        Vector3 facing = Normalised(dx, dy, dz);
        serverUnit->SetHeading(&facing.x);
        unit->SetHeading(&facing.x);
        controller->ResyncWithUnit();
        return;
    }
    if (serverUnit->flags_ & kUnitBlocked) return;
    movers_.push_front(BriefingMover{target, Vector3{dx, dy, dz}, BriefingMover::kWalking, unit, waited != 0, 0.0f});
    controller->brain.Walk(&target, run, 0);
}

void CBriefingScreen::ShowItem(std::string_view name, int index, int unused) {
    (void)unused;
    auto* unit = GameFindClientUnit(TextKeyFromName(name));
    if (unit) {
        // A person joins the speakers (at the end, once).
        auto entry = std::find_if(speakers_.begin(), speakers_.end(), [unit](const BriefingSpeaker& s) { return s.unit == unit; });
        if (entry != speakers_.end()) speakers_.erase(entry);
        speakers_.push_back(BriefingSpeaker{unit, std::string(name)});
        auto* player = g_serverWorld->GetPlayer(0);
        if (player->Server()) player->Server()->SendPair19(kEventJoins, unit->id_);
        if (auto* serverUnit = g_serverWorld->FindUnit(unit->id_)) {
            if (leader_ && leader_->id_ == unit->id_) serverUnit->SetFlag40000000Off(false);
            serverUnit->ServerController()->actionDone = SpeakerAnimationDone;
        }
        return;
    }
    if (index < 0) return;

    // An item: one shown under this name goes first.
    for (const BriefingShownItem& shown : shown_) {
        if (text::CompareIgnoringCase(shown.name, name) != 0) continue;
        HideSpeaker(name, 0, 0);
        break;
    }
    std::string key(name);
    text::ReplaceChar(&key, '_', ' ');
    str::MakeLower(key);
    const itemdb::ItemRecordHead* record;
    // The quest item table's (this window calls it the loot table) name index's
    // operator[] (an unknown name gets a null entry).
    record = g_itemDatabase->questItems.Named(key);
    if (!record) return;

    float x = static_cast<float>(kSlotXs[index % kSlots]);
    float y = static_cast<float>(kSlotYs[index % kSlots]);
    CUiMesh* back = nullptr;
    CUiMesh* frame = nullptr;
    if (index < kSlots) {
        back = AddBlankSprite();
        back->SetRectangle(20.0f, 100.0f, x - kSlotBack, y - kSlotBack, x + kSlotBack, y + kSlotBack, 0, 0, 0x100, 0x100);
        back->texture_ = nullptr;
        back->flags_ |= kPlainFlag;
        back->flatColour_ = kSlotBackdrop;
        frame = AddBlankSprite();
        frame->SetFrame(20.0f, 100.0f, x - kSlotFrame, y - kSlotFrame, x + kSlotFrame, y + kSlotFrame, 5.0f, 2, 0xFE, 0xB4,
                        0xF9);
    }
    std::string figure;
    figure = str::Format(ItemFigureFormat(), record->number);
    CUiMesh* item = AddSprite(figure);
    if (!item) {
        if (back) RemoveSceneObject(back);
        if (frame) RemoveSceneObject(frame);
        return;
    }
    if (index >= kTintFrom) {
        item->flatColour_ = kTintedItem;
        item->flags_ |= kPlainFlag;
    }
    std::string image;
    image = str::Format(ItemImageFormat(), record->number);
    CTexture* texture = CreateTexture(0x100, 0x100, 0x20);
    CopyImage(texture, image, 0x40, 0x40, 0x40, 0x40, 0, 0xC0, 0, 0);
    item->texture_ = texture;
    item->Place(x, y, kItemDepth);
    Quaternion turn;
    turn.SetAxisAngle(Vector3{1.0f, 0.0f, 0.0f}, TurnedOver());
    item->localRotation_ = turn;

    shown_.push_front(BriefingShownItem{std::string(name), item, back, frame});
}
