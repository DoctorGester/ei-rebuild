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

#include <cstring>
#include <new>

namespace {

// CList<ScriptGroup>::AddTail (004C8CE0) and CList<ScriptObjectRef>::AddTail (004C9210); RET 4,
// the new node in EAX. The element is constructed in the node, then assigned from `value` (a
// group's objects copied, a reference shared).
__declspec(naked) void* __fastcall TempGroupsAddTail(mfc::CList<ScriptGroup>*, void*, const ScriptGroup*)
    EI_ORIGINAL(0x004C8CE0)
__declspec(naked) void* __fastcall TempObjectsAddTail(mfc::CList<ScriptObjectRef>*, void*, const ScriptObjectRef*)
    EI_ORIGINAL(0x004C9210)

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
    ConstructList(&particles_, scriptvtable::kParticleList);
    ConstructList(&fxSources_, scriptvtable::kFxSourceList);
    ConstructList(&lightning_, scriptvtable::kLightningList);
    ConstructList(&blocks_, scriptvtable::kBlockList);
    ConstructArray(&definitions_, scriptvtable::kDefinitions);
    ScriptFloatsConstruct(Named(&globals_.floats));
    ScriptStringsConstruct(Named(&globals_.strings));
    ScriptObjectsConstruct(Named(&globals_.objects));
    ScriptGroupsConstruct(Named(&globals_.groups));
    ConstructList(&instances_, scriptvtable::kInstanceList);
    ConstructList(&runs_, scriptvtable::kRunList);
    ConstructList(&worldBlocks_, scriptvtable::kBlockList);
    ConstructArray(&areas_, scriptvtable::kAreas);
    ConstructList(&tempGroups_, scriptvtable::kTempGroupList);
    ConstructList(&tempObjects_, scriptvtable::kTempObjectList);
    new (&errorText_) mfc::CString();

    // 256 point light slots, zeroed (again) after the array made them.
    pointLights_.SetSize(0x100, -1);
    std::memset(pointLights_.data, 0, static_cast<std::size_t>(pointLights_.size) * sizeof(ScriptPointLight));

    // The evaluator's temporaries start with one empty group and one null reference.
    ScriptGroup group;
    ConstructArray(static_cast<mfc::CArray<ScriptObjectRef>*>(&group), scriptvtable::kRefs);
    TempGroupsAddTail(&tempGroups_, nullptr, &group);
    ScriptGroupDestructOriginal(&group);
    ScriptObjectRef none{nullptr};
    TempObjectsAddTail(&tempObjects_, nullptr, &none);
    none.Reset();

    ScriptReset(this);
    return this;
}
EI_REPLACE_METHOD(0x004BDE40, CScript::Construct);

ScriptErrorInfo* CScript::GetError(ScriptErrorInfo* out) {
    new (&out->text) mfc::CString(errorText_);
    out->code = errorCode_;
    out->line = errorLine_;
    return out;
}
EI_REPLACE_METHOD(0x00456360, CScript::GetError);
