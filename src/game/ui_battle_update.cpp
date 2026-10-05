// The battle screen's every-frame update (00638280, slot 24) and the quick item panel's unit
// (0063D2B0). See docs/notes/ui.md ("Ninth pass").
#include "ui_camp.h"
#include "unit.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/mfc.h"

#include <cstdint>
#include <cstring>

// --- 0063D2B0: the quick item panel's unit ----------------------------------------------------

namespace {

// 0063CAE0 (thiscall, RET 8; Rebirth-patched): the next slot's picture (image `image` copied into
// the panel's texture) and a sprite of model `model` (or a plain square); counts it in +0x14C
// (at most eight).
__declspec(naked) void __fastcall AddItemSprite(CHudQuickItems*, void*, const char* /*model*/, const char* /*image*/)
    EI_ORIGINAL(0x0063CAE0)
// 0064B060 (thiscall, RET 8): the entries' CArray SetSize (zeroed new entries; `growBy` -1 keeps
// the step). The original inlines the same code after each sprite.
__declspec(naked) void __fastcall EntriesSetSize(void*, void*, int /*size*/, int /*growBy*/) EI_ORIGINAL(0x0064B060)
// 00578BC0 (jumps to 005699F0 with the world 007AF5B8): the local player's view; its +0x1C is the
// player. 00665440 (thiscall on the player, RET 4): whether `unit` is the player's own.
__declspec(naked) std::uint8_t* __fastcall LocalView(void*) EI_ORIGINAL(0x00578BC0)
__declspec(naked) int __fastcall PlayerOwns(std::uint8_t*, void*, CUnit*) EI_ORIGINAL(0x00665440)
// 004E0150 (thiscall on the text manager [007AF788], RET 0x10): an item's texts.
__declspec(naked) BOOL __fastcall ItemTexts(void*, void*, CEngineObject*, mfc::CString*, mfc::CString*, mfc::CString*)
    EI_ORIGINAL(0x004E0150)
// 005E7060 (thiscall on a sprite, RET 4): its scale.
__declspec(naked) void __fastcall SpriteScale(CEngineObject*, void*, float) EI_ORIGINAL(0x005E7060)
// 005E6FA0 (Rebirth-patched): places a sprite.
__declspec(naked) void __fastcall SpritePlace(CEngineObject*, void*, float, float, float) EI_ORIGINAL(0x005E6FA0)

// A CString an item's virtual method returns (by hidden pointer); released when it goes.
struct ReturnedString {
    char* data = nullptr;
    mfc::CString* Slot() { return reinterpret_cast<mfc::CString*>(&data); }
    ~ReturnedString() { Slot()->~CString(); }
};
// The item's slot 19 (the picture's name) and 20 (the model's name).
void ItemName(CEngineObject* item, int slot, ReturnedString* out) {
    using Fn = mfc::CString*(__thiscall*)(CEngineObject*, mfc::CString*);
    reinterpret_cast<Fn>((*reinterpret_cast<void***>(item))[slot])(item, out->Slot());
}

} // namespace

