// Clearing parts of a CScript while the world runs: dropping references to deleted world objects
// (after every object removal), and the zone reset when the party enters a zone (instances and runs
// dropped, effects removed, globals zeroed, the WorldScript blocks started again). See script.h and
// docs/notes/script.md.
#include "script.h"

#include "lib/crt.h"
#include "game_lists.h"
#include "originals_library.h"
#include "originals_script.h"
#include "world.h"
#include "game_singletons.h"

#include "ei/hook.h"
#include "lib/mfc_array.h"

#include <cstring>

namespace {

// CList::RemoveAll as inlined: each element destroyed in place, the list emptied, its blocks freed.
template <typename T, typename Destroy>
void RemoveAll(mfc::CList<T>& list, Destroy destroy) {
    for (auto* node = list.head; node; node = node->next) destroy(&node->data);
    list.count = 0;
    list.free = nullptr;
    list.tail = nullptr;
    list.head = nullptr;
    mfc::PlexFreeDataChain(list.blocks);
    list.blocks = nullptr;
}

// The group variables (names, then values) of a removed instance or run.
void DestroyGroupVariables(ScriptVars& vars) {
    ScriptNamesDestruct(&vars.groupNames);
    mfc::impl::DestructArrayOf<gamearrays::GroupArrayClass>(mfc::impl::ArrayAs<gamearrays::Groups>(vars.groups));
}

} // namespace

// 004BED90 (called after every removal of a world object): the runs', the instances' and the global
// variables drop deleted objects (object variables released, group members removed), then the
// evaluation temporaries are freed.
void __fastcall ScriptPurgeDeletedObjects(CScript* script) {
    for (auto* node = script->runs_.head; node;) {
        auto* next = node->next;
        node->data.vars.RemoveDeletedObjects();
        node = next;
    }
    for (auto* node = script->instances_.head; node;) {
        auto* next = node->next;
        node->data.vars.RemoveDeletedObjects();
        node = next;
    }
    script->globals_.RemoveDeletedObjects();
    ScriptFreeTemporaries(script);
}

// 004BE490: every instance and every run removed from the head of its list and destroyed.
void __fastcall ScriptDropInstancesAndRuns(CScript* script) {
    while (script->instances_.count != 0) {
        alignas(ScriptDefinition) unsigned char element[sizeof(ScriptDefinition)];
        script->instances_.PopHead(reinterpret_cast<ScriptDefinition*>(element));
        auto* instance = reinterpret_cast<ScriptDefinition*>(element);
        ScriptVars& vars = instance->vars;
        DestroyGroupVariables(vars);
        ScriptObjectsDestruct(reinterpret_cast<ScriptNamedValues<ScriptObjectRef>*>(&vars.objects));
        ScriptStringsDestruct(reinterpret_cast<ScriptNamedValues<mfc::CString>*>(&vars.strings));
        ScriptFloatsDestruct(reinterpret_cast<ScriptNamedValues<float>*>(&vars.floats));
        mfc::impl::DestructHeader<mfc::impl::ScriptIfThenArrayClass>(&instance->ifThen);
        instance->params.~CString();
        instance->name.~CString();
    }
    while (script->runs_.count != 0) {
        alignas(ScriptRun) unsigned char element[sizeof(ScriptRun)];
        script->runs_.PopHead(reinterpret_cast<ScriptRun*>(element));
        ScriptVars& vars = reinterpret_cast<ScriptRun*>(element)->vars;
        DestroyGroupVariables(vars);
        ScriptNamesDestruct(&vars.objectNames);
        vars.objects.vtable = scriptvtable::kRefsBase;
        if (vars.objects.data) {
            gamearrays::ObjectRefs::DestructElements(vars.objects.data, vars.objects.size);
            gamecrt::operator_delete(vars.objects.data);
        }
        ScriptNamesDestruct(&vars.stringNames);
        ScriptNamesDestruct(&vars.strings);
        ScriptNamesDestruct(&vars.floatNames);
        vars.floats.vtable = scriptvtable::kFloatsBase;
        if (vars.floats.data) gamecrt::operator_delete(vars.floats.data);
    }
}

// 004BEB00 (when the party enters a zone): instances and runs dropped, the For depth and the
// temporaries reset, every effect removed (point light slots zeroed; FX source, particle source and
// lightning lists emptied), the global values reset, and every WorldScript block run again.
void __fastcall ScriptResetZone(CScript* script) {
    ScriptDropInstancesAndRuns(script);
    script->forDepth_ = 0;
    ScriptFreeTemporaries(script);

    mfc::CArray<ScriptPointLight>& lights = script->pointLights_;
    for (int i = 0; i < lights.size; ++i) {
        if (CEngineObject* object = lights.data[i].object) {
            TheServerWorld()->RemoveTimedObject(object->id_);
            lights.data[i].object->Release();
        }
    }
    if (lights.size) std::memset(lights.data, 0, static_cast<unsigned>(lights.size) * sizeof(ScriptPointLight));

    RemoveAll(script->fxSources_, [](ScriptFxSource* source) { source->ScriptFxSourceDestruct(); });
    RemoveAll(script->particles_, [](ScriptParticleSource* source) {
        if (CEngineObject* object = source->object) {
            TheServerWorld()->RemoveTimedObject(object->id_);
            source->object->Release();
        }
        source->object = nullptr;
    });
    RemoveAll(script->lightning_, [](ScriptLightning* bolt) { bolt->ScriptLightningDestruct(); });

    script->globals_.ResetValues();
    for (auto* node = script->worldBlocks_.head; node;) {
        auto* next = node->next;
        script->RunWorldScript(&node->data);
        node = next;
    }
}

EI_REPLACE(0x004BED90, ScriptPurgeDeletedObjects);
EI_REPLACE(0x004BE490, ScriptDropInstancesAndRuns);
EI_REPLACE(0x004BEB00, ScriptResetZone);
