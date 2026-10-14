// The CArray members the script loader calls out of line (MFC template code the game compiled for its
// own element types): Add for bindings, nested calls, commands and definitions, SetAtGrow for
// commands, the nested call's destructor body, the per-type variable name lookups and two array
// constructors; also the parameter names' and the look queue's SetAtGrow and a string GetAt copy.
// Each Add / SetAtGrow inlines CArray::SetSize(n, -1) (scriptarray::InlineSetSize with the
// instantiation's element helpers) and the element's assignment operator. See script.h.
#include "script.h"
#include "script_array.h"

#include "lib/crt.h"

#include "ei/hook.h"
#include "originals_script.h"
#include "unit_ai.h"

#pragma warning(disable : 4740) // naked forwarders

namespace {

using namespace scriptarray;

// ConstructElements instantiations (MFC, config/library.csv), __stdcall (first, count): bindings
// zeroed; each nested call zeroed with a new SSCCmd; each definition with its arrays' vtables.
__declspec(naked) void __stdcall ConstructBindings(void*, int) EI_ORIGINAL(0x004CCA50)
__declspec(naked) void __stdcall ConstructSubCalls(void*, int) EI_ORIGINAL(0x004CD130)
__declspec(naked) void __stdcall ConstructDefinitions(void*, int) EI_ORIGINAL(0x004CEB80)
// 004CDE10: ConstructElements of 16-byte elements (zeroes).
__declspec(naked) void __stdcall ConstructLooks(void*, int) EI_ORIGINAL(0x004CDE10)

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
const ElementKind kLooks{sizeof(AILook), ConstructLooks, nullptr, nullptr};

// Grows `array` by one element and returns the old size (the new element's index).
template <typename T>
int Append(mfc::CArray<T>* array, const ElementKind& kind) {
    int index = array->size;
    InlineSetSize(Raw(array), index + 1, kind);
    return index;
}

} // namespace

int __fastcall ScriptBindingsAdd(mfc::CArray<ScriptBinding>* array, void*, const ScriptBinding* binding) {
    int index = Append(array, kBindings);
    array->data[index] = *binding;
    return index;
}
EI_REPLACE(0x004D5B50, ScriptBindingsAdd);

int __fastcall ScriptSubCallsAdd(mfc::CArray<ScriptSubCall>* array, void*, const ScriptSubCall* call) {
    int index = Append(array, kSubCalls);
    ScriptSubCall& element = array->data[index];
    element.type = call->type;
    element.argIndex = call->argIndex;
    element.cmd->ScriptCmdAssign(call->cmd);
    return index;
}
EI_REPLACE(0x004D5CB0, ScriptSubCallsAdd);

int __fastcall ScriptCmdsAdd(mfc::CArray<SSCCmd>* array, void*, const SSCCmd* cmd) {
    int index = Append(array, kCmds);
    array->data[index].ScriptCmdAssign(cmd);
    return index;
}
EI_REPLACE(0x004D5ED0, ScriptCmdsAdd);

void __fastcall ScriptCmdsSetAtGrow(mfc::CArray<SSCCmd>* array, void*, int index, const SSCCmd* cmd) {
    if (index >= array->size) InlineSetSize(Raw(array), index + 1, kCmds);
    array->data[index].ScriptCmdAssign(cmd);
}
EI_REPLACE(0x004D65E0, ScriptCmdsSetAtGrow);

int __fastcall ScriptDefinitionsAdd(mfc::CArray<ScriptDefinition>* array, void*, const ScriptDefinition* definition) {
    int index = Append(array, kDefinitions);
    array->data[index].ScriptDefinitionAssign(definition);   // inlined in the original
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

template <typename T>
int ScriptNamedValues<T>::FindName(const char* name) const {
    for (int i = 0; i < names.size; ++i) {
        if (gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(static_cast<const char*>(names.data[i])),
                            reinterpret_cast<const unsigned char*>(name)) == 0)
            return i;
    }
    return -1;
}
template int ScriptNamedValues<float>::FindName(const char*) const;
template int ScriptNamedValues<mfc::CString>::FindName(const char*) const;
template int ScriptNamedValues<ScriptObjectRef>::FindName(const char*) const;
template int ScriptNamedValues<ScriptGroup>::FindName(const char*) const;
EI_REPLACE_METHOD(0x004D5A50, ScriptNamedValues<float>::FindName);
EI_REPLACE_METHOD(0x004D5A90, ScriptNamedValues<mfc::CString>::FindName);
EI_REPLACE_METHOD(0x004D5AD0, ScriptNamedValues<ScriptObjectRef>::FindName);
EI_REPLACE_METHOD(0x004D5B10, ScriptNamedValues<ScriptGroup>::FindName);

mfc::CArray<SSCCmd>* __fastcall ScriptCmdArrayConstruct(mfc::CArray<SSCCmd>* array) {
    array->data = nullptr;
    array->growBy = 0;
    array->maxSize = 0;
    array->size = 0;
    array->vtable = scriptvtable::kCmds;
    return array;
}
EI_REPLACE(0x004D5A10, ScriptCmdArrayConstruct);

mfc::CArray<ScriptIfThen>* __fastcall ScriptIfThenArrayConstruct(mfc::CArray<ScriptIfThen>* array) {
    array->data = nullptr;
    array->growBy = 0;
    array->maxSize = 0;
    array->size = 0;
    array->vtable = scriptvtable::kIfThen;
    return array;
}
EI_REPLACE(0x004D5A30, ScriptIfThenArrayConstruct);

void __fastcall ScriptNamesSetAtGrow(mfc::CArray<mfc::CString>* array, void*, int index, const mfc::CString* text) {
    if (index >= array->size) InlineSetSize(Raw(array), index + 1, kStrings);
    array->data[index] = *text;
}
EI_REPLACE(0x004CE5F0, ScriptNamesSetAtGrow);

void __fastcall ScriptStringsCopyTo(const mfc::CArray<mfc::CString>* array, void*, int index, mfc::CString* out) {
    mfc::CString copy(array->data[index]);   // the by-value GetAt (004C2680)
    *out = copy;
}
EI_REPLACE(0x004C2F80, ScriptStringsCopyTo);

void __fastcall ScriptLooksSetAtGrow(mfc::CArray<AILook>* array, void*, int index, const AILook* look) {
    if (index >= array->size) InlineSetSize(Raw(array), index + 1, kLooks);
    array->data[index] = *look;
}
EI_REPLACE(0x004CDCB0, ScriptLooksSetAtGrow);
