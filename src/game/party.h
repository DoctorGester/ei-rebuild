#pragma once

// CParty (vtable 00743000, GetClassName 0065F560 "CParty"; constructor 00661050, destructor
// 006611D0): the local player's party data: the items it owns (+0x40), the party unit records,
// and a second interface at +0x2C. Its virtual slots 13-26 are the manager getters CWorld has
// too (24: 0055E940 makes the party's own copy of an item). Only what the item list uses is
// declared. See docs/notes/objects.md.

#include "engine_object.h"
#include "map_object_figures.h"

#include <cstddef>

class CItem;

class CParty : public CEngineObject {
public:
    virtual void Slot13();
    virtual void Slot14();
    virtual void Slot15();
    virtual void Slot16();
    virtual void Slot17();
    virtual void Slot18();
    virtual void Slot19();
    virtual void Slot20();
    virtual void Slot21();
    virtual void Slot22();
    virtual void Slot23();
    // 24: 0055E940. A new item made like `item` (the item factory, slot 18) and given its values
    // (its slot 15 with the spell manager, slot 22).
    virtual CItem* CopyItem(CItem* item);

    // 00661DB0 (RET 0xC; party_items.cpp). Puts `item` among the party's items with a count of 1
    // (with `keepCount`, the item's own +0x48). With `merge`, an item the list already has that is
    // the same (slot 14) only gets the count added and is returned. Otherwise the party's copy
    // (slot 24) gets the count, a reference and the end of the list, and is returned. Null for
    // a null item.
    // The flags are read as bytes (their low bytes).
    CItem* AddItem(CItem* item, int keepCount, int merge);
    // 00661F30 (RET 8; party_items.cpp). Takes `item` out of the list: when its count is above 1
    // and not `all`, the count drops by one and the item is returned; otherwise it is released
    // and removed, and the result is null (also when it is not in the list).
    CItem* RemoveItem(CItem* item, int all);   // `all` read as a byte

    std::uint8_t unknown_1C[0x40 - 0x1C];
    AlignedPtrArray items_;          // +0x40 CItem*
};
static_assert(offsetof(CParty, items_) == 0x40);
