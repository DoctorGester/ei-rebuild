// The items' Load / Save: CItem's and the subclasses' (CItem's
// first, then their own fields). Reached by the world's save and load (the item manager's
// SaveObjects / LoadObjects). See item.h and docs/notes/items_spells.md ("Item rules").
#include "item.h"

#include "base_manager.h"
#include "core_streams.h"
#include "spell.h"
#include "world.h"

#include <cstdint>

namespace {

constexpr std::uint32_t kDword = 4;

} // namespace

void CItem::Load(CStream* stream) {
    stream->ReadValue(prototypeId_);
    stream->ReadValue(condition_);
    stream->ReadValue(conditionMax_);
    stream->ReadValue(charge_);
    stream->ReadValue(chargeMax_);
    stream->ReadValue(chargeRate_);
    stream->ReadValue(weight_);
    stream->ReadValue(price_);
    stream->ReadValue(unreadValue_);
    CSpellManager* spells = WorldContext()->SpellManager();
    std::int32_t id = 0;
    stream->ReadValue(id);
    CSpell* spell = id == kNoObjectId ? nullptr : spells->FindObjectAs<CSpell>(static_cast<unsigned>(id));
    SetSpell(spell);
    stream->ReadValue(count_);
    stream->ReadValue(magicLevel_);
}

void CItem::Save(CStream* stream) {
    stream->WriteValue(prototypeId_);
    stream->WriteValue(condition_);
    stream->WriteValue(conditionMax_);
    stream->WriteValue(charge_);
    stream->WriteValue(chargeMax_);
    stream->WriteValue(chargeRate_);
    stream->WriteValue(weight_);
    stream->WriteValue(price_);
    stream->WriteValue(unreadValue_);
    std::int32_t spellId = spell_ != nullptr ? spell_->id_ : kNoObjectId;
    stream->WriteValue(spellId);
    stream->WriteValue(count_);
    stream->WriteValue(magicLevel_);
}

void CItemWeapon::Load(CStream* stream) {
    CItem::Load(stream);
    stream->ReadValue(actionValue_);
    stream->ReadValue(damage_);
    stream->ReadValue(damageSpread_);
    stream->ReadValue(reach_);
}

void CItemWeapon::Save(CStream* stream) {
    CItem::Save(stream);
    stream->WriteValue(actionValue_);
    stream->WriteValue(damage_);
    stream->WriteValue(damageSpread_);
    stream->WriteValue(reach_);
}

void CItemArmor::Load(CStream* stream) {
    CItem::Load(stream);
    stream->Read(absorb_[0], sizeof(absorb_[0]));
    stream->Read(absorb_[1], sizeof(absorb_[1]));
}

void CItemArmor::Save(CStream* stream) {
    CItem::Save(stream);
    stream->Write(absorb_[0], sizeof(absorb_[0]));
    stream->Write(absorb_[1], sizeof(absorb_[1]));
}

void CItemQuick::Load(CStream* stream) {
    CItem::Load(stream);
    stream->ReadValue(actionValue_);
}

void CItemQuick::Save(CStream* stream) {
    CItem::Save(stream);
    stream->WriteValue(actionValue_);
}

void CItemQuest::Load(CStream* stream) {
    CItem::Load(stream);
    stream->ReadValue(questItemId_);
}

void CItemQuest::Save(CStream* stream) {
    CItem::Save(stream);
    stream->WriteValue(questItemId_);
}

void CItemLoot::Load(CStream* stream) {
    CItem::Load(stream);
    std::uint32_t value = 0;
    stream->ReadValue(value);
    kind_ = static_cast<LootKind>(value);
    stream->ReadValue(value);
    recordType_ = static_cast<LootRecordType>(value);
    stream->ReadValue(index_);
}

void CItemLoot::Save(CStream* stream) {
    CItem::Save(stream);
    stream->WriteValue(kind_);
    stream->WriteValue(recordType_);
    stream->WriteValue(index_);
}
