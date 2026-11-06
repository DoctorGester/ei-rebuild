// Small allocation helpers of the script runtime: the parser's new command blocks and WorldScript
// block, the area lookups and the temporary object references of the
// evaluator. See script.h and docs/notes/script.md.
#include "game/script.h"
#include "game/objects/map_object.h"

#include "game/effects/world_effect.h"

#include <cstdint>

namespace {

// An empty block at the end of `list`; its id is the list's new count with `idBits`.
ScriptBlock* AppendBlock(std::list<ScriptBlock>* list, std::uint32_t idBits) {
    ScriptBlock& block = list->emplace_back();
    block.id = static_cast<int>(static_cast<std::uint32_t>(list->size()) | idBits);
    return &block;
}

} // namespace

ScriptBlock* Script::NewCommandList() {
    return AppendBlock(&blocks, 0);
}

ScriptBlock* Script::NewWorldScript() {
    return AppendBlock(&worldBlocks, 0x80000000u);
}

ScriptArea* Script::FindArea(int id) {
    for (ScriptArea& area : areas)
        if (area.id == id) return &area;
    return nullptr;
}

ScriptArea* Script::FindOrAddArea(int id) {
    if (ScriptArea* found = FindArea(id)) return found;
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

Script::Script() : pointLights(0x100), tempGroups(1), tempObjects(1) { Reset(); }

// The reset leaves no effect (the point light slots are zeroed), so the members' destructors drop none.
Script::~Script() { Reset(); }

ScriptErrorInfo* Script::GetError(ScriptErrorInfo* out) {
    out->text = errorText;
    out->code = static_cast<int>(errorCode);
    out->line = errorLine;
    return out;
}

// --- Script reset ------------------------------------------------------------------------------------

// Script reset: empties a script before a zone's mission script is loaded (world
// creation and zone changes call it for both world scripts, from the constructor).
// See script.h and docs/notes/script.md.

void Script::Reset() {
    loaded = 0;
    fileCount = 0;
    errorText = "Unknown error";
    errorLine = 0;

    DropInstancesAndRuns();
    worldBlocks.clear();
    globals.Clear();
    forDepth = 0;
    FreeTemporaries();

    // Point lights: objects dropped, slots zeroed.
    for (ScriptPointLight& light : pointLights) light = {};

    fxSources.clear();
    particles.clear();
    lightning.clear();

    areas.clear();
    nextParticleId = -2;
    nextFxId = -2;
    definitions.clear();
    blocks.clear();
}

// Script::Load calls it before each `Script` section: the if-then pairs and variables emptied, not killed,
// the parameters and the name empty.
void ScriptDefinition::Reset() {
    ifThen.clear();
    vars.Clear();
    killed = false;
    params.clear();
    name.clear();
}
