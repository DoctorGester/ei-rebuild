// Differential checks for the script savegame loaders (src/game/script_load.cpp).
//
// Record / replay: the original reads the real stream into a fresh, empty scratch object (every
// Read recorded: size, result, bytes), ours replays the recording into the real object. Both must
// make the same reads, and the objects must end equal; the scratch is then destroyed (its object
// references released). This needs the real object to be empty on entry (it always is when a save
// is loaded: the world is new); otherwise ours runs alone on the real stream and only the shape is
// checked. Command blocks, definitions and runs are compared through their savers (bytes written).
// Nested loaders run through their own wrappers in both runs. CScript::Load itself runs ours only.
#include "fake_stream.h"
#include "verify.h"

#include "../../src/game/script.h"
#include "../../src/lib/crt.h"

#include "ei/vcall.h"
#include "../../src/game/originals_library.h"
#include "../../src/game/originals_script.h"

#include <cstdio>
#include <cstring>
#include <vector>

#pragma warning(disable : 4740) // naked forwarders

namespace {
void* s_unused;

// ---- Streams ----------------------------------------------------------------------------------------

// Laid out like the game's stream (vtable: slot 0 the deleting destructor, Read returning the bytes read, Write; then +4
// and the version at +8, which the loaders read).
class LoadStream {
public:
    virtual void* DeletingDestruct(unsigned) { return this; }
    virtual int Read(void* buffer, unsigned size) = 0;
    virtual void Write(const void*, unsigned) {}
    explicit LoadStream(const ScriptStream* real) {
        std::memcpy(&unknown_04, reinterpret_cast<const char*>(real) + 4, 8);
    }
    ScriptStream* AsScript() { return reinterpret_cast<ScriptStream*>(this); }
    int unknown_04;
    int version;
};

class RecordStream : public LoadStream {
public:
    explicit RecordStream(ScriptStream* real) : LoadStream(real), real_(real) {}
    int Read(void* buffer, unsigned size) override {
        int result = ei::VCall<int>(real_, 1, buffer, size);
        ops.push_back({size, result, bytes.size()});
        auto* p = static_cast<const unsigned char*>(buffer);
        bytes.insert(bytes.end(), p, p + size);
        return result;
    }
    struct Op {
        unsigned size;
        int result;
        std::size_t at;
    };
    std::vector<Op> ops;
    std::vector<unsigned char> bytes;

private:
    ScriptStream* real_;
};

class ReplayStream : public LoadStream {
public:
    ReplayStream(const ScriptStream* real, const RecordStream& record) : LoadStream(real), record_(record) {}
    int Read(void* buffer, unsigned size) override {
        if (next_ >= record_.ops.size() || record_.ops[next_].size != size) {
            mismatch_ = true;
            std::memset(buffer, 0, size);
            return 0;
        }
        const RecordStream::Op& op = record_.ops[next_++];
        std::memcpy(buffer, record_.bytes.data() + op.at, size);
        return op.result;
    }
    bool Same() const { return !mismatch_ && next_ == record_.ops.size(); }
    std::size_t Reads() const { return next_; }

private:
    const RecordStream& record_;
    std::size_t next_ = 0;
    bool mismatch_ = false;
};

class CaptureStream : public FakeStream {
public:
    std::uint32_t Read(void*, std::uint32_t) override { return 0; }
    void Write(const void* buffer, std::uint32_t size) override {
        auto* p = static_cast<const unsigned char*>(buffer);
        bytes.insert(bytes.end(), p, p + size);
    }
    std::vector<unsigned char> bytes;
};

// ---- Original helpers used to compare and to destroy scratch objects ---------------------------------

__declspec(naked) void __fastcall NamesSetSize(void*, void*, int, int) EI_ORIGINAL(0x004C2510)
__declspec(naked) void __fastcall CmdsSetSize(void*, void*, int, int) EI_ORIGINAL(0x004CD2B0)
void __stdcall DestructStrings(void* a0, int a1) { CStringElementsDestruct(a0, a1); }
void __stdcall DestructRefs(void* a0, int a1) { ScriptRefElementsDestruct(a0, a1); }
__declspec(naked) void __fastcall DefinitionDestruct(void*) EI_ORIGINAL(0x004BE9D0)
__declspec(naked) void __fastcall VarsDestruct(void*) EI_ORIGINAL(0x004BE090)
__declspec(naked) void __fastcall VarsConstruct(void*) EI_ORIGINAL(0x004C00D0)
// Savers (through their addresses).
__declspec(naked) void __fastcall DeclSave(const void*, void*, ScriptStream*) EI_ORIGINAL(0x004C1230)
__declspec(naked) void __fastcall DefinitionSave(const void*, void*, ScriptStream*) EI_ORIGINAL(0x004BBFE0)
__declspec(naked) void __fastcall RunSave(const void*, void*, ScriptStream*) EI_ORIGINAL(0x004BB320)

template <typename Save>
std::vector<unsigned char> Saved(Save save, const void* object) {
    CaptureStream capture;
    save(object, nullptr, &capture);
    return capture.bytes;
}

template <typename T>
bool Empty(const mfc::CArray<T>& a) { return !a.data && a.size == 0 && a.maxSize == 0; }
template <typename T>
mfc::CArray<T> EmptyLike(const mfc::CArray<T>& a) {
    mfc::CArray<T> e{};
    e.vtable = a.vtable;
    e.growBy = a.growBy;
    return e;
}
bool Empty(const ScriptList& l) { return !l.head && !l.tail && l.count == 0; }
ScriptList EmptyLike(const ScriptList& l) {
    ScriptList e{};
    e.vtable = l.vtable;
    e.blockSize = l.blockSize;
    return e;
}

// Walked node count equals the count field, and the prev links match.
bool WellFormed(const ScriptList& l) {
    int n = 0;
    const ScriptListNode* prev = nullptr;
    for (const ScriptListNode* node = l.head; node; node = node->next) {
        if (node->prev != prev || ++n > l.count) return false;
        prev = node;
    }
    return n == l.count && l.tail == prev;
}

template <typename T>
bool SameShape(const mfc::CArray<T>& a, const mfc::CArray<T>& b) { return a.size == b.size && a.maxSize == b.maxSize; }

bool SameString(const mfc::CString& a, const mfc::CString& b) {
    return std::strcmp(static_cast<const char*>(a), static_cast<const char*>(b)) == 0;
}
bool SameNames(const mfc::CArray<mfc::CString>& a, const mfc::CArray<mfc::CString>& b) {
    if (!SameShape(a, b)) return false;
    for (int i = 0; i < a.size; ++i)
        if (!SameString(a.data[i], b.data[i])) return false;
    return true;
}

void FreeNames(mfc::CArray<mfc::CString>& names) { NamesSetSize(&names, nullptr, 0, -1); }

void Release(CEngineObject* object) {
    if (object) object->Release();
}

void Report(const char* name, bool same, const ReplayStream& replay, const RecordStream& record, const char* what) {
    ei::verify::Check(name, true, same && replay.Same(), "%s; reads %u of %u%s", what, unsigned(replay.Reads()),
                      unsigned(record.ops.size()), replay.Same() ? "" : " (read sequence differs)");
}

bool SameGroup(const ScriptGroup& a, const ScriptGroup& b) {
    if (a.size != b.size || a.maxSize != b.maxSize || a.growBy != b.growBy) return false;
    for (int k = 0; k < a.size; ++k)
        if (a.data[k].object != b.data[k].object) return false;
    return true;
}

// ---- Variables ----------------------------------------------------------------------------------------

template <typename T>
using VarsLoadFn = void(__fastcall*)(ScriptNamedValues<T>*, void*, ScriptStream*);

template <typename T>
void __fastcall VarsLoadOurs(ScriptNamedValues<T>* self, void*, ScriptStream* stream) {
    self->Load(stream);
}

// Runs the record / replay check for one variable loader; `same` compares the values, `destroy`
// frees the scratch values (the names are freed here).
template <typename T, typename Same, typename Destroy>
void VarsVerify(const char* name, VarsLoadFn<T> original, VarsLoadFn<T> ours, ScriptNamedValues<T>* self,
                ScriptStream* stream, Same same, Destroy destroy) {
    if (!Empty(self->values) || !Empty(self->names)) {
        ours(self, nullptr, stream);
        ei::verify::Check(name, true, self->values.size == self->names.size, "ours only: %d values, %d names",
                          self->values.size, self->names.size);
        return;
    }
    ScriptNamedValues<T> scratch{EmptyLike(self->values), EmptyLike(self->names)};
    RecordStream record(stream);
    original(&scratch, nullptr, record.AsScript());
    ReplayStream replay(stream, record);
    ours(self, nullptr, replay.AsScript());
    bool ok = SameShape(scratch.values, self->values) && SameNames(scratch.names, self->names);
    for (int i = 0; ok && i < self->values.size; ++i) ok = same(scratch.values.data[i], self->values.data[i]);
    char what[64];
    std::snprintf(what, sizeof(what), "%d vs %d values", scratch.values.size, self->values.size);
    Report(name, ok, replay, record, what);
    destroy(scratch.values);
    FreeNames(scratch.names);
}

// 004C2D40: SUB ESP,0xC; PUSH EBX; PUSH EBP; PUSH ESI (6 bytes).
VarsLoadFn<float> s_floats;
void __fastcall FloatsLoadVerify(ScriptNamedValues<float>* self, void*, ScriptStream* stream) {
    VarsVerify<float>(
        "ScriptFloatsLoad", s_floats, VarsLoadOurs<float>, self, stream,
        [](const float& a, const float& b) { return std::memcmp(&a, &b, 4) == 0; },
        [](mfc::CArray<float>& a) { gamecrt::operator_delete(a.data); });
}

// 004C3200: SUB ESP,0xC; PUSH EBX; PUSH EBP; PUSH ESI; PUSH EDI (7 bytes).
VarsLoadFn<mfc::CString> s_strings;
void __fastcall StringsLoadVerify(ScriptNamedValues<mfc::CString>* self, void*, ScriptStream* stream) {
    VarsVerify<mfc::CString>(
        "ScriptStringsLoad", s_strings, VarsLoadOurs<mfc::CString>, self, stream, SameString,
        [](mfc::CArray<mfc::CString>& a) { FreeNames(a); });
}

// 004C36B0: SUB ESP,0xC; PUSH EBX; PUSH EBP (5 bytes).
VarsLoadFn<ScriptObjectRef> s_objects;
void __fastcall ObjectsLoadVerify(ScriptNamedValues<ScriptObjectRef>* self, void*, ScriptStream* stream) {
    VarsVerify<ScriptObjectRef>(
        "ScriptObjectsLoad", s_objects, VarsLoadOurs<ScriptObjectRef>, self, stream,
        [](const ScriptObjectRef& a, const ScriptObjectRef& b) { return a.object == b.object; },
        [](mfc::CArray<ScriptObjectRef>& a) {
            if (a.data) {
                DestructRefs(a.data, a.size);
                gamecrt::operator_delete(a.data);
            }
        });
}

// 004C40D0: SUB ESP,0xC; PUSH EBX; PUSH EBP (5 bytes).
VarsLoadFn<ScriptGroup> s_groups;
void __fastcall GroupsLoadVerify(ScriptNamedValues<ScriptGroup>* self, void*, ScriptStream* stream) {
    VarsVerify<ScriptGroup>(
        "ScriptGroupsLoad", s_groups, VarsLoadOurs<ScriptGroup>, self, stream,
        SameGroup,
        [](mfc::CArray<ScriptGroup>& a) {
            if (a.data) {
                for (int i = 0; i < a.size; ++i) GroupDestroy(&a.data[i], nullptr, 0);
                gamecrt::operator_delete(a.data);
            }
        });
}

// ---- ScriptVars::Load ----------------------------------------------------------------------------------

bool EmptyVars(const ScriptVars& v) {
    return Empty(v.floats) && Empty(v.floatNames) && Empty(v.strings) && Empty(v.stringNames) && Empty(v.objects) &&
           Empty(v.objectNames) && Empty(v.groups) && Empty(v.groupNames);
}

// Every value (float bits, string, object pointer, group members, sizes, capacities and growBy) and
// every name; all of them are written by the load (sizes from an empty set, every slot read).
bool SameVars(const ScriptVars& a, const ScriptVars& b) {
    bool ok = SameShape(a.floats, b.floats) && SameShape(a.strings, b.strings) && SameShape(a.objects, b.objects) &&
              SameShape(a.groups, b.groups) && SameNames(a.floatNames, b.floatNames) &&
              SameNames(a.stringNames, b.stringNames) && SameNames(a.objectNames, b.objectNames) &&
              SameNames(a.groupNames, b.groupNames);
    for (int i = 0; ok && i < a.floats.size; ++i) ok = std::memcmp(&a.floats.data[i], &b.floats.data[i], 4) == 0;
    for (int i = 0; ok && i < a.strings.size; ++i) ok = SameString(a.strings.data[i], b.strings.data[i]);
    for (int i = 0; ok && i < a.objects.size; ++i) ok = a.objects.data[i].object == b.objects.data[i].object;
    for (int i = 0; ok && i < a.groups.size; ++i) ok = SameGroup(a.groups.data[i], b.groups.data[i]);
    return ok;
}

// 004B3AD0: PUSH ECX; PUSH EBX; PUSH EBP; PUSH ESI; MOV ESI,[ESP+0x14] (8 bytes). Called on new
// definitions, instances and runs (empty sets): the original loads into a scratch set built by the
// ScriptVars constructor 004C00D0, ours replays into the real one; the scratch is destroyed with
// 004BE090 (its references released).
void(__fastcall* s_vars)(ScriptVars*, void*, ScriptStream*);
void __fastcall VarsLoadVerify(ScriptVars* self, void*, ScriptStream* stream) {
    if (!EmptyVars(*self)) {
        self->Load(stream);
        ei::verify::Check("ScriptVarsLoad", true,
                          self->floats.size == self->floatNames.size && self->strings.size == self->stringNames.size &&
                              self->objects.size == self->objectNames.size && self->groups.size == self->groupNames.size,
                          "ours only: value and name counts differ");
        return;
    }
    alignas(ScriptVars) unsigned char raw[sizeof(ScriptVars)];
    VarsConstruct(raw);
    auto* scratch = reinterpret_cast<ScriptVars*>(raw);
    RecordStream record(stream);
    s_vars(scratch, nullptr, record.AsScript());
    ReplayStream replay(stream, record);
    self->Load(replay.AsScript());
    char what[96];
    std::snprintf(what, sizeof(what), "%d/%d/%d/%d vs %d/%d/%d/%d variables", scratch->floats.size,
                  scratch->strings.size, scratch->objects.size, scratch->groups.size, self->floats.size,
                  self->strings.size, self->objects.size, self->groups.size);
    Report("ScriptVarsLoad", SameVars(*scratch, *self), replay, record, what);
    VarsDestruct(scratch);
}

// ---- SSCCmd::Load ---------------------------------------------------------------------------------------

__declspec(naked) SSCCmd* __fastcall CmdConstruct(void*) EI_ORIGINAL(0x004D0A50)
__declspec(naked) void __fastcall CmdDestruct(void*) EI_ORIGINAL(0x004BF640)
__declspec(naked) void __fastcall CmdSave(const void*, void*, ScriptStream*) EI_ORIGINAL(0x004B6360)

bool EmptyCmd(const SSCCmd& c) {
    return Empty(c.floatArgs) && Empty(c.stringArgs) && Empty(c.objectArgs) && Empty(c.groupArgs) &&
           Empty(c.localBindings) && Empty(c.globalBindings) && Empty(c.subCalls);
}
template <typename T>
bool NullSlots(const mfc::CArray<T*>& a) {
    for (int i = 0; i < a.size; ++i)
        if (a.data[i]) return false;
    return true;
}

// A loaded command (`b`, ours) against the original's (`a`): SSCCmd::Save's bytes, every array's
// size and capacity, null object / group slots and the called block pointer.
bool SameLoadedCmd(const SSCCmd& a, const SSCCmd& b) {
    return SameShape(a.floatArgs, b.floatArgs) && SameShape(a.stringArgs, b.stringArgs) &&
           SameShape(a.objectArgs, b.objectArgs) && SameShape(a.groupArgs, b.groupArgs) &&
           SameShape(a.localBindings, b.localBindings) && SameShape(a.globalBindings, b.globalBindings) &&
           SameShape(a.subCalls, b.subCalls) && NullSlots(b.objectArgs) && NullSlots(b.groupArgs) &&
           a.calledScript == b.calledScript && Saved(CmdSave, &a) == Saved(CmdSave, &b);
}

// 004B5FE0: SUB ESP,0xC; PUSH EBX; MOV EBX,[ESP+0x14] (8 bytes). The original reads into a fresh
// scratch command (constructed with 004D0A50); compared: SSCCmd::Save's bytes (id, floats, strings,
// argument counts, bindings, the nested trees, the called block's id), every array's size and
// capacity, null object / group slots, the called block pointer. Nested commands load through
// this wrapper again (004B6330 calls it) in both runs.
void(__fastcall* s_cmd)(SSCCmd*, void*, ScriptStream*);
void __fastcall CmdLoadVerify(SSCCmd* self, void*, ScriptStream* stream) {
    if (!EmptyCmd(*self)) {
        self->Load(stream);
        ei::verify::Check("ScriptCmdLoad", true, NullSlots(self->objectArgs) && NullSlots(self->groupArgs),
                          "ours only: command %d", self->id);
        return;
    }
    alignas(SSCCmd) unsigned char raw[sizeof(SSCCmd)];
    SSCCmd* scratch = CmdConstruct(raw);
    RecordStream record(stream);
    s_cmd(scratch, nullptr, record.AsScript());
    ReplayStream replay(stream, record);
    self->Load(replay.AsScript());
    bool ok = SameLoadedCmd(*scratch, *self);
    char what[64];
    std::snprintf(what, sizeof(what), "command %d vs %d", scratch->id, self->id);
    Report("ScriptCmdLoad", ok, replay, record, what);
    CmdDestruct(scratch);
}

// 004B6330: PUSH ESI; MOV ESI,[ESP+8]; PUSH EDI (6 bytes). One nested call element (SSCCmd::Load
// calls it per element). The original reads into a scratch element whose command is fresh (built
// with 004D0A50), ours replays into the real one; type, argument index and the two commands
// compared as in ScriptCmdLoad. Both command loads run through the 004B5FE0 wrapper.
void(__fastcall* s_subCall)(ScriptSubCall*, void*, ScriptStream*);
void __fastcall SubCallLoadVerify(ScriptSubCall* self, void*, ScriptStream* stream) {
    alignas(SSCCmd) unsigned char raw[sizeof(SSCCmd)];
    ScriptSubCall scratch{0, 0, CmdConstruct(raw)};
    RecordStream record(stream);
    s_subCall(&scratch, nullptr, record.AsScript());
    ReplayStream replay(stream, record);
    self->Load(replay.AsScript());
    bool ok = scratch.type == self->type && scratch.argIndex == self->argIndex && self->cmd &&
              SameLoadedCmd(*scratch.cmd, *self->cmd);
    char what[96];
    std::snprintf(what, sizeof(what), "type %d arg %d vs type %d arg %d", scratch.type, scratch.argIndex, self->type,
                  self->argIndex);
    Report("ScriptSubCallLoad", ok, replay, record, what);
    CmdDestruct(scratch.cmd);
}

// 004B3D10: PUSH -1; PUSH 0x717EB8 (7 bytes). A string (every name and string value of a loaded
// save): the original reads into a scratch empty CString, ours replays into the real one; the
// texts compared.
void(__fastcall* s_string)(mfc::CString*, ScriptStream*);
void __fastcall StringLoadVerify(mfc::CString* self, ScriptStream* stream) {
    mfc::CString scratch;
    RecordStream record(stream);
    s_string(&scratch, record.AsScript());
    ReplayStream replay(stream, record);
    ScriptStringLoad(self, replay.AsScript());
    char what[96];
    std::snprintf(what, sizeof(what), "\"%.32s\" vs \"%.32s\"", static_cast<const char*>(scratch),
                  static_cast<const char*>(*self));
    Report("ScriptStringLoad", SameString(scratch, *self), replay, record, what);
}

// ---- Command blocks and if-then pairs -------------------------------------------------------------------

// 004C57C0: SUB ESP,0x10; PUSH EBX; PUSH EBP; PUSH ESI; PUSH EDI (7 bytes).
void(__fastcall* s_declCommands)(ScriptDecl*, ScriptStream*);
void __fastcall DeclCommandsVerify(ScriptDecl* self, ScriptStream* stream) {
    if (!Empty(self->commands)) {
        ScriptDeclCommandsLoad(self, stream);
        ei::verify::Check("ScriptDeclCommandsLoad", true, self->commands.size >= 0, "ours only");
        return;
    }
    ScriptDecl scratch{EmptyLike(self->commands), self->id};
    RecordStream record(stream);
    s_declCommands(&scratch, record.AsScript());
    ReplayStream replay(stream, record);
    ScriptDeclCommandsLoad(self, replay.AsScript());
    bool ok = SameShape(scratch.commands, self->commands) && Saved(DeclSave, &scratch) == Saved(DeclSave, self);
    char what[64];
    std::snprintf(what, sizeof(what), "block %d: %d vs %d commands", self->id, scratch.commands.size, self->commands.size);
    Report("ScriptDeclCommandsLoad", ok, replay, record, what);
    CmdsSetSize(&scratch.commands, nullptr, 0, -1);
}

// 004CA000: SUB ESP,0x10; PUSH EBX; PUSH EBP (5 bytes).
void(__fastcall* s_ifThen)(mfc::CArray<ScriptIfThen>*, ScriptStream*);
void __fastcall IfThenVerify(mfc::CArray<ScriptIfThen>* self, ScriptStream* stream) {
    if (!Empty(*self)) {
        ScriptIfThenLoad(self, stream);
        ei::verify::Check("ScriptIfThenLoad", true, self->size >= 0, "ours only");
        return;
    }
    mfc::CArray<ScriptIfThen> scratch = EmptyLike(*self);
    RecordStream record(stream);
    s_ifThen(&scratch, record.AsScript());
    ReplayStream replay(stream, record);
    ScriptIfThenLoad(self, replay.AsScript());
    bool ok = SameShape(scratch, *self) &&
              (self->size <= 0 || std::memcmp(scratch.data, self->data, self->size * sizeof(ScriptIfThen)) == 0);
    char what[64];
    std::snprintf(what, sizeof(what), "%d vs %d pairs", scratch.size, self->size);
    Report("ScriptIfThenLoad", ok, replay, record, what);
    gamecrt::operator_delete(scratch.data);
}

// 004BB590: PUSH ECX; PUSH ESI; MOV ESI,[ESP+0xC] (6 bytes). Both fields are always written: the
// original reads into a scratch pair, ours replays into the real one.
void(__fastcall* s_pair)(ScriptIfThen*, void*, ScriptStream*);
void __fastcall PairLoadVerify(ScriptIfThen* self, void*, ScriptStream* stream) {
    ScriptIfThen scratch{};
    RecordStream record(stream);
    s_pair(&scratch, nullptr, record.AsScript());
    ReplayStream replay(stream, record);
    self->Load(replay.AsScript());
    char what[96];
    std::snprintf(what, sizeof(what), "blocks %p %p vs %p %p", static_cast<void*>(scratch.conditions),
                  static_cast<void*>(scratch.commands), static_cast<void*>(self->conditions),
                  static_cast<void*>(self->commands));
    Report("ScriptIfThenPairLoad", scratch.conditions == self->conditions && scratch.commands == self->commands, replay,
           record, what);
}

// 004BFA50: MOV EDX,[ESP+4]; TEST EDX,0x80000000 (10 bytes). Pure lookup, compared with the
// original. SSCCmd::Load calls it (every command of a loaded save).
ScriptDecl*(__fastcall* s_findBlock)(const CScript*, void*, int);
ScriptDecl* __fastcall FindBlockVerify(const CScript* self, void*, int id) {
    ScriptDecl* expected = s_findBlock(self, nullptr, id);
    ScriptDecl* actual = self->FindBlock(id);
    ei::verify::Check("ScriptFindBlock", expected, actual, "id %d", id);
    return expected;
}

// ---- Effects (src/game/script_effects.cpp) -------------------------------------------------------------
// A loaded save with point lights or FX sources (script_fx): the original reads into a zeroed scratch
// element, ours replays into the real one; all fields compared (an FX source's name as text); the
// scratch's object reference dropped again.

// 004B18B0: PUSH EBX; PUSH ESI; MOV ESI,[ESP+0xC] (6 bytes).
void(__fastcall* s_pointLight)(ScriptPointLight*, void*, ScriptStream*);
void __fastcall PointLightLoadVerify(ScriptPointLight* self, void*, ScriptStream* stream) {
    ScriptPointLight scratch{};
    RecordStream record(stream);
    s_pointLight(&scratch, nullptr, record.AsScript());
    ReplayStream replay(stream, record);
    self->ScriptPointLightLoad(replay.AsScript());
    char what[64];
    std::snprintf(what, sizeof(what), "light %d vs %d", scratch.id, self->id);
    Report("ScriptPointLightLoad", std::memcmp(&scratch, self, sizeof(scratch)) == 0, replay, record, what);
    Release(scratch.object);
}

// 004B1B40: PUSH -1; PUSH 0x717E98 (7 bytes).
void(__fastcall* s_fxSource)(ScriptFxSource*, void*, ScriptStream*);
void __fastcall FxSourceLoadVerify(ScriptFxSource* self, void*, ScriptStream* stream) {
    ScriptFxSource scratch{};
    RecordStream record(stream);
    s_fxSource(&scratch, nullptr, record.AsScript());
    ReplayStream replay(stream, record);
    self->ScriptFxSourceLoad(replay.AsScript());
    bool same = scratch.object == self->object && scratch.id == self->id && SameString(scratch.name, self->name) &&
                std::memcmp(&scratch.position, &self->position, sizeof(ScriptFxSource) - offsetof(ScriptFxSource, position)) == 0;
    char what[96];
    std::snprintf(what, sizeof(what), "source %d vs %d, \"%.32s\"", scratch.id, self->id,
                  static_cast<const char*>(self->name));
    Report("ScriptFxSourceLoad", same, replay, record, what);
    Release(scratch.object);
}

// ---- Lists ------------------------------------------------------------------------------------------------

using ListLoadFn = void(__fastcall*)(ScriptList*, ScriptStream*);

// Record / replay for a list loader: `same` compares two nodes' elements, `destroy` frees one
// scratch element; the scratch's blocks are freed here.
template <typename Same, typename Destroy>
void ListVerify(const char* name, ListLoadFn original, ListLoadFn ours, ScriptList* self, ScriptStream* stream,
                Same same, Destroy destroy) {
    if (!Empty(*self)) {
        ours(self, stream);
        ei::verify::Check(name, true, WellFormed(*self), "ours only: list of %d not well formed", self->count);
        return;
    }
    ScriptList scratch = EmptyLike(*self);
    RecordStream record(stream);
    original(&scratch, record.AsScript());
    ReplayStream replay(stream, record);
    ours(self, replay.AsScript());
    bool ok = scratch.count == self->count && WellFormed(scratch) && WellFormed(*self);
    int index = 0;
    for (ScriptListNode *a = scratch.head, *b = self->head; ok && a && b; a = a->next, b = b->next, ++index)
        ok = same(a->data.bytes, b->data.bytes);
    char what[64];
    std::snprintf(what, sizeof(what), "%d vs %d nodes, node %d", scratch.count, self->count, index);
    Report(name, ok, replay, record, what);
    for (ScriptListNode* node = scratch.head; node; node = node->next) destroy(node->data.bytes);
    mfc::PlexFreeDataChain(scratch.blocks);
}

template <typename T>
T& At(char* base, std::size_t offset) { return *reinterpret_cast<T*>(base + offset); }

// 004CA480: PUSH -1; PUSH 0x7197D8 (7 bytes).
ListLoadFn s_particles;
void __fastcall ParticlesVerify(ScriptList* self, ScriptStream* stream) {
    ListVerify("ScriptParticlesLoad", s_particles, &ScriptParticlesLoad, self, stream,
               [](char* a, char* b) { return std::memcmp(a, b, 0x24) == 0; },
               [](char* d) { Release(At<CEngineObject*>(d, 0)); });
}

// 004CA5D0: PUSH -1; PUSH 0x719800 (7 bytes).
ListLoadFn s_fx;
void __fastcall FxSourcesVerify(ScriptList* self, ScriptStream* stream) {
    ListVerify("ScriptFxSourcesLoad", s_fx, &ScriptFxSourcesLoad, self, stream,
               [](char* a, char* b) {
                   return std::memcmp(a, b, 8) == 0 && SameString(At<mfc::CString>(a, 8), At<mfc::CString>(b, 8)) &&
                          std::memcmp(a + 0xC, b + 0xC, 0x28) == 0;
               },
               [](char* d) {
                   Release(At<CEngineObject*>(d, 0));
                   At<mfc::CString>(d, 8).~CString();
               });
}

// 004CA720: MOV EAX,FS:[0] (6 bytes).
ListLoadFn s_lightning;
void __fastcall LightningVerify(ScriptList* self, ScriptStream* stream) {
    ListVerify("ScriptLightningLoad", s_lightning, &ScriptLightningLoad, self, stream,
               [](char* a, char* b) { return std::memcmp(a, b, 8) == 0; },
               [](char* d) { Release(At<CEngineObject*>(d, 0)); });
}

// 004CA810: PUSH -1; PUSH 0x719840 (7 bytes).
ListLoadFn s_worldBlocks;
void __fastcall WorldBlocksVerify(ScriptList* self, ScriptStream* stream) {
    ListVerify("ScriptWorldBlocksLoad", s_worldBlocks, &ScriptWorldBlocksLoad, self, stream,
               [](char* a, char* b) {
                   auto* x = reinterpret_cast<ScriptDecl*>(a);
                   auto* y = reinterpret_cast<ScriptDecl*>(b);
                   return SameShape(x->commands, y->commands) && Saved(DeclSave, x) == Saved(DeclSave, y);
               },
               [](char* d) { CmdsSetSize(&reinterpret_cast<ScriptDecl*>(d)->commands, nullptr, 0, -1); });
}

// 004CA980: MOV EAX,FS:[0] (6 bytes).
ListLoadFn s_instances;
void __fastcall InstancesVerify(ScriptList* self, ScriptStream* stream) {
    ListVerify("ScriptInstancesLoad", s_instances, &ScriptInstancesLoad, self, stream,
               [](char* a, char* b) {
                   auto* x = reinterpret_cast<ScriptDefinition*>(a);
                   auto* y = reinterpret_cast<ScriptDefinition*>(b);
                   return SameShape(x->ifThen, y->ifThen) && Saved(DefinitionSave, x) == Saved(DefinitionSave, y);
               },
               [](char* d) { DefinitionDestruct(d); });
}

// 004CAC00: MOV EAX,FS:[0] (6 bytes). The run saver finds the instance index through 007AA620,
// which CScript::Load sets.
ListLoadFn s_runs;
void __fastcall RunsVerify(ScriptList* self, ScriptStream* stream) {
    ListVerify("ScriptRunsLoad", s_runs, &ScriptRunsLoad, self, stream,
               [](char* a, char* b) {
                   auto* x = reinterpret_cast<ScriptRun*>(a);
                   auto* y = reinterpret_cast<ScriptRun*>(b);
                   return std::memcmp(x, y, 0x14) == 0 && Saved(RunSave, x) == Saved(RunSave, y);
               },
               [](char* d) { VarsDestruct(d + 0x14); });
}

// ---- CScript::Load -----------------------------------------------------------------------------------------

// Ours only: the loading pointer is cleared again and every list is well formed.
void __fastcall LoadVerify(CScript* self, void*, ScriptStream* stream) {
    self->LoadState(stream);
    char* base = reinterpret_cast<char*>(self);
    bool ok = *reinterpret_cast<CScript**>(0x007AA620) == nullptr;
    const int lists[] = {0x14, 0x30, 0x4C, 0x70, 0x140, 0x15C, 0x178};
    int bad = 0;
    for (int offset : lists) {
        if (!WellFormed(*reinterpret_cast<ScriptList*>(base + offset))) {
            ok = false;
            bad = offset;
        }
    }
    ei::verify::Check("ScriptLoad", true, ok, "list +0x%X not well formed or 007AA620 left set", bad);
}

} // namespace

