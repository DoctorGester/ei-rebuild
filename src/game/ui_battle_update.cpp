// The battle screen's every-frame update (00638280, slot 24), its left button release (00638B30),
// its quest variable notice (006369D0, slot 33), the selection's refresh (00637DC0) and the quick
// item panel's unit (0063D2B0). See docs/notes/ui.md ("Battle screen").
#include "camera.h"
#include "item.h"
#include "player_messages.h"
#include "game_strings.h"
#include "item_db_text.h"
#include "map_object.h"
#include "map_object_figures.h"
#include "net_classes.h"
#include "net_messages.h"
#include "originals_gameplay.h"
#include "originals_world.h"
#include "stl_sorts.h"
#include "text_reader.h"
#include "ui.h"
#include "ui_camp.h"
#include "unit.h"
#include "unit_db.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/nan_audit.h"
#include "lib/crt.h"
#include "text_split.h"
#include "lib/mfc.h"
#include "lib/stlp.h"
#include "originals_library.h"
#include "originals_engine.h"
#include "originals_units.h"

#include <cstdint>
#include <cstring>
#include <bit>
#include "originals_ui.h"
#include "world.h"
#include "world_glue.h"
#include "lib/mfc_array.h"
#include "game_data.h"
#include "game_singletons.h"

// --- 0063D2B0: the quick item panel's unit ----------------------------------------------------

namespace {

// 0063CAE0 (thiscall, RET 8; Rebirth-patched): the next slot's picture (image `image` copied into
// the panel's texture) and a sprite of model `model` (or a plain square); counts it in +0x14C
// (at most eight).
__declspec(naked) void __fastcall AddItemSprite(CHudQuickItems*, void*, const char* /*model*/, const char* /*image*/)
    EI_ORIGINAL(0x0063CAE0)
// 0064B060 (thiscall, RET 8): the entries' CArray SetSize (zeroed new entries; `growBy` -1 keeps
// the step). The original inlines the same code after each sprite.
void EntriesSetSize(void* entries, int size) {
    mfc::impl::Plain12::SetSize(static_cast<mfc::impl::Plain12::Array*>(entries), nullptr, size, -1);
}

// The item's slot 19 (the picture's name) and 20 (the model's name).
void ItemName(CEngineObject* item, int slot, ReturnedString* out) {
    using Fn = mfc::CString*(__thiscall*)(CEngineObject*, mfc::CString*);
    ei::ObjectSlot<Fn>(item, slot)(item, out->Slot());
}

} // namespace

