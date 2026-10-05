// Checks for the script destructors and ScriptVars::AppendFrom (src/game/script_destroy.cpp,
// script_vars_append.cpp; ticket S6).
//
// Destructors free, so the original cannot run on the real object. It runs on a deep copy made with
// the game's own copy functions instead, and ours on the real object; then:
// - the words each one writes: every dword of the copy that the original changed must hold the same
//   value in the real object after ours, every other dword of the real object must be unchanged
//   (vtables are written; data pointers, sizes and capacities are left);
// - the reference counts each one drops: for every CString buffer (CStringData::nRefs) and world
//   object (refCount_) the real object holds, ours must drop it by the number of times the object
//   holds it (nested commands included), and by as much as the original dropped it on the copy when
//   the copy took exactly that many references. Counters ours drops to zero (the buffer or object
//   may be freed) are not read afterwards.
// Frees themselves are not checkable. Nested destructor calls made while the original runs on a copy
// go straight to the original (the copy's nested commands are copies too); nested calls made by ours
// are verified in turn.
#include "verify.h"

#include "../../src/game/script.h"
#include "../../src/lib/crt.h"

#include <cstdint>
#include <cstring>
#include <vector>

#pragma warning(disable : 4740) // naked forwarders

namespace {
void* s_unused;

// The game's copy functions: ScriptVars copy constructor (004CB7E0), constructor (004C00D0) and
// ScriptDefinition::operator= (004CB460), CArray<SSCCmd>::Copy (004CC0C0).
__declspec(naked) void __fastcall VarsCopyConstructAt(void*, void*, const void*) EI_ORIGINAL(0x004CB7E0)
__declspec(naked) void __fastcall VarsConstructAt(void*) EI_ORIGINAL(0x004C00D0)
__declspec(naked) void __fastcall DefinitionAssign(void*, void*, const void*) EI_ORIGINAL(0x004CB460)
__declspec(naked) void __fastcall CommandsCopy(void*, void*, const void*) EI_ORIGINAL(0x004CC0C0)

// Set while an original destructor runs on a copy.
int g_inOriginal = 0;

// The reference counters an object holds, with how many times it holds each.
class Counters {
public:
    void String(const mfc::CString& s) {
        int* nRefs = reinterpret_cast<int*>(const_cast<char*>(static_cast<const char*>(s))) - 3;
        if (*nRefs < 0) return;   // the shared empty string or a locked buffer
        Add(nRefs);
    }
    void Strings(const MfcArray<mfc::CString>& a) {
        for (int i = 0; i < a.size; ++i) String(a.data[i]);
    }
    void Object(const CEngineObject* o) {
        if (o) Add(reinterpret_cast<int*>(reinterpret_cast<std::uintptr_t>(o) + 4));   // refCount_
    }
    void Vars(const ScriptVars& v) {
        Strings(v.floatNames);
        Strings(v.strings);
        Strings(v.stringNames);
        Strings(v.objectNames);
        Strings(v.groupNames);
        for (int i = 0; i < v.objects.size; ++i) Object(v.objects.data[i].object);
        for (int i = 0; i < v.groups.size; ++i) {
            const ScriptGroup& g = v.groups.data[i];
            for (int j = 0; j < g.size; ++j) Object(g.data[j].object);
        }
    }
    void Cmd(const SSCCmd& c) {
        Strings(c.stringArgs);
        for (int i = 0; i < c.subCalls.size; ++i)
            if (const SSCCmd* nested = c.subCalls.data[i].cmd) Cmd(*nested);
    }

    std::vector<int> Read() const {
        std::vector<int> out;
        for (const Entry& e : entries_) out.push_back(*e.counter);
        return out;
    }

    // t0: before the copy, t1: after it, t2: after the original on the copy. Reads the counters
    // after ours and checks them; returns the number of mismatching counters.
    int CheckAfterOurs(const std::vector<int>& t0, const std::vector<int>& t1, const std::vector<int>& t2,
                       int* checked) const {
        int bad = 0;
        for (std::size_t i = 0; i < entries_.size(); ++i) {
            int held = entries_[i].held;
            if (t2[i] - held <= 0) continue;   // ours frees it
            ++*checked;
            int t3 = *entries_[i].counter;
            bool ok = t3 == t2[i] - held;
            if (t1[i] - t0[i] == held) ok = ok && t2[i] - t1[i] == t3 - t2[i];
            if (!ok) ++bad;
        }
        return bad;
    }

