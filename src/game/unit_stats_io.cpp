// UnitStats::LoadFrom / SaveTo (005287A0 / 00528800): the statistics block in saved games (the
// server controller's copy, combat_save.cpp; the party's records, party_save.cpp). See unit.h.
#include "unit.h"

#include "ei/hook.h"

namespace {
// The size the block had in older saves, by format version.
constexpr int kVersionFlagsOnly = 0x45;     // below: up to the flags (0x6F8)
constexpr int kVersionFixedMax = 0x50;      // below: up to fixedMaxHitPoints (0x6FC)
constexpr int kVersionPending = 0x63;       // below: up to pendingExperience (0x700)
} // namespace

int UnitStats::LoadFrom(CStream* stream) {
    pendingExperience = 0.0f;
    int version = stream->version_;
    unsigned size;
    if (version < kVersionFlagsOnly) size = offsetof(UnitStats, flags);
    else if (version < kVersionFixedMax) size = offsetof(UnitStats, fixedMaxHitPoints);
    else if (version < kVersionPending) size = offsetof(UnitStats, pendingExperience);
    else size = sizeof(UnitStats);
    return stream->Read(this, size);
}
EI_REWRITTEN(0x005287A0, UnitStats::LoadFrom);

void UnitStats::SaveTo(CStream* stream) { stream->Write(this, sizeof(UnitStats)); }
EI_REWRITTEN(0x00528800, UnitStats::SaveTo);
