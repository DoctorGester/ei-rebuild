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

void CItem::LoadImpl(CStream* stream) {
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

void CItem::SaveImpl(CStream* stream) {
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

void CItemWeapon::LoadImpl(CStream* stream) {
    CItem::LoadImpl(stream);
    stream->Read(&actionValue_, kDword);
    stream->Read(&damage_, kDword);
    stream->Read(&damageSpread_, kDword);
    stream->Read(&reach_, kDword);
}

void CItemWeapon::SaveImpl(CStream* stream) {
    CItem::SaveImpl(stream);
    stream->Write(&actionValue_, kDword);
    stream->Write(&damage_, kDword);
    stream->Write(&damageSpread_, kDword);
    stream->Write(&reach_, kDword);
}

void CItemArmor::LoadImpl(CStream* stream) {
    CItem::LoadImpl(stream);
    stream->Read(absorb_[0], sizeof(absorb_[0]));
    stream->Read(absorb_[1], sizeof(absorb_[1]));
}

void CItemArmor::SaveImpl(CStream* stream) {
    CItem::SaveImpl(stream);
    stream->Write(absorb_[0], sizeof(absorb_[0]));
    stream->Write(absorb_[1], sizeof(absorb_[1]));
}

void CItemQuick::LoadImpl(CStream* stream) {
    CItem::LoadImpl(stream);
    stream->Read(&actionValue_, kDword);
}

void CItemQuick::SaveImpl(CStream* stream) {
    CItem::SaveImpl(stream);
    stream->Write(&actionValue_, kDword);
}

void CItemQuest::LoadImpl(CStream* stream) {
    CItem::LoadImpl(stream);
    stream->Read(&questItemId_, kDword);
}

void CItemQuest::SaveImpl(CStream* stream) {
    CItem::SaveImpl(stream);
    stream->Write(&questItemId_, kDword);
}

void CItemLoot::LoadImpl(CStream* stream) {
    CItem::LoadImpl(stream);
    std::uint32_t value = StreamSlot(stream);
    stream->Read(&value, kDword);
    kind_ = value;
    stream->Read(&value, kDword);
    recordType_ = value;
    stream->Read(&index_, kDword);
}

void CItemLoot::SaveImpl(CStream* stream) {
    CItem::SaveImpl(stream);
    stream->Write(&kind_, kDword);
    stream->Write(&recordType_, kDword);
    stream->Write(&index_, kDword);
}

// --- The virtual slots' entry points (rule 4) -----------------------------------------------------

EI_REPLACE_METHOD(0x006A0270, CItem::LoadImpl);
EI_REPLACE_METHOD(0x006A0360, CItem::SaveImpl);
EI_REPLACE_METHOD(0x006A2E60, CItemWeapon::LoadImpl);
EI_REPLACE_METHOD(0x006A2EB0, CItemWeapon::SaveImpl);
EI_REPLACE_METHOD(0x006A3A70, CItemArmor::LoadImpl);
EI_REPLACE_METHOD(0x006A3AA0, CItemArmor::SaveImpl);
EI_REPLACE_METHOD(0x006A3C80, CItemQuick::LoadImpl);
EI_REPLACE_METHOD(0x006A3CA0, CItemQuick::SaveImpl);
EI_REPLACE_METHOD(0x006A4570, CItemQuest::LoadImpl);
EI_REPLACE_METHOD(0x006A4590, CItemQuest::SaveImpl);
EI_REPLACE_METHOD(0x006A5630, CItemLoot::LoadImpl);
EI_REPLACE_METHOD(0x006A5680, CItemLoot::SaveImpl);
