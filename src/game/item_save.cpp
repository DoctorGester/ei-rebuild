// The items' Load / Save (slots 9 / 10): CItem's (006A0270 / 006A0360) and the subclasses' (CItem's
// first, then their own fields). Reached by the world's save and load (the item manager's
// SaveObjects / LoadObjects). See item.h and docs/notes/items_spells.md ("Item rules").
#include "item.h"

#include "base_manager.h"
#include "core_streams.h"
#include "spell.h"
#include "world.h"

#include "ei/hook.h"

#include <cstdint>

namespace {

constexpr std::uint32_t kDword = 4;

// The original reads some values into the stack slot of its `stream` argument: a read that writes
// nothing leaves the pointer's own value there.
std::uint32_t StreamSlot(CStream* stream) { return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(stream)); }

} // namespace

void CItem::Load(void* data) {
    auto* stream = static_cast<CStream*>(data);
    stream->Read(&unknown_18, kDword);
    stream->Read(&condition_, kDword);
    stream->Read(&conditionMax_, kDword);
    stream->Read(&charge_, kDword);
    stream->Read(&chargeMax_, kDword);
    stream->Read(&chargeRate_, kDword);
    stream->Read(&weight_, kDword);
    stream->Read(&price_, kDword);
    stream->Read(&unknown_3C, kDword);
    CSpellManager* spells = static_cast<CWorld*>(ManagerContext())->SpellManager();
    std::uint32_t id = StreamSlot(stream);
    stream->Read(&id, kDword);
    CEngineObject* spell = id == 0xFFFFFFFFu ? nullptr : spells->FindObject(id);
    CSpell* previous = spell_;
    spell_ = static_cast<CSpell*>(spell);
    if (spell != nullptr) ++spell->refCount_;
    if (previous != nullptr) previous->Release();
    stream->Read(&count_, kDword);
    stream->Read(&magicLevel_, kDword);
}

void CItem::Save(void* data) {
    auto* stream = static_cast<CStream*>(data);
    stream->Write(&unknown_18, kDword);
    stream->Write(&condition_, kDword);
    stream->Write(&conditionMax_, kDword);
    stream->Write(&charge_, kDword);
    stream->Write(&chargeMax_, kDword);
    stream->Write(&chargeRate_, kDword);
    stream->Write(&weight_, kDword);
    stream->Write(&price_, kDword);
    stream->Write(&unknown_3C, kDword);
    std::int32_t spellId = spell_ != nullptr ? spell_->id_ : -1;
    stream->Write(&spellId, kDword);
    stream->Write(&count_, kDword);
    stream->Write(&magicLevel_, kDword);
}

void CItemWeapon::Load(void* data) {
    auto* stream = static_cast<CStream*>(data);
    CItem::Load(stream);
    stream->Read(&unknown_4C, kDword);
    stream->Read(&damage_, kDword);
    stream->Read(&damageSpread_, kDword);
    stream->Read(&reach_, kDword);
}

void CItemWeapon::Save(void* data) {
    auto* stream = static_cast<CStream*>(data);
    CItem::Save(stream);
    stream->Write(&unknown_4C, kDword);
    stream->Write(&damage_, kDword);
    stream->Write(&damageSpread_, kDword);
    stream->Write(&reach_, kDword);
}

void CItemArmor::Load(void* data) {
    auto* stream = static_cast<CStream*>(data);
    CItem::Load(stream);
    stream->Read(absorb_[0], sizeof(absorb_[0]));
    stream->Read(absorb_[1], sizeof(absorb_[1]));
}

void CItemArmor::Save(void* data) {
    auto* stream = static_cast<CStream*>(data);
    CItem::Save(stream);
    stream->Write(absorb_[0], sizeof(absorb_[0]));
    stream->Write(absorb_[1], sizeof(absorb_[1]));
}

void CItemQuick::Load(void* data) {
    auto* stream = static_cast<CStream*>(data);
    CItem::Load(stream);
    stream->Read(&unknown_4C, kDword);
}

void CItemQuick::Save(void* data) {
    auto* stream = static_cast<CStream*>(data);
    CItem::Save(stream);
    stream->Write(&unknown_4C, kDword);
}

void CItemQuest::Load(void* data) {
    auto* stream = static_cast<CStream*>(data);
    CItem::Load(stream);
    stream->Read(&questItemId_, kDword);
}

void CItemQuest::Save(void* data) {
    auto* stream = static_cast<CStream*>(data);
    CItem::Save(stream);
    stream->Write(&questItemId_, kDword);
}

void CItemLoot::Load(void* data) {
    auto* stream = static_cast<CStream*>(data);
    CItem::Load(stream);
    std::uint32_t value = StreamSlot(stream);
    stream->Read(&value, kDword);
    kind_ = value;
    stream->Read(&value, kDword);
    recordType_ = value;
    stream->Read(&index_, kDword);
}

void CItemLoot::Save(void* data) {
    auto* stream = static_cast<CStream*>(data);
    CItem::Save(stream);
    stream->Write(&kind_, kDword);
    stream->Write(&recordType_, kDword);
    stream->Write(&index_, kDword);
}

// --- The virtual slots' entry points (rule 4) -----------------------------------------------------

namespace {
void __fastcall ItemLoadHook(CItem* self, void*, void* stream) { self->CItem::Load(stream); }
void __fastcall ItemSaveHook(CItem* self, void*, void* stream) { self->CItem::Save(stream); }
void __fastcall WeaponLoadHook(CItemWeapon* self, void*, void* stream) { self->CItemWeapon::Load(stream); }
void __fastcall WeaponSaveHook(CItemWeapon* self, void*, void* stream) { self->CItemWeapon::Save(stream); }
void __fastcall ArmorLoadHook(CItemArmor* self, void*, void* stream) { self->CItemArmor::Load(stream); }
void __fastcall ArmorSaveHook(CItemArmor* self, void*, void* stream) { self->CItemArmor::Save(stream); }
void __fastcall QuickLoadHook(CItemQuick* self, void*, void* stream) { self->CItemQuick::Load(stream); }
void __fastcall QuickSaveHook(CItemQuick* self, void*, void* stream) { self->CItemQuick::Save(stream); }
void __fastcall QuestLoadHook(CItemQuest* self, void*, void* stream) { self->CItemQuest::Load(stream); }
void __fastcall QuestSaveHook(CItemQuest* self, void*, void* stream) { self->CItemQuest::Save(stream); }
void __fastcall LootLoadHook(CItemLoot* self, void*, void* stream) { self->CItemLoot::Load(stream); }
void __fastcall LootSaveHook(CItemLoot* self, void*, void* stream) { self->CItemLoot::Save(stream); }
} // namespace
EI_REPLACE(0x006A0270, ItemLoadHook);
EI_REPLACE(0x006A0360, ItemSaveHook);
EI_REPLACE(0x006A2E60, WeaponLoadHook);
EI_REPLACE(0x006A2EB0, WeaponSaveHook);
EI_REPLACE(0x006A3A70, ArmorLoadHook);
EI_REPLACE(0x006A3AA0, ArmorSaveHook);
EI_REPLACE(0x006A3C80, QuickLoadHook);
EI_REPLACE(0x006A3CA0, QuickSaveHook);
EI_REPLACE(0x006A4570, QuestLoadHook);
EI_REPLACE(0x006A4590, QuestSaveHook);
EI_REPLACE(0x006A5630, LootLoadHook);
EI_REPLACE(0x006A5680, LootSaveHook);