void CHudQuickItems::SetUnit(CUnit* unit) {
    mfc::CString tip;
    mfc::CString unused;
    unit_ = unit;
    ClearSceneObjects();
    unknown_14C = 0;
    selected_ = -1;
    for (int i = 0; i < 8; ++i) {
        regions_.data_[i]->SetTip(0, 1, 0);
        regionItem_[i] = -1;
    }
    EntriesSetSize(&entriesVtable_, nullptr, 0, -1);
    if (!unit_) return;

    // The entry of the sprite just added.
    auto finish = [&](CEngineObject* item, bool own) {
        EntriesSetSize(&entriesVtable_, nullptr, unknown_14C, -1);
        Entry& entry = entries_[unknown_14C - 1];
        entry.own = own;
        entry.id = *reinterpret_cast<const int*>(reinterpret_cast<const std::uint8_t*>(item) + 0xC);
        entry.item = item;
    };
    // The sprite just added: placed in its row (x from 007428AC, 695; Rebirth moves it), its
    // region pointing at it, scaled, opaque.
    auto place = [&](int slot) {
        CEngineObject* sprite = sceneObjects_.data_[unknown_14C - 1];
        const float right = *reinterpret_cast<const volatile float*>(0x007428AC);
        if (slot < 4)
            SpritePlace(sprite, nullptr, right - static_cast<float>(slot * 50), 575.0f, 20.0f);
        else
            SpritePlace(sprite, nullptr, right - static_cast<float>(slot * 50 - 200), 525.0f, 20.0f);
        int last = unknown_14C - 1;
        regionItem_[slot] = last;
        if (last >= 0 && last < unknown_14C) {
            CEngineObject* shown = sceneObjects_.data_[last];
            SpriteScale(shown, nullptr, slot < 4 ? 0.55f : 0.85f);
            std::uint32_t opaque = 0xFF000000;
            std::memcpy(reinterpret_cast<std::uint8_t*>(shown) + 0xD8, &opaque, 4);
        }
    };

    // The unit's four quick items in the bottom row.
    for (unsigned slot = 0; slot < 4; ++slot) {
        CEngineObject* item = unit_->GetQuickItem(slot);
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
        regions_.data_[slot]->SetTipString(tip, 1, static_cast<int>(slot) + 0x2A);
        place(static_cast<int>(slot));
    }

    // The local player's own unit also shows up to four of the player's 0x3009 items above, from
    // the last of the player's items backwards (node: +4 previous, +8 item).
    std::uint8_t* player = *reinterpret_cast<std::uint8_t**>(LocalView(reinterpret_cast<void*>(0x007AFEA5)) + 0x1C);
    if (!PlayerOwns(player, nullptr, unit_)) return;
    player = *reinterpret_cast<std::uint8_t**>(LocalView(reinterpret_cast<void*>(0x007AFEA5)) + 0x1C);
    std::uint8_t* node = *reinterpret_cast<std::uint8_t**>(player + 0xE8);
    if (!node) return;
    int slot = 4;
    while (node) {
        if (slot - 4 >= 4) break;
        CEngineObject* item = *reinterpret_cast<CEngineObject**>(node + 8);
        node = *reinterpret_cast<std::uint8_t**>(node + 4);
        if (!item || *reinterpret_cast<const int*>(reinterpret_cast<const std::uint8_t*>(item) + 0x10) != 0x3009)
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
        ItemTexts(*reinterpret_cast<void**>(0x007AF788), nullptr, item, &tip, &unused, &unused);
        regions_.data_[slot]->SetTipString(tip, 1, 0);
        place(slot);
        ++slot;
    }
}
EI_REPLACE_METHOD(0x0063D2B0, CHudQuickItems::SetUnit);

// --- 00638280: the battle screen's update ------------------------------------------------------

