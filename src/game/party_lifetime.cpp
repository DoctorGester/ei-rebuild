// Party unit records: constructor and destructor bodies, the item lists' clear, and the party's
// record array (MFC CArray::SetSize and ConstructElements). The record's item lists are aligned
// pointer arrays; four more copies of their SetSize (00519D70) are registered here. See party.h and
// docs/notes/items_spells.md ("Party").
#include "party.h"
#include "unit.h"

#include "lib/crt.h"

#include "ei/hook.h"

#include <cstring>
#include <initializer_list>
#include <new>

namespace {

// The aligned block of a list (kept at data[-1]) freed, the pointer left as it is.
void FreeStorage(AlignedPtrArray& list) {
    if (list.data != nullptr) gamecrt::operator_delete(list.data[-1]);
}

void ReleaseAll(const AlignedPtrArray& list) {
    for (int i = 0; i < list.size; ++i) static_cast<CEngineObject*>(list.data[i])->Release();
}

// The values the constructor and destructor set (twice in the constructor, around ClearItems).
void SetDefaults(PartyUnitRecord& record) {
    record.unknown_770 = 2;
    record.weaponIndex = 0;
    record.unknown_778 = 0;
    record.unknown_77C = -1;
}

// MFC's default growth: an eighth of the size (rounded towards zero), 4..1024.
int DefaultGrowBy(int size) {
    int grow = size / 8;
    if (grow < 4) return 4;
    return grow > 1024 ? 1024 : grow;
}

} // namespace

// --- PartyUnitRecord ----------------------------------------------------------------------------

PartyUnitRecord* PartyUnitRecord::Construct() {
    for (mfc::CString& text : texts) new (&text) mfc::CString();
    for (CEngineObject*** data : {&weapons, &quickItems, &armor, &spells}) {
        AlignedPtrArray& list = ItemList(*data);
        list.data = nullptr;
        list.growBy = 0;
        list.capacity = 0;
        list.size = 0;
    }
    unknown_724 = 0.8f;
    unknown_728 = 0.2f;
    height = 1.0f;
    SetDefaults(*this);
    ClearItems();
    SetDefaults(*this);
    unknown_780 = 1;
    return this;
}
EI_REPLACE_METHOD(0x0065F5F0, PartyUnitRecord::Construct);

void PartyUnitRecord::Destruct() {
    ClearItems();
    SetDefaults(*this);
    unknown_780 = 1;
    FreeStorage(ItemList(spells));
    FreeStorage(ItemList(armor));
    FreeStorage(ItemList(quickItems));
    FreeStorage(ItemList(weapons));
    for (int i = 6; i >= 0; --i) texts[i].~CString();
}
EI_REPLACE_METHOD(0x0065F740, PartyUnitRecord::Destruct);

void PartyUnitRecord::ClearItems() {
    ReleaseAll(ItemList(weapons));
    ReleaseAll(ItemList(quickItems));
    ReleaseAll(ItemList(armor));
    ReleaseAll(ItemList(spells));
    ItemList(weapons).SetSize(0, -1);
    ItemList(quickItems).SetSize(0, -1);
    ItemList(armor).SetSize(0, -1);
    ItemList(spells).SetSize(0, -1);
}
EI_REPLACE_METHOD(0x0065F860, PartyUnitRecord::ClearItems);

// The record lists' own copies of AlignedPtrArray::SetSize (00519D70; the same code): the record
// load 0065F9B0 and 006621F0 call them.
EI_REPLACE_METHOD(0x00663960, AlignedPtrArray::SetSize);
EI_REPLACE_METHOD(0x00663AD0, AlignedPtrArray::SetSize);
EI_REPLACE_METHOD(0x00663C00, AlignedPtrArray::SetSize);
EI_REPLACE_METHOD(0x00663D30, AlignedPtrArray::SetSize);

// --- The record array ---------------------------------------------------------------------------

void __stdcall ConstructPartyUnitRecords(PartyUnitRecord* records, int count) {
    std::memset(records, 0, static_cast<std::size_t>(count) * sizeof(PartyUnitRecord));
    for (PartyUnitRecord* record = records; count != 0; --count, ++record)
        if (record != nullptr) record->Construct();
}
EI_REPLACE(0x00664880, ConstructPartyUnitRecords);

void PartyMemberArray::SetSize(int newSize, int newGrowBy) {
    if (newGrowBy != -1) growBy = newGrowBy;
    if (newSize == 0) {
        if (data != nullptr) {
            for (int i = 0; i != size; ++i) data[i].Destruct();
            gamecrt::operator_delete(data);
            data = nullptr;
        }
        capacity = 0;
        size = 0;
        return;
    }
    if (data == nullptr) {
        data = static_cast<PartyUnitRecord*>(gamecrt::operator_new(static_cast<std::size_t>(newSize) * sizeof(PartyUnitRecord)));
        ConstructPartyUnitRecords(data, newSize);
        capacity = newSize;
        size = newSize;
        return;
    }
    if (newSize <= capacity) {
        if (newSize > size) {
            ConstructPartyUnitRecords(data + size, newSize - size);
        } else {
            for (int i = newSize; i != size; ++i) data[i].Destruct();
        }
        size = newSize;
        return;
    }
    int grow = growBy != 0 ? growBy : DefaultGrowBy(size);
    int newCapacity = capacity + grow;
    if (newSize >= newCapacity) newCapacity = newSize;
    auto* fresh =
        static_cast<PartyUnitRecord*>(gamecrt::operator_new(static_cast<std::size_t>(newCapacity) * sizeof(PartyUnitRecord)));
    std::memcpy(fresh, data, static_cast<std::size_t>(size) * sizeof(PartyUnitRecord));
    ConstructPartyUnitRecords(fresh + size, newSize - size);
    gamecrt::operator_delete(data);
    data = fresh;
    size = newSize;
    capacity = newCapacity;
}
EI_REPLACE_METHOD(0x00663FA0, PartyMemberArray::SetSize);