void CHudQuickItems::SetUnit(CUnit* unit) {
    mfc::CString tip;
    mfc::CString unused;
    unit_ = unit;
    ClearSceneObjects();
    shownCount_ = 0;
    selected_ = -1;
    for (int i = 0; i < 8; ++i) {
        regions_.data[i]->SetTip(0, 1, 0);
        regionItem_[i] = -1;
    }
    EntriesSetSize(&entriesVtable_, 0);
    if (!unit_) return;

    // The entry of the sprite just added.
    auto finish = [&](CEngineObject* item, bool own) {
        EntriesSetSize(&entriesVtable_, shownCount_);
        Entry& entry = entries_[shownCount_ - 1];
        entry.own = own;
        entry.id = item->id_;
        entry.item = item;
    };
    // The sprite just added: placed in its row (x from 007428AC, 695; Rebirth moves it), its
    // region pointing at it, scaled, opaque.
    auto place = [&](int slot) {
        CEngineObject* sprite = sceneObjects_.data[shownCount_ - 1];
        const float right = ei::GameData<const volatile float>(0x007428AC);
        if (slot < 4)
            SpritePlaceOriginal((void*)sprite, nullptr, right - static_cast<float>(slot * 50), 575.0f, 20.0f);
        else
            SpritePlaceOriginal((void*)sprite, nullptr, right - static_cast<float>(slot * 50 - 200), 525.0f, 20.0f);
        int last = shownCount_ - 1;
        regionItem_[slot] = last;
        if (last >= 0 && last < shownCount_) {
            CEngineObject* shown = sceneObjects_.data[last];
            AsUiMesh(shown)->SetScale(slot < 4 ? 0.55f : 0.85f);
            SceneMesh(last)->flatSpecular_ = 0xFF000000;
        }
    };

    // The unit's four quick items in the bottom row.
    for (unsigned slot = 0; slot < 4; ++slot) {
        CItem* item = unit_->GetQuickItem(slot);
        if (!item) continue;
        {
            ReturnedString image;
            ItemName(item, 19, &image);
            ReturnedString model;
            ItemName(item, 20, &model);
            AddItemSprite(this, nullptr, model.data, image.data);
        }
        finish(item, true);
        ItemTip(slot, &tip);
        regions_.data[slot]->SetTipString(tip, 1, static_cast<int>(slot) + 0x2A);
        place(static_cast<int>(slot));
    }

    // The local player's own unit also shows up to four of the player's 0x3009 items above, from
    // the last of the player's items backwards (node: +4 previous, +8 item).
    std::uint8_t* player = *reinterpret_cast<std::uint8_t**>(GameLocalView() + 0x1C);
    if (!reinterpret_cast<CPlayer*>(player)->IsLeader(unit_)) return;
    player = *reinterpret_cast<std::uint8_t**>(GameLocalView() + 0x1C);
    std::uint8_t* node = *reinterpret_cast<std::uint8_t**>(player + 0xE8);
    if (!node) return;
    int slot = 4;
    while (node) {
        if (slot - 4 >= 4) break;
        CEngineObject* item = *reinterpret_cast<CEngineObject**>(node + 8);
        node = *reinterpret_cast<std::uint8_t**>(node + 4);
        if (!item || item->type_ != kItemQuest)
            continue;
        {
            ReturnedString image;
            ItemName(item, 19, &image);
            ReturnedString model;
            ItemName(item, 20, &model);
            AddItemSprite(this, nullptr, model.data, image.data);
        }
        finish(item, false);
        // The original passes the same CString for the second and third texts.
        TheTextManager()->GetItemTexts(static_cast<CItem*>(item), &tip, &unused, &unused);
        regions_.data[slot]->SetTipString(tip, 1, 0);
        place(slot);
        ++slot;
    }
}
EI_REWRITTEN(0x0063D2B0, CHudQuickItems::SetUnit);

// --- 00638280: the battle screen's update ------------------------------------------------------

