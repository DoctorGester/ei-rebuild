#pragma once

// The items' and the item constructor's compiled tables (C19, engine_object_class.h) in place of game.exe's
// 00743DA4 (CItem), 00743DFC (weapon), 00743E54 (armour), 00743EAC (quick), 00743F04 (quest), 00743F5C (loot),
// 00743FB4 (scroll) and 00744020 (the constructor): ItemClass declares CItem's 22 slots with CItem's bodies, each
// class's tag overrides the slots its table changes with the bodies game.exe's table reached (the `...Impl` bodies
// and the class's members called qualified; class_names_640.h's slots 11 / 12). The constructor bodies
// (item_lifetime.cpp, item_constructor.cpp) store them.

#include "item.h"

#include "class_names_640.h"
#include "engine_object_class.h"
#include "game_strings.h"

template <typename Self>
class ItemClass : public EngineObjectClass<Self, CItem> {
public:
    int Create() override { return Item()->CItem::Create(); }    // 2: 0069FE20
    void Destroy() override { Item()->CItem::Destroy(); }        // 3: 0069FE50
    int RunTick() override { return Item()->CItem::RunTick(); }  // 6: 006A0070
    void Load(CStream* stream) override { Item()->LoadImpl(stream); }   // 9: 006A0270
    void Save(CStream* stream) override { Item()->SaveImpl(stream); }   // 10: 006A0360
    virtual void InitFromRecord(const itemdb::ItemRecordHead* record) { Item()->InitFromRecordImpl(record); }   // 13: 006A0490
    virtual BOOL IsSameAs(CItem* other) { return Item()->IsSameAsImpl(other); }   // 14: 0069FE90
    virtual BOOL CopyTo(CItem* target, CSpellManager* spells) { return Item()->CopyToImpl(target, spells); }   // 15: 0069FFC0
    virtual float GetPrice() { return Item()->price_; }   // 16: 004622F0
    // 17-20: 00462300 / 00462330 / 00462360 / 00462390, "" built in the caller's result.
    virtual mfc::CString ModelName() { return mfc::CString(game::EmptyText()); }
    virtual mfc::CString FirstListName() { return mfc::CString(game::EmptyText()); }
    virtual mfc::CString FigureName() { return mfc::CString(game::EmptyText()); }
    virtual mfc::CString PictureName() { return mfc::CString(game::EmptyText()); }
    virtual ItemUse* GetUse(ItemUse* out) { return Item()->NoUse(out); }   // 21: 004623C0

protected:
    CItem* Item() { return reinterpret_cast<CItem*>(this); }
};

struct CItemClass final : ItemClass<CItemClass> {};

class CItemWeaponClass final : public ItemClass<CItemWeaponClass> {
public:
    void Load(CStream* stream) override { Weapon()->LoadImpl(stream); }   // 9: 006A2E60
    void Save(CStream* stream) override { Weapon()->SaveImpl(stream); }   // 10: 006A2EB0
    const char* ClassName() override { return ItemWeaponClassName(Item()); }   // 11: 006A0570
    int ClassId() override { return ItemWeaponClassId(Item()); }               // 12: 006A0580
    void InitFromRecord(const itemdb::ItemRecordHead* record) override { Weapon()->InitFromRecordImpl(record); }   // 13: 006A0660
    BOOL IsSameAs(CItem* other) override { return Weapon()->IsSameAsImpl(other); }   // 14: 006A06C0
    BOOL CopyTo(CItem* target, CSpellManager* spells) override { return Weapon()->CopyToImpl(target, spells); }   // 15: 006A0730
    mfc::CString ModelName() override { return Weapon()->ModelNameImpl(); }           // 17: 006A0840
    mfc::CString FirstListName() override { return Weapon()->FirstListNameImpl(); }   // 18: 006A0F70
    mfc::CString FigureName() override { return Weapon()->FigureNameImpl(); }         // 19: 006A2750
    mfc::CString PictureName() override { return Weapon()->PictureNameImpl(); }       // 20: 006A27E0

private:
    CItemWeapon* Weapon() { return As<CItemWeapon>(); }
};

