// Clearing parts of a Script while the world runs: dropping references to deleted world objects
// (after every object removal), and the zone reset when the party enters a zone (instances and runs
// dropped, effects removed, globals zeroed, the WorldScript blocks started again). See script.h and
// docs/notes/script.md.
#include "game/script.h"
#include "game/effects/world_effect.h"

// Called after every removal of a world object: the runs', the instances' and the global
// variables drop deleted objects (object variables released, group members removed), then the
// evaluation temporaries are freed.
void Script::PurgeDeletedObjects() {
    for (ScriptRun& run : runs) run.vars.RemoveDeletedObjects();
    for (ScriptDefinition& instance : instances) instance.vars.RemoveDeletedObjects();
    globals.RemoveDeletedObjects();
    FreeTemporaries();
}

// Every instance, then every run, destroyed.
void Script::DropInstancesAndRuns() {
    instances.clear();
    runs.clear();
}

// When the party enters a zone: instances and runs dropped, the For depth and the
// temporaries reset, every effect removed (point light slots zeroed; FX source, particle source and
// lightning lists emptied), the global values reset, and every WorldScript block run again.
void Script::ResetZone() {
    DropInstancesAndRuns();
    forDepth = 0;
    FreeTemporaries();

    for (ScriptPointLight& light : pointLights) light = {};
    fxSources.clear();
    particles.clear();
    lightning.clear();

    globals.ResetValues();
    for (ScriptBlock& block : worldBlocks) RunWorldScript(&block);
}