namespace {

// The panels' units (thiscall): weapon bar 0063C180, quick items 0063D2B0 (through its address),
// spells 0063E820 (RET 4), actions 0063F800 (RET 8).
// 00530930 (__fastcall on a unit): its prototype record.
static const UnitPrototypeRecord* UnitPrototype(CUnit* unit) {
    return reinterpret_cast<const UnitPrototypeRecord*>(unit->GetPrototype());
}
// The unit panel's model view for the unit it now shows, copied from the prototype as dwords: the
// size from `first`'s (+0x20), the height from the shown unit's (+0x24; the same unit).
void ShowPanelView(CHudUnitPanel* panel, CUnit* first) {
    std::memcpy(&panel->viewSize_[0], &UnitPrototype(first)->panelViewSize, 4);
    std::memcpy(&panel->viewHeight_[0], &UnitPrototype(panel->shownUnit_)->height, 4);
}
// 0057F440 (thiscall on the session 007AFE20): whether this is a network client (AL).
// The world (007AF5B8, thiscall): 0055FB00 the unit with an id (RET 4), 00566030 (RET 4) makes
// `unit` the controlled one, 005656A0 (RET 8) the units inside a selection box into a list.
static CUnit* FindUnit(void* world, void*, int id) { return ::WorldFindUnit(world, nullptr, id); }

// vector<int> pieces (library): 005F8F10 constructs from an allocator (RET 4), 0064B620
// (__fastcall) constructs an element, 004E2FA0 the reallocating insert (RET 0xC), 0060B1E0 /
// 0064B750 (__fastcall) sort's introsort loop (RET 8) and final insertion sort, 0064AF30 assigns
// (RET 4), 004E2A20 / 005F9880 the destructors of the two instantiations.
using IdVector = CBattleScreen::IdVector;
using PartyIds = CBattleScreen::PartyIds;

// The party CArray<CUnit*> (+0x168): 005EBDB0 SetSize (RET 8), 0060AD50 SetAtGrow (RET 8),
// 0060AEC0 (stdcall) ConstructElements; 006E79A0 the game's memmove.
// 006407E0 (thiscall on the portraits, RET 4): rebuilds them (`cameraFirst`: the first is the
// camera's player; the original pushes a dword whose low byte only is set).
// 006010E0 (thiscall): the children's update. 005F8A80 (thiscall on the interface, RET 4): the
// next queued message. 005F7350 (RET 0xC): opens a window on top. 00634650 (RET 0x18): the
// message box's constructor.
void __fastcall UpdateChildren(CUiWindow* a0) { UiUpdateChildrenOriginal((void*)a0); }

// STLport list<CUnit*> (00411850 constructor, RET 4) and list<int> (00404A70 constructor,
// 004049D0 end(), 00404A40 insert, RET 4 / 4 / 0xC); 00404D70 (__fastcall) frees a node.
using List = stlp::list<std::uintptr_t>;
using ListNode = List::Node;
void __fastcall NodeFree(void* a0, std::uint32_t a1) { libcrt::NodeDeallocate(a0, (std::size_t)a1); }
void*& NodeFreeList() { return ei::GameData<void*>(0x0079B228); }   // the 12-byte nodes


// push_back of `*value`: the constructing helper, or the reallocating insert when full.
void PushId(IdVector* ids, const int* value) {
    if (ids->Finish() != ids->EndOfStorage()) {
        stlp::ConstructAt(ids->Finish(), value);   // 0064B620
        ++ids->Finish();
    } else {
        ids->InsertOverflow<stlp::PlainElement<int>>(ids->Finish(), *value, 1);   // 004E2FA0
    }
}
// The same with the construction inlined (as the original's third case).
void PushIdInline(IdVector* ids, const int* value) {
    if (ids->Finish() != ids->EndOfStorage()) {
        if (ids->Finish()) *ids->Finish() = *value;
        ++ids->Finish();
    } else {
        ids->InsertOverflow<stlp::PlainElement<int>>(ids->Finish(), *value, 1);   // 004E2FA0
    }
}

// std::sort: the introsort loop with depth 2 * floor(log2(n)), then the final insertion sort.
void SortIds(IdVector* ids) {
    if (ids->begin() == ids->end()) return;
    int depth = 0;
    for (int n = static_cast<int>(ids->end() - ids->begin()); n != 1; n >>= 1) ++depth;
    stl_sorts::IdIntroSort(ids->begin(), ids->end(), nullptr, depth * 2);
    stl_sorts::IdFinalSort(ids->begin(), ids->end());
}

// A multiplayer client: the party is the session's players' units, the camera's player first.
void RebuildClientParty(CBattleScreen* screen) {
    net::ClientSession* session = ei::GameData<net::ClientSession*>(0x0079B5D8);
    ListNode** players = reinterpret_cast<ListNode**>(&session->players_);
    PartyIds ids;
    ids.Construct();
    ids.camera = -1;
    for (ListNode* node = (*players)->next; node != *players; node = node->next) {
        net::PlayerMessage* player = reinterpret_cast<net::PlayerMessage*>(node->data);
        int* unitId = &player->id_;
        int* playerId = &player->serial_;
        if (player->state_ == 0 && *unitId >= 0) {
            if (!FindUnit(TheClientWorld(), nullptr, *unitId)) {
                PushId(&ids.without, playerId);
                continue;
            }
            PushId(&ids.withUnit, unitId);
            net::BindingMessage* camera = static_cast<net::BindingMessage*>(session->binding_);
            if (camera && !(camera->flags7_ & CRefCounted::kDetached) &&
                camera->first_.ptr == player)
                ids.camera = *unitId;
        } else {
            PushIdInline(&ids.without, playerId);
        }
    }
    SortIds(&ids.withUnit);
    SortIds(&ids.without);
    if (!ids.Equals(screen->clientParty_)) {
        auto* party = &screen->party_;
        party->SetSize(0, -1);
        bool cameraFirst = false;
        for (int* id = ids.withUnit.begin(); id != ids.withUnit.end(); ++id) {
            int unitId = *id;
            CUnit* unit = FindUnit(TheClientWorld(), nullptr, unitId);
            if (!unit) continue;
            if (unitId == ids.camera) {
                TheClientWorld()->Select(unit);
                // Insert at the front (the original's inlined InsertAt).
                int n = party->size;
                if (n <= 0) {
                    party->SetSize(1, -1);
                } else {
                    party->SetSize(n + 1, -1);
                    libcrt::Memmove(party->data + 1, party->data, static_cast<std::size_t>(n) * 4);
                    mfc::PlainElements<CUnit*>::Construct(party->data, 1);   // 0060AEC0
                }
                party->data[0] = unit;
                cameraFirst = true;
            } else {
                party->SetAtGrow(party->size, unit);
            }
        }
        PortraitsRebuildOriginal(screen->portraits_, nullptr, cameraFirst ? 1 : 0);
        if (screen->clientParty_.camera != ids.camera) {
            TheCamera()->SetTargetId(ids.camera);
            screen->RefreshSelection();
        }
        screen->clientParty_.withUnit.AssignVector<stlp::PlainElement<int>>(ids.withUnit);   // 0064AF30
        screen->clientParty_.camera = ids.camera;
        screen->clientParty_.without.AssignVector<stlp::PlainElement<int>>(ids.without);
    }
    ids.without.FreeStorage();
    ids.withUnit.FreeStorage();
}

// Frees a list's nodes (00404D70) and empties it (the header is freed by the caller).
void FreeListNodes(List* list) {
    ListNode* header = list->head;
    for (ListNode* node = header->next; node != header;) {
        ListNode* next = node->next;
        if (node) NodeFree(node, 0xC);
        node = next;
    }
    header->next = header;
    header->prev = header;
}

} // namespace