namespace {

// The panels' units (thiscall): weapon bar 0063C180, quick items 0063D2B0 (through its address),
// spells 0063E820 (RET 4), actions 0063F800 (RET 8).
__declspec(naked) void __fastcall WeaponBarSetUnit(CUiWindow*, void*, CUnit*) EI_ORIGINAL(0x0063C180)
__declspec(naked) void __fastcall QuickItemsSetUnit(CUiWindow*, void*, CUnit*) EI_ORIGINAL(0x0063D2B0)
__declspec(naked) void __fastcall SpellsSetUnit(CUiWindow*, void*, CUnit*) EI_ORIGINAL(0x0063E820)
__declspec(naked) void __fastcall ActionsSetUnit(CUiWindow*, void*, CUnit*, int) EI_ORIGINAL(0x0063F800)
// 00530930 (__fastcall on a unit): its prototype record.
__declspec(naked) std::uint8_t* __fastcall UnitPrototype(CUnit*) EI_ORIGINAL(0x00530930)
// 0063B920 (thiscall on the battle screen, RET 8): shows `unit` in the unit panel (its +0x164).
__declspec(naked) void __fastcall ShowPanelUnit(CBattleScreenSetup*, void*, CUnit*, void*) EI_ORIGINAL(0x0063B920)
// 0057F440 (thiscall on the session 007AFE20): whether this is a network client (AL).
__declspec(naked) bool __fastcall IsNetworkClient(void*) EI_ORIGINAL(0x0057F440)
// The world (007AF5B8, thiscall): 0055FB00 the unit with an id (RET 4), 00566030 (RET 4) makes
// `unit` the controlled one, 005656A0 (RET 8) the units inside a selection box into a list.
__declspec(naked) CUnit* __fastcall FindUnit(void*, void*, int) EI_ORIGINAL(0x0055FB00)
__declspec(naked) void __fastcall WorldControl(void*, void*, CUnit*) EI_ORIGINAL(0x00566030)
__declspec(naked) void __fastcall UnitsInBox(void*, void*, const void*, void*) EI_ORIGINAL(0x005656A0)
constexpr std::uintptr_t kWorld = 0x007AF5B8;

// vector<int> pieces (library): 005F8F10 constructs from an allocator (RET 4), 0064B620
// (__fastcall) constructs an element, 004E2FA0 the reallocating insert (RET 0xC), 0060B1E0 /
// 0064B750 (__fastcall) sort's introsort loop (RET 8) and final insertion sort, 0064AF30 assigns
// (RET 4), 004E2A20 / 005F9880 the destructors of the two instantiations.
using IdVector = CBattleScreenSetup::IdVector;
using PartyIds = CBattleScreenSetup::PartyIds;
__declspec(naked) void __fastcall IdsConstruct(IdVector*, void*, const void*) EI_ORIGINAL(0x005F8F10)
__declspec(naked) void __fastcall IdConstruct(int*, const int*) EI_ORIGINAL(0x0064B620)
__declspec(naked) void __fastcall IdsInsertOverflow(IdVector*, void*, int*, const int*, int) EI_ORIGINAL(0x004E2FA0)
__declspec(naked) void __fastcall IdsIntroSort(int*, int*, const int*, int) EI_ORIGINAL(0x0060B1E0)
__declspec(naked) void __fastcall IdsFinalSort(int*, int*) EI_ORIGINAL(0x0064B750)
__declspec(naked) void __fastcall IdsAssign(IdVector*, void*, const IdVector*) EI_ORIGINAL(0x0064AF30)
__declspec(naked) void __fastcall IdsDestroyA(IdVector*) EI_ORIGINAL(0x004E2A20)
__declspec(naked) void __fastcall IdsDestroyB(IdVector*) EI_ORIGINAL(0x005F9880)
// 006381E0 (thiscall, RET 4): both id vectors equal (AL).
__declspec(naked) bool __fastcall PartyIdsEqual(const PartyIds*, void*, const PartyIds*) EI_ORIGINAL(0x006381E0)

// The party CArray<CUnit*> (+0x168): 005EBDB0 SetSize (RET 8), 0060AD50 SetAtGrow (RET 8),
// 0060AEC0 (stdcall) ConstructElements; 006E79A0 the game's memmove.
__declspec(naked) void __fastcall PartySetSize(UiDwordArray*, void*, int, int) EI_ORIGINAL(0x005EBDB0)
__declspec(naked) void __fastcall PartySetAtGrow(UiDwordArray*, void*, int, CUnit*) EI_ORIGINAL(0x0060AD50)
__declspec(naked) void __stdcall PartyConstruct(void*, int) EI_ORIGINAL(0x0060AEC0)
__declspec(naked) void* __cdecl GameMemmove(void*, const void*, std::size_t) EI_ORIGINAL(0x006E79A0)
// 006407E0 (thiscall on the portraits, RET 4): rebuilds them (`cameraFirst`: the first is the
// camera's player; the original pushes a dword whose low byte only is set).
__declspec(naked) void __fastcall PortraitsRebuild(CUiWindow*, void*, int) EI_ORIGINAL(0x006407E0)
// 0040D420 (thiscall on the camera 0079B2A8, RET 4): follows the unit with an id. 00637DC0
// (thiscall on the battle screen): the selection's refresh.
__declspec(naked) void __fastcall CameraFollow(void*, void*, int) EI_ORIGINAL(0x0040D420)
__declspec(naked) void __fastcall RefreshSelection(CBattleScreenSetup*) EI_ORIGINAL(0x00637DC0)

// 006010E0 (thiscall): the children's update. 005F8A80 (thiscall on the interface, RET 4): the
// next queued message. 005F7350 (RET 0xC): opens a window on top. 00634650 (RET 0x18): the
// message box's constructor.
__declspec(naked) void __fastcall UpdateChildren(CUiWindow*) EI_ORIGINAL(0x006010E0)
__declspec(naked) void __fastcall ShowNextMessage(CInterface3D*, void*, int) EI_ORIGINAL(0x005F8A80)
__declspec(naked) void __fastcall PushWindow(CInterface3D*, void*, void*, int, int) EI_ORIGINAL(0x005F7350)
__declspec(naked) void* __fastcall MessageBoxConstruct(void*, void*, CInterface3D*, const char*, const char*, int, int,
                                                       int) EI_ORIGINAL(0x00634650)

// The game menu object 007AFEA5: 00578BA0 the local player's data (its +0x30 / +0x34 unit
// records, 0x784 bytes each), 00578A20 (RET 0xC) whether the saved zone exit can be taken now
// (the records, the exit, a second array it fills).
__declspec(naked) std::uint8_t* __fastcall LocalPlayerData(void*) EI_ORIGINAL(0x00578BA0)
__declspec(naked) int __fastcall CanLeaveZone(void*, void*, void*, int, void*) EI_ORIGINAL(0x00578A20)
// 0059D2E0 (thiscall on a CArray<void*>, RET 8): SetAtGrow.
__declspec(naked) void __fastcall RecordsSetAtGrow(void*, void*, int, void*) EI_ORIGINAL(0x0059D2E0)

// STLport list<CUnit*> (00411850 constructor, RET 4) and list<int> (00404A70 constructor,
// 004049D0 end(), 00404A40 insert, RET 4 / 4 / 0xC); 00404D70 (__fastcall) frees a node.
struct ListNode {
    ListNode* next;
    ListNode* prev;
    std::uintptr_t value;
};
struct List {
    ListNode* header;
};
__declspec(naked) void __fastcall UnitListConstruct(List*, void*, const void*) EI_ORIGINAL(0x00411850)
__declspec(naked) void __fastcall IndexListConstruct(List*, void*, const void*) EI_ORIGINAL(0x00404A70)
__declspec(naked) ListNode** __fastcall IndexListEnd(List*, void*, ListNode**) EI_ORIGINAL(0x004049D0)
__declspec(naked) ListNode** __fastcall IndexListInsert(List*, void*, ListNode**, ListNode*, const int*)
    EI_ORIGINAL(0x00404A40)
__declspec(naked) void __fastcall NodeFree(void*, std::uint32_t) EI_ORIGINAL(0x00404D70)
void*& NodeFreeList() { return *reinterpret_cast<void**>(0x0079B228); }   // the 12-byte nodes

// An MFC CArray<void*> as the update builds them on the stack (vtable 0073FAA4).
struct PtrArray {
    void* vtable;
    void** data;
    int size;
    int capacity;
    int growBy;
};

template <typename T>
T& At(void* p, int offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(p) + offset);
}

