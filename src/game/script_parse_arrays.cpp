// The CArray members the script loader calls out of line (MFC template code the game compiled for its
// own element types): Add for bindings, nested calls, commands and definitions, SetAtGrow for
// commands, the nested call's destructor body, the per-type variable name lookups and two array
// constructors. Each Add / SetAtGrow inlines CArray::SetSize(n, -1) (scriptarray::InlineSetSize with
// the instantiation's element helpers) and the element's assignment operator. See script.h.
#include "script.h"
#include "script_array.h"

#include "lib/crt.h"

#include "ei/hook.h"
#include "originals_script.h"

#pragma warning(disable : 4740) // naked forwarders

namespace {

using namespace scriptarray;

// ConstructElements instantiations (MFC, config/library.csv), __stdcall (first, count): bindings
// zeroed; each nested call zeroed with a new SSCCmd; each definition with its arrays' vtables.
__declspec(naked) void __stdcall ConstructBindings(void*, int) EI_ORIGINAL(0x004CCA50)
__declspec(naked) void __stdcall ConstructSubCalls(void*, int) EI_ORIGINAL(0x004CD130)
__declspec(naked) void __stdcall ConstructDefinitions(void*, int) EI_ORIGINAL(0x004CEB80)

// The element destructors the inlined SetSize runs when the array empties or shrinks (neither
// happens in Add or SetAtGrow, whose new size is above the old one).
void DestroySubCalls(char* first, int count) {
    for (auto* call = reinterpret_cast<ScriptSubCall*>(first); count > 0; --count, ++call) ScriptSubCallDestruct(call);
}
void DestroyDefinitions(char* first, int count) {
    for (auto* definition = reinterpret_cast<ScriptDefinition*>(first); count > 0; --count, ++definition)
        ScriptDefinitionDestruct(definition);
}

const ElementKind kBindings{sizeof(ScriptBinding), ConstructBindings, nullptr, nullptr};
const ElementKind kSubCalls{sizeof(ScriptSubCall), ConstructSubCalls, DestroySubCalls, DestroySubCalls};
const ElementKind kDefinitions{sizeof(ScriptDefinition), ConstructDefinitions, DestroyDefinitions, DestroyDefinitions};

// Grows `array` by one element and returns the old size (the new element's index).
template <typename T>
int Append(MfcArray<T>* array, const ElementKind& kind) {
    int index = array->size;
    InlineSetSize(Raw(array), index + 1, kind);
    return index;
}

// ScriptDefinition::operator= as Add inlines it: the if-then pairs through their SetSize (004C59C0)
// and a by-value GetAt per pair, the variables through ScriptVars::operator= (004CBAE0).
void AssignDefinition(ScriptDefinition* to, const ScriptDefinition* from) {
    to->index = from->index;
    to->name = from->name;
    to->params = from->params;
    IfThenArraySetSizeOriginal(&to->ifThen, nullptr, from->ifThen.size, -1);
    for (int i = 0; i < from->ifThen.size; ++i) to->ifThen.data[i] = from->ifThen.data[i];
    ScriptVarsAssign(&to->vars, nullptr, &from->vars);
}

} // namespace

int __fastcall ScriptBindingsAdd(MfcArray<ScriptBinding>* array, void*, const ScriptBinding* binding) {
    int index = Append(array, kBindings);
    array->data[index] = *binding;
    return index;
}
EI_REPLACE(0x004D5B50, ScriptBindingsAdd);

int __fastcall ScriptSubCallsAdd(MfcArray<ScriptSubCall>* array, void*, const ScriptSubCall* call) {
    int index = Append(array, kSubCalls);
    ScriptSubCall& element = array->data[index];
    element.type = call->type;
    element.argIndex = call->argIndex;
    ScriptCmdAssign(element.cmd, nullptr, call->cmd);
    return index;
}
EI_REPLACE(0x004D5CB0, ScriptSubCallsAdd);

int __fastcall ScriptCmdsAdd(MfcArray<SSCCmd>* array, void*, const SSCCmd* cmd) {
    int index = Append(array, kCmds);
    ScriptCmdAssign(&array->data[index], nullptr, cmd);
    return index;
}
EI_REPLACE(0x004D5ED0, ScriptCmdsAdd);

void __fastcall ScriptCmdsSetAtGrow(MfcArray<SSCCmd>* array, void*, int index, const SSCCmd* cmd) {
    if (index >= array->size) InlineSetSize(Raw(array), index + 1, kCmds);
    ScriptCmdAssign(&array->data[index], nullptr, cmd);
}
EI_REPLACE(0x004D65E0, ScriptCmdsSetAtGrow);

int __fastcall ScriptDefinitionsAdd(MfcArray<ScriptDefinition>* array, void*, const ScriptDefinition* definition) {
    int index = Append(array, kDefinitions);
    AssignDefinition(&array->data[index], definition);
    return index;
}
EI_REPLACE(0x004D6100, ScriptDefinitionsAdd);

void __fastcall ScriptSubCallDestruct(ScriptSubCall* call) {
    SSCCmd* cmd = call->cmd;
    if (cmd) {
        ScriptCmdDestruct(cmd);
        gamecrt::operator_delete(cmd);
    }
}
EI_REPLACE(0x004C5480, ScriptSubCallDestruct);

int __fastcall ScriptFindVariableName(const ScriptNamedValues<float>* vars, void*, const char* name) {
    for (int i = 0; i < vars->names.size; ++i) {
        if (gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(static_cast<const char*>(vars->names.data[i])),
                            reinterpret_cast<const unsigned char*>(name)) == 0)
            return i;
    }
    return -1;
}
EI_REPLACE(0x004D5A50, ScriptFindVariableName);
EI_REPLACE(0x004D5A90, ScriptFindVariableName);
EI_REPLACE(0x004D5AD0, ScriptFindVariableName);
EI_REPLACE(0x004D5B10, ScriptFindVariableName);

MfcArray<SSCCmd>* __fastcall ScriptCmdArrayConstruct(MfcArray<SSCCmd>* array) {
    array->data = nullptr;
    array->growBy = 0;
    array->maxSize = 0;
    array->size = 0;
    array->vtable = scriptvtable::kCmds;
    return array;
}
EI_REPLACE(0x004D5A10, ScriptCmdArrayConstruct);

MfcArray<ScriptIfThen>* __fastcall ScriptIfThenArrayConstruct(MfcArray<ScriptIfThen>* array) {
    array->data = nullptr;
    array->growBy = 0;
    array->maxSize = 0;
    array->size = 0;
    array->vtable = scriptvtable::kIfThen;
    return array;
}
EI_REPLACE(0x004D5A30, ScriptIfThenArrayConstruct);