void CBattleScreen::Update() {
    // The one selected party unit is dead (its slot 55, IsDead): the panels lose their unit and the
    // unit panel shows it.
    auto* party = &party_;
    CUnit* first = nullptr;
    int selected = 0;
    for (int i = 0; i < party->size; ++i) {
        auto* unit = party->data[i];
        if (unit && (unit->flags_ & 0x10)) {
            if (!first) first = unit;
            ++selected;
        }
    }
    if (first) {
        if (first->IsDead() && selected == 1) {
            weaponBar_->SetUnit(nullptr);
            quickItems_->SetUnit(nullptr);
            spellColumn_->SetUnit(nullptr);
            actionColumn_->SetUnit(nullptr, 0);
            CHudUnitPanel* panel = static_cast<CHudUnitPanel*>(unitPanel_);
            if (first != panel->shownUnit_) {
                panel->shownUnit_ = first;
                ShowPanelView(panel, first);
                panel->owner_->UnitName(panel->shownUnit_, &panel->strings_[0]);
            }
        }
    }

    if (IsMultiplayerClient()) RebuildClientParty(this);

    UpdateChildren(this);
    interface_->ShowMessage(0);

    // A zone exit saved with the game: once it can be taken, the "leave_zone" question.
    if (exitPending_) {
        // The local player's unit records (0x784 bytes each) and the ones in the exit.
        mfc::CArray<std::uint8_t*> records = {ei::GamePointer<void>(0x0073FAA4), nullptr, 0, 0, 0};
        mfc::CArray<std::uint8_t*> second = {ei::GamePointer<void>(0x0073FAA4), nullptr, 0, 0, 0};
        PartyRecords* data = reinterpret_cast<PartyRecords*>(LocalPlayerData());
        for (int i = 0; i < data->count; ++i)
            records.SetAtGrow(records.size, reinterpret_cast<std::uint8_t*>(&data->records[i]));   // 0059D2E0
        if (GamePartyInExit(&records, pendingExit_, &second)) {
            void* memory = gamecrt::operator_new(0x180);
            void* box = memory ? reinterpret_cast<CMessageBox*>(memory)->Construct(interface_, "leave_zone", "leave_zone_msg", 0x10000, 3, 1)
                               : nullptr;
            interface_->PushWindow(static_cast<CUiWindow*>(box), 1, 1);
            exitPending_ = 0;
        }
        second.vtable = ei::GamePointer<void>(0x0073FAA4);
        if (second.data) gamecrt::operator_delete(second.data);
        records.vtable = ei::GamePointer<void>(0x0073FAA4);
        if (records.data) gamecrt::operator_delete(records.data);
    }

    // The selection box: party units inside it are marked (flag 0x100 and their figure's slot 26
    // with 1), the others unmarked (with 0); dead ones (0x10) are left alone.
    if (boxSelecting_) {
        List units;
        units.Construct();   // 00411850
        TheClientWorld()->UnitsInBox(reinterpret_cast<const long*>(&selectionBox_), reinterpret_cast<stlp::list<CEngineObject*>*>(&units));
        List inside;
        inside.Construct();   // 00404A70
        for (ListNode* node = units.head->next; node != units.head; node = node->next) {
            for (int i = 0; i < party->size; ++i) {
                if (reinterpret_cast<std::uintptr_t>(party->data[i]) != node->data) continue;
                List::Insert(inside.End(), static_cast<std::uintptr_t>(i));   // 004049D0 end, 00404A40 insert
            }
        }
        using MarkFn = void(__thiscall*)(void*, int);
        auto mark = [](CUnit* unit, int on) {
            void* figure = unit->Object3D()->attached;
            ei::ObjectSlot<MarkFn>(figure, 26)(figure, on);
        };
        for (int i = 0; i < party->size; ++i) {
            auto* unit = party->data[i];
            std::uint32_t flags = unit->flags_;
            if (!(flags & 0x10) && (flags & 0x100)) {
                unit->flags_ = flags & ~0x100u;
                mark(unit, 0);
            }
        }
        for (ListNode* node = inside.head->next; node != inside.head; node = node->next) {
            auto* unit = party->data[node->data];
            std::uint32_t flags = unit->flags_;
            if (!(flags & 0x10) && !(flags & 0x100)) {
                unit->flags_ = flags | 0x100;
                mark(unit, 1);
            }
        }
        FreeListNodes(&inside);
        if (inside.head) NodeFree(inside.head, 0xC);
        FreeListNodes(&units);
        if (units.head) {
            *reinterpret_cast<void**>(units.head) = NodeFreeList();
            NodeFreeList() = units.head;
        }
    }

    // The quest sound asked for.
    if (questSound_ != 0) {
        const char* sound = nullptr;
        switch (questSound_) {
        case 1: sound = "buttons\\quest.wav"; break;
        case 2: sound = "buttons\\subcomplete.wav"; break;
        case 3: sound = "buttons\\complete.wav"; break;
        }
        if (sound) interface_->PlaySound(sound, 0);
        questSound_ = 0;
    }
}
namespace {
void __fastcall BattleScreenUpdate(CBattleScreen* self) { self->Update(); }
} // namespace
EI_REPLACE(0x00638280, BattleScreenUpdate);

