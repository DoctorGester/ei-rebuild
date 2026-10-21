// The CScript constructor (004BDE40) and CScript::GetError (00456360). A-E1; see script.h and
// docs/notes/script.md.
//
// The constructor runs for the world's two scripts only (00589ED0, the world's constructor, which
// the static initialiser 00589BE0 calls before the hooks are installed), so in the hooked game the
// original's body has always run already; the rewrite is for the standalone build.
#include "script.h"

#include "originals_script.h"
#include "world_glue.h"

#include "ei/hook.h"
#include "game_lists.h"
#include "game/game_slots.h"

#include <cstring>
#include <new>

namespace {


template <typename T>
void ConstructArray(mfc::CArray<T>* array, void* vtable) {
    array->vtable = vtable;
    array->data = nullptr;
    array->growBy = 0;
    array->maxSize = 0;
    array->size = 0;
}

// A CList constructed empty with the script lists' block size.
template <typename T>
void ConstructList(mfc::CList<T>* list, void* vtable) {
    list->vtable = vtable;
    list->count = 0;
    list->free = nullptr;
    list->tail = nullptr;
    list->head = nullptr;
    list->blocks = nullptr;
    list->blockSize = 10;
}

template <typename T>
ScriptNamedValues<T>* Named(mfc::CArray<T>* values) { return reinterpret_cast<ScriptNamedValues<T>*>(values); }

} // namespace

CScript* CScript::Construct() {
    ConstructArray(&pointLights_, scriptvtable::kPointLights);
    ConstructList(&particles_, gameslots::ParticleSourceListClass::Vtable());
    ConstructList(&fxSources_, gameslots::FxSourceListClass::Vtable());
    ConstructList(&lightning_, gameslots::LightningListClass::Vtable());
    ConstructList(&blocks_, gameslots::DeclListClass::Vtable());
    ConstructArray(&definitions_, scriptvtable::kDefinitions);
    ScriptFloatsConstruct(Named(&globals_.floats));
    ScriptStringsConstruct(Named(&globals_.strings));
    ScriptObjectsConstruct(Named(&globals_.objects));
    ScriptGroupsConstruct(Named(&globals_.groups));
    ConstructList(&instances_, gameslots::DefinitionListClass::Vtable());
    ConstructList(&runs_, gameslots::RunListClass::Vtable());
    ConstructList(&worldBlocks_, gameslots::DeclListClass::Vtable());
    ConstructArray(&areas_, scriptvtable::kAreas);
    ConstructList(&tempGroups_, gameslots::GroupListClass::Vtable());
    ConstructList(&tempObjects_, gameslots::ObjectRefListClass::Vtable());
    new (&errorText_) mfc::CString();

    // 256 point light slots, zeroed (again) after the array made them.
    pointLights_.SetSize(0x100, -1);
    std::memset(pointLights_.data, 0, static_cast<std::size_t>(pointLights_.size) * sizeof(ScriptPointLight));

    // The evaluator's temporaries start with one empty group and one null reference.
    ScriptGroup group;
    ConstructArray(static_cast<mfc::CArray<ScriptObjectRef>*>(&group), mfc::impl::ScriptGroupClass::Vtable());
    // An empty group and a null reference at the temporary lists' heads (004C8CE0, 004C9210).
    tempGroups_.PushHead(&group);
    ScriptGroupDestructOriginal(&group);
    ScriptObjectRef none{nullptr};
    tempObjects_.PushHead(&none);
    none.Reset();

    ScriptReset(this);
    return this;
}
EI_REWRITTEN(0x004BDE40, CScript::Construct);

ScriptErrorInfo* CScript::GetError(ScriptErrorInfo* out) {
    new (&out->text) mfc::CString(errorText_);
    out->code = errorCode_;
    out->line = errorLine_;
    return out;
}
EI_REWRITTEN(0x00456360, CScript::GetError);
