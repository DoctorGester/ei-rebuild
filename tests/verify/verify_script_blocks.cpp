// Checks for the script allocation helpers of src/game/script_blocks.cpp (ticket S4).
//
// The list and array appends are compared with the original run on a scratch image of the CScript
// that holds only the field the function touches: for the lists an empty list with the real count
// and block size (the original allocates its node from a CPlex block of its own, freed afterwards),
// for the areas a copy of the array with its own buffer (the elements shared shallowly; the original
// may grow and free that buffer, never the elements). The new elements must be equal byte for byte
// (every byte of them is written by both), as must the count / size / capacity. How ours linked its
// node into the real list is checked on ours only (prev = old tail, next null, tail, old tail's next
// or head). SSCCmd::Clear is compared with the original run on a deep copy of the command
// (CArray<SSCCmd>::Copy 004CC0C0), destroyed afterwards.
#include "verify.h"

#include "../../src/game/script.h"
#include "../../src/lib/crt.h"
#include "../../src/lib/mfc.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#pragma warning(disable : 4740) // naked forwarders

namespace {

template <typename T>
T& Field(void* object, std::size_t offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(object) + offset);
}

__declspec(naked) void __fastcall CommandsCopy(void*, void*, const void*) EI_ORIGINAL(0x004CC0C0)
__declspec(naked) void __fastcall CmdDestroy(void*, void*, int) EI_ORIGINAL(0x004BF620)

// A CScript image large enough for every field these functions touch (up to the temporaries
// list's block size at +0x1E4).
struct ScratchScript {
    alignas(8) char bytes[0x200];
    ScratchScript() { std::memset(bytes, 0, sizeof(bytes)); }
    CScript* Script() { return reinterpret_cast<CScript*>(bytes); }
};

// An empty copy of the list at `offset` that keeps the count and block size (the block id is the
// count after the append).
void MakeScratchList(ScratchScript& scratch, CScript* real, std::size_t offset) {
    const ScriptList& source = Field<ScriptList>(real, offset);
    ScriptList& list = Field<ScriptList>(scratch.bytes, offset);
    list.vtable = source.vtable;
    list.count = source.count;
    list.blockSize = source.blockSize;
}

// How ours linked `node` after `oldTail`.
bool LinkedAtTail(const ScriptList& list, const ScriptListNode* node, const ScriptListNode* oldTail,
                  const ScriptListNode* oldHead) {
    if (node->prev != oldTail || node->next != nullptr || list.tail != node) return false;
    if (oldTail) return oldTail->next == node && list.head == oldHead;
    return list.head == node;
}

const ScriptListNode* NodeOf(const void* data) {
    return reinterpret_cast<const ScriptListNode*>(static_cast<const char*>(data) - 8);
}

// ---- Command blocks ---------------------------------------------------------------------------------

using BlockFn = ScriptDecl*(__fastcall*)(CScript*, void*);

ScriptDecl* CheckBlock(const char* name, BlockFn original, BlockFn ours, CScript* self, std::size_t offset) {
    ScratchScript scratch;
    MakeScratchList(scratch, self, offset);
    ScriptList& list = Field<ScriptList>(self, offset);
    ScriptListNode* oldTail = list.tail;
    ScriptListNode* oldHead = list.head;

    ScriptDecl* expected = original(scratch.Script(), nullptr);
    ScriptDecl* actual = ours(self, nullptr);

    ScriptList& scratchList = Field<ScriptList>(scratch.bytes, offset);
    bool same = std::memcmp(expected, actual, sizeof(ScriptDecl)) == 0;
    bool counts = scratchList.count == list.count;
    bool linked = LinkedAtTail(list, NodeOf(actual), oldTail, oldHead);
    char detail[200];
    std::snprintf(detail, sizeof(detail), "id %d vs %d, count %d vs %d, size %d, same %d, linked %d",
                  expected->id, actual->id, scratchList.count, list.count, actual->commands.size, same,
                  linked);
    ei::verify::Report(name, same && counts && linked, detail);
    mfc::PlexFreeDataChain(scratchList.blocks);   // the scratch block owns nothing
    return actual;
}

// 004BF540 / 004BF900: PUSH -1; PUSH 0x718a40 / 0x718b20 (7 bytes).
BlockFn s_newCommandList;
ScriptDecl* __fastcall NewCommandListVerify(CScript* self, void*) {
    return CheckBlock("ScriptNewCommandList", s_newCommandList, ScriptNewCommandList, self, 0x70);
}
BlockFn s_newWorldScript;
ScriptDecl* __fastcall NewWorldScriptVerify(CScript* self, void*) {
    return CheckBlock("ScriptNewWorldScript", s_newWorldScript, ScriptNewWorldScript, self, 0x178);
}

// ---- Areas ------------------------------------------------------------------------------------------