// --- 006381E0: the party ids compared --------------------------------------------------------------

namespace {
bool SameIds(const IdVector& a, const IdVector& b) {
    if (a.end() - a.begin() != b.end() - b.begin()) return false;
    for (const int *x = a.begin(), *y = b.begin(); x != a.end(); ++x, ++y)
        if (*x != *y) return false;
    return true;
}
} // namespace

bool CBattleScreen::PartyIds::Equals(const PartyIds& other) const {
    return SameIds(withUnit, other.withUnit) && SameIds(without, other.without);
}
EI_REWRITTEN(0x006381E0, CBattleScreen::PartyIds::Equals);

// --- 00638B30: the battle screen's left button release -----------------------------------------

namespace {

// The world (007AF5B8, thiscall): 00565E40 (RET 4) the unit at a screen point (x, y), 00566100
// (RET 4) with 0 clears the selection (the selected units' flag 0x10 and marks).
CUnit* UnitAtPoint(void* world, void*, int* point) { return static_cast<CUnit*>(static_cast<CWorldClient*>(world)->UnitAt(reinterpret_cast<const long*>(point))); }

} // namespace

void CBattleScreen::OnLButtonUp(int buttons, int x, int y) {
    if (portraitMode_ != 0) return;
    if (!viewPressed_) {
        InputOf(this)->OnLeftUp(buttons, x, y);
        return;
    }
    viewPressed_ = false;
    void* world = TheClientWorld();
    if (!boxSelecting_ || IsMultiplayerClient()) {
        // The original passes the address of its own (x, y) arguments and reads them back.
        int point[2] = {x, y};
        CUnit* unit = UnitAtPoint(world, nullptr, point);
        if (unit && !(unit->flags_ & 0x400000)) unit = nullptr;
        SelectUnitAt(this, nullptr, unit, point[0], point[1]);
        return;
    }

    auto* party = &party_;
    auto unitAt = [&](int i) { return party->data[i]; };
    int first = -1;
    List units;
    units.Construct();   // 00411850
    static_cast<CWorldClient*>(world)->UnitsInBox(reinterpret_cast<const long*>(&selectionBox_),
                                                  reinterpret_cast<stlp::list<CEngineObject*>*>(&units));
    List inside;
    inside.Construct();   // 00404A70
    if (units.head->next != units.head) {
        for (ListNode* node = units.head->next; node != units.head; node = node->next) {
            for (int i = 0; i < party->size; ++i) {
                if (reinterpret_cast<std::uintptr_t>(party->data[i]) != node->data) continue;
                List::Insert(inside.End(), static_cast<std::uintptr_t>(i));   // 004049D0 end, 00404A40 insert
                if (first < 0) first = i;
            }
        }
        if (first >= 0) {
            static_cast<CWorldClient*>(world)->Deselect(nullptr);
            using MarkFn = void(__thiscall*)(void*, int);
            // Portrait i's selection marker (scene object 5 i + 4) and face state.
            auto marker = [&](int i) { return portraits_->SceneMesh(i * 5 + 4); };
            auto slot = [&](int i) -> HudPortraitSlot& { return static_cast<CHudPortraits*>(portraits_)->slots_[i]; };
            for (int i = 0; i < party->size; ++i) {
                CUnit* unit = unitAt(i);
                std::uint32_t flags = unit->flags_;
                if (!(flags & 0x10) && (flags & 0x100)) {
                    unit->flags_ = flags & ~0x100u;
                    void* figure = unit->Object3D()->attached;
                    ei::ObjectSlot<MarkFn>(figure, 26)(figure, 0);
                }
                marker(i)->visible_ = 0;
                slot(i).restB_ = ei::GameData<const Quaternion>(0x007C8150);   // identity
            }
            for (ListNode* node = inside.head->next; node != inside.head; node = node->next) {
                int i = static_cast<int>(node->data);
                marker(i)->visible_ = 1;
                const Vector3 axis = {-1.0f, 0.0f, 0.0f};
                const float angle = ei::GameData<const volatile float>(0x007C8144) *
                                    0.14285715f /*0073CDCC*/;   // 1/7
                Quaternion turn;
                turn.SetAxisAngle(axis, angle);
                slot(i).restB_ = turn;
                static_cast<CWorldClient*>(world)->Select(unitAt(static_cast<int>(node->data)));
            }
            int id = unitAt(first)->id_;
            using EventFn = void(__thiscall*)(void*, int, int, int);
            ei::VirtualSlot<EventFn>(vtable_, 0x80 / 4)(this, id, 0, 0);
        }
    }
    RefreshSelection();
    boxSelecting_ = false;
    FreeListNodes(&inside);
    if (inside.head) NodeFree(inside.head, 0xC);
    FreeListNodes(&units);
    if (units.head) {
        *reinterpret_cast<void**>(units.head) = NodeFreeList();
        NodeFreeList() = units.head;
    }
}
EI_REPLACE_METHOD(0x00638B30, InputForward<&CBattleScreen::OnLButtonUp>::Call);

