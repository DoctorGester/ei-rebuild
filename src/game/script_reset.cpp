// CScript reset: empties a script before a zone's mission script is loaded (world
// creation and zone changes call it for both world scripts, from the constructor).
// See script.h and docs/notes/script.md.
#include "game_arrays.h"
#include "game_lists.h"
#include "script.h"
#include "world_effect.h"
#include "game_strings.h"

#include "lib/crt.h"
#include "lib/mfc.h"
#include "lib/mfc_array.h"
#include "lib/static_object.h"
#include "record_arrays.h"
#include "world.h"
#include "game_data.h"
#include "game_singletons.h"

#include <cstdint>
#include <cstring>

namespace {

// ---- Original code ------------------------------------------------------------------------------

// Element destructors: ScriptDefinition, ScriptVars, ScriptDecl, group.
inline void GroupDestruct(void* a0) { static_cast<ScriptGroup*>((void*)a0)->DestructBody(); }

// The world: timed objects.

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

void ScriptReset(CScript* script) {
    script->LoadedFlag() = 0;
    script->FileCount() = 0;
    script->ErrorText() = "Unknown error";
    script->ErrorLine() = 0;

    // Instances and runs, removed from the head into a local; a run's destructor is its variables'.
    while (script->instances_.count != 0) {
        lib::InPlace<ScriptDefinition> instance;
        script->instances_.PopHead(instance.Get());
        ScriptDefinitionDestruct(instance.Get());
    }
    while (script->runs_.count != 0) {
        lib::InPlace<ScriptRun> run;
        script->runs_.PopHead(run.Get());
        ScriptVarsDestruct(&run->vars);
    }

    RemoveAll(script->worldBlocks_, [](ScriptDecl* block) { block->DestroyElement(0); });

    ScriptVars* globals = script->Globals();
    ScriptFloatsClear(&globals->floats);
    ScriptStringsClear(&globals->strings);
    ScriptObjectsClear(&globals->objects);
    ScriptGroupsClear(&globals->groups);
    script->forDepth_ = 0;

    // Temporaries: every entry but one dropped, the last one emptied.
    auto& groups = script->tempGroups_;
    while (groups.count > 1) {
        lib::InPlace<ScriptGroup> group;
        groups.PopTail(group.Get());
        GroupDestruct(group.Get());
    }
    groups.tail->data.SetSize(0, -1);
    auto& objects = script->tempObjects_;
    while (objects.count > 1) {
        lib::InPlace<ScriptObjectRef> object;
        objects.PopTail(object.Get());
        object->Reset();
    }
    objects.tail->data.Assign(nullptr);

    // Point lights: objects released, slots zeroed.
    auto& lights = script->pointLights_;
    for (int i = 0; i < lights.size; ++i) {
        CEngineObject* object = lights.data[i].object;
        if (!object) continue;
        TheServerWorld()->RemoveTimedObject(object->id_);
        static_cast<CEngineObject*>(lights.data[i].object)->Release();   // reloaded, through the vtable
    }
    std::memset(lights.data, 0, static_cast<unsigned>(lights.size) * sizeof(ScriptPointLight));

    RemoveAll(script->fxSources_, [](ScriptFxSource* source) { source->DestroyElement(0); });
    RemoveAll(script->particles_, [](ScriptParticleSource* source) { source->DestroyElement(0); });
    RemoveAll(script->lightning_, [](ScriptLightning* bolt) { ScriptLightningElementsDestruct(bolt, 1); });

    mfc::impl::ArrayAs<mfc::impl::ScriptAreas>(script->areas_).SetSize(0, -1);
    script->nextParticleId_ = -2;
    script->nextFxId_ = -2;
    mfc::impl::ArrayAs<recordarrays::Definitions>(script->definitions_).SetSize(0, -1);

    // Command blocks, removed from the head.
    while (script->blocks_.count != 0) {
        lib::InPlace<ScriptDecl> block;
        script->blocks_.PopHead(block.Get());
        ScriptDeclDestruct(block.Get());
    }
}

// ScriptDefinition reset (CScript::Load calls it before each `Script` section): the if-then
// pairs freed inline (no element destructor), each variable array through its instantiation's
// SetSize(0, -1) in member order, then index 0 and the parameters and name assigned the empty
// string constant (in that order).
void ScriptDefinitionReset(ScriptDefinition* definition) {
    if (definition->ifThen.data) {
        gamecrt::operator_delete(definition->ifThen.data);
        definition->ifThen.data = nullptr;
    }
    definition->ifThen.maxSize = 0;
    definition->ifThen.size = 0;
    ScriptVars& vars = definition->vars;
    vars.floats.values.SetSize(0, -1);
    vars.floats.names.SetSize(0, -1);
    vars.strings.values.SetSize(0, -1);
    vars.strings.names.SetSize(0, -1);
    mfc::impl::ArrayAs<gamearrays::ObjectRefs>(vars.objects.values).SetSize(0, -1);
    vars.objects.names.SetSize(0, -1);
    mfc::impl::ArrayAs<gamearrays::Groups>(vars.groups.values).SetSize(0, -1);
    vars.groups.names.SetSize(0, -1);
    definition->index = 0;
    const char* empty = game::EmptyText();
    definition->params = empty;
    definition->name = empty;
}
