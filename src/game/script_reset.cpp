// CScript reset (004BE6E0): empties a script before a zone's mission script is loaded (world
// creation and zone changes call it for both world scripts, the constructor 004BDE40 at the end).
// See script.h and docs/notes/script.md.
#include "script.h"
#include "game_strings.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "originals_library.h"
#include "originals_world.h"
#include "originals_library.h"
#include "originals_script.h"

#include <cstdint>
#include <cstring>

#pragma warning(disable : 4740) // naked forwarders

namespace {

template <typename T>
T& Field(void* object, std::size_t offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(object) + offset);
}

// ---- Original code ------------------------------------------------------------------------------

// CList::RemoveHead / RemoveTail instantiations (__thiscall, the element returned through a
// hidden pointer: copy-constructed into `out`, the node freed).
void* __fastcall InstancesRemoveHead(void* a0, void*, void* a2) { return ScriptInstancesRemoveHeadOriginal(a0, nullptr, a2); }
void* __fastcall RunsRemoveHead(void* a0, void*, void* a2) { return ScriptRunsRemoveHeadOriginal(a0, nullptr, a2); }
__declspec(naked) void* __fastcall BlocksRemoveHead(void*, void*, void* /*out*/) EI_ORIGINAL(0x004C69B0)
void* __fastcall TempGroupsRemoveTail(void* a0, void*, void* a2) { return TempGroupsRemoveTailOriginal(a0, nullptr, a2); }
void* __fastcall TempObjectsRemoveTail(void* a0, void*, void* a2) { return TempObjectsRemoveTailOriginal(a0, nullptr, a2); }
// Element destructors (__thiscall): ScriptDefinition, ScriptVars, ScriptDecl, group.
__declspec(naked) void __fastcall DefinitionDestruct(void*) EI_ORIGINAL(0x004BE9D0)
__declspec(naked) void __fastcall VarsDestruct(void*) EI_ORIGINAL(0x004BE090)
__declspec(naked) void __fastcall DeclDestruct(void*) EI_ORIGINAL(0x004C54A0)
inline void __fastcall GroupDestruct(void* a0) { ScriptGroupDestructOriginal((void*)a0); }
// Node element destructors (__thiscall (flags), 0 = in place): WorldScript block (0x18), FX
// source (0x34), particle source (0x24); lightning DestructElements (__stdcall (first, count)).
__declspec(naked) void __fastcall WorldBlockDestroy(void*, void*, int) EI_ORIGINAL(0x004CE2D0)
__declspec(naked) void __fastcall FxSourceDestroy(void*, void*, int) EI_ORIGINAL(0x004CE2B0)
__declspec(naked) void __fastcall ParticleDestroy(void*, void*, int) EI_ORIGINAL(0x004CE270)
__declspec(naked) void __stdcall LightningElementsDestruct(void*, int) EI_ORIGINAL(0x004CD690)
// CArray::SetSize (__thiscall (size, growBy)): areas, definitions; a temporary group.
void __fastcall GroupSetSize(void* a0, void*, int a2, int a3) { PtrArraySetSize(a0, nullptr, a2, a3); }
// Rewritten, called through their addresses (their verify wrappers run).
__declspec(naked) void __fastcall RefReset(void*) EI_ORIGINAL(0x004B24B0)
__declspec(naked) void __fastcall RefAssign(void*, void*, CEngineObject*) EI_ORIGINAL(0x004B24D0)
__declspec(naked) void __fastcall FloatsClear(void*) EI_ORIGINAL(0x004C1650)
__declspec(naked) void __fastcall StringsClear(void*) EI_ORIGINAL(0x004C16B0)
__declspec(naked) void __fastcall ObjectsClear(void*) EI_ORIGINAL(0x004C1700)
__declspec(naked) void __fastcall GroupsClear(void*) EI_ORIGINAL(0x004C1760)

// The world (007AFF60): timed objects.
struct World {
    void RemoveTimedObject(int id);       // 0058E5D0
};
void World::RemoveTimedObject(int id) { ::WorldRemoveTimed(this, nullptr, id); }
World* TheWorld() { return reinterpret_cast<World*>(0x007AFF60); }

ScriptList& ListAt(CScript* script, std::size_t offset) { return Field<ScriptList>(script, offset); }

// CList::RemoveAll as the reset inlines it: each element destroyed in place, the list emptied
// and its blocks freed.
template <typename Destroy>
void RemoveAll(ScriptList& list, Destroy destroy) {
    for (ScriptListNode* node = list.head; node; node = node->next) destroy(node->data.bytes);
    list.count = 0;
    list.free = nullptr;
    list.tail = nullptr;
    list.head = nullptr;
    mfc::PlexFreeDataChain(list.blocks);
    list.blocks = nullptr;
}

} // namespace

