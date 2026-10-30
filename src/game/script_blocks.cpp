// Small allocation helpers of the script runtime: the parser's new command blocks and WorldScript
// block, the area lookup that creates missing areas and the temporary object references of the
// evaluator. See script.h and docs/notes/script.md.
#include "script.h"

#include "game_strings.h"
#include "world_effect.h"
#include "world_glue.h"

#include <cstdint>

namespace {

// An empty block at the end of `list`; its id is the list's new count with `idBits`.
ScriptDecl* AppendBlock(std::list<ScriptDecl>* list, std::uint32_t idBits) {
    ScriptDecl& block = list->emplace_back();
    block.id = static_cast<int>(static_cast<std::uint32_t>(list->size()) | idBits);
    return &block;
}

} // namespace

ScriptDecl* Script::NewCommandList() {
    return AppendBlock(&blocks, 0);
}

ScriptDecl* Script::NewWorldScript() {
    return AppendBlock(&worldBlocks, 0x80000000u);
}

// A linear search by id; when absent an empty area with the id is appended.
ScriptArea* Script::FindArea(int id) {
    for (ScriptArea& area : areas)
        if (area.id == id) return &area;
    ScriptArea& area = areas.emplace_back();
    area.id = id;
    return &area;
}

// A null reference at the end of the temporaries.
ScriptObjectRef* Script::NewTempObject() {
    return &tempObjects.emplace_back();
}

// --- Script construct --------------------------------------------------------------------------------

// The Script constructor and destructor and Script::GetError. See script.h and docs/notes/script.md.
// They run for the world's two scripts only, from the world's constructor and destructor.

Script::Script() : pointLights(0x100), tempGroups(1), tempObjects(1) { ScriptReset(this); }

// The reset leaves no effect (the point light slots are zeroed), so the members' destructors drop none.
Script::~Script() { ScriptReset(this); }

ScriptErrorInfo* Script::GetError(ScriptErrorInfo* out) {
    out->text = errorText;
    out->code = errorCode;
    out->line = errorLine;
    return out;
}

// --- Script reset ------------------------------------------------------------------------------------

// Script reset: empties a script before a zone's mission script is loaded (world
// creation and zone changes call it for both world scripts, from the constructor).
// See script.h and docs/notes/script.md.

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
    script->tempGroups.front().Clear();
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
