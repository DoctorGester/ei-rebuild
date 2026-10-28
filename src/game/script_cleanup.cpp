// Clearing parts of a CScript while the world runs: dropping references to deleted world objects
// (after every object removal), and the zone reset when the party enters a zone (instances and runs
// dropped, effects removed, globals zeroed, the WorldScript blocks started again). See script.h and
// docs/notes/script.md.
#include "script.h"
#include "world_effect.h"

// Called after every removal of a world object: the runs', the instances' and the global
// variables drop deleted objects (object variables released, group members removed), then the
// evaluation temporaries are freed.
void ScriptPurgeDeletedObjects(CScript* script) {
    for (ScriptRun& run : script->runs_) run.vars.RemoveDeletedObjects();
    for (ScriptDefinition& instance : script->instances_) instance.vars.RemoveDeletedObjects();
    script->globals_.RemoveDeletedObjects();
    ScriptFreeTemporaries(script);
}

// Every instance, then every run, destroyed.
void ScriptDropInstancesAndRuns(CScript* script) {
    script->instances_.clear();
    script->runs_.clear();
}

// When the party enters a zone: instances and runs dropped, the For depth and the
// temporaries reset, every effect removed (point light slots zeroed; FX source, particle source and
// lightning lists emptied), the global values reset, and every WorldScript block run again.
void ScriptResetZone(CScript* script) {
    ScriptDropInstancesAndRuns(script);
    script->forDepth_ = 0;
    ScriptFreeTemporaries(script);

    for (ScriptPointLight& light : script->pointLights_) light = {};
    script->fxSources_.clear();
    script->particles_.clear();
    script->lightning_.clear();

    script->globals_.ResetValues();
    for (ScriptDecl& block : script->worldBlocks_) script->RunWorldScript(&block);
}