void __fastcall ScriptReset(CScript* script) {
    script->LoadedFlag() = 0;
    script->FileCount() = 0;
    script->ErrorText() = reinterpret_cast<const char*>(0x0078BBDC);   // "Unknown error"
    script->ErrorLine() = 0;

    // Instances (+0x140) and runs (+0x15C), removed from the head; a run's destructor is its
    // variables' (+0x14).
    alignas(4) unsigned char element[0xC0];
    while (ListAt(script, 0x140).count != 0) {
        InstancesRemoveHead(&ListAt(script, 0x140), nullptr, element);
        DefinitionDestruct(element);
    }
    while (ListAt(script, 0x15C).count != 0) {
        RunsRemoveHead(&ListAt(script, 0x15C), nullptr, element);
        VarsDestruct(element + 0x14);
    }

    RemoveAll(ListAt(script, 0x178), [](char* data) { WorldBlockDestroy(data, nullptr, 0); });

    ScriptVars* globals = script->Globals();
    FloatsClear(&globals->floats);
    StringsClear(&globals->strings);
    ObjectsClear(&globals->objects);
    GroupsClear(&globals->groups);
    Field<int>(script, 0x1E8) = 0;

    // Temporaries: every entry but one dropped, the last one emptied.
    ScriptList& groups = ListAt(script, 0x1B0);
    while (groups.count > 1) {
        TempGroupsRemoveTail(&groups, nullptr, element);
        GroupDestruct(element);
    }
    GroupSetSize(groups.tail->data.bytes, nullptr, 0, -1);
    ScriptList& objects = ListAt(script, 0x1CC);
    while (objects.count > 1) {
        TempObjectsRemoveTail(&objects, nullptr, element);
        RefReset(element);
    }
    RefAssign(objects.tail->data.bytes, nullptr, nullptr);

    // Point lights (+0x04 data, +0x08 count; 0x20-byte slots): objects released, slots zeroed.
    for (int i = 0; i < Field<int>(script, 0x08); ++i) {
        CEngineObject* object = Field<CEngineObject**>(script, 0x04)[i * 8];
        if (!object) continue;
        TheWorld()->RemoveTimedObject(object->id_);
        Field<CEngineObject**>(script, 0x04)[i * 8]->Release();
    }
    std::memset(Field<void*>(script, 0x04), 0, static_cast<unsigned>(Field<int>(script, 0x08)) * 0x20);

    RemoveAll(ListAt(script, 0x30), [](char* data) { FxSourceDestroy(data, nullptr, 0); });
    RemoveAll(ListAt(script, 0x14), [](char* data) { ParticleDestroy(data, nullptr, 0); });
    RemoveAll(ListAt(script, 0x4C), [](char* data) { LightningElementsDestruct(data, 1); });

    AreasSetSize(&Field<char>(script, 0x194), nullptr, 0, -1);
    Field<int>(script, 0x68) = -2;
    Field<int>(script, 0x6C) = -2;
    DefinitionsSetSize(&Field<char>(script, 0x8C), nullptr, 0, -1);

    // Command blocks (+0x70), removed from the head.
    while (ListAt(script, 0x70).count != 0) {
        BlocksRemoveHead(&ListAt(script, 0x70), nullptr, element);
        DeclDestruct(element);
    }
}
EI_REPLACE(0x004BE6E0, ScriptReset);

// ScriptDefinition reset (004D3320; CScript::Load calls it before each `Script` section): the if-then
// pairs freed inline (no element destructor), each variable array through its instantiation's
// SetSize(0, -1) in member order, then index 0 and the parameters and name assigned the empty
// string constant 0079B298 (in that order).
void __fastcall ScriptDefinitionReset(ScriptDefinition* definition) {
    if (definition->ifThen.data) {
        gamecrt::operator_delete(definition->ifThen.data);
        definition->ifThen.data = nullptr;
    }
    definition->ifThen.maxSize = 0;
    definition->ifThen.size = 0;
    ScriptVars& vars = definition->vars;
    FloatsSetSize(&vars.floats, nullptr, 0, -1);
    NamesSetSizeOriginal(&vars.floatNames, nullptr, 0, -1);
    NamesSetSizeOriginal(&vars.strings, nullptr, 0, -1);
    NamesSetSizeOriginal(&vars.stringNames, nullptr, 0, -1);
    GroupSetSize(&vars.objects, nullptr, 0, -1);
    NamesSetSizeOriginal(&vars.objectNames, nullptr, 0, -1);
    GroupValuesSetSize(&vars.groups, nullptr, 0, -1);
    NamesSetSizeOriginal(&vars.groupNames, nullptr, 0, -1);
    definition->index = 0;
    const char* empty = game::EmptyText();
    definition->params = empty;
    definition->name = empty;
}
EI_REPLACE(0x004D3320, ScriptDefinitionReset);