// --- 006369D0: a quest variable's notice -------------------------------------------------------

namespace {

// 004E0020 (thiscall on the text manager, RET 8): the text of a key. 004DBE40 (__fastcall, RET 4): splits a text
// at a separator.
// 0065CF30 (thiscall on the message log, RET 4): adds a line.

// FCOMP; TEST AH,0x40: equal.
bool Equal(float a, float b) {
    EI_NAN_AUDIT(a, b);
    return a == b;
}

} // namespace

void CBattleScreen::OnQuestVariable(const char* name, float value) {
    CTextManager* texts = (CTextManager*)GameClientTexts();
    console::CStringArray parts;
    mfc::CString text;
    mfc::CString title;
    text::Split(name, &parts, '.');
    const float complete = 2.0f /*0073CDB8*/;   // 2
    const float failed = 3.0f /*0073BAA4*/;     // 3
    if (parts.size == 3) {
        const char* key;
        if (Equal(value, complete)) {
            if (questSound_ <= 3) questSound_ = 3;
            key = "combat_complete_quest";
        } else if (Equal(value, failed)) {
            value = 1.0f;
            key = "combat_failed_quest";
        } else {
            key = "combat_get_quest";
        }
        TextsGetString(texts, nullptr, key, &title);
        text.Empty();
        BOOL found;
        {
            mfc::CString quest("quest ");
            mfc::CString textKey = quest + parts[2];
            found = texts->Open(textKey);
        }
        if (found) texts->ReadLine(&text, 1);
        mfc::CString colour("\007C\356\343" "1 ");   // colour code 0x31E3EE43 and a space
        text = colour + title + " " + text;
    } else if (parts.size == 4) {
        if (!IsMultiplayerClient()) {
            const char* dot = ".";
            mfc::CString variable = parts[0] + dot + parts[1] + dot + parts[2];
            double current = LocalPlayerVariable(variable);
            // FCOMP of the unrounded result with 0 (0073B858); TEST AH,0x40.
            EI_NAN_AUDIT(current);
            if (current == static_cast<double>(0.0f /*0073B858*/)) return;
        }
        const char* key;
        if (Equal(value, complete)) {
            key = "combat_complete_subobj";
        } else if (Equal(value, failed)) {
            value = 1.0f;
            key = "combat_failed_subobj";
        } else {
            key = "combat_get_subobj";
        }
        TextsGetString(texts, nullptr, key, &title);
        text.Empty();
        bool found = false;
        {
            mfc::CString quest("quest ");
            mfc::CString textKey = quest + parts[2];
            if (texts->Open(textKey)) {
                mfc::CString sub("#subobj ");
                mfc::CString lineKey = sub + parts[3];
                found = texts->FindLine(lineKey) != 0;
            }
        }
        if (found) {
            TextNextLine(texts);
            texts->ReadLine(&text, 1);
        }
        mfc::CString colour("\007C\377\263" "1 ");   // colour code 0x31B3FF43 and a space
        text = colour + title + " " + text;
    }
    text = text + "\007C\344\327\247\n";   // colour code and a line end

    CHudTopPanel* top = static_cast<CHudTopPanel*>(topPanel_);
    const char* line = text;
    top->PageText(0) += line;
    if (top->page_ == 0) {
        CBattleScreen* screen = top->owner_;
        static_cast<CUiScrollText*>((void*)screen->children_[9])->AddText(line);
    }
    static_cast<CHudClock*>(clock_)->highlight_ = true;
    int sound = static_cast<int>(value);
    if (questSound_ > sound) sound = questSound_;
    questSound_ = sound;
}
EI_REPLACE_METHOD(0x006369D0, CBattleScreen::OnQuestVariable);

