// Saving and loading a spell (CSpell's Load / Save) and an active effect record (MapEffect, which
// the map objects' Load / Save call for each of their effects).
#include "spell.h"

#include "core_streams.h"
#include "map_object.h"

#include <cstdint>
#include <cstring>

void MapEffect::Load(ByteStream* stream) {
    stream->ReadValue(type);
    stream->ReadValue(power);
    stream->ReadValue(ticksLeft);
    stream->ReadValue(value);
    stream->ReadValue(strength);
}

void MapEffect::Save(ByteStream* stream) const {
    stream->WriteValue(type);
    stream->WriteValue(power);
    stream->WriteValue(ticksLeft);
    stream->WriteValue(value);
    stream->WriteValue(strength);
}

void CSpell::Load(ByteStream* stream) {
    std::memset(modifiers, 0, sizeof(modifiers));
    MapEffect record = Effect();
    record.Load(stream);
    SetEffect(record);
    stream->ReadValue(speed);
    stream->ReadValue(range);
    stream->ReadValue(targetCount);
    stream->ReadValue(targetFlags);
    stream->ReadValue(manaCost);
    stream->ReadValue(complexity);
    stream->ReadValue(price);
    stream->ReadValue(castTime);
    stream->Read(modifiers, sizeof(modifiers));
    std::uint32_t stateFlags = 0;
    stream->ReadValue(stateFlags);
    flags = stateFlags;
    stream->ReadValue(skillNeeded);
}

void CSpell::Save(ByteStream* stream) {
    Effect().Save(stream);
    stream->WriteValue(speed);
    stream->WriteValue(range);
    stream->WriteValue(targetCount);
    stream->WriteValue(targetFlags);
    stream->WriteValue(manaCost);
    stream->WriteValue(complexity);
    stream->WriteValue(price);
    stream->WriteValue(castTime);
    stream->Write(modifiers, sizeof(modifiers));
    stream->WriteValue(flags);
    stream->WriteValue(skillNeeded);
}
