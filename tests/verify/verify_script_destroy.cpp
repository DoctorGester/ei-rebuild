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

} // namespace

EI_VERIFY(0x004BE090, VarsDestructVerify, 7, s_varsDestruct);
EI_VERIFY(0x004BE9D0, DefinitionDestructVerify, 7, s_definitionDestruct);
EI_VERIFY(0x004BF640, CmdDestructVerify, 7, s_cmdDestruct);
EI_VERIFY(0x004B3900, AppendFromVerify, 0, s_unused);