class CItemArmorClass final : public ItemClass<CItemArmorClass> {
public:
    void Load(CStream* stream) override { Armor()->LoadImpl(stream); }   // 9: 006A3A70
    void Save(CStream* stream) override { Armor()->SaveImpl(stream); }   // 10: 006A3AA0
    const char* ClassName() override { return ItemArmorClassName(Item()); }   // 11: 006A2F80
    int ClassId() override { return ItemArmorClassId(Item()); }               // 12: 006A2F90
    void InitFromRecord(const itemdb::ItemRecordHead* record) override { Armor()->InitFromRecordImpl(record); }   // 13: 006A3070
    BOOL IsSameAs(CItem* other) override { return Armor()->IsSameAsImpl(other); }   // 14: 006A30D0
    BOOL CopyTo(CItem* target, CSpellManager* spells) override { return Armor()->CopyToImpl(target, spells); }   // 15: 006A3140
    mfc::CString ModelName() override { return Armor()->ModelNameImpl(); }           // 17: 006A3240
    mfc::CString FirstListName() override { return Armor()->FirstListNameImpl(); }   // 18: 006A36A0
    mfc::CString FigureName() override { return Armor()->FigureNameImpl(); }         // 19: 006A3720
    mfc::CString PictureName() override { return Armor()->PictureNameImpl(); }       // 20: 006A37B0

private:
    CItemArmor* Armor() { return As<CItemArmor>(); }
};

class CItemQuickClass final : public ItemClass<CItemQuickClass> {
public:
    void Load(CStream* stream) override { Quick()->LoadImpl(stream); }   // 9: 006A3C80
    void Save(CStream* stream) override { Quick()->SaveImpl(stream); }   // 10: 006A3CA0
    const char* ClassName() override { return ItemQuickClassName(Item()); }   // 11: 006A3AF0
    int ClassId() override { return ItemQuickClassId(Item()); }               // 12: 006A3B00
    void InitFromRecord(const itemdb::ItemRecordHead* record) override { Quick()->InitFromRecordImpl(record); }   // 13: 006A3BE0
    BOOL IsSameAs(CItem* other) override { return Quick()->IsSameAsImpl(other); }   // 14: 006A3C20
    BOOL CopyTo(CItem* target, CSpellManager* spells) override { return Quick()->CopyToImpl(target, spells); }   // 15: 006A3C50
    mfc::CString FigureName() override { return Quick()->FigureNameImpl(); }     // 19: 006A3CD0
    mfc::CString PictureName() override { return Quick()->PictureNameImpl(); }   // 20: 006A4040

private:
    CItemQuick* Quick() { return As<CItemQuick>(); }
};

class CItemQuestClass final : public ItemClass<CItemQuestClass> {
public:
    void Load(CStream* stream) override { Quest()->LoadImpl(stream); }   // 9: 006A4570
    void Save(CStream* stream) override { Quest()->SaveImpl(stream); }   // 10: 006A4590
    const char* ClassName() override { return ItemQuestClassName(Item()); }   // 11: 006A43E0
    int ClassId() override { return ItemQuestClassId(Item()); }               // 12: 006A43F0
    void InitFromRecord(const itemdb::ItemRecordHead* record) override { Quest()->InitFromRecordImpl(record); }   // 13: 006A44D0
    BOOL IsSameAs(CItem* other) override { return Quest()->IsSameAsImpl(other); }   // 14: 006A4510
    BOOL CopyTo(CItem* target, CSpellManager* spells) override { return Quest()->CopyToImpl(target, spells); }   // 15: 006A4540
    mfc::CString FigureName() override { return Quest()->FigureNameImpl(); }     // 19: 006A45C0
    mfc::CString PictureName() override { return Quest()->PictureNameImpl(); }   // 20: 006A47B0
    ItemUse* GetUse(ItemUse* out) override { return Quest()->GetUseImpl(out); }  // 21: 006A43C0

private:
    CItemQuest* Quest() { return As<CItemQuest>(); }
};

