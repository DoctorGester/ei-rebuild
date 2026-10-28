// Saving and loading a spell (CSpell's Load / Save) and an active effect record (MapEffect, which
// the map objects' Load / Save call for each of their effects).
#include "spell.h"

#include "core_streams.h"
#include "map_object.h"

#include <cstdint>
#include <cstring>

void MapEffect::Load(CStream* stream) {
    stream->ReadValue(type);
    stream->ReadValue(power);
    stream->ReadValue(ticksLeft);
    stream->ReadValue(value);
    stream->ReadValue(strength);
}

void MapEffect::Save(CStream* stream) const {
    stream->WriteValue(type);
    stream->WriteValue(power);
    stream->WriteValue(ticksLeft);
    stream->WriteValue(value);
    stream->WriteValue(strength);
}

void CSpell::Load(CStream* stream) {
    std::memset(modifiers_, 0, sizeof(modifiers_));
    MapEffect record = Effect();
    record.Load(stream);
    SetEffect(record);
    stream->ReadValue(speed_);
    stream->ReadValue(range_);
    stream->ReadValue(targetCount_);
    stream->ReadValue(targetFlags_);
    stream->ReadValue(manaCost_);
    stream->ReadValue(complexity_);
    stream->ReadValue(price_);
    stream->ReadValue(castTime_);
    stream->Read(modifiers_, sizeof(modifiers_));
    std::uint32_t flags = 0;
    stream->ReadValue(flags);
    flags_ = flags;
    stream->ReadValue(skillNeeded_);
}

void CSpell::Save(CStream* stream) {
    Effect().Save(stream);
    stream->WriteValue(speed_);
    stream->WriteValue(range_);
    stream->WriteValue(targetCount_);
    stream->WriteValue(targetFlags_);
    stream->WriteValue(manaCost_);
    stream->WriteValue(complexity_);
    stream->WriteValue(price_);
    stream->WriteValue(castTime_);
    stream->Write(modifiers_, sizeof(modifiers_));
    stream->WriteValue(flags_);
    stream->WriteValue(skillNeeded_);
}
