// CScript reset (004BE6E0): empties a script before a zone's mission script is loaded (world
// creation and zone changes call it for both world scripts, the constructor 004BDE40 at the end).
// See script.h and docs/notes/script.md.
#include "game_arrays.h"
#include "game_lists.h"
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

// ---- Original code ------------------------------------------------------------------------------

// 004CD690: the lightning list's DestructElements (__stdcall (first, count); ScriptLightningElementsDestruct,
// script_effects, which ei_vectors does not link).
__declspec(naked) void __stdcall LightningElementsDestruct(void*, int) EI_ORIGINAL(0x004CD690)
// Element destructors (__thiscall): ScriptDefinition, ScriptVars, ScriptDecl, group.
inline void __fastcall GroupDestruct(void* a0) { ScriptGroupDestructOriginal((void*)a0); }

// The world (007AFF60): timed objects.
struct World {
    void RemoveTimedObject(int id);       // 0058E5D0
};
void World::RemoveTimedObject(int id) { ::WorldRemoveTimed(this, nullptr, id); }
World* TheWorld() { return reinterpret_cast<World*>(0x007AFF60); }

// CList::RemoveAll as the reset inlines it: each element destroyed in place, the list emptied
// and its blocks freed.
template <typename List, typename Destroy>
void RemoveAll(List& list, Destroy destroy) {
    for (auto* node = list.head; node; node = node->next) destroy(&node->data);
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

    // Instances and runs, removed from the head; a run's destructor is its variables'.
    alignas(4) unsigned char element[sizeof(ScriptDefinition)];
    static_assert(sizeof(ScriptDefinition) >= sizeof(ScriptRun));
    while (script->instances_.count != 0) {
        ScriptInstancesRemoveHeadOriginal(&script->instances_, nullptr, element);
        ScriptDefinitionDestruct(reinterpret_cast<ScriptDefinition*>(element));
    }
    while (script->runs_.count != 0) {
        ScriptRunsRemoveHeadOriginal(&script->runs_, nullptr, element);
        ScriptVarsDestruct(&reinterpret_cast<ScriptRun*>(element)->vars);
    }

    RemoveAll(script->worldBlocks_, [](ScriptDecl* block) { ScriptWorldBlockDestroy(block, nullptr, 0); });

    ScriptVars* globals = script->Globals();
    ScriptFloatsClear(reinterpret_cast<ScriptNamedValues<float>*>(&globals->floats));
    ScriptStringsClear(reinterpret_cast<ScriptNamedValues<mfc::CString>*>(&globals->strings));
    ScriptObjectsClear(reinterpret_cast<ScriptNamedValues<ScriptObjectRef>*>(&globals->objects));
    ScriptGroupsClear(reinterpret_cast<ScriptNamedValues<ScriptGroup>*>(&globals->groups));
    script->forDepth_ = 0;

    // Temporaries: every entry but one dropped, the last one emptied.
    auto& groups = script->tempGroups_;
    while (groups.count > 1) {
        gamelists::GroupRemoveTail(reinterpret_cast<gamelists::Groups::List*>(&groups), nullptr,
                                   reinterpret_cast<ScriptGroup*>(element));
        GroupDestruct(element);
    }
    PtrArraySetSize(&groups.tail->data, nullptr, 0, -1);
    auto& objects = script->tempObjects_;
    while (objects.count > 1) {
        gamelists::ObjectRefRemoveTail(reinterpret_cast<gamelists::ObjectRefs::List*>(&objects), nullptr,
                                       reinterpret_cast<ScriptObjectRef*>(element));
        reinterpret_cast<ScriptObjectRef*>(element)->Reset();
    }
    objects.tail->data.Assign(nullptr);

    // Point lights: objects released, slots zeroed.
    auto& lights = script->pointLights_;
    for (int i = 0; i < lights.size; ++i) {
        CEngineObject* object = lights.data[i].object;
        if (!object) continue;
        TheWorld()->RemoveTimedObject(object->id_);
        lights.data[i].object->Release();
    }
    std::memset(lights.data, 0, static_cast<unsigned>(lights.size) * sizeof(ScriptPointLight));

    RemoveAll(script->fxSources_, [](ScriptFxSource* source) { ScriptFxSourceDestroy(source, nullptr, 0); });
    RemoveAll(script->particles_, [](ScriptParticleSource* source) { ScriptParticleDestroy(&source->object, nullptr, 0); });
    RemoveAll(script->lightning_, [](ScriptLightning* bolt) { LightningElementsDestruct(bolt, 1); });

    AreasSetSize(&script->areas_, nullptr, 0, -1);
    script->nextParticleId_ = -2;
    script->nextFxId_ = -2;
    DefinitionsSetSize(&script->definitions_, nullptr, 0, -1);

    // Command blocks, removed from the head.
    while (script->blocks_.count != 0) {
        gamelists::DeclRemoveHead(reinterpret_cast<gamelists::Decls::List*>(&script->blocks_), nullptr,   // 004C69B0
                                  reinterpret_cast<ScriptDecl*>(element));
        ScriptDeclDestruct(reinterpret_cast<ScriptDecl*>(element));
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
    PtrArraySetSize(&vars.objects, nullptr, 0, -1);
    NamesSetSizeOriginal(&vars.objectNames, nullptr, 0, -1);
    GroupValuesSetSize(&vars.groups, nullptr, 0, -1);
    NamesSetSizeOriginal(&vars.groupNames, nullptr, 0, -1);
    definition->index = 0;
    const char* empty = game::EmptyText();
    definition->params = empty;
    definition->name = empty;
}
EI_REPLACE(0x004D3320, ScriptDefinitionReset);
