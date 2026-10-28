// CScript reset: empties a script before a zone's mission script is loaded (world
// creation and zone changes call it for both world scripts, from the constructor).
// See script.h and docs/notes/script.md.
#include "script.h"
#include "world_effect.h"
#include "game_strings.h"

void ScriptReset(CScript* script) {
    script->LoadedFlag() = 0;
    script->FileCount() = 0;
    script->ErrorText() = "Unknown error";
    script->ErrorLine() = 0;

    script->instances_.clear();
    script->runs_.clear();
    script->worldBlocks_.clear();
    script->globals_.Clear();
    script->forDepth_ = 0;

    // Temporaries: every entry but one dropped, the last one emptied.
    script->tempGroups_.resize(1);
    script->tempGroups_.front().clear();
    script->tempObjects_.resize(1);
    script->tempObjects_.front().object = nullptr;

    // Point lights: objects dropped, slots zeroed.
    for (ScriptPointLight& light : script->pointLights_) light = {};

    script->fxSources_.clear();
    script->particles_.clear();
    script->lightning_.clear();

    script->areas_.clear();
    script->nextParticleId_ = -2;
    script->nextFxId_ = -2;
    script->definitions_.clear();
    script->blocks_.clear();
}

// CScript::Load calls it before each `Script` section: the if-then pairs and variables emptied, index 0, the
// parameters and the name empty.
void ScriptDefinition::Reset() {
    ifThen.clear();
    vars.Clear();
    index = 0;
    const char* empty = game::EmptyText();
    params = empty;
    name = empty;
}
