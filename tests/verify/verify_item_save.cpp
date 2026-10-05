// Checks for item_save.cpp (the items' Load / Save, slots 9 / 10). Reached by the world's save
// (every new game's autosave) and load (world_load, ui_saveload).
// - Save: the original and ours write into capture streams; bytes and write sizes are compared and
//   the original's writes go on to the real stream.
// - Load counts the spell it finds, so only ours runs, reading through a stream that records what
//   it reads from the real one; the class's original Save (ours when it is marked verified) of the
//   loaded item must then write exactly those bytes in the same pieces.
#include "verify.h"

#include "fake_stream.h"
#include "../../src/game/item.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

// Records writes (Save) or the bytes read from `real` (Load).
class RecordStream : public FakeStream {
public:
    explicit RecordStream(CStream* real) : real_(real) {
        if (real != nullptr) version_ = real->version_;
    }
    std::uint32_t Read(void* buffer, std::uint32_t size) override {
        std::uint32_t n = real_ != nullptr ? real_->Read(buffer, size) : 0;
        Record(buffer, size);
        return n;
    }
    void Write(const void* buffer, std::uint32_t size) override { Record(buffer, size); }
    void Forward(CStream* to) const {
        std::size_t pos = 0;
        for (std::uint32_t size : chunks) {
            to->Write(bytes.data() + pos, size);
            pos += size;
        }
    }
    bool Same(const RecordStream& other) const { return bytes == other.bytes && chunks == other.chunks; }
    int FirstDifference(const RecordStream& other) const {
        std::size_t n = bytes.size() < other.bytes.size() ? bytes.size() : other.bytes.size();
        for (std::size_t i = 0; i < n; ++i)
            if (bytes[i] != other.bytes[i]) return static_cast<int>(i);
        return bytes.size() == other.bytes.size() ? -1 : static_cast<int>(n);
    }

    std::vector<unsigned char> bytes;
    std::vector<std::uint32_t> chunks;

private:
    void Record(const void* buffer, std::uint32_t size) {
        auto* p = static_cast<const unsigned char*>(buffer);
        bytes.insert(bytes.end(), p, p + size);
        chunks.push_back(size);
    }
    CStream* real_;
};

template <typename Item>
using StreamFn = void(__fastcall*)(Item*, void*, CStream*);

template <typename Item>
void CheckSave(const char* name, StreamFn<Item> original, Item* self, CStream* stream) {
    RecordStream expected(nullptr), actual(nullptr);
    original(self, nullptr, &expected);
    self->SaveItem(&actual);
    ei::verify::Check(name, true, expected.Same(actual), "item %d (kind %X): %u vs %u bytes, first difference at %d",
                      self->id_, self->type_, static_cast<unsigned>(expected.bytes.size()),
                      static_cast<unsigned>(actual.bytes.size()), expected.FirstDifference(actual));
    expected.Forward(stream);
}

template <typename Item>
void CheckLoad(const char* name, StreamFn<Item> originalSave, Item* self, CStream* stream) {
    RecordStream read(stream), written(nullptr);
    self->LoadItem(&read);
    if (originalSave != nullptr) originalSave(self, nullptr, &written);
    else self->SaveItem(&written);
    ei::verify::Check(name, true, read.Same(written), "item %d (kind %X): %u bytes read, %u written back, first difference at %d",
                      self->id_, self->type_, static_cast<unsigned>(read.bytes.size()),
                      static_cast<unsigned>(written.bytes.size()), read.FirstDifference(written));
}

// Stolen bytes: 006A0360, 006A3CA0, 006A4590 PUSH; PUSH; MOV r,[ESP+0Ch] (6); 006A2EB0, 006A3AA0,
// 006A5680 PUSH ESI; MOV ESI,[ESP+8] (5).
StreamFn<CItem> s_itemSave;
StreamFn<CItemWeapon> s_weaponSave;
StreamFn<CItemArmor> s_armorSave;
StreamFn<CItemQuick> s_quickSave;
StreamFn<CItemQuest> s_questSave;
StreamFn<CItemLoot> s_lootSave;
void* s_unused;

void __fastcall ItemSaveVerify(CItem* self, void*, CStream* s) { CheckSave("CItem::SaveItem", s_itemSave, self, s); }
void __fastcall WeaponSaveVerify(CItemWeapon* self, void*, CStream* s) {
    CheckSave("CItemWeapon::SaveItem", s_weaponSave, self, s);
}
void __fastcall ArmorSaveVerify(CItemArmor* self, void*, CStream* s) {
    CheckSave("CItemArmor::SaveItem", s_armorSave, self, s);
}
void __fastcall QuickSaveVerify(CItemQuick* self, void*, CStream* s) {
    CheckSave("CItemQuick::SaveItem", s_quickSave, self, s);
}
void __fastcall QuestSaveVerify(CItemQuest* self, void*, CStream* s) {
    CheckSave("CItemQuest::SaveItem", s_questSave, self, s);
}
void __fastcall LootSaveVerify(CItemLoot* self, void*, CStream* s) { CheckSave("CItemLoot::SaveItem", s_lootSave, self, s); }

void __fastcall ItemLoadVerify(CItem* self, void*, CStream* s) { CheckLoad("CItem::LoadItem", s_itemSave, self, s); }
void __fastcall WeaponLoadVerify(CItemWeapon* self, void*, CStream* s) {
    CheckLoad("CItemWeapon::LoadItem", s_weaponSave, self, s);
}
void __fastcall ArmorLoadVerify(CItemArmor* self, void*, CStream* s) {
    CheckLoad("CItemArmor::LoadItem", s_armorSave, self, s);
}
void __fastcall QuickLoadVerify(CItemQuick* self, void*, CStream* s) {
    CheckLoad("CItemQuick::LoadItem", s_quickSave, self, s);
}
void __fastcall QuestLoadVerify(CItemQuest* self, void*, CStream* s) {
    CheckLoad("CItemQuest::LoadItem", s_questSave, self, s);
}
void __fastcall LootLoadVerify(CItemLoot* self, void*, CStream* s) { CheckLoad("CItemLoot::LoadItem", s_lootSave, self, s); }

} // namespace

EI_VERIFY(0x006A0360, ItemSaveVerify, 6, s_itemSave);
EI_VERIFY(0x006A2EB0, WeaponSaveVerify, 5, s_weaponSave);
EI_VERIFY(0x006A3AA0, ArmorSaveVerify, 5, s_armorSave);
EI_VERIFY(0x006A3CA0, QuickSaveVerify, 6, s_quickSave);
EI_VERIFY(0x006A4590, QuestSaveVerify, 6, s_questSave);
EI_VERIFY(0x006A5680, LootSaveVerify, 5, s_lootSave);
EI_VERIFY(0x006A0270, ItemLoadVerify, 0, s_unused);
EI_VERIFY(0x006A2E60, WeaponLoadVerify, 0, s_unused);
EI_VERIFY(0x006A3A70, ArmorLoadVerify, 0, s_unused);
EI_VERIFY(0x006A3C80, QuickLoadVerify, 0, s_unused);
EI_VERIFY(0x006A4570, QuestLoadVerify, 0, s_unused);
EI_VERIFY(0x006A5630, LootLoadVerify, 0, s_unused);