class CItemLootClass final : public ItemClass<CItemLootClass> {
public:
    void Load(CStream* stream) override { Loot()->LoadImpl(stream); }   // 9: 006A5630
    void Save(CStream* stream) override { Loot()->SaveImpl(stream); }   // 10: 006A5680
    const char* ClassName() override { return ItemLootClassName(Item()); }   // 11: 006A49C0
    int ClassId() override { return ItemLootClassId(Item()); }               // 12: 006A49D0
    void InitFromRecord(const itemdb::ItemRecordHead* record) override { Loot()->InitFromRecordImpl(record); }   // 13: 006A4AB0
    BOOL IsSameAs(CItem* other) override { return Loot()->IsSameAsImpl(other); }   // 14: 006A4B00
    BOOL CopyTo(CItem* target, CSpellManager* spells) override { return Loot()->CopyToImpl(target, spells); }   // 15: 006A4B50
    float GetPrice() override { return Loot()->CItemLoot::GetPrice(); }          // 16: 006A5470
    mfc::CString FigureName() override { return Loot()->FigureNameImpl(); }     // 19: 006A4B90
    mfc::CString PictureName() override { return Loot()->PictureNameImpl(); }   // 20: 006A5030

private:
    CItemLoot* Loot() { return As<CItemLoot>(); }
};

// The scroll's slots 9, 10, 13 and 14 are CItemQuick's bodies.
class CItemSpellClass final : public ItemClass<CItemSpellClass> {
public:
    void Load(CStream* stream) override { As<CItemQuick>()->LoadImpl(stream); }   // 9: 006A3C80
    void Save(CStream* stream) override { As<CItemQuick>()->SaveImpl(stream); }   // 10: 006A3CA0
    const char* ClassName() override { return ItemSpellClassName(Item()); }   // 11: 006A5700
    int ClassId() override { return ItemSpellClassId(Item()); }               // 12: 006A5710
    void InitFromRecord(const itemdb::ItemRecordHead* record) override { As<CItemQuick>()->InitFromRecordImpl(record); }   // 13: 006A3BE0
    BOOL IsSameAs(CItem* other) override { return As<CItemQuick>()->IsSameAsImpl(other); }   // 14: 006A3C20
    BOOL CopyTo(CItem* target, CSpellManager* spells) override { return As<CItemSpell>()->CopyToImpl(target, spells); }   // 15: 006A57F0
    mfc::CString FigureName() override { return As<CItemSpell>()->FigureNameImpl(); }     // 19: 006A5820
    mfc::CString PictureName() override { return As<CItemSpell>()->PictureNameImpl(); }   // 20: 006A58E0
};

class CItemConstructorClass final : public EngineObjectClass<CItemConstructorClass, CItemConstructor> {
public:
    int Create() override { return Constructor()->CItemConstructor::Create(); }   // 2: 006A5B90
    void Destroy() override { Constructor()->CItemConstructor::Destroy(); }       // 3: 006A5BD0
    const char* ClassName() override { return ItemConstructorClassName(Object()); }   // 11: 006A5AE0
    int ClassId() override { return ItemConstructorClassId(Object()); }               // 12: 006A5AF0

private:
    CItemConstructor* Constructor() { return As<CItemConstructor>(); }
};

inline void* CItem::Vtable() { return CItemClass::Vtable(); }
inline void* CItemWeapon::Vtable() { return CItemWeaponClass::Vtable(); }
inline void* CItemArmor::Vtable() { return CItemArmorClass::Vtable(); }
inline void* CItemQuick::Vtable() { return CItemQuickClass::Vtable(); }
inline void* CItemQuest::Vtable() { return CItemQuestClass::Vtable(); }
inline void* CItemLoot::Vtable() { return CItemLootClass::Vtable(); }
inline void* CItemSpell::Vtable() { return CItemSpellClass::Vtable(); }
inline void* CItemConstructor::Vtable() { return CItemConstructorClass::Vtable(); }