// 004BCE10: MOV EAX,FS:[0] (6 bytes).
mfc::CArray<ScriptAreaPart>*(__fastcall* s_findArea)(CScript*, void*, int);
mfc::CArray<ScriptAreaPart>* __fastcall FindAreaVerify(CScript* self, void*, int id) {
    ScratchScript scratch;
    auto& areas = Field<mfc::CArray<ScriptArea>>(self, 0x194);
    auto& copy = Field<mfc::CArray<ScriptArea>>(scratch.bytes, 0x194);
    copy = areas;
    if (areas.data) {
        copy.data = static_cast<ScriptArea*>(
            gamecrt::operator_new(static_cast<unsigned>(areas.maxSize) * sizeof(ScriptArea)));
        std::memcpy(copy.data, areas.data, static_cast<unsigned>(areas.size) * sizeof(ScriptArea));
    }
    int oldSize = areas.size;

    mfc::CArray<ScriptAreaPart>* expected = s_findArea(scratch.Script(), nullptr, id);
    mfc::CArray<ScriptAreaPart>* actual = ScriptFindArea(self, nullptr, id);

    int expectedIndex = static_cast<int>(reinterpret_cast<ScriptArea*>(expected) - copy.data);
    int actualIndex = static_cast<int>(reinterpret_cast<ScriptArea*>(actual) - areas.data);
    bool match = expectedIndex == actualIndex && copy.vtable == areas.vtable && copy.size == areas.size &&
                 copy.maxSize == areas.maxSize && copy.growBy == areas.growBy &&
                 std::memcmp(copy.data, areas.data, static_cast<unsigned>(areas.size) * sizeof(ScriptArea)) == 0;
    char detail[200];
    std::snprintf(detail, sizeof(detail), "id %d: index %d vs %d, size %d -> %d vs %d, capacity %d vs %d", id,
                  expectedIndex, actualIndex, oldSize, copy.size, areas.size, copy.maxSize, areas.maxSize);
    ei::verify::Report("ScriptFindArea", match, detail);
    if (copy.data) gamecrt::operator_delete(copy.data);   // the elements belong to the real array
    return actual;
}

// ---- Temporary object references ----------------------------------------------------------------------

// 004B59F0: PUSH -1; PUSH 0x717f08 (7 bytes).
ScriptObjectRef*(__fastcall* s_newTempObject)(CScript*, void*);
ScriptObjectRef* __fastcall NewTempObjectVerify(CScript* self, void*) {
    ScratchScript scratch;
    MakeScratchList(scratch, self, 0x1CC);
    ScriptList& list = Field<ScriptList>(self, 0x1CC);
    ScriptListNode* oldTail = list.tail;
    ScriptListNode* oldHead = list.head;

    ScriptObjectRef* expected = s_newTempObject(scratch.Script(), nullptr);
    ScriptObjectRef* actual = ScriptNewTempObject(self, nullptr);

    ScriptList& scratchList = Field<ScriptList>(scratch.bytes, 0x1CC);
    bool linked = LinkedAtTail(list, NodeOf(actual), oldTail, oldHead);
    bool match = expected->object == actual->object && scratchList.count == list.count && linked;
    char detail[200];
    std::snprintf(detail, sizeof(detail), "object %p vs %p, count %d vs %d, linked %d",
                  static_cast<void*>(expected->object), static_cast<void*>(actual->object), scratchList.count,
                  list.count, linked);
    ei::verify::Report("ScriptNewTempObject", match, detail);
    mfc::PlexFreeDataChain(scratchList.blocks);   // a null reference owns nothing
    return actual;
}

// ---- SSCCmd::Clear --------------------------------------------------------------------------------------

// 004B5F70: PUSH ESI; PUSH EDI; MOV ESI,ECX; XOR EDI,EDI (6 bytes).
void(__fastcall* s_cmdClear)(SSCCmd*);
void __fastcall CmdClearVerify(SSCCmd* self) {
    // A deep copy through CArray<SSCCmd>::Copy from a one-element view of the command.
    mfc::CArray<SSCCmd> source{reinterpret_cast<void*>(0x0073D3DC), self, 1, 1, 0};
    mfc::CArray<SSCCmd> copies{reinterpret_cast<void*>(0x0073D3DC), nullptr, 0, 0, 0};
    CommandsCopy(&copies, nullptr, &source);
    SSCCmd* copy = copies.data;
    // The arrays' vtables and growBy are not copied by the assignment; Clear keeps them.
    for (std::size_t offset = 0x04; offset < 0x90; offset += 0x14) {
        Field<void*>(copy, offset) = Field<void*>(self, offset);
        Field<int>(copy, offset + 0x10) = Field<int>(self, offset + 0x10);
    }

    s_cmdClear(copy);
    ScriptCmdClear(self);

    bool match = std::memcmp(copy, self, sizeof(SSCCmd)) == 0;
    ei::verify::Report("ScriptCmdClear", match, match ? "" : "cleared commands differ");
    CmdDestroy(copy, nullptr, 0);
    gamecrt::operator_delete(copies.data);
}

} // namespace

EI_VERIFY(0x004BF540, NewCommandListVerify, 7, s_newCommandList);
EI_VERIFY(0x004BF900, NewWorldScriptVerify, 7, s_newWorldScript);
EI_VERIFY(0x004BCE10, FindAreaVerify, 6, s_findArea);
EI_VERIFY(0x004B59F0, NewTempObjectVerify, 7, s_newTempObject);
EI_VERIFY(0x004B5F70, CmdClearVerify, 6, s_cmdClear);
