// The briefing / base screen's dialogue people and items (CBriefingScreen, ui_briefing.h): a
// person walking to a place (006089D0) and a dialogue's "#show" line (00605DE0). See
// docs/notes/ui.md, "Briefing / base screen".
#include "ui_briefing.h"

#include "core_maths.h"
#include "core_transform.h"
#include "item_db.h"
#include "maths.h"
#include "text_split.h"
#include "unit.h"
#include "unit_ai.h"
#include "world.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/stlp.h"
#include "originals_ai.h"
#include "originals_ui.h"
#include "originals_units.h"
#include "originals_world.h"

#include <cmath>
#include <new>

namespace {

void* ServerWorld() { return reinterpret_cast<void*>(0x007AFF60); }
void* ClientGlue() { return reinterpret_cast<void*>(0x007AFEA5); }
void* SectorMap() { return reinterpret_cast<void*>(0x007B01F4); }   // the server world + 0x294
void* LootNames() { return reinterpret_cast<void*>(0x007C8764); }   // the loot table's (007C8758) name index
float TurnedOver() { return *reinterpret_cast<const float*>(0x007410D0); }   // pi
const char* ItemFigureFormat() { return reinterpret_cast<const char*>(0x007934DC); }   // "initqu%ditem"
const char* ItemImageFormat() { return reinterpret_cast<const char*>(0x007934D0); }    // "quitem%04d"
const int* SlotXs() { return reinterpret_cast<const int*>(0x0074111C); }   // 300, 500, 200, 400, 600, 75, 725
const int* SlotYs() { return reinterpret_cast<const int*>(0x00741138); }   // 300 x5, 500, 500
using ActionDoneFn = void(__fastcall*)(CUnit*);
ActionDoneFn SpeakerActionDone() { return reinterpret_cast<ActionDoneFn>(0x006023F0); }

// 0060AC20: the name index's find-or-insert (thiscall, RET 4; the value's address).
__declspec(naked) const itemdb::ItemRecordHead** __fastcall LootNameAt(void* /*names*/, void*, const stlp::string* /*key*/)
    EI_ORIGINAL(0x0060AC20)
// 0060AB00 (dest in ECX, source in EDX, plain RET): a speaker entry copy-constructed in place.
// 0060A660 (thiscall on the speakers' vector, RET 0xC): insert_overflow(pos, value, count).
__declspec(naked) void __fastcall SpeakerCopyConstruct(BriefingSpeaker* /*dest*/, const BriefingSpeaker* /*source*/)
    EI_ORIGINAL(0x0060AB00)
__declspec(naked) void __fastcall SpeakersInsertOverflow(void* /*vector*/, void*, BriefingSpeaker* /*pos*/,
                                                         const BriefingSpeaker* /*value*/, int /*count*/) EI_ORIGINAL(0x0060A660)

constexpr int kEventJoins = 1;
constexpr int kSlots = 7;
constexpr float kSlotBack = 60.0f;       // 0073F090
constexpr float kSlotFrame = 65.0f;      // 00741350
constexpr float kItemDepth = 12.0f;
constexpr std::uint32_t kPlainFlag = 0x100;
constexpr std::uint32_t kSlotBackdrop = 0xA0000000;
constexpr std::uint32_t kTintedItem = 0x82FFFFFF;
constexpr int kTintFrom = 8;             // items from index 8 on are tinted
constexpr std::uint32_t kUnitBlocked = 0x800000;

int Compare(const char* a, const char* b) {
    return gamecrt::mbsicmp(reinterpret_cast<const unsigned char*>(a), reinterpret_cast<const unsigned char*>(b));
}

// A copy of `v`, normalised unless its length is zero (or NaN: FCOM, TEST AH,0x40).
Vector3 Normalised(float x, float y, float z) {
    Vector3 out{x, y, z};
    float length = std::sqrt(x * x + z * z + y * y);
    if (length == 0.0f || x87::IsNaN(length)) return out;
    float inverse = 1.0f / length;
    out.x = StoreFloat(x * inverse);
    out.y = StoreFloat(y * inverse);
    out.z = StoreFloat(z * inverse);
    return out;
}

} // namespace

// --- 006089D0 -----------------------------------------------------------------------------------

void CBriefingScreen::MovePerson(CUnit* unit, float x, float y, float z, float dx, float dy, float dz, bool waited,
                                 bool immediate, int run) {
    (void)z;   // the server unit's height is used instead
    auto* serverUnit = reinterpret_cast<CUnit*>(WorldFindUnit(ServerWorld(), nullptr, unit->id_));
    if (!serverUnit) return;
    auto* controller = reinterpret_cast<UnitController*>(serverUnit->controller);
    if (!controller) return;
    Vector3 target{x, y, serverUnit->positionZ};
    if (immediate) {
        SectorMapMove(SectorMap(), nullptr, serverUnit, &target);
        Vector3 facing = Normalised(dx, dy, dz);
        reinterpret_cast<CTransformObject*>(serverUnit)->SetHeading(&facing.x);
        reinterpret_cast<CTransformObject*>(unit)->SetHeading(&facing.x);
        ControllerRepositionOriginal(controller);
        return;
    }
    if (serverUnit->flags_ & kUnitBlocked) return;
    // list::push_front of the mover (node pool 0x30; its padding bytes are left as allocated).
    auto* node = static_cast<BriefingMover*>(stlp::allocate(sizeof(BriefingMover)));
    node->target[0] = target.x;
    node->target[1] = target.y;
    node->target[2] = target.z;
    node->direction[0] = dx;
    node->direction[1] = dy;
    node->direction[2] = dz;
    node->state = BriefingMover::kWalking;
    node->person = unit;
    node->waited = waited;
    node->time = 0.0f;
    BriefingMover* first = movers_->next;
    node->next = first;
    node->prev = first->prev;
    first->prev->next = node;
    first->prev = node;
    controller->Brain()->Walk(&target, run, 0);
}

