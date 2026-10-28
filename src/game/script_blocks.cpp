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

ScriptDecl* CScript::NewCommandList() {
    return AppendBlock(&blocks_, 0);
}

ScriptDecl* CScript::NewWorldScript() {
    return AppendBlock(&worldBlocks_, 0x80000000u);
}

// A linear search by id; when absent an empty area with the id is appended.
ScriptArea* CScript::FindArea(int id) {
    for (ScriptArea& area : areas_)
        if (area.id == id) return &area;
    ScriptArea& area = areas_.emplace_back();
    area.id = id;
    return &area;
}

// A null reference at the end of the temporaries.
ScriptObjectRef* CScript::NewTempObject() {
    return &tempObjects_.emplace_back();
}
