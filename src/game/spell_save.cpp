// Saving and loading a spell (CSpell slots 9 / 10) and an active effect record (MapEffect, which
// the map objects' Load / Save call for each of their effects).
#include "spell.h"

#include "core_streams.h"
#include "map_object.h"

#include "ei/hook.h"

#include <cstdint>
#include <cstring>

void MapEffect::Load(CStream* stream) {
    stream->Read(&type, 1);
    stream->Read(&power, 1);
    stream->Read(&ticksLeft, 4);
    stream->Read(&value, 4);
    stream->Read(&strength, 4);
}
EI_REPLACE_METHOD(0x00688550, MapEffect::Load);

void MapEffect::Save(CStream* stream) const {
    stream->Write(&type, 1);
    stream->Write(&power, 1);
    stream->Write(&ticksLeft, 4);
    stream->Write(&value, 4);
    stream->Write(&strength, 4);
}
EI_REPLACE_METHOD(0x006885A0, MapEffect::Save);

// flags_ is read through the stream argument's own stack slot: a read that writes nothing leaves
// the stream's address there (kept).
void CSpell::LoadSpell(CStream* stream) {
    std::memset(modifiers_, 0, sizeof(modifiers_));
    Effect().Load(stream);
    stream->Read(&speed_, 4);
    stream->Read(&range_, 4);
    stream->Read(&targetCount_, 4);
    stream->Read(&targetFlags_, 4);
    stream->Read(&manaCost_, 4);
    stream->Read(&complexity_, 4);
    stream->Read(&price_, 4);
    stream->Read(&prototypeField14_, 4);
    stream->Read(modifiers_, sizeof(modifiers_));
    auto flags = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(stream));
    stream->Read(&flags, 4);
    flags_ = flags;
    stream->Read(&skillNeeded_, 4);
}
EI_REPLACE_METHOD(0x0068B860, CSpell::LoadSpell);

void CSpell::SaveSpell(CStream* stream) {
    Effect().Save(stream);
    stream->Write(&speed_, 4);
    stream->Write(&range_, 4);
    stream->Write(&targetCount_, 4);
    stream->Write(&targetFlags_, 4);
    stream->Write(&manaCost_, 4);
    stream->Write(&complexity_, 4);
    stream->Write(&price_, 4);
    stream->Write(&prototypeField14_, 4);
    stream->Write(modifiers_, sizeof(modifiers_));
    stream->Write(&flags_, 4);
    stream->Write(&skillNeeded_, 4);
}
EI_REPLACE_METHOD(0x0068B920, CSpell::SaveSpell);
