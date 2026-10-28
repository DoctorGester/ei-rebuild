// UnitStats::LoadFrom / SaveTo: the statistics block in saved games (the
// server controller's copy, combat_save.cpp; the party's records, party_save.cpp). See unit.h.
#include "unit.h"
#include "save_versions.h"

namespace {
} // namespace

int UnitStats::LoadFrom(CStream* stream) {
    pendingExperience = 0.0f;
    int version = stream->version_;
    unsigned size;
    if (version < kVersionStatsPastFlags) size = offsetof(UnitStats, flags);
    else if (version < kVersionFixedMaxHitPoints) size = offsetof(UnitStats, fixedMaxHitPoints);
    else if (version < kVersionPendingExperience) size = offsetof(UnitStats, pendingExperience);
    else size = sizeof(UnitStats);
    return stream->Read(this, size);
}

void UnitStats::SaveTo(CStream* stream) { stream->Write(this, sizeof(UnitStats)); }
