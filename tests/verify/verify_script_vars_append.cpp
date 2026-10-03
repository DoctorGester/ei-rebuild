// Checks for the variable appends (src/game/script_vars_append.cpp). They allocate and reference
// objects, so ours runs alone; the other set is captured first (it may be this set) and the new
// slots must then hold its names and values: float bits as FLD / FSTP leave them, the same string,
// object, or group members.
#include "verify.h"

#include "../../src/game/script.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {
void* s_unused;

std::vector<std::string> Names(const MfcArray<mfc::CString>& names) {
    std::vector<std::string> out;
    for (int i = 0; i < names.size; ++i) out.emplace_back(static_cast<const char*>(names.data[i]));
    return out;
}

// Common checks: sizes, and the names copied into slots old..old+n-1.
template <typename T>
bool SameShape(const ScriptNamedValues<T>* self, int oldSize, int otherValues, const std::vector<std::string>& names) {
    if (self->values.size != oldSize + otherValues) return false;
    if (self->names.size != oldSize + static_cast<int>(names.size())) return false;
    for (std::size_t i = 0; i < names.size(); ++i)
        if (names[i] != static_cast<const char*>(self->names.data[oldSize + i])) return false;
    return true;
}

std::uint32_t AfterFpu(std::uint32_t bits) {
    bool nan = (bits & 0x7F800000) == 0x7F800000 && (bits & 0x007FFFFF) != 0;
    return nan ? bits | 0x00400000 : bits;
}

void __fastcall FloatsVerify(ScriptNamedValues<float>* self, void*, const ScriptNamedValues<float>* other) {
    int old = self->values.size, values = other->values.size;
    std::vector<std::string> names = Names(other->names);
    std::vector<std::uint32_t> bits(static_cast<std::size_t>(names.size()));
    if (!bits.empty()) std::memcpy(bits.data(), other->values.data, bits.size() * 4);
    ScriptFloatsAppend(self, nullptr, other);
    bool ok = SameShape(self, old, values, names);
    for (std::size_t i = 0; ok && i < bits.size(); ++i) {
        std::uint32_t got;
        std::memcpy(&got, &self->values.data[old + i], 4);
        ok = got == AfterFpu(bits[i]);
    }
    ei::verify::Check("ScriptFloatsAppend", true, ok, "%d + %d values", old, values);
}

void __fastcall StringsVerify(ScriptNamedValues<mfc::CString>* self, void*, const ScriptNamedValues<mfc::CString>* other) {
    int old = self->values.size, values = other->values.size;
    std::vector<std::string> names = Names(other->names);
    std::vector<std::string> strings;
    for (std::size_t i = 0; i < names.size(); ++i) strings.emplace_back(static_cast<const char*>(other->values.data[i]));
    ScriptStringsAppend(self, nullptr, other);
    bool ok = SameShape(self, old, values, names);
    for (std::size_t i = 0; ok && i < strings.size(); ++i) ok = strings[i] == static_cast<const char*>(self->values.data[old + i]);
    ei::verify::Check("ScriptStringsAppend", true, ok, "%d + %d values", old, values);
}

void __fastcall ObjectsVerify(ScriptNamedValues<ScriptObjectRef>* self, void*, const ScriptNamedValues<ScriptObjectRef>* other) {
    int old = self->values.size, values = other->values.size;
    std::vector<std::string> names = Names(other->names);
    std::vector<CEngineObject*> objects;
    for (std::size_t i = 0; i < names.size(); ++i) objects.push_back(other->values.data[i].object);
    ScriptObjectsAppend(self, nullptr, other);
    bool ok = SameShape(self, old, values, names);
    for (std::size_t i = 0; ok && i < objects.size(); ++i) ok = objects[i] == self->values.data[old + i].object;
    ei::verify::Check("ScriptObjectsAppend", true, ok, "%d + %d values", old, values);
}

void __fastcall GroupsVerify(ScriptNamedValues<ScriptGroup>* self, void*, const ScriptNamedValues<ScriptGroup>* other) {
    int old = self->values.size, values = other->values.size;
    std::vector<std::string> names = Names(other->names);
    std::vector<std::vector<CEngineObject*>> groups;
    for (std::size_t i = 0; i < names.size(); ++i) {
        const ScriptGroup& g = other->values.data[i];
        std::vector<CEngineObject*> members;
        for (int k = 0; k < g.size; ++k) members.push_back(g.data[k].object);
        groups.push_back(members);
    }
    ScriptGroupsAppend(self, nullptr, other);
    bool ok = SameShape(self, old, values, names);
    for (std::size_t i = 0; ok && i < groups.size(); ++i) {
        const ScriptGroup& g = self->values.data[old + i];
        ok = g.size == static_cast<int>(groups[i].size());
        for (int k = 0; ok && k < g.size; ++k) ok = g.data[k].object == groups[i][k];
    }
    ei::verify::Check("ScriptGroupsAppend", true, ok, "%d + %d values", old, values);
}

// ---- Clears: ours only, then every array is empty with no storage ---------------------------------

template <typename T>
bool Empty(const MfcArray<T>& a) { return !a.data && a.size == 0 && a.maxSize == 0; }
template <typename T>
bool Empty(const ScriptNamedValues<T>* v) { return Empty(v->values) && Empty(v->names); }

void __fastcall FloatsClearVerify(ScriptNamedValues<float>* self) {
    ScriptFloatsClear(self);
    ei::verify::Check("ScriptFloatsClear", true, Empty(self), "%p", self);
}
void __fastcall StringsClearVerify(ScriptNamedValues<mfc::CString>* self) {
    ScriptStringsClear(self);
    ei::verify::Check("ScriptStringsClear", true, Empty(self), "%p", self);
}
void __fastcall ObjectsClearVerify(ScriptNamedValues<ScriptObjectRef>* self) {
    ScriptObjectsClear(self);
    ei::verify::Check("ScriptObjectsClear", true, Empty(self), "%p", self);
}
void __fastcall GroupsClearVerify(ScriptNamedValues<ScriptGroup>* self) {
    ScriptGroupsClear(self);
    ei::verify::Check("ScriptGroupsClear", true, Empty(self), "%p", self);
}
void __fastcall VarsClearVerify(ScriptVars* self) {
    ScriptVarsClear(self);
    bool ok = Empty(self->floats) && Empty(self->floatNames) && Empty(self->strings) && Empty(self->stringNames) &&
              Empty(self->objects) && Empty(self->objectNames) && Empty(self->groups) && Empty(self->groupNames);
    ei::verify::Check("ScriptVarsClear", true, ok, "%p", self);
}

} // namespace

EI_VERIFY(0x004C1650, FloatsClearVerify, 0, s_unused);
EI_VERIFY(0x004C16B0, StringsClearVerify, 0, s_unused);
EI_VERIFY(0x004C1700, ObjectsClearVerify, 0, s_unused);
EI_VERIFY(0x004C1760, GroupsClearVerify, 0, s_unused);
EI_VERIFY(0x004C0020, VarsClearVerify, 0, s_unused);
EI_VERIFY(0x004C2B70, FloatsVerify, 0, s_unused);
EI_VERIFY(0x004C2FE0, StringsVerify, 0, s_unused);
EI_VERIFY(0x004C3450, ObjectsVerify, 0, s_unused);
EI_VERIFY(0x004C3DC0, GroupsVerify, 0, s_unused);