EI_VERIFY(0x004C0D00, LoadVerify, 0, s_unused);
EI_VERIFY(0x004C2D40, FloatsLoadVerify, 6, s_floats);
EI_VERIFY(0x004C3200, StringsLoadVerify, 7, s_strings);
EI_VERIFY(0x004C36B0, ObjectsLoadVerify, 5, s_objects);
EI_VERIFY(0x004C40D0, GroupsLoadVerify, 5, s_groups);
EI_VERIFY(0x004B5FE0, CmdLoadVerify, 8, s_cmd);
EI_VERIFY(0x004B6330, SubCallLoadVerify, 6, s_subCall);
EI_VERIFY(0x004B3D10, StringLoadVerify, 7, s_string);
EI_VERIFY(0x004B3AD0, VarsLoadVerify, 8, s_vars);
EI_VERIFY(0x004BB590, PairLoadVerify, 6, s_pair);
EI_VERIFY(0x004BFA50, FindBlockVerify, 10, s_findBlock);
EI_VERIFY(0x004C57C0, DeclCommandsVerify, 7, s_declCommands);
EI_VERIFY(0x004CA000, IfThenVerify, 5, s_ifThen);
EI_VERIFY(0x004CA480, ParticlesVerify, 7, s_particles);
EI_VERIFY(0x004CA5D0, FxSourcesVerify, 7, s_fx);
EI_VERIFY(0x004CA720, LightningVerify, 6, s_lightning);
EI_VERIFY(0x004CA810, WorldBlocksVerify, 7, s_worldBlocks);
EI_VERIFY(0x004CA980, InstancesVerify, 6, s_instances);
EI_VERIFY(0x004CAC00, RunsVerify, 6, s_runs);
EI_VERIFY(0x004B18B0, PointLightLoadVerify, 6, s_pointLight);
EI_VERIFY(0x004B1B40, FxSourceLoadVerify, 7, s_fxSource);
