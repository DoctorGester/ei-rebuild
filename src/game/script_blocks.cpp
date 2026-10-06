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
#include "lib/mfc.h"

#include "ei/hook.h"
#include "originals_library.h"
#include "originals_script.h"

#include <cstdint>

#pragma warning(disable : 4740) // naked forwarders

namespace {

template <typename T>
T& Field(void* object, std::size_t offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(object) + offset);
}

// ---- Original code (MFC template instantiations, config/library.csv) ------------------------------

// CList<ScriptDecl>::NewNode (__thiscall (prev, next)); the element is constructed (vtable 0073D3DC).
ScriptListNode* __fastcall NewBlockNode(ScriptList* a0, void*, ScriptListNode* a2, ScriptListNode* a3) { return (ScriptListNode*)ScriptBlockListNewNodeOriginal((void*)a0, nullptr, (void*)a2, (void*)a3); }
// CArray<ScriptArea>::SetSize (__thiscall (size, growBy)); CArray<ScriptAreaPart>::Copy.
__declspec(naked) void __fastcall AreasSetSize(void*, void*, int, int) EI_ORIGINAL(0x004CDB30)
__declspec(naked) void __fastcall PartsCopy(void*, void*, const void*) EI_ORIGINAL(0x004CE2F0)
// ConstructElements<ScriptObjectRef> (__stdcall (first, count); zeroes).
__declspec(naked) void __stdcall ConstructRefs(void*, int) EI_ORIGINAL(0x004CC800)
// SetSize of SSCCmd's arrays (__thiscall (size, growBy)): strings, object pointers, group pointers,
// bindings (both arrays), nested calls (dropped elements delete their command).
__declspec(naked) void __fastcall StringArgsSetSize(void*, void*, int, int) EI_ORIGINAL(0x004C2510)
__declspec(naked) void __fastcall ObjectArgsSetSize(void*, void*, int, int) EI_ORIGINAL(0x004C46F0)
__declspec(naked) void __fastcall GroupArgsSetSize(void*, void*, int, int) EI_ORIGINAL(0x004C49F0)
__declspec(naked) void __fastcall BindingsSetSize(void*, void*, int, int) EI_ORIGINAL(0x004C4CE0)
void __fastcall SubCallsSetSize(void* a0, void*, int a2, int a3) { SubCallArraySetSizeOriginal(a0, nullptr, a2, a3); }

constexpr std::uintptr_t kCommandsVtable = 0x0073D3DC;  // CArray<SSCCmd> (the temporary ScriptDecl)
constexpr std::uintptr_t kPartsVtable = 0x0073D328;     // CArray<ScriptAreaPart> (the temporary area)

// CList::AddTail of an empty ScriptDecl, as 004BF540 / 004BF900 inline it: the node after the tail,
// the empty temporary's commands copied in (004CC0C0), linked; the id is the list's count (the
// field right after the tail) with `idBits`.
ScriptDecl* AppendBlock(ScriptList* list, std::uint32_t idBits) {
    mfc::CArray<SSCCmd> empty{reinterpret_cast<void*>(kCommandsVtable), nullptr, 0, 0, 0};
    ScriptListNode* node = NewBlockNode(list, nullptr, list->tail, nullptr);
    auto* block = reinterpret_cast<ScriptDecl*>(node->data.bytes);
    CommandsCopy(&block->commands, nullptr, &empty);
    if (list->tail) list->tail->next = node;
    else list->head = node;
    list->tail = node;
    block->id = static_cast<int>(static_cast<std::uint32_t>(list->count) | idBits);
    return block;
}

} // namespace

// 004BF540 CScript::NewCommandList.
ScriptDecl* __fastcall ScriptNewCommandList(CScript* script, void*) {
    return AppendBlock(&Field<ScriptList>(script, 0x70), 0);
}
EI_REPLACE(0x004BF540, ScriptNewCommandList);

// 004BF900 CScript::NewWorldScript.
ScriptDecl* __fastcall ScriptNewWorldScript(CScript* script, void*) {
    return AppendBlock(&Field<ScriptList>(script, 0x178), 0x80000000u);
}
EI_REPLACE(0x004BF900, ScriptNewWorldScript);

// 004BCE10 CScript::FindArea. A linear search by id; when absent the array grows by one (SetSize
// 004CDB30 constructs the slot), the empty temporary's parts are copied in (004CE2F0) and the last
// slot gets the id.
mfc::CArray<ScriptAreaPart>* __fastcall ScriptFindArea(CScript* script, void*, int id) {
    auto& areas = Field<mfc::CArray<ScriptArea>>(script, 0x194);
    for (int i = 0; i < areas.size; ++i) {
        if (areas.data[i].id == id) return &areas.data[i].parts;
    }
    mfc::CArray<ScriptAreaPart> empty{reinterpret_cast<void*>(kPartsVtable), nullptr, 0, 0, 0};
    int index = areas.size;
    AreasSetSize(&areas, nullptr, index + 1, -1);
    PartsCopy(&areas.data[index].parts, nullptr, &empty);
    areas.data[areas.size - 1].id = id;
    return &areas.data[areas.size - 1].parts;
}
EI_REPLACE(0x004BCE10, ScriptFindArea);

// 004B59F0 CScript::NewTempObject: CList<ScriptObjectRef>::AddTail(null reference) inlined. A new
// CPlex block of `blockSize` 12-byte nodes is chained onto the free list (last node first) when
// it is empty; the element is constructed (004CC800) and then assigned null (released if set).
ScriptObjectRef* __fastcall ScriptNewTempObject(CScript* script, void*) {
    auto& list = Field<ScriptList>(script, 0x1CC);
    ScriptListNode* tail = list.tail;
    if (!list.free) {
        mfc::CPlex* block = mfc::PlexCreate(list.blocks, static_cast<unsigned>(list.blockSize), 0xC);
        char* first = reinterpret_cast<char*>(block + 1);
        for (int i = list.blockSize - 1; i >= 0; --i) {
            auto* node = reinterpret_cast<ScriptListNode*>(first + i * 0xC);
            node->next = list.free;
            list.free = node;
        }
    }
    ScriptListNode* node = list.free;
    list.free = node->next;
    node->prev = tail;
    node->next = nullptr;
    ++list.count;
    auto* ref = reinterpret_cast<ScriptObjectRef*>(node->data.bytes);
    ConstructRefs(ref, 1);
    if (ref->object) ref->object->Release();
    ref->object = nullptr;
    if (list.tail) list.tail->next = node;
    else list.head = node;
    list.tail = node;
    return ref;
}
EI_REPLACE(0x004B59F0, ScriptNewTempObject);

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
    StringArgsSetSize(&cmd->stringArgs, nullptr, 0, -1);
    ObjectArgsSetSize(&cmd->objectArgs, nullptr, 0, -1);
    GroupArgsSetSize(&cmd->groupArgs, nullptr, 0, -1);
    BindingsSetSize(&cmd->localBindings, nullptr, 0, -1);
    BindingsSetSize(&cmd->globalBindings, nullptr, 0, -1);
    SubCallsSetSize(&cmd->subCalls, nullptr, 0, -1);
    cmd->calledScript = nullptr;
}
EI_REPLACE(0x004B5F70, ScriptCmdClear);
