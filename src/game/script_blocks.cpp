// Small allocation helpers of the script runtime (ticket S4): the parser's new command blocks
// (004BF540) and WorldScript block (004BF900), the area lookup that creates missing areas
// (004BCE10), the temporary object references of the evaluator (004B59F0) and SSCCmd::Clear
// (004B5F70). See script.h and docs/notes/script.md.
//
// The original inlines CList::AddTail / NewNode and builds a temporary element on its stack that it
// copies into the new node or array slot. Both temporaries are empty, so the copies (the
// instantiations' Copy, called through their addresses as the original does) leave the new element
// empty; their uninitialised +0x14 word is copied too and then overwritten by the id, which is not
// reproduced. The temporaries own nothing, so their destruction is a no-op and is left out.
#include "script.h"

#include "lib/crt.h"
#include "record_arrays.h"
#include "lib/mfc.h"

#include "ei/hook.h"
#include "originals_library.h"
#include "originals_script.h"
#include "lib/mfc_array.h"
#include "lib/mfc_list.h"

#include <cstdint>

#pragma warning(disable : 4740) // naked forwarders

namespace {

// ---- Original code (MFC template instantiations, config/library.csv) ------------------------------

// CList<ScriptDecl>::NewNode (__thiscall (prev, next)); the element is constructed (vtable 0073D3DC).
using BlockList = mfc::CList<ScriptDecl>;
BlockList::Node* NewBlockNode(BlockList* list, BlockList::Node* prev, BlockList::Node* next) {
    return mfc::impl::NewNodeAs<mfc::impl::CommandBlockList>(list, prev, next);
}
// CArray<ScriptArea>::SetSize (__thiscall (size, growBy)); CArray<ScriptAreaPart>::Copy.
// SetSize of SSCCmd's arrays (__thiscall (size, growBy)): strings, object pointers, group pointers,
// bindings (both arrays), nested calls (dropped elements delete their command).


// CList::AddTail of an empty ScriptDecl, as 004BF540 / 004BF900 inline it: the node after the tail,
// the empty temporary's commands copied in (004CC0C0), linked; the id is the list's count (the
// field right after the tail) with `idBits`.
ScriptDecl* AppendBlock(BlockList* list, std::uint32_t idBits) {
    ScriptCommands empty;
    empty.vtable = mfc::impl::ScriptCommandsClass::Vtable();
    empty.data = nullptr;
    empty.size = empty.maxSize = empty.growBy = 0;
    BlockList::Node* node = NewBlockNode(list, list->tail, nullptr);
    ScriptDecl* block = &node->data;
    block->commands.CopyFrom(empty);
    if (list->tail) list->tail->next = node;
    else list->head = node;
    list->tail = node;
    block->id = static_cast<int>(static_cast<std::uint32_t>(list->count) | idBits);
    return block;
}

} // namespace

// 004BF540 CScript::NewCommandList.
ScriptDecl* CScript::NewCommandList() {
    return AppendBlock(&blocks_, 0);
}
EI_REPLACE_METHOD(0x004BF540, CScript::NewCommandList);

// 004BF900 CScript::NewWorldScript.
ScriptDecl* __fastcall ScriptNewWorldScript(CScript* script, void*) {
    return AppendBlock(&script->worldBlocks_, 0x80000000u);
}
EI_REPLACE(0x004BF900, ScriptNewWorldScript);

// 004BCE10 CScript::FindArea. A linear search by id; when absent the array grows by one (SetSize
// 004CDB30 constructs the slot), the empty temporary's parts are copied in (004CE2F0) and the last
// slot gets the id.
mfc::CArray<ScriptAreaPart>* CScript::FindArea(int id) {
    auto& areas = areas_;
    for (int i = 0; i < areas.size; ++i) {
        if (areas.data[i].id == id) return &areas.data[i].parts;
    }
    mfc::CArray<ScriptAreaPart> empty{mfc::impl::ScriptAreaPartsClass::Vtable(), nullptr, 0, 0, 0};
    int index = areas.size;
    mfc::impl::ArrayAs<mfc::impl::ScriptAreas>(areas).SetSize(index + 1, -1);
    areas.data[index].parts.Copy(empty);   // 004CE2F0
    areas.data[areas.size - 1].id = id;
    return &areas.data[areas.size - 1].parts;
}
EI_REPLACE_METHOD(0x004BCE10, CScript::FindArea);

// 004B59F0 CScript::NewTempObject: CList<ScriptObjectRef>::AddTail(null reference) inlined. A new
// CPlex block of `blockSize` 12-byte nodes is chained onto the free list (last node first) when
// it is empty; the element is constructed (004CC800) and then assigned null (released if set).
ScriptObjectRef* CScript::NewTempObject() {
    using Node = mfc::CList<ScriptObjectRef>::Node;
    static_assert(sizeof(Node) == 0xC);
    auto& list = tempObjects_;
    Node* tail = list.tail;
    if (!list.free) {
        mfc::CPlex* block = mfc::PlexCreate(list.blocks, static_cast<unsigned>(list.blockSize), sizeof(Node));
        Node* first = reinterpret_cast<Node*>(block + 1);
        for (int i = list.blockSize - 1; i >= 0; --i) {
            Node* node = first + i;
            node->next = list.free;
            list.free = node;
        }
    }
    Node* node = list.free;
    list.free = node->next;
    node->prev = tail;
    node->next = nullptr;
    ++list.count;
    ScriptObjectRef* ref = &node->data;
    mfc::PlainElements<ScriptObjectRef>::Construct(ref, 1);   // 004CC800
    if (ref->object) ref->object->Release();
    ref->object = nullptr;
    if (list.tail) list.tail->next = node;
    else list.head = node;
    list.tail = node;
    return ref;
}
EI_REPLACE_METHOD(0x004B59F0, CScript::NewTempObject);

// 004B5F70 SSCCmd::Clear. The float array is emptied inline (its growBy kept), the others through
// their SetSize(0, -1).
void __fastcall ScriptCmdClear(SSCCmd* cmd) {
    cmd->id = 0;
    if (cmd->floatArgs.data) {
        gamecrt::operator_delete(cmd->floatArgs.data);
        cmd->floatArgs.data = nullptr;
    }
    cmd->floatArgs.maxSize = 0;
    cmd->floatArgs.size = 0;
    cmd->stringArgs.SetSize(0, -1);
    cmd->objectArgs.SetSize(0, -1);
    cmd->groupArgs.SetSize(0, -1);
    cmd->localBindings.SetSize(0, -1);
    cmd->globalBindings.SetSize(0, -1);
    mfc::impl::ArrayAs<recordarrays::SubCalls>(cmd->subCalls).SetSize(0, -1);
    cmd->calledScript = nullptr;
}
EI_REPLACE(0x004B5F70, ScriptCmdClear);

// 004D0AF0 (A-S11): a nested call's record emptied for the next parameter: no type, no argument,
// its command cleared (a tail jump to 004B5F70). Only the original LoadFunction calls it (ours,
// script_parse_load.cpp, has the same three steps inline).
void __fastcall ScriptSubCallClear(ScriptSubCall* call) {
    call->type = 0;
    call->argIndex = 0;
    ScriptCmdClear(call->cmd);
}
EI_REPLACE(0x004D0AF0, ScriptSubCallClear);
