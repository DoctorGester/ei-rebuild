// Script reset: empties a script before a zone's mission script is loaded (world
// creation and zone changes call it for both world scripts, from the constructor).
// See script.h and docs/notes/script.md.
#include "script.h"
#include "world_effect.h"
#include "game_strings.h"

void ScriptReset(Script* script) {
    script->loaded = 0;
    script->fileCount = 0;
    script->errorText = "Unknown error";
    script->errorLine = 0;

    script->instances.clear();
    script->runs.clear();
    script->worldBlocks.clear();
    script->globals.Clear();
    script->forDepth = 0;

    // Temporaries: every entry but one dropped, the last one emptied.
    script->tempGroups.resize(1);
    script->tempGroups.front().clear();
    script->tempObjects.resize(1);
    script->tempObjects.front().object = nullptr;

    // Point lights: objects dropped, slots zeroed.
    for (ScriptPointLight& light : script->pointLights) light = {};

    script->fxSources.clear();
    script->particles.clear();
    script->lightning.clear();

    script->areas.clear();
    script->nextParticleId = -2;
    script->nextFxId = -2;
    script->definitions.clear();
    script->blocks.clear();
}

// Script::Load calls it before each `Script` section: the if-then pairs and variables emptied, index 0, the
// parameters and the name empty.
void ScriptDefinition::Reset() {
    ifThen.clear();
    vars.Clear();
    index = 0;
    const char* empty = game::kEmptyText;
    params = empty;
    name = empty;
}