namespace {
void __fastcall MovePersonHook(CBriefingScreen* self, void*, CUnit* unit, float x, float y, float z, float dx, float dy,
                               float dz, bool waited, bool immediate, int run) {
    self->MovePerson(unit, x, y, z, dx, dy, dz, waited, immediate, run);
}
} // namespace
EI_REPLACE(0x006089D0, MovePersonHook);

// --- 00605DE0 -----------------------------------------------------------------------------------

void CBriefingScreen::ShowItem(const char* name, int index, int unused) {
    (void)unused;
    auto* unit = static_cast<CUnit*>(GameUnitByIdOriginal(ClientGlue(), nullptr, TextKeyFromName(name)));
    if (unit) {
        // A person joins the speakers (at the end, once).
        BriefingSpeaker* entry = speakers_;
        while (entry != speakersEnd_ && entry->unit != unit) ++entry;
        if (entry != speakersEnd_) {
            // vector::erase (0060B490, then the last entry's destructor 0060AC00).
            for (BriefingSpeaker* p = entry; p + 1 != speakersEnd_; ++p) {
                p->unit = p[1].unit;
                p->name = p[1].name;
                p->unknown_08 = p[1].unknown_08;
            }
            --speakersEnd_;
            speakersEnd_->name.~CString();
        }
        BriefingSpeaker added{unit, mfc::CString(), false};
        if (speakersEnd_ != speakersCapacity_) {
            SpeakerCopyConstruct(speakersEnd_, &added);
            ++speakersEnd_;
        } else {
            SpeakersInsertOverflow(&speakers_, nullptr, speakersEnd_, &added, 1);
        }
        speakersEnd_[-1].unit = unit;
        speakersEnd_[-1].name = name;
        auto* player = static_cast<WorldPlayer*>(WorldGetPlayer(ServerWorld(), nullptr, 0));
        if (player->server) PlayerSendDialogueEventOriginal(player->server, nullptr, kEventJoins, unit->id_);
        if (auto* serverUnit = reinterpret_cast<CUnit*>(WorldFindUnit(ServerWorld(), nullptr, unit->id_))) {
            if (leader_ && leader_->id_ == unit->id_) serverUnit->SetFlag40000000Off(false);
            reinterpret_cast<UnitController*>(serverUnit->controller)->actionDone = SpeakerActionDone();
        }
        return;
    }
    if (index < 0) return;

    // An item: one shown under this name goes first.
    for (BriefingShownItem* node = shown_->next; node != shown_; node = node->next) {
        if (Compare(node->name, name) != 0) continue;
        BriefingHideSpeakerOriginal(this, nullptr, name, 0, 0);
        break;
    }
    mfc::CString key(name);
    text::ReplaceChar(&key, '_', ' ');
    key.MakeLower();
    const itemdb::ItemRecordHead* record;
    {
        stlp::string lookup(static_cast<const char*>(key));
        record = *LootNameAt(LootNames(), nullptr, &lookup);
    }
    if (!record) return;

    float x = static_cast<float>(SlotXs()[index % kSlots]);
    float y = static_cast<float>(SlotYs()[index % kSlots]);
    CUiMesh* back = nullptr;
    CUiMesh* frame = nullptr;
    if (index < kSlots) {
        back = AddSprite(nullptr);
        back->SetRectangle(20.0f, 100.0f, x - kSlotBack, y - kSlotBack, x + kSlotBack, y + kSlotBack, 0, 0, 0x100, 0x100);
        back->texture_ = nullptr;
        back->flags_ |= kPlainFlag;
        back->plainColour_ = kSlotBackdrop;
        frame = AddSprite(nullptr);
        frame->SetFrame(20.0f, 100.0f, x - kSlotFrame, y - kSlotFrame, x + kSlotFrame, y + kSlotFrame, 5.0f, 2, 0xFE, 0xB4,
                        0xF9);
    }
    mfc::CString figure;
    figure.Format(ItemFigureFormat(), record->number);
    CUiMesh* item = AddSprite(figure);
    if (!item) {
        if (back) RemoveSceneObject(reinterpret_cast<CEngineObject*>(back));
        if (frame) RemoveSceneObject(reinterpret_cast<CEngineObject*>(frame));
        return;
    }
    if (index >= kTintFrom) {
        item->plainColour_ = kTintedItem;
        item->flags_ |= kPlainFlag;
    }
    mfc::CString image;
    image.Format(ItemImageFormat(), record->number);
    CEngineObject* texture = CreateTexture(0x100, 0x100, 0x20);
    CopyImage(texture, image, 0x40, 0x40, 0x40, 0x40, 0, 0xC0, 0, 0);
    item->texture_ = texture;
    item->Place(x, y, kItemDepth);
    Quaternion turn;
    turn.SetAxisAngle(Vector3{1.0f, 0.0f, 0.0f}, TurnedOver());
    item->orientation_ = turn;

    // list::push_front of {name, item, back, frame} (node pool 0x18).
    auto* node = static_cast<BriefingShownItem*>(stlp::allocate(sizeof(BriefingShownItem)));
    new (&node->name) mfc::CString(name);
    node->item = item;
    node->back = reinterpret_cast<CEngineObject*>(back);
    node->frame = reinterpret_cast<CEngineObject*>(frame);
    BriefingShownItem* first = shown_->next;
    node->next = first;
    node->prev = first->prev;
    first->prev->next = node;
    first->prev = node;
}
EI_REPLACE_METHOD(0x00605DE0, CBriefingScreen::ShowItem);