    // Model check (no original to compare with): every counter read before (t0) that ours does not
    // free must have dropped by the number of times the object holds it.
    int CheckDrops(const std::vector<int>& t0, int* checked) const {
        int bad = 0;
        for (std::size_t i = 0; i < entries_.size(); ++i) {
            if (t0[i] - entries_[i].held <= 0) continue;
            ++*checked;
            if (*entries_[i].counter != t0[i] - entries_[i].held) ++bad;
        }
        return bad;
    }

private:
    struct Entry {
        int* counter;
        int held;
    };
    void Add(int* counter) {
        for (Entry& e : entries_)
            if (e.counter == counter) {
                ++e.held;
                return;
            }
        entries_.push_back({counter, 1});
    }
    std::vector<Entry> entries_;
};

std::vector<std::uint32_t> Words(const void* p, std::size_t size) {
    std::vector<std::uint32_t> out(size / 4);
    std::memcpy(out.data(), p, size);
    return out;
}

// The written-words rule above; returns the offset of the first difference, or -1.
int FirstWrongWord(const std::vector<std::uint32_t>& copyBefore, const void* copyAfter,
                   const std::vector<std::uint32_t>& realBefore, const void* realAfter) {
    std::vector<std::uint32_t> copy = Words(copyAfter, copyBefore.size() * 4);
    std::vector<std::uint32_t> real = Words(realAfter, realBefore.size() * 4);
    for (std::size_t i = 0; i < copy.size(); ++i) {
        bool ok = copy[i] != copyBefore[i] ? real[i] == copy[i] : real[i] == realBefore[i];
        if (!ok) return static_cast<int>(i * 4);
    }
    return -1;
}

// The common sequence: `copy` was just made from `self` (counters t0 taken before).
template <typename T, typename Original, typename Ours>
void Compare(const char* name, T* self, T* copy, const Counters& counters, const std::vector<int>& t0,
             Original original, Ours ours) {
    std::vector<int> t1 = counters.Read();
    std::vector<std::uint32_t> copyBefore = Words(copy, sizeof(T));
    std::vector<std::uint32_t> realBefore = Words(self, sizeof(T));
    ++g_inOriginal;
    original(copy);
    --g_inOriginal;
    std::vector<int> t2 = counters.Read();
    ours(self);
    int checked = 0;
    int badCounters = counters.CheckAfterOurs(t0, t1, t2, &checked);
    int wrongWord = FirstWrongWord(copyBefore, copy, realBefore, self);
    ei::verify::Check(name, true, badCounters == 0 && wrongWord < 0,
                      "%d of %d reference counts wrong; first wrong word at +0x%X", badCounters, checked,
                      static_cast<unsigned>(wrongWord));
}

// ---- ScriptVars ------------------------------------------------------------------------------------

// 004BE090: PUSH -1; PUSH 0x718836 (7 bytes). The copy: the copy constructor 004CB7E0.
void(__fastcall* s_varsDestruct)(ScriptVars*);
void __fastcall VarsDestructVerify(ScriptVars* self) {
    if (g_inOriginal) return s_varsDestruct(self);
    Counters counters;
    counters.Vars(*self);
    std::vector<int> t0 = counters.Read();
    alignas(ScriptVars) unsigned char raw[sizeof(ScriptVars)];
    auto* copy = reinterpret_cast<ScriptVars*>(raw);
    VarsCopyConstructAt(copy, nullptr, self);
    Compare("ScriptVarsDestruct", self, copy, counters, t0, s_varsDestruct, ScriptVarsDestruct);
}

// ---- ScriptDefinition ---------------------------------------------------------------------------------

// 004BE9D0: PUSH -1; PUSH 0x7189FF (7 bytes). The copy: built as CScript::Load builds its local
// definition (empty strings, the if-then vtable 0073D430, the ScriptVars constructor) and assigned
// from the real one with 004CB460 (as the S5 reset check does).
void(__fastcall* s_definitionDestruct)(ScriptDefinition*);
void __fastcall DefinitionDestructVerify(ScriptDefinition* self) {
    if (g_inOriginal) return s_definitionDestruct(self);
    Counters counters;
    counters.String(self->name);
    counters.String(self->params);
    counters.Vars(self->vars);
    std::vector<int> t0 = counters.Read();
    alignas(ScriptDefinition) unsigned char raw[sizeof(ScriptDefinition)];
    auto* copy = reinterpret_cast<ScriptDefinition*>(raw);
    new (&copy->name) mfc::CString;
    new (&copy->params) mfc::CString;
    copy->ifThen.vtable = reinterpret_cast<void*>(0x0073D430);
    copy->ifThen.data = nullptr;
    copy->ifThen.size = 0;
    copy->ifThen.maxSize = 0;
    copy->ifThen.growBy = 0;
    VarsConstructAt(&copy->vars);
    copy->index = 0;
    DefinitionAssign(copy, nullptr, self);
    Compare("ScriptDefinitionDestruct", self, copy, counters, t0, s_definitionDestruct, ScriptDefinitionDestruct);
}

// ---- SSCCmd -------------------------------------------------------------------------------------------

// 004BF640: PUSH -1; PUSH 0x718AA2 (7 bytes). The copy: CArray<SSCCmd>::Copy from a one-element view
// of the command (constructed elements, then SSCCmd::operator=, nested calls deep-copied); its
// storage is freed after.
void(__fastcall* s_cmdDestruct)(SSCCmd*);
void __fastcall CmdDestructVerify(SSCCmd* self) {
    if (g_inOriginal) return s_cmdDestruct(self);
    Counters counters;
    counters.Cmd(*self);
    std::vector<int> t0 = counters.Read();
    MfcArray<SSCCmd> source{reinterpret_cast<void*>(0x0073D3DC), self, 1, 1, 0};
    MfcArray<SSCCmd> copies{reinterpret_cast<void*>(0x0073D3DC), nullptr, 0, 0, 0};
    CommandsCopy(&copies, nullptr, &source);
    Compare("ScriptCmdDestruct", self, copies.data, counters, t0, s_cmdDestruct, ScriptCmdDestruct);
    gamecrt::operator_delete(copies.data);
}

// ---- Element destructors (ticket S7) ---------------------------------------------------------------
// Each takes (element, edx, flags) and returns the element. With bit 0 of the flags the element is
// deleted too (not checkable: the call is passed through).

// 004CE1E0: PUSH ESI; MOV ESI,ECX; MOV ECX,[ESI] (5 bytes). The copy: the slot with the object's
// count raised for it.
void*(__fastcall* s_refDestroy)(CEngineObject**, void*, int);
void* __fastcall RefDestroyVerify(CEngineObject** self, void*, int flags) {
    if (g_inOriginal || (flags & 1)) return ScriptRefDestroy(self, nullptr, flags);
    Counters counters;
    counters.Object(*self);
    std::vector<int> t0 = counters.Read();
    CEngineObject* copy = *self;
    if (copy) ++copy->refCount_;
    std::vector<int> t1 = counters.Read();
    std::vector<std::uint32_t> copyBefore = Words(&copy, 4);
    std::vector<std::uint32_t> realBefore = Words(self, 4);
    ++g_inOriginal;
    void* originalResult = s_refDestroy(&copy, nullptr, 0);
    --g_inOriginal;
    std::vector<int> t2 = counters.Read();
    void* result = ScriptRefDestroy(self, nullptr, 0);
    int checked = 0;
    int badCounters = counters.CheckAfterOurs(t0, t1, t2, &checked);
    int wrongWord = FirstWrongWord(copyBefore, &copy, realBefore, self);
    ei::verify::Check("ScriptRefDestroy", true,
                      badCounters == 0 && wrongWord < 0 && result == self &&
                          originalResult == static_cast<void*>(&copy),
                      "%d of %d reference counts wrong; first wrong word at +0x%X; returned %p for %p",
                      badCounters, checked, static_cast<unsigned>(wrongWord), result, static_cast<void*>(self));
    return result;
}

// 004CE210: ours only (PUSH ESI; MOV ESI,ECX; CALL: no 5 bytes without the relative call). The
// group's body (004B5EF0) writes the base vtable and releases every reference; the array stays.
void* __fastcall GroupDestroyVerify(void* self, void*, int flags) {
    if (flags & 1) return ScriptGroupDestroy(self, nullptr, flags);
    auto* group = static_cast<ScriptGroup*>(self);
    Counters counters;
    for (int i = 0; i < group->size; ++i) counters.Object(group->data[i].object);
    std::vector<int> t0 = counters.Read();
    std::vector<std::uint32_t> before = Words(self, sizeof(ScriptGroup));
    void* result = ScriptGroupDestroy(self, nullptr, 0);
    std::vector<std::uint32_t> after = Words(self, sizeof(ScriptGroup));
    bool words = after[0] == 0x0073D11C;
    for (std::size_t i = 1; i < after.size(); ++i) words = words && after[i] == before[i];
    int checked = 0;
    int badCounters = counters.CheckDrops(t0, &checked);
    ei::verify::Check("ScriptGroupDestroy", true, words && badCounters == 0 && result == self,
                      "words ok %d, %d of %d reference counts wrong, returned %p for %p", words ? 1 : 0,
                      badCounters, checked, result, self);
    return result;
}

// 004CE230: PUSH ESI; MOV ESI,ECX; PUSH EDI; MOV EDI,[ESI+8] (7 bytes). The copy: the element with
// the command deep-copied (CArray<SSCCmd>::Copy from a one-element view); the original deletes the
// copy's command (the array storage) as ours deletes the real one.
void*(__fastcall* s_subCallDestroy)(ScriptSubCall*, void*, int);
void* __fastcall SubCallDestroyVerify(ScriptSubCall* self, void*, int flags) {
    if (g_inOriginal || (flags & 1)) return ScriptSubCallElementDestroy(self, nullptr, flags);
    Counters counters;
    if (self->cmd) counters.Cmd(*self->cmd);
    std::vector<int> t0 = counters.Read();
    ScriptSubCall copy = *self;
    MfcArray<SSCCmd> source{reinterpret_cast<void*>(0x0073D3DC), self->cmd, self->cmd ? 1 : 0, 1, 0};
    MfcArray<SSCCmd> copies{reinterpret_cast<void*>(0x0073D3DC), nullptr, 0, 0, 0};
    if (self->cmd) {
        CommandsCopy(&copies, nullptr, &source);
        copy.cmd = copies.data;
    }
    std::vector<int> t1 = counters.Read();
    std::vector<std::uint32_t> copyBefore = Words(&copy, sizeof(copy));
    std::vector<std::uint32_t> realBefore = Words(self, sizeof(*self));
    ++g_inOriginal;
    void* originalResult = s_subCallDestroy(&copy, nullptr, 0);
    --g_inOriginal;
    std::vector<int> t2 = counters.Read();
    void* result = ScriptSubCallElementDestroy(self, nullptr, 0);
    int checked = 0;
    int badCounters = counters.CheckAfterOurs(t0, t1, t2, &checked);
    int wrongWord = FirstWrongWord(copyBefore, &copy, realBefore, self);
    ei::verify::Check("ScriptSubCallElementDestroy", true,
                      badCounters == 0 && wrongWord < 0 && result == self &&
                          originalResult == static_cast<void*>(&copy),
                      "%d of %d reference counts wrong; first wrong word at +0x%X; returned %p for %p",
                      badCounters, checked, static_cast<unsigned>(wrongWord), result, static_cast<void*>(self));
    return result;
}

// 004CE270: ours only. It removes the object from the world's timed objects (which flags and
// releases it when it is one) and releases it: the original cannot run first on a copy without
// changing the world. Checked: the slot cleared, the object's count down by one or two, the return.
void* __fastcall ParticleDestroyVerify(CEngineObject** self, void*, int flags) {
    if (flags & 1) return ScriptParticleDestroy(self, nullptr, flags);
    CEngineObject* object = *self;
    int before = object ? object->refCount_ : 0;
    void* result = ScriptParticleDestroy(self, nullptr, 0);
    bool ok = result == static_cast<void*>(self) && *self == nullptr;
    // Read the count only when it cannot have reached zero (the object may be freed).
    if (object && before > 2) ok = ok && (object->refCount_ == before - 1 || object->refCount_ == before - 2);
    ei::verify::Check("ScriptParticleDestroy", true, ok, "slot %p, returned %p for %p",
                      static_cast<void*>(*self), result, static_cast<void*>(self));
    return result;
}

// 004CE2B0: ours only (the same relative call after 3 bytes). The FX source's body (004BD540)
// clears the object slot (the world's timed removal and release are as 004CE270's).
void* __fastcall FxSourceDestroyVerify(void* self, void*, int flags) {
    if (flags & 1) return ScriptFxSourceDestroy(self, nullptr, flags);
    void* result = ScriptFxSourceDestroy(self, nullptr, 0);
    ei::verify::Check("ScriptFxSourceDestroy", true, result == self && *static_cast<void**>(self) == nullptr,
                      "slot %p, returned %p for %p", *static_cast<void**>(self), result, self);
    return result;
}

// 004CE2D0: ours only (as above). The block is a CArray<SSCCmd> (0x14 bytes) and one more dword;
// its body (004BF7E0) writes the base vtable 0073D3D0, destroys the commands and frees the array.
void* __fastcall WorldBlockDestroyVerify(void* self, void*, int flags) {
    if (flags & 1) return ScriptWorldBlockDestroy(self, nullptr, flags);
    auto* cmds = static_cast<MfcArray<SSCCmd>*>(self);
    Counters counters;
    for (int i = 0; i < cmds->size; ++i) counters.Cmd(cmds->data[i]);
    std::vector<int> t0 = counters.Read();
    std::vector<std::uint32_t> before = Words(self, 0x18);
    void* result = ScriptWorldBlockDestroy(self, nullptr, 0);
    std::vector<std::uint32_t> after = Words(self, 0x18);
    bool words = after[0] == 0x0073D3D0;
    for (std::size_t i = 1; i < after.size(); ++i) words = words && after[i] == before[i];
    int checked = 0;
    int badCounters = counters.CheckDrops(t0, &checked);
    ei::verify::Check("ScriptWorldBlockDestroy", true, words && badCounters == 0 && result == self,
                      "words ok %d, %d of %d reference counts wrong, returned %p for %p", words ? 1 : 0,
                      badCounters, checked, result, self);
    return result;
}

// 004BF620: ours only (PUSH ESI; MOV ESI,ECX; CALL). The body is the command destructor 004BF640,
// whose own wrapper compares it with the original; here the return value and the reference counts.
void* __fastcall CmdElementDestroyVerify(SSCCmd* self, void*, int flags) {
    if (flags & 1) return ScriptCmdElementDestroy(self, nullptr, flags);
    Counters counters;
    counters.Cmd(*self);
    std::vector<int> t0 = counters.Read();
    void* result = ScriptCmdElementDestroy(self, nullptr, 0);
    int checked = 0;
    int badCounters = counters.CheckDrops(t0, &checked);
    ei::verify::Check("ScriptCmdElementDestroy", true, badCounters == 0 && result == static_cast<void*>(self),
                      "%d of %d reference counts wrong, returned %p for %p", badCounters, checked, result,
                      static_cast<void*>(self));
    return result;
}

// ---- AppendFrom ---------------------------------------------------------------------------------------

// 004B3900: ours only (the four appends check the contents): every array's size as the appends
// set it (the other set's counts captured first; it may be this set).
void __fastcall AppendFromVerify(ScriptVars* self, void*, const ScriptVars* other) {
    const MfcArray<int>* mine[8];
    const MfcArray<int>* theirs[8];
    for (int i = 0; i < 8; ++i) {
        mine[i] = reinterpret_cast<const MfcArray<int>*>(reinterpret_cast<const char*>(self) + i * 0x14);
        theirs[i] = reinterpret_cast<const MfcArray<int>*>(reinterpret_cast<const char*>(other) + i * 0x14);
    }
    // Values grow by the other's value count; names become the old value count plus the other's
    // name count.
    int expected[8];
    for (int i = 0; i < 8; i += 2) {
        expected[i] = mine[i]->size + theirs[i]->size;
        expected[i + 1] = mine[i]->size + theirs[i + 1]->size;
    }
    ScriptVarsAppendFrom(self, nullptr, other);
    int wrong = -1;
    for (int i = 0; i < 8 && wrong < 0; ++i)
        if (mine[i]->size != expected[i]) wrong = i;
    ei::verify::Check("ScriptVarsAppendFrom", true, wrong < 0, "array %d: size %d, expected %d", wrong,
                      wrong < 0 ? 0 : mine[wrong]->size, wrong < 0 ? 0 : expected[wrong]);
}

// ---- Variable array constructors and destructors (src/game/script_arrays.cpp) -------------------------

// Constructors: against the original. The original constructs a 0xCC-filled scratch, ours the real
// set; all 0x28 bytes (every one is written) and the returned pointers compared. 004BC690 / 004BC740 /
// 004BC870 / 004BC950: MOV EAX,ECX; XOR ECX,ECX; MOV [EAX+4],ECX (7 bytes). Reached in the Rebirth
// launch, from the original LoadCommands (004D1330, patched there): script_parse, script_exec.
template <typename T>
using NamedConstructFn = ScriptNamedValues<T>*(__fastcall*)(ScriptNamedValues<T>*);
template <typename T>
ScriptNamedValues<T>* ConstructVerify(const char* name, NamedConstructFn<T> original, NamedConstructFn<T> ours,
                                      ScriptNamedValues<T>* self) {
    alignas(4) unsigned char scratch[sizeof(ScriptNamedValues<T>)];
    std::memset(scratch, 0xCC, sizeof(scratch));
    auto* expected = reinterpret_cast<ScriptNamedValues<T>*>(scratch);
    ScriptNamedValues<T>* originalResult = original(expected);
    ScriptNamedValues<T>* result = ours(self);
    ei::verify::Check(name, true,
                      std::memcmp(scratch, self, sizeof(scratch)) == 0 && originalResult == expected && result == self,
                      "constructed %p", static_cast<void*>(self));
    return result;
}
NamedConstructFn<float> s_floatsConstruct;
ScriptNamedValues<float>* __fastcall FloatsConstructVerify(ScriptNamedValues<float>* self) {
    return ConstructVerify("ScriptFloatsConstruct", s_floatsConstruct, &ScriptFloatsConstruct, self);
}
NamedConstructFn<mfc::CString> s_stringsConstruct;
ScriptNamedValues<mfc::CString>* __fastcall StringsConstructVerify(ScriptNamedValues<mfc::CString>* self) {
    return ConstructVerify("ScriptStringsConstruct", s_stringsConstruct, &ScriptStringsConstruct, self);
}
NamedConstructFn<ScriptObjectRef> s_objectsConstruct;
ScriptNamedValues<ScriptObjectRef>* __fastcall ObjectsConstructVerify(ScriptNamedValues<ScriptObjectRef>* self) {
    return ConstructVerify("ScriptObjectsConstruct", s_objectsConstruct, &ScriptObjectsConstruct, self);
}
NamedConstructFn<ScriptGroup> s_groupsConstruct;
ScriptNamedValues<ScriptGroup>* __fastcall GroupsConstructVerify(ScriptNamedValues<ScriptGroup>* self) {
    return ConstructVerify("ScriptGroupsConstruct", s_groupsConstruct, &ScriptGroupsConstruct, self);
}

// The game's copy constructors for the destructor checks: float / string / object values with
// their names (004CBB80 / 004CBCA0 / 004CBDB0) and a CString array (004CBA00).
__declspec(naked) void __fastcall FloatsCopyConstructAt(void*, void*, const void*) EI_ORIGINAL(0x004CBB80)
__declspec(naked) void __fastcall StringsCopyConstructAt(void*, void*, const void*) EI_ORIGINAL(0x004CBCA0)
__declspec(naked) void __fastcall ObjectsCopyConstructAt(void*, void*, const void*) EI_ORIGINAL(0x004CBDB0)
__declspec(naked) void __fastcall NamesCopyConstructAt(void*, void*, const void*) EI_ORIGINAL(0x004CBA00)
__declspec(naked) void __fastcall GroupsCopyConstructAt(void*, void*, const void*) EI_ORIGINAL(0x004CBEF0)

// Destructors with a game copy constructor: against the original on a deep copy (the rules at the top
// of this file). The 7 stolen bytes are PUSH -1; PUSH <handler>.
template <typename T, typename Count>
void DestructOnCopy(const char* name, void(__fastcall* original)(T*), void(__fastcall* ours)(T*),
                    void(__fastcall* copyConstruct)(void*, void*, const void*), T* self, Count count) {
    if (g_inOriginal) return original(self);
    Counters counters;
    count(counters, *self);
    std::vector<int> t0 = counters.Read();
    alignas(T) unsigned char raw[sizeof(T)];
    auto* copy = reinterpret_cast<T*>(raw);
    copyConstruct(copy, nullptr, self);
    Compare(name, self, copy, counters, t0, original, ours);
}

// 004BC6C0 / 004BC770 / 004BC8A0: reached from the instance and run lists' RemoveHead (004C7A10 /
// 004C82B0: the reset at every new game and zone load), the definition list's destructor and, in
// the Rebirth launch, the original LoadCommands.
void(__fastcall* s_floatsDestruct)(ScriptNamedValues<float>*);
void __fastcall FloatsDestructVerify(ScriptNamedValues<float>* self) {
    DestructOnCopy("ScriptFloatsDestruct", s_floatsDestruct, &ScriptFloatsDestruct, FloatsCopyConstructAt, self,
                   [](Counters& c, const ScriptNamedValues<float>& v) { c.Strings(v.names); });
}
void(__fastcall* s_stringsDestruct)(ScriptNamedValues<mfc::CString>*);
void __fastcall StringsDestructVerify(ScriptNamedValues<mfc::CString>* self) {
    DestructOnCopy("ScriptStringsDestruct", s_stringsDestruct, &ScriptStringsDestruct, StringsCopyConstructAt, self,
                   [](Counters& c, const ScriptNamedValues<mfc::CString>& v) {
                       c.Strings(v.names);
                       c.Strings(v.values);
                   });
}
void(__fastcall* s_objectsDestruct)(ScriptNamedValues<ScriptObjectRef>*);
void __fastcall ObjectsDestructVerify(ScriptNamedValues<ScriptObjectRef>* self) {
    DestructOnCopy("ScriptObjectsDestruct", s_objectsDestruct, &ScriptObjectsDestruct, ObjectsCopyConstructAt, self,
                   [](Counters& c, const ScriptNamedValues<ScriptObjectRef>& v) {
                       c.Strings(v.names);
                       for (int i = 0; i < v.values.size; ++i) c.Object(v.values.data[i].object);
                   });
}
// 004C81B0: reached in the Rebirth launch from the original LoadCommands (004D1330: script_parse,
// script_exec; ours and the instance loader destroy the whole set instead). PUSH -1; PUSH 0x719480 (7).
void(__fastcall* s_groupsDestruct)(ScriptNamedValues<ScriptGroup>*);
void __fastcall GroupsDestructVerify(ScriptNamedValues<ScriptGroup>* self) {
    DestructOnCopy("ScriptGroupsDestruct", s_groupsDestruct, &ScriptGroupsDestruct, GroupsCopyConstructAt, self,
                   [](Counters& c, const ScriptNamedValues<ScriptGroup>& v) {
                       c.Strings(v.names);
                       for (int i = 0; i < v.values.size; ++i)
                           for (int k = 0; k < v.values.data[i].size; ++k) c.Object(v.values.data[i].data[k].object);
                   });
}
// 004BC810: PUSH -1; PUSH 0x7184E8 (7 bytes). The CString array's scalar deleting destructor
// (004C1860).
void(__fastcall* s_namesDestruct)(MfcArray<mfc::CString>*);
void __fastcall NamesDestructVerify(MfcArray<mfc::CString>* self) {
    DestructOnCopy("ScriptNamesDestruct", s_namesDestruct, &ScriptNamesDestruct, NamesCopyConstructAt, self,
                   [](Counters& c, const MfcArray<mfc::CString>& v) { c.Strings(v); });
}

// Destructors without a copy constructor: ours only. Only the vtables listed change (to the base
// ones); every reference the array holds drops by the number of times it is held.
template <typename T>
void DestructModel(const char* name, void(__fastcall* ours)(T*), T* self, const Counters& counters,
                   std::size_t vtableWord, void* baseVtable) {
    std::vector<int> t0 = counters.Read();
    std::vector<std::uint32_t> before = Words(self, sizeof(T));
    ours(self);
    std::vector<std::uint32_t> after = Words(self, sizeof(T));
    bool words = after[vtableWord] == reinterpret_cast<std::uintptr_t>(baseVtable);
    for (std::size_t i = 0; i < after.size(); ++i)
        if (i != vtableWord) words = words && after[i] == before[i];
    int checked = 0;
    int badCounters = counters.CheckDrops(t0, &checked);
    ei::verify::Check(name, true, words && badCounters == 0, "words ok %d, %d of %d reference counts wrong",
                      words ? 1 : 0, badCounters, checked);
}

// 004BC9A0: the reference array's scalar deleting destructor (004C1840).
void __fastcall RefArrayDestructVerify(MfcArray<ScriptObjectRef>* self) {
    Counters counters;
    for (int i = 0; i < self->size; ++i) counters.Object(self->data[i].object);
    DestructModel("ScriptRefArrayDestruct", &ScriptRefArrayDestruct, self, counters, 0, scriptvtable::kRefsBase);
}
// 004BCA20: the group array's scalar deleting destructor (004C18A0). The groups' own vtables are in
// the freed storage and not checked.
void __fastcall GroupArrayDestructVerify(MfcArray<ScriptGroup>* self) {
    Counters counters;
    for (int i = 0; i < self->size; ++i)
        for (int k = 0; k < self->data[i].size; ++k) counters.Object(self->data[i].data[k].object);
    DestructModel("ScriptGroupArrayDestruct", &ScriptGroupArrayDestruct, self, counters, 0,
                  scriptvtable::kGroupsBase);
}

// 004BCAE0: PUSH -1; PUSH 0x7185DE (7 bytes). Every finished run (the run list's FreeNode 004CDAB0)
// and the run list's DestructElements: script_exec, script_parse, console. Against the original on a
// copy: the run's first five words and its variables deep-copied (004CB7E0).
void(__fastcall* s_runDestruct)(ScriptRun*);
void __fastcall RunDestructVerify(ScriptRun* self) {
    if (g_inOriginal) return s_runDestruct(self);
    Counters counters;
    counters.Vars(self->vars);
    std::vector<int> t0 = counters.Read();
    alignas(ScriptRun) unsigned char raw[sizeof(ScriptRun)];
    auto* copy = reinterpret_cast<ScriptRun*>(raw);
    std::memcpy(copy, self, offsetof(ScriptRun, vars));
    VarsCopyConstructAt(&copy->vars, nullptr, &self->vars);
    Compare("ScriptRunDestruct", self, copy, counters, t0, s_runDestruct, ScriptRunDestruct);
}

} // namespace

