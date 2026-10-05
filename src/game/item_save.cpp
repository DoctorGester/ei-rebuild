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

void CItem::LoadItem(CStream* stream) {
    stream->Read(&unknown_18, kDword);
    stream->Read(&unknown_20, kDword);
    stream->Read(&unknown_24, kDword);
    stream->Read(&charge_, kDword);
    stream->Read(&chargeMax_, kDword);
    stream->Read(&chargeRate_, kDword);
    stream->Read(&unknown_34, kDword);
    stream->Read(&price_, kDword);
    stream->Read(&unknown_3C, kDword);
    CSpellManager* spells = static_cast<CWorld*>(ManagerContext())->SpellManager();
    std::uint32_t id = StreamSlot(stream);
    stream->Read(&id, kDword);
    CEngineObject* spell = id == 0xFFFFFFFFu ? nullptr : spells->FindObject(id);
    auto* previous = static_cast<CEngineObject*>(spell_);
    spell_ = spell;
    if (spell != nullptr) ++spell->refCount_;
    if (previous != nullptr) previous->Release();
    stream->Read(&unknown_48, kDword);
    stream->Read(&unknown_40, kDword);
}
EI_REPLACE_METHOD(0x006A0270, CItem::LoadItem);

void CItem::SaveItem(CStream* stream) {
    stream->Write(&unknown_18, kDword);
    stream->Write(&unknown_20, kDword);
    stream->Write(&unknown_24, kDword);
    stream->Write(&charge_, kDword);
    stream->Write(&chargeMax_, kDword);
    stream->Write(&chargeRate_, kDword);
    stream->Write(&unknown_34, kDword);
    stream->Write(&price_, kDword);
    stream->Write(&unknown_3C, kDword);
    std::int32_t spellId = spell_ != nullptr ? static_cast<CEngineObject*>(spell_)->id_ : -1;
    stream->Write(&spellId, kDword);
    stream->Write(&unknown_48, kDword);
    stream->Write(&unknown_40, kDword);
}
EI_REPLACE_METHOD(0x006A0360, CItem::SaveItem);

void CItemWeapon::LoadItem(CStream* stream) {
    CItem::LoadItem(stream);
    stream->Read(&unknown_4C, kDword);
    stream->Read(&damage_, kDword);
    stream->Read(&damageSpread_, kDword);
    stream->Read(&unknown_58, kDword);
}
EI_REPLACE_METHOD(0x006A2E60, CItemWeapon::LoadItem);

void CItemWeapon::SaveItem(CStream* stream) {
    CItem::SaveItem(stream);
    stream->Write(&unknown_4C, kDword);
    stream->Write(&damage_, kDword);
    stream->Write(&damageSpread_, kDword);
    stream->Write(&unknown_58, kDword);
}
EI_REPLACE_METHOD(0x006A2EB0, CItemWeapon::SaveItem);

void CItemArmor::LoadItem(CStream* stream) {
    CItem::LoadItem(stream);
    stream->Read(absorb_[0], sizeof(absorb_[0]));
    stream->Read(absorb_[1], sizeof(absorb_[1]));
}
EI_REPLACE_METHOD(0x006A3A70, CItemArmor::LoadItem);

void CItemArmor::SaveItem(CStream* stream) {
    CItem::SaveItem(stream);
    stream->Write(absorb_[0], sizeof(absorb_[0]));
    stream->Write(absorb_[1], sizeof(absorb_[1]));
}
EI_REPLACE_METHOD(0x006A3AA0, CItemArmor::SaveItem);

void CItemQuick::LoadItem(CStream* stream) {
    CItem::LoadItem(stream);
    stream->Read(&unknown_4C, kDword);
}
EI_REPLACE_METHOD(0x006A3C80, CItemQuick::LoadItem);

void CItemQuick::SaveItem(CStream* stream) {
    CItem::SaveItem(stream);
    stream->Write(&unknown_4C, kDword);
}
EI_REPLACE_METHOD(0x006A3CA0, CItemQuick::SaveItem);

void CItemQuest::LoadItem(CStream* stream) {
    CItem::LoadItem(stream);
    stream->Read(&unknown_4C, kDword);
}
EI_REPLACE_METHOD(0x006A4570, CItemQuest::LoadItem);

void CItemQuest::SaveItem(CStream* stream) {
    CItem::SaveItem(stream);
    stream->Write(&unknown_4C, kDword);
}
EI_REPLACE_METHOD(0x006A4590, CItemQuest::SaveItem);

void CItemLoot::LoadItem(CStream* stream) {
    CItem::LoadItem(stream);
    std::uint32_t value = StreamSlot(stream);
    stream->Read(&value, kDword);
    kind_ = value;
    stream->Read(&value, kDword);
    recordType_ = value;
    stream->Read(&index_, kDword);
}
EI_REPLACE_METHOD(0x006A5630, CItemLoot::LoadItem);

void CItemLoot::SaveItem(CStream* stream) {
    CItem::SaveItem(stream);
    stream->Write(&kind_, kDword);
    stream->Write(&recordType_, kDword);
    stream->Write(&index_, kDword);
}
EI_REPLACE_METHOD(0x006A5680, CItemLoot::SaveItem);