// push_back of `*value`: the constructing helper, or the reallocating insert when full.
void PushId(IdVector* ids, const int* value) {
    if (ids->end != ids->capacity) {
        IdConstruct(ids->end, value);
        ++ids->end;
    } else {
        IdsInsertOverflow(ids, nullptr, ids->end, value, 1);
    }
}
// The same with the construction inlined (as the original's third case).
void PushIdInline(IdVector* ids, const int* value) {
    if (ids->end != ids->capacity) {
        if (ids->end) *ids->end = *value;
        ++ids->end;
    } else {
        IdsInsertOverflow(ids, nullptr, ids->end, value, 1);
    }
}

// std::sort: the introsort loop with depth 2 * floor(log2(n)), then the final insertion sort.
void SortIds(IdVector* ids) {
    if (ids->begin == ids->end) return;
    int depth = 0;
    for (int n = static_cast<int>(ids->end - ids->begin); n != 1; n >>= 1) ++depth;
    IdsIntroSort(ids->begin, ids->end, nullptr, depth * 2);
    IdsFinalSort(ids->begin, ids->end);
}

// A multiplayer client: the party is the session's players' units, the camera's player first.
void RebuildClientParty(CBattleScreenSetup* screen) {
    std::uint8_t* session = *reinterpret_cast<std::uint8_t**>(0x0079B5D8);
    ListNode** players = reinterpret_cast<ListNode**>(session + 0x25D8);
    PartyIds ids;
    std::uint8_t allocators[2] = {};
    IdsConstruct(&ids.withUnit, nullptr, &allocators[0]);
    IdsConstruct(&ids.without, nullptr, &allocators[1]);
    ids.camera = -1;
    for (ListNode* node = (*players)->next; node != *players; node = node->next) {
        std::uint8_t* player = reinterpret_cast<std::uint8_t*>(node->value);
        int* unitId = &At<int>(player, 0x2C);
        int* playerId = &At<int>(player, 0x30);
        if (At<int>(player, 0x24) == 0 && *unitId >= 0) {
            if (!FindUnit(reinterpret_cast<void*>(kWorld), nullptr, *unitId)) {
                PushId(&ids.without, playerId);
                continue;
            }
            PushId(&ids.withUnit, unitId);
            std::uint8_t* camera = *reinterpret_cast<std::uint8_t**>(session + 0x25F0);
            if (camera && !(At<std::uint32_t>(camera, 4) & 0x80000000u) &&
                At<std::uint8_t*>(camera, 0xC) == player)
                ids.camera = *unitId;
        } else {
            PushIdInline(&ids.without, playerId);
        }
    }
    SortIds(&ids.withUnit);
    SortIds(&ids.without);
    if (!PartyIdsEqual(&ids, nullptr, reinterpret_cast<const PartyIds*>(&screen->partyWithUnit_))) {
        auto* party = reinterpret_cast<UiDwordArray*>(screen->partyArray_);
        PartySetSize(party, nullptr, 0, -1);
        bool cameraFirst = false;
        for (int* id = ids.withUnit.begin; id != ids.withUnit.end; ++id) {
            int unitId = *id;
            CUnit* unit = FindUnit(reinterpret_cast<void*>(kWorld), nullptr, unitId);
            if (!unit) continue;
            if (unitId == ids.camera) {
                WorldControl(reinterpret_cast<void*>(kWorld), nullptr, unit);
                // Insert at the front (the original's inlined InsertAt).
                int n = party->size_;
                if (n <= 0) {
                    PartySetSize(party, nullptr, 1, -1);
                } else {
                    PartySetSize(party, nullptr, n + 1, -1);
                    GameMemmove(party->data_ + 1, party->data_, static_cast<std::size_t>(n) * 4);
                    PartyConstruct(party->data_, 1);
                }
                party->data_[0] = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(unit));
                cameraFirst = true;
            } else {
                PartySetAtGrow(party, nullptr, party->size_, unit);
            }
        }
        PortraitsRebuild(screen->portraits_, nullptr, cameraFirst ? 1 : 0);
        if (screen->unknown_210 != ids.camera) {
            CameraFollow(reinterpret_cast<void*>(0x0079B2A8), nullptr, ids.camera);
            RefreshSelection(screen);
        }
        IdsAssign(&screen->partyWithUnit_, nullptr, &ids.withUnit);
        screen->unknown_210 = ids.camera;
        IdsAssign(&screen->partyWithout_, nullptr, &ids.without);
    }
    IdsDestroyB(&ids.without);
    IdsDestroyA(&ids.withUnit);
}

