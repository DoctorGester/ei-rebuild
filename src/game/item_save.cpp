// The items' Load / Save (slots 9 / 10): CItem's and the subclasses' (CItem's
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

} // namespace

void CItem::Load(CStream* stream) {
    stream->Read(&prototypeId_, kDword);
    stream->Read(&condition_, kDword);
    stream->Read(&conditionMax_, kDword);
    stream->Read(&charge_, kDword);
    stream->Read(&chargeMax_, kDword);
    stream->Read(&chargeRate_, kDword);
    stream->Read(&weight_, kDword);
    stream->Read(&price_, kDword);
    stream->Read(&value3C_, kDword);
    CSpellManager* spells = WorldContext()->SpellManager();
    std::uint32_t id = 0;
    stream->Read(&id, kDword);
    CEngineObject* spell = id == 0xFFFFFFFFu ? nullptr : spells->FindObject(id);
    CSpell* previous = spell_;
    spell_ = static_cast<CSpell*>(spell);
    if (spell != nullptr) ++spell->refCount_;
    if (previous != nullptr) previous->Release();
    stream->Read(&count_, kDword);
    stream->Read(&magicLevel_, kDword);
}

void CItem::Save(CStream* stream) {
    stream->Write(&prototypeId_, kDword);
    stream->Write(&condition_, kDword);
    stream->Write(&conditionMax_, kDword);
    stream->Write(&charge_, kDword);
    stream->Write(&chargeMax_, kDword);
    stream->Write(&chargeRate_, kDword);
    stream->Write(&weight_, kDword);
    stream->Write(&price_, kDword);
    stream->Write(&value3C_, kDword);
    std::int32_t spellId = spell_ != nullptr ? spell_->id_ : -1;
    stream->Write(&spellId, kDword);
    stream->Write(&count_, kDword);
    stream->Write(&magicLevel_, kDword);
}

void CItemWeapon::Load(CStream* stream) {
    CItem::Load(stream);
    stream->Read(&actionValue_, kDword);
    stream->Read(&damage_, kDword);
    stream->Read(&damageSpread_, kDword);
    stream->Read(&reach_, kDword);
}

void CItemWeapon::Save(CStream* stream) {
    CItem::Save(stream);
    stream->Write(&actionValue_, kDword);
    stream->Write(&damage_, kDword);
    stream->Write(&damageSpread_, kDword);
    stream->Write(&reach_, kDword);
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
    stream->Read(&actionValue_, kDword);
}

void CItemQuick::Save(CStream* stream) {
    CItem::Save(stream);
    stream->Write(&actionValue_, kDword);
}

void CItemQuest::Load(CStream* stream) {
    CItem::Load(stream);
    stream->Read(&questItemId_, kDword);
}

void CItemQuest::Save(CStream* stream) {
    CItem::Save(stream);
    stream->Write(&questItemId_, kDword);
}

void CItemLoot::Load(CStream* stream) {
    CItem::Load(stream);
    std::uint32_t value = 0;
    stream->Read(&value, kDword);
    kind_ = value;
    stream->Read(&value, kDword);
    recordType_ = value;
    stream->Read(&index_, kDword);
}

void CItemLoot::Save(CStream* stream) {
    CItem::Save(stream);
    stream->Write(&kind_, kDword);
    stream->Write(&recordType_, kDword);
    stream->Write(&index_, kDword);
}

// --- The virtual slots' entry points (rule 4) -----------------------------------------------------

EI_REWRITTEN(0x006A0270, CItem::Load);
EI_REWRITTEN(0x006A0360, CItem::Save);
EI_REWRITTEN(0x006A2E60, CItemWeapon::Load);
EI_REWRITTEN(0x006A2EB0, CItemWeapon::Save);
EI_REWRITTEN(0x006A3A70, CItemArmor::Load);
EI_REWRITTEN(0x006A3AA0, CItemArmor::Save);
EI_REWRITTEN(0x006A3C80, CItemQuick::Load);
EI_REWRITTEN(0x006A3CA0, CItemQuick::Save);
EI_REWRITTEN(0x006A4570, CItemQuest::Load);
EI_REWRITTEN(0x006A4590, CItemQuest::Save);
EI_REWRITTEN(0x006A5630, CItemLoot::Load);
EI_REWRITTEN(0x006A5680, CItemLoot::Save);