// --- 00637DC0: the selection's refresh --------------------------------------------------------

namespace {

// The panels' highlights (thiscall, RET 8; each called with (index, 0)): quick item slot
// 0063CD20, spell 0063E750, action 0063F730.

} // namespace

void CBattleScreen::RefreshSelection() {
    auto* party = &party_;
    CUnit* first = nullptr;
    int selected = 0;
    for (int i = 0; i < party->size; ++i) {
        auto* unit = party->data[i];
        if (!unit || !(unit->flags_ & 0x10)) continue;
        if (!first) first = unit;
        ++selected;
    }
    // Exactly one living unit selected: the panels show it; otherwise they are emptied. The action
    // column's extra button stays while anyone is selected (the original pushes SETG DL there with
    // EDX's upper bytes left over; the callee reads the low byte only).
    CUnit* shown = (first && first->IsDead() == 0 && selected == 1) ? first : nullptr;
    weaponBar_->SetUnit(shown);
    quickItems_->SetUnit(shown);
    spellColumn_->SetUnit(shown);
    actionColumn_->SetUnit(shown, selected > 0 ? 1 : 0);

    // The unit panel shows the first selected unit (also a dead one, or the first of several).
    CHudUnitPanel* panel = static_cast<CHudUnitPanel*>(unitPanel_);
    if (first != panel->shownUnit_) {
        panel->shownUnit_ = first;
        if (first) {
            ShowPanelView(panel, first);
            panel->owner_->UnitName(panel->shownUnit_, &panel->strings_[0]);
        }
    }

    // No pending action; every highlight cleared.
    pendingSlot_ = -1;
    pendingAction_ = 0;
    pendingItem_ = -1;
    CHudQuickItems* quick = static_cast<CHudQuickItems*>(quickItems_);
    for (int i = 0; i < 8; ++i) quick->Highlight(i, 0);
    quick->selected_ = -1;
    CHudSpellColumn* spells = static_cast<CHudSpellColumn*>(spellColumn_);
    for (int i = 0; i < spells->count_; ++i) spells->Highlight(i, 0);
    spells->selected_ = -1;
    CHudActionColumn* actions = static_cast<CHudActionColumn*>(actionColumn_);
    for (int i = 0; i < actions->count_; ++i) actions->Highlight(i, 0);
    actions->selected_ = -1;
}
EI_REWRITTEN(0x00637DC0, CBattleScreen::RefreshSelection);
