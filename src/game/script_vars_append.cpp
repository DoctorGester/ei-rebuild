// Appending one variable set's values to another, one type at a time (004C2B70 floats, 004C2FE0
// strings, 004C3450 objects, 004C3DC0 groups); ScriptVars::AppendFrom (004B3900) calls the four.
// The loader uses it to add a file's GlobalVars to the globals. See script.h and
// docs/notes/script.md ("Variable appends").
//
// Each grows the values by the other set's value count (the inlined CArray::SetSize), the names
// to old size + the other set's name count, then copies, for every one of the other set's names,
// the name and the value with the same index into the new slots.
#include "script.h"
#include "script_array.h"

#include "lib/crt.h"

#include "ei/hook.h"

#pragma warning(disable : 4740) // naked forwarders

using namespace scriptarray;

namespace {

// CArray members called out of line: 004C2510 CStringArray SetSize, 004C2680 GetAt (CString by
// value; also on string values), 004C21E0 GetAt of a reference array (ScriptObjectRef by value,
// referenced), 004C3B00 GetAt of a group array (ScriptGroup by value).
__declspec(naked) void __fastcall NamesSetSize(void*, void*, int, int) EI_ORIGINAL(0x004C2510)
__declspec(naked) mfc::CString* __fastcall StringAt(const void*, void*, void* /*out*/, int) EI_ORIGINAL(0x004C2680)
__declspec(naked) ScriptObjectRef* __fastcall RefAt(const void*, void*, void* /*out*/, int) EI_ORIGINAL(0x004C21E0)
__declspec(naked) ScriptGroup* __fastcall GroupAt(const void*, void*, void* /*out*/, int) EI_ORIGINAL(0x004C3B00)
// Rewritten script helpers, called through their addresses.
__declspec(naked) void __fastcall GroupSetSize(ScriptGroup*, void*, int, int) EI_ORIGINAL(0x004C2050)
__declspec(naked) void __fastcall RefReset(ScriptObjectRef*) EI_ORIGINAL(0x004B24B0)
__declspec(naked) CEngineObject* __fastcall RefConstruct(ScriptObjectRef*, void*, const ScriptObjectRef*) EI_ORIGINAL(0x004B2500)
// 004CE1E0: a reference's destructor (__thiscall (flags)).
__declspec(naked) void __fastcall RefDestroy(void*, void*, int) EI_ORIGINAL(0x004CE1E0)

// The float is copied through the FPU (FLD / FSTP), as the original does: a signalling NaN
// becomes quiet.
__declspec(naked) void __cdecl CopyFloat(float* /*to*/, const float* /*from*/) {
    __asm {
        mov eax, [esp + 8]
        fld dword ptr [eax]
        mov eax, [esp + 4]
        fstp dword ptr [eax]
        ret
    }
}

// A CString returned by value into raw storage; destroyed at the end of the scope.
struct TempString {
    void* data;
    mfc::CString& Get() { return *reinterpret_cast<mfc::CString*>(this); }
    ~TempString() { Get().~CString(); }
};

void Release(CEngineObject* object) {
    if (object) object->Release();
}

// The name of the other set's variable i into slot `to` of this set's names.
void CopyName(MfcArray<mfc::CString>* names, int to, const MfcArray<mfc::CString>* otherNames, int i) {
    TempString name;
    StringAt(otherNames, nullptr, &name, i);
    names->data[to] = name.Get();
}

} // namespace

void __fastcall ScriptFloatsAppend(ScriptNamedValues<float>* self, void*, const ScriptNamedValues<float>* other) {
    int old = self->values.size;
    InlineSetSize(Raw(&self->values), other->values.size + old, kFloats);
    NamesSetSize(&self->names, nullptr, other->names.size + old, -1);
    for (int i = 0; i < other->names.size; ++i) {
        CopyName(&self->names, old + i, &other->names, i);
        CopyFloat(&self->values.data[old + i], &other->values.data[i]);
    }
}
EI_REPLACE(0x004C2B70, ScriptFloatsAppend);

void __fastcall ScriptStringsAppend(ScriptNamedValues<mfc::CString>* self, void*,
                                    const ScriptNamedValues<mfc::CString>* other) {
    int old = self->values.size;
    InlineSetSize(Raw(&self->values), other->values.size + old, kStrings);
    NamesSetSize(&self->names, nullptr, other->names.size + old, -1);
    for (int i = 0; i < other->names.size; ++i) {
        CopyName(&self->names, old + i, &other->names, i);
        TempString value;
        StringAt(&other->values, nullptr, &value, i);
        self->values.data[old + i] = value.Get();
    }
}
EI_REPLACE(0x004C2FE0, ScriptStringsAppend);

// The new slot drops its (null) object, then takes the other's object with a reference of its own.
void __fastcall ScriptObjectsAppend(ScriptNamedValues<ScriptObjectRef>* self, void*,
                                    const ScriptNamedValues<ScriptObjectRef>* other) {
    int old = self->values.size;
    InlineSetSize(Raw(&self->values), other->values.size + old, kRefs);
    NamesSetSize(&self->names, nullptr, other->names.size + old, -1);
    for (int i = 0; i < other->names.size; ++i) {
        CopyName(&self->names, old + i, &other->names, i);
        ScriptObjectRef value;
        RefAt(&other->values, nullptr, &value, i);
        ScriptObjectRef* slot = &self->values.data[old + i];
        Release(slot->object);
        slot->object = nullptr;
        slot->object = value.object;
        if (value.object) ++value.object->refCount_;
        Release(value.object);
    }
}
EI_REPLACE(0x004C3450, ScriptObjectsAppend);

// The new group is sized to the other's and each member reference copied in place.
void __fastcall ScriptGroupsAppend(ScriptNamedValues<ScriptGroup>* self, void*,
                                   const ScriptNamedValues<ScriptGroup>* other) {
    int old = self->values.size;
    InlineSetSize(Raw(&self->values), other->values.size + old, kGroups);
    NamesSetSize(&self->names, nullptr, other->names.size + old, -1);
    for (int i = 0; i < other->names.size; ++i) {
        CopyName(&self->names, old + i, &other->names, i);
        ScriptGroup copy;
        GroupAt(&other->values, nullptr, &copy, i);
        ScriptGroup* group = &self->values.data[old + i];
        GroupSetSize(group, nullptr, copy.size, -1);
        for (int k = 0; k < copy.size; ++k) {
            ScriptObjectRef member;
            RefAt(&copy, nullptr, &member, k);
            RefReset(&group->data[k]);
            RefConstruct(&group->data[k], nullptr, &member);
            Release(member.object);
        }
        // The copy's destructor (vtable 0073D11C): each reference destroyed, the array freed.
        copy.vtable = reinterpret_cast<void*>(0x0073D11C);
        if (copy.data) {
            for (int k = 0; k < copy.size; ++k) RefDestroy(&copy.data[k], nullptr, 0);
            gamecrt::operator_delete(copy.data);
        }
    }
}
EI_REPLACE(0x004C3DC0, ScriptGroupsAppend);
