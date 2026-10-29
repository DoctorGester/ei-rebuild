// Small allocation helpers of the script runtime: the parser's new command blocks and WorldScript
// block, the area lookup that creates missing areas and the temporary object references of the
// evaluator. See script.h and docs/notes/script.md.
#include "script.h"

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