// Frees a list's nodes (00404D70) and empties it (the header is freed by the caller).
void FreeListNodes(List* list) {
    ListNode* header = list->header;
    for (ListNode* node = header->next; node != header;) {
        ListNode* next = node->next;
        if (node) NodeFree(node, 0xC);
        node = next;
    }
    header->next = header;
    header->prev = header;
}

} // namespace

void CBattleScreenSetup::Update() {
    // The one selected party unit's request (its slot 55): the panels lose their unit and the
    // unit panel shows it.
    auto* party = reinterpret_cast<UiDwordArray*>(partyArray_);
    CUnit* first = nullptr;
    int selected = 0;
    for (int i = 0; i < party->size_; ++i) {
        auto* unit = reinterpret_cast<CUnit*>(static_cast<std::uintptr_t>(party->data_[i]));
        if (unit && (At<std::uint8_t>(unit, 8) & 0x10)) {
            if (!first) first = unit;
            ++selected;
        }
    }
    if (first) {
        using AskFn = int(__thiscall*)(CUnit*);
        if (reinterpret_cast<AskFn>((*reinterpret_cast<void***>(first))[55])(first) && selected == 1) {
            WeaponBarSetUnit(weaponBar_, nullptr, nullptr);
            QuickItemsSetUnit(quickItems_, nullptr, nullptr);
            SpellsSetUnit(spellColumn_, nullptr, nullptr);
            ActionsSetUnit(actionColumn_, nullptr, nullptr, 0);
            CUiWindow* panel = unitPanel_;
            if (first != At<CUnit*>(panel, 0x14C)) {
                At<CUnit*>(panel, 0x14C) = first;
                At<int>(panel, 0x15C) = At<int>(UnitPrototype(first), 0x20);
                At<int>(panel, 0x154) = At<int>(UnitPrototype(At<CUnit*>(panel, 0x14C)), 0x24);
                ShowPanelUnit(At<CBattleScreenSetup*>(panel, 0x148), nullptr, At<CUnit*>(panel, 0x14C),
                              &At<std::uint8_t>(panel, 0x164));
            }
        }
    }

    if (IsNetworkClient(reinterpret_cast<void*>(0x007AFE20))) RebuildClientParty(this);

    UpdateChildren(this);
    ShowNextMessage(interface_, nullptr, 0);

    // A zone exit saved with the game: once it can be taken, the "leave_zone" question.
    if (savedFlag_) {
        PtrArray records = {reinterpret_cast<void*>(0x0073FAA4), nullptr, 0, 0, 0};
        PtrArray second = {reinterpret_cast<void*>(0x0073FAA4), nullptr, 0, 0, 0};
        void* menu = reinterpret_cast<void*>(0x007AFEA5);
        std::uint8_t* data = LocalPlayerData(menu);
        for (int i = 0; i < At<int>(data, 0x34); ++i)
            RecordsSetAtGrow(&records, nullptr, records.size, At<std::uint8_t*>(data, 0x30) + i * 0x784);
        if (CanLeaveZone(menu, nullptr, &records, savedSelection_, &second)) {
            void* memory = gamecrt::operator_new(0x180);
            void* box = memory ? MessageBoxConstruct(memory, nullptr, interface_,
                                                     reinterpret_cast<const char*>(0x007943FC),    // "leave_zone"
                                                     reinterpret_cast<const char*>(0x00794408),    // "leave_zone_msg"
                                                     0x10000, 3, 1)
                               : nullptr;
            PushWindow(interface_, nullptr, box, 1, 1);
            savedFlag_ = 0;
        }
        second.vtable = reinterpret_cast<void*>(0x0073FAA4);
        if (second.data) gamecrt::operator_delete(second.data);
        records.vtable = reinterpret_cast<void*>(0x0073FAA4);
        if (records.data) gamecrt::operator_delete(records.data);
    }

    // The selection box: party units inside it are marked (flag 0x100 and their figure's slot 26
    // with 1), the others unmarked (with 0); dead ones (0x10) are left alone.
    if (unknown_1C1) {
        std::uint8_t allocator = 0;
        List units;
        UnitListConstruct(&units, nullptr, &allocator);
        UnitsInBox(reinterpret_cast<void*>(kWorld), nullptr, selectionBox_, &units);
        List inside;
        IndexListConstruct(&inside, nullptr, &allocator);
        for (ListNode* node = units.header->next; node != units.header; node = node->next) {
            for (int i = 0; i < party->size_; ++i) {
                if (party->data_[i] != node->value) continue;
                ListNode* end;
                IndexListEnd(&inside, nullptr, &end);
                ListNode* inserted;
                IndexListInsert(&inside, nullptr, &inserted, end, &i);
            }
        }
        using MarkFn = void(__thiscall*)(void*, int);
        auto mark = [](CUnit* unit, int on) {
            void* figure = At<void*>(unit, 0x94);
            reinterpret_cast<MarkFn>((*reinterpret_cast<void***>(figure))[26])(figure, on);
        };
        for (int i = 0; i < party->size_; ++i) {
            auto* unit = reinterpret_cast<CUnit*>(static_cast<std::uintptr_t>(party->data_[i]));
            std::uint32_t flags = At<std::uint32_t>(unit, 8);
            if (!(flags & 0x10) && (flags & 0x100)) {
                At<std::uint32_t>(unit, 8) = flags & ~0x100u;
                mark(unit, 0);
            }
        }
        for (ListNode* node = inside.header->next; node != inside.header; node = node->next) {
            auto* unit = reinterpret_cast<CUnit*>(static_cast<std::uintptr_t>(party->data_[node->value]));
            std::uint32_t flags = At<std::uint32_t>(unit, 8);
            if (!(flags & 0x10) && !(flags & 0x100)) {
                At<std::uint32_t>(unit, 8) = flags | 0x100;
                mark(unit, 1);
            }
        }
        FreeListNodes(&inside);
        if (inside.header) NodeFree(inside.header, 0xC);
        FreeListNodes(&units);
        if (units.header) {
            *reinterpret_cast<void**>(units.header) = NodeFreeList();
            NodeFreeList() = units.header;
        }
    }

    // The quest sound asked for.
    if (unknown_1FC != 0) {
        const char* sound = nullptr;
        switch (unknown_1FC) {
        case 1: sound = reinterpret_cast<const char*>(0x007943B8); break;   // buttons\quest.wav
        case 2: sound = reinterpret_cast<const char*>(0x007943CC); break;   // buttons\subcomplete.wav
        case 3: sound = reinterpret_cast<const char*>(0x007943E4); break;   // buttons\complete.wav
        }
        if (sound) interface_->PlaySound(sound, 0);
        unknown_1FC = 0;
    }
}
namespace {
void __fastcall BattleScreenUpdate(CBattleScreenSetup* self) { self->Update(); }
} // namespace
EI_REPLACE(0x00638280, BattleScreenUpdate);