EI_VERIFY(0x004BE090, VarsDestructVerify, 7, s_varsDestruct);
EI_VERIFY(0x004BE9D0, DefinitionDestructVerify, 7, s_definitionDestruct);
EI_VERIFY(0x004BF640, CmdDestructVerify, 7, s_cmdDestruct);
EI_VERIFY(0x004B3900, AppendFromVerify, 0, s_unused);
EI_VERIFY(0x004CE1E0, RefDestroyVerify, 5, s_refDestroy);
EI_VERIFY(0x004CE210, GroupDestroyVerify, 0, s_unused);
EI_VERIFY(0x004CE230, SubCallDestroyVerify, 7, s_subCallDestroy);
EI_VERIFY(0x004CE270, ParticleDestroyVerify, 0, s_unused);
EI_VERIFY(0x004CE2B0, FxSourceDestroyVerify, 0, s_unused);
EI_VERIFY(0x004CE2D0, WorldBlockDestroyVerify, 0, s_unused);
EI_VERIFY(0x004BF620, CmdElementDestroyVerify, 0, s_unused);
EI_VERIFY(0x004BC690, FloatsConstructVerify, 7, s_floatsConstruct);
EI_VERIFY(0x004BC740, StringsConstructVerify, 7, s_stringsConstruct);
EI_VERIFY(0x004BC870, ObjectsConstructVerify, 7, s_objectsConstruct);
EI_VERIFY(0x004BC950, GroupsConstructVerify, 7, s_groupsConstruct);
EI_VERIFY(0x004BC6C0, FloatsDestructVerify, 7, s_floatsDestruct);
EI_VERIFY(0x004BC770, StringsDestructVerify, 7, s_stringsDestruct);
EI_VERIFY(0x004BC8A0, ObjectsDestructVerify, 7, s_objectsDestruct);
EI_VERIFY(0x004C81B0, GroupsDestructVerify, 7, s_groupsDestruct);
EI_VERIFY(0x004BC810, NamesDestructVerify, 7, s_namesDestruct);
EI_VERIFY(0x004BC9A0, RefArrayDestructVerify, 0, s_unused);
EI_VERIFY(0x004BCA20, GroupArrayDestructVerify, 0, s_unused);
EI_VERIFY(0x004BCAE0, RunDestructVerify, 7, s_runDestruct);
