// Checks for the battle screen's update and the quick item panel's unit (ui_battle_update.cpp).
//
// CHudQuickItems::SetUnit (0063D2B0) runs side by side: it drops every sprite, tooltip and entry
// first, so the original runs, its result is recorded (fields, entries, region items, and each
// sprite's position, orientation, texture corner and colour), then ours runs over it and both
// must agree.
// CBattleScreenSetup::Update (00638280, every frame) runs ours only: the quest sound request is
// consumed, a cleared zone-exit flag means a window was opened, a single player's party array
// is left alone, and with a selection box every live party unit is marked exactly when the
// world's box query (005656A0) lists it.
#include "verify.h"

#include "../../src/game/ui_camp.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace {

template <typename T>
T Get(const void* p, int offset) {
    T v;
    std::memcpy(&v, static_cast<const std::uint8_t*>(p) + offset, sizeof(T));
    return v;
}

void Append(std::vector<std::uint8_t>& out, const void* p, std::size_t n) {
    auto* b = static_cast<const std::uint8_t*>(p);
    out.insert(out.end(), b, b + n);
}

std::vector<std::uint8_t> QuickItemsState(CHudQuickItems* panel) {
    std::vector<std::uint8_t> out;
    Append(out, &panel->unknown_14C, 4);
    Append(out, &panel->selected_, 4);
    Append(out, &panel->unit_, 4);
    Append(out, &panel->entryCount_, 4);
    if (panel->entries_) Append(out, panel->entries_, static_cast<std::size_t>(panel->entryCount_) * 12);
    Append(out, panel->regionItem_, sizeof(panel->regionItem_));
    Append(out, &panel->sceneObjects_.size_, 4);
    for (int i = 0; i < panel->sceneObjects_.size_; ++i) {
        auto* sprite = reinterpret_cast<const std::uint8_t*>(panel->sceneObjects_.data_[i]);
        if (!sprite) continue;
        Append(out, sprite + 0x1C, 12);    // local position
        Append(out, sprite + 0x34, 16);    // local rotation
        Append(out, sprite + 0xCC, 16);    // texture corner, -1, colour
    }
    return out;
}

} // namespace

// 0063D2B0: PUSH -1; PUSH imm32 (7).
static void(__fastcall* s_setUnit)(CHudQuickItems*, void*, CUnit*);
static void __fastcall QuickItemsSetUnitVerify(CHudQuickItems* self, void*, CUnit* unit) {
    s_setUnit(self, nullptr, unit);
    std::vector<std::uint8_t> expected = QuickItemsState(self);
    self->SetUnit(unit);
    std::vector<std::uint8_t> actual = QuickItemsState(self);
    int at = -1;
    if (expected.size() != actual.size()) {
        at = -2;
    } else {
        for (std::size_t i = 0; i < expected.size(); ++i)
            if (expected[i] != actual[i]) {
                at = static_cast<int>(i);
                break;
            }
    }
    ei::verify::Check("CHudQuickItems::SetUnit", -1, at, "state byte %d differs (sizes %d/%d, sprites %d)", at,
                      static_cast<int>(expected.size()), static_cast<int>(actual.size()), self->unknown_14C);
}
EI_VERIFY(0x0063D2B0, QuickItemsSetUnitVerify, 7, s_setUnit);

namespace {

// The selection box query, as the update makes it (list<CUnit*> 00411850, world 005656A0, nodes
// freed with 00404D70 and the header put on the 12-byte free list).
struct ListNode {
    ListNode* next;
    ListNode* prev;
    std::uintptr_t value;
};
using ListConstructFn = void(__thiscall*)(ListNode**, const void*);
using BoxFn = void(__thiscall*)(void*, const void*, ListNode**);
using NodeFreeFn = void(__fastcall*)(void*, std::uint32_t);

std::vector<std::uintptr_t> UnitsInBox(const void* box) {
    std::uint8_t allocator = 0;
    ListNode* header = nullptr;
    reinterpret_cast<ListConstructFn>(0x00411850)(&header, &allocator);
    reinterpret_cast<BoxFn>(0x005656A0)(reinterpret_cast<void*>(0x007AF5B8), box, &header);
    std::vector<std::uintptr_t> out;
    for (ListNode* node = header->next; node != header; node = node->next) out.push_back(node->value);
    for (ListNode* node = header->next; node != header;) {
        ListNode* next = node->next;
        reinterpret_cast<NodeFreeFn>(0x00404D70)(node, 0xC);
        node = next;
    }
    header->next = header;
    header->prev = header;
    void*& freeList = *reinterpret_cast<void**>(0x0079B228);
    *reinterpret_cast<void**>(header) = freeList;
    freeList = header;
    return out;
}

} // namespace

// 00638280: ours only.
static void* s_battleUpdate;
static void __fastcall BattleScreenUpdateVerify(CBattleScreenSetup* self) {
    const UiDwordArray* party = reinterpret_cast<const UiDwordArray*>(self->partyArray_);
    bool client = reinterpret_cast<bool(__thiscall*)(void*)>(0x0057F440)(reinterpret_cast<void*>(0x007AFE20));
    std::uint32_t* partyData = party->data_;
    int partySize = party->size_;
    bool savedFlag = self->savedFlag_ != 0;
    CUiWindow* top = self->interface_->GetActiveWindow();

    self->Update();

    ei::verify::Check("CBattleScreenSetup::Update", 0, self->unknown_1FC, "quest sound %d left", self->unknown_1FC);
    if (savedFlag && !self->savedFlag_) {
        ei::verify::Check("CBattleScreenSetup::Update", true, self->interface_->GetActiveWindow() != top,
                          "zone exit asked without a new window");
    }
    if (!client) {
        ei::verify::Check("CBattleScreenSetup::Update", true, party->data_ == partyData && party->size_ == partySize,
                          "single player party changed (%d -> %d)", partySize, party->size_);
    }
    if (self->unknown_1C1) {
        std::vector<std::uintptr_t> inside = UnitsInBox(self->selectionBox_);
        int wrong = -1;
        for (int i = 0; i < party->size_ && wrong < 0; ++i) {
            void* unit = reinterpret_cast<void*>(static_cast<std::uintptr_t>(party->data_[i]));
            std::uint32_t flags = Get<std::uint32_t>(unit, 8);
            if (flags & 0x10) continue;
            bool listed = false;
            for (std::uintptr_t u : inside) listed |= u == party->data_[i];
            if (((flags & 0x100) != 0) != listed) wrong = i;
        }
        ei::verify::Check("CBattleScreenSetup::Update", -1, wrong, "party unit %d marked wrongly", wrong);
    }
}
EI_VERIFY(0x00638280, BattleScreenUpdateVerify, 0, s_battleUpdate);
