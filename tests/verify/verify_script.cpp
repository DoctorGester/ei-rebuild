// Differential checks for the script engine's object references, groups, variables and SSCCmd
// helpers (src/game/script*.cpp).
#include "verify.h"

#include "../../src/game/random.h"
#include "../../src/game/script.h"
#include "../../src/lib/crt.h"

#include <cstring>
#include <vector>

namespace {
void* s_unused;

// ---- Helpers ------------------------------------------------------------------------------------

// A copy of a group holding its own references, so original code can run on it while ours runs
// on the real group. Destroy releases the references and frees the array.
ScriptGroup Clone(const ScriptGroup& g) {
    ScriptGroup c{};
    c.vtable = g.vtable;
    c.growBy = g.growBy;
    if (g.data) {
        c.data = static_cast<ScriptObjectRef*>(gamecrt::operator_new(g.maxSize * 4));
        for (int i = 0; i < g.maxSize; ++i) c.data[i].object = i < g.size ? g.data[i].object : nullptr;
        for (int i = 0; i < g.size; ++i)
            if (c.data[i].object) ++c.data[i].object->refCount_;
    }
    c.size = g.size;
    c.maxSize = g.maxSize;
    return c;
}

void Destroy(ScriptGroup& g) {
    if (g.data) {
        for (int i = 0; i < g.size; ++i)
            if (g.data[i].object) g.data[i].object->Release();
        gamecrt::operator_delete(g.data);
    }
    g.data = nullptr;
    g.size = g.maxSize = 0;
}

bool SameGroup(const ScriptGroup& a, const ScriptGroup& b) {
    if (a.size != b.size || a.maxSize != b.maxSize || a.growBy != b.growBy) return false;
    for (int i = 0; i < a.size; ++i)
        if (a.data[i].object != b.data[i].object) return false;
    return true;
}

bool Sorted(const ScriptGroup& g) {
    for (int i = 1; i < g.size; ++i)
        if (reinterpret_cast<std::uintptr_t>(g.data[i - 1].object) >= reinterpret_cast<std::uintptr_t>(g.data[i].object))
            return false;
    return true;
}

// Captures what a save function writes.
class CaptureStream : public ScriptStream {
public:
    void Slot0() override {}
    void Read(void*, unsigned) override {}
    void Write(const void* buffer, unsigned size) override {
        auto* p = static_cast<const unsigned char*>(buffer);
        bytes.insert(bytes.end(), p, p + size);
        chunks.push_back(size);
    }
    // Passes the captured writes on to the real stream, in the same pieces.
    void Forward(ScriptStream* stream) const {
        std::size_t pos = 0;
        for (unsigned size : chunks) {
            stream->Write(bytes.data() + pos, size);
            pos += size;
        }
    }
    std::vector<unsigned char> bytes;
    std::vector<unsigned> chunks;
};

// Reads from the real stream and records the bytes, then plays them back for the second run.
class RecordStream : public ScriptStream {
public:
    explicit RecordStream(ScriptStream* real) : real_(real) {}
    void Slot0() override {}
    void Read(void* buffer, unsigned size) override {
        real_->Read(buffer, size);
        auto* p = static_cast<const unsigned char*>(buffer);
        bytes.insert(bytes.end(), p, p + size);
    }
    void Write(const void*, unsigned) override {}
    std::vector<unsigned char> bytes;

private:
    ScriptStream* real_;
};

class ReplayStream : public ScriptStream {
public:
    explicit ReplayStream(const std::vector<unsigned char>& bytes) : bytes_(bytes) {}
    void Slot0() override {}
    void Read(void* buffer, unsigned size) override {
        for (unsigned i = 0; i < size; ++i) static_cast<unsigned char*>(buffer)[i] = pos_ < bytes_.size() ? bytes_[pos_++] : 0;
    }
    void Write(const void*, unsigned) override {}
    bool Consumed() const { return pos_ == bytes_.size(); }

private:
    const std::vector<unsigned char>& bytes_;
    std::size_t pos_ = 0;
};

} // namespace

// ---- ScriptObjectRef ----------------------------------------------------------------------------

// Reset, Assign, Construct and ReleaseIfDeleted change reference counts: run ours only and check
// the slot afterwards.
static void __fastcall ResetVerify(ScriptObjectRef* self) {
    self->Reset();
    ei::verify::Check("ScriptObjectRef::Reset", true, self->object == nullptr, "%p", self->object);
}
EI_VERIFY(0x004B24B0, ResetVerify, 0, s_unused);

static void __fastcall AssignVerify(ScriptObjectRef* self, void*, CEngineObject* object) {
    int refs = object ? object->refCount_ : 0;
    bool same = self->object == object;
    self->Assign(object);
    // Assigning the object already held releases it first; the count is then back where it was.
    bool ok = self->object == object && (!object || object->refCount_ == refs + (same ? 0 : 1));
    ei::verify::Check("ScriptObjectRef::Assign", true, ok, "%p refs %d -> %d", object, refs, object ? object->refCount_ : 0);
}
EI_VERIFY(0x004B24D0, AssignVerify, 0, s_unused);

static CEngineObject* __fastcall ConstructVerify(ScriptObjectRef* self, void*, const ScriptObjectRef* other) {
    CEngineObject* object = other->object;
    int refs = object ? object->refCount_ : 0;
    CEngineObject* result = self->Construct(*other);
    bool ok = result == object && self->object == object && (!object || object->refCount_ == refs + 1);
    ei::verify::Check("ScriptObjectRef::Construct", true, ok, "%p", object);
    return result;
}
EI_VERIFY(0x004B2500, ConstructVerify, 0, s_unused);

// 004B2530 / 004B2550: PUSH ESI; MOV ESI,[ECX]; TEST ESI,ESI (5 bytes). Pure.
static CEngineObject*(__fastcall* s_asUnit)(const ScriptObjectRef*);
static CEngineObject* __fastcall AsUnitVerify(const ScriptObjectRef* self) {
    CEngineObject* expected = s_asUnit(self);
    ei::verify::Check("ScriptObjectRef::AsUnit", expected, self->AsUnit(), "%p", self->object);
    return expected;
}
EI_VERIFY(0x004B2530, AsUnitVerify, 5, s_asUnit);

static void*(__fastcall* s_perception)(const ScriptObjectRef*);
static void* __fastcall PerceptionVerify(const ScriptObjectRef* self) {
    void* expected = s_perception(self);
    ei::verify::Check("ScriptObjectRef::GetUnitPerception", expected, self->GetUnitPerception(), "%p", self->object);
    return expected;
}
EI_VERIFY(0x004B2550, PerceptionVerify, 5, s_perception);

static void __fastcall ReleaseIfDeletedVerify(ScriptObjectRef* self) {
    CEngineObject* before = self->object;
    bool deleted = before && (before->flags_ & 1);
    self->ReleaseIfDeleted();
    bool ok = self->object == (deleted ? nullptr : before);
    ei::verify::Check("ScriptObjectRef::ReleaseIfDeleted", true, ok, "%p", before);
}
EI_VERIFY(0x004B2570, ReleaseIfDeletedVerify, 0, s_unused);

// Load: the original reads from the real stream into a scratch reference; ours replays the bytes.
// 004B2590: PUSH ESI; MOV ESI,ECX; MOV ECX,[ESP+8] (7 bytes).
static void(__fastcall* s_refLoad)(ScriptObjectRef*, void*, ScriptStream*);
static void __fastcall RefLoadVerify(ScriptObjectRef* self, void*, ScriptStream* stream) {
    ScriptObjectRef scratch{self->object};
    if (scratch.object) ++scratch.object->refCount_;
    RecordStream record(stream);
    s_refLoad(&scratch, nullptr, &record);
    ReplayStream replay(record.bytes);
    self->Load(&replay);
    bool ok = scratch.object == self->object && replay.Consumed();
    ei::verify::Check("ScriptObjectRef::Load", true, ok, "%p vs %p", scratch.object, self->object);
    scratch.Reset();
}
EI_VERIFY(0x004B2590, RefLoadVerify, 7, s_refLoad);

// Save: both write into capture streams; the original's bytes then go to the real stream.
// 004B25F0: PUSH ECX; MOV EAX,[ECX]; MOV [ESP+0],-1 (11 bytes; the last one has a disp8).
static void(__fastcall* s_refSave)(const ScriptObjectRef*, void*, ScriptStream*);
static void __fastcall RefSaveVerify(const ScriptObjectRef* self, void*, ScriptStream* stream) {
    CaptureStream expected, actual;
    s_refSave(self, nullptr, &expected);
    self->Save(&actual);
    ei::verify::Check("ScriptObjectRef::Save", true, expected.bytes == actual.bytes && expected.chunks == actual.chunks, "%p", self->object);
    expected.Forward(stream);
}
EI_VERIFY(0x004B25F0, RefSaveVerify, 11, s_refSave);

// ---- ScriptGroup --------------------------------------------------------------------------------

// The set operations only write `this`: run the original into a clone and ours into the real
// group, then compare elements, size and capacity.
using SetOp = void(__fastcall*)(ScriptGroup*, void*, const ScriptGroup*, const ScriptGroup*);
static void SetOpVerify(const char* name, SetOp original, void (ScriptGroup::*ours)(const ScriptGroup&, const ScriptGroup&),
                        ScriptGroup* self, const ScriptGroup* a, const ScriptGroup* b) {
    ScriptGroup scratch = Clone(*self);
    original(&scratch, nullptr, a, b);
    (self->*ours)(*a, *b);
    ei::verify::Check(name, true, SameGroup(scratch, *self), "a %d b %d -> %d vs %d", a->size, b->size, scratch.size, self->size);
    Destroy(scratch);
}

// 004B2620 / 004B2920 / 004B2B60: SUB ESP,n; MOV EAX,[ESP+n+4] (7 bytes).
static SetOp s_union, s_difference, s_intersection;
static void __fastcall UnionVerify(ScriptGroup* self, void*, const ScriptGroup* a, const ScriptGroup* b) {
    SetOpVerify("ScriptGroup::Union", s_union, &ScriptGroup::Union, self, a, b);
}
EI_VERIFY(0x004B2620, UnionVerify, 7, s_union);
static void __fastcall DifferenceVerify(ScriptGroup* self, void*, const ScriptGroup* a, const ScriptGroup* b) {
    SetOpVerify("ScriptGroup::Difference", s_difference, &ScriptGroup::Difference, self, a, b);
}
EI_VERIFY(0x004B2920, DifferenceVerify, 7, s_difference);
static void __fastcall IntersectionVerify(ScriptGroup* self, void*, const ScriptGroup* a, const ScriptGroup* b) {
    SetOpVerify("ScriptGroup::Intersection", s_intersection, &ScriptGroup::Intersection, self, a, b);
}
EI_VERIFY(0x004B2B60, IntersectionVerify, 7, s_intersection);

// 004B2D60: MOV EAX,[ECX+8]; MOV ECX,[ECX+4] (6 bytes).
static void(__fastcall* s_sort)(ScriptGroup*);
static void __fastcall SortVerify(ScriptGroup* self) {
    ScriptGroup scratch = Clone(*self);
    s_sort(&scratch);
    self->Sort();
    ei::verify::Check("ScriptGroup::Sort", true, SameGroup(scratch, *self), "size %d", self->size);
    Destroy(scratch);
}
EI_VERIFY(0x004B2D60, SortVerify, 6, s_sort);

// 004B2DA0: MOV EAX,[ESP+4]; MOV EDX,[ECX+8] (7 bytes). Pure.
static int(__fastcall* s_indexOf)(const ScriptGroup*, void*, const ScriptObjectRef*);
static int __fastcall IndexOfVerify(const ScriptGroup* self, void*, const ScriptObjectRef* ref) {
    int expected = s_indexOf(self, nullptr, ref);
    ei::verify::Check("ScriptGroup::IndexOf", expected, self->IndexOf(*ref), "size %d", self->size);
    return expected;
}
EI_VERIFY(0x004B2DA0, IndexOfVerify, 7, s_indexOf);

// Remove / Insert / RemoveDeleted / InsertAll modify the group in place: clone, run the original
// on the clone and ours on the real group, compare.
// 004B2E60: PUSH ECX; MOV EAX,[ECX+8]; PUSH EBX (5 bytes).
static void(__fastcall* s_remove)(ScriptGroup*, void*, CEngineObject*);
static void __fastcall RemoveVerify(ScriptGroup* self, void*, CEngineObject* object) {
    ScriptGroup scratch = Clone(*self);
    s_remove(&scratch, nullptr, object);
    self->Remove(object);
    ei::verify::Check("ScriptGroup::Remove", true, SameGroup(scratch, *self), "%p size %d", object, self->size);
    Destroy(scratch);
}
EI_VERIFY(0x004B2E60, RemoveVerify, 5, s_remove);

// 004B2EF0: SUB ESP,0x10; MOV EAX,[ESP+0x14] (7 bytes).
static void(__fastcall* s_insert)(ScriptGroup*, void*, CEngineObject*);
static void __fastcall InsertVerify(ScriptGroup* self, void*, CEngineObject* object) {
    ScriptGroup scratch = Clone(*self);
    s_insert(&scratch, nullptr, object);
    self->Insert(object);
    ei::verify::Check("ScriptGroup::Insert", true, SameGroup(scratch, *self) && Sorted(*self), "%p size %d", object, self->size);
    Destroy(scratch);
}
EI_VERIFY(0x004B2EF0, InsertVerify, 7, s_insert);

// 004B3110: PUSH ECX; PUSH EBX; MOV EBX,ECX; XOR EAX,EAX (6 bytes).
static void(__fastcall* s_removeDeleted)(ScriptGroup*);
static void __fastcall RemoveDeletedVerify(ScriptGroup* self) {
    ScriptGroup scratch = Clone(*self);
    s_removeDeleted(&scratch);
    self->RemoveDeleted();
    ei::verify::Check("ScriptGroup::RemoveDeleted", true, SameGroup(scratch, *self), "size %d vs %d", scratch.size, self->size);
    Destroy(scratch);
}
EI_VERIFY(0x004B3110, RemoveDeletedVerify, 6, s_removeDeleted);

// 004B31D0: MOV EAX,[ESP+4]; PUSH ESI (5 bytes).
static void(__fastcall* s_insertAll)(ScriptGroup*, void*, const mfc::CTypedPtrList<CEngineObject>*);
static void __fastcall InsertAllVerify(ScriptGroup* self, void*, const mfc::CTypedPtrList<CEngineObject>* list) {
    ScriptGroup scratch = Clone(*self);
    s_insertAll(&scratch, nullptr, list);
    self->InsertAll(list);
    ei::verify::Check("ScriptGroup::InsertAll", true, SameGroup(scratch, *self), "list %d -> %d vs %d", list->count,
                      scratch.size, self->size);
    Destroy(scratch);
}
EI_VERIFY(0x004B31D0, InsertAllVerify, 5, s_insertAll);

// 004B3330: MOV EDX,[ECX+8]; XOR EAX,EAX (5 bytes). Pure.
static BOOL(__fastcall* s_contains)(const ScriptGroup*, void*, CEngineObject*);
static BOOL __fastcall ContainsVerify(const ScriptGroup* self, void*, CEngineObject* object) {
    BOOL expected = s_contains(self, nullptr, object);
    ei::verify::Check("ScriptGroup::Contains", expected, self->Contains(object), "%p size %d", object, self->size);
    return expected;
}
EI_VERIFY(0x004B3330, ContainsVerify, 5, s_contains);

// 004B3360: SUB ESP,0xC; PUSH EBX; MOV EBX,ECX (6 bytes). The original reads the real stream into
// a clone; ours replays the recorded bytes.
static void(__fastcall* s_groupLoad)(ScriptGroup*, void*, ScriptStream*);
static void __fastcall GroupLoadVerify(ScriptGroup* self, void*, ScriptStream* stream) {
    ScriptGroup scratch = Clone(*self);
    RecordStream record(stream);
    s_groupLoad(&scratch, nullptr, &record);
    ReplayStream replay(record.bytes);
    self->Load(&replay);
    ei::verify::Check("ScriptGroup::Load", true, SameGroup(scratch, *self) && replay.Consumed(), "size %d vs %d",
                      scratch.size, self->size);
    Destroy(scratch);
}
EI_VERIFY(0x004B3360, GroupLoadVerify, 6, s_groupLoad);

// ---- ScriptVars ---------------------------------------------------------------------------------

// ResetValues and RemoveDeletedObjects release references: run ours and check the result.
static void __fastcall ResetValuesVerify(ScriptVars* self) {
    self->ResetValues();
    bool ok = true;
    for (int i = 0; i < self->floats.size; ++i) ok &= reinterpret_cast<std::uint32_t*>(self->floats.data)[i] == 0;
    for (int i = 0; i < self->strings.size; ++i) ok &= static_cast<const char*>(self->strings.data[i])[0] == 0;
    for (int i = 0; i < self->objects.size; ++i) ok &= self->objects.data[i].object == nullptr;
    for (int i = 0; i < self->groups.size; ++i) ok &= !self->groups.data[i].data && self->groups.data[i].size == 0;
    ei::verify::Check("ScriptVars::ResetValues", true, ok, "%d/%d/%d/%d", self->floats.size, self->strings.size,
                      self->objects.size, self->groups.size);
}
EI_VERIFY(0x004B35E0, ResetValuesVerify, 0, s_unused);

static void __fastcall RemoveDeletedObjectsVerify(ScriptVars* self) {
    self->RemoveDeletedObjects();
    bool ok = true;
    for (int i = 0; i < self->objects.size; ++i) {
        CEngineObject* o = self->objects.data[i].object;
        ok &= !o || !(o->flags_ & 1);
    }
    for (int g = 0; g < self->groups.size; ++g) {
        const ScriptGroup& group = self->groups.data[g];
        for (int i = 0; i < group.size; ++i) ok &= group.data[i].object && !(group.data[i].object->flags_ & 1);
    }
    ei::verify::Check("ScriptVars::RemoveDeletedObjects", true, ok, "%d objects %d groups", self->objects.size,
                      self->groups.size);
}
EI_VERIFY(0x004B3940, RemoveDeletedObjectsVerify, 0, s_unused);

// 004B36D0: PUSH EBX; MOV EBX,[ESP+8] (5 bytes). Pure.
static BOOL(__fastcall* s_hasVariable)(const ScriptVars*, void*, const char*);
static BOOL __fastcall HasVariableVerify(const ScriptVars* self, void*, const char* name) {
    BOOL expected = s_hasVariable(self, nullptr, name);
    ei::verify::Check("ScriptVars::HasVariable", expected, self->HasVariable(name), "\"%s\"", name);
    return expected;
}
EI_VERIFY(0x004B36D0, HasVariableVerify, 5, s_hasVariable);

// 004B37D0: PUSH EBX; PUSH EBP; MOV EBP,[ESP+0x14] (6 bytes). Writes only its two outputs.
static BOOL(__fastcall* s_findVariable)(const ScriptVars*, void*, const char*, int*, int*);
static BOOL __fastcall FindVariableVerify(const ScriptVars* self, void*, const char* name, int* index, int* type) {
    int expectedIndex = 0, expectedType = 0;
    BOOL expected = s_findVariable(self, nullptr, name, &expectedIndex, &expectedType);
    BOOL actual = self->FindVariable(name, index, type);
    bool same = expected == actual && expectedIndex == *index && expectedType == *type;
    ei::verify::Check("ScriptVars::FindVariable", true, same, "\"%s\": %d %d/%d vs %d %d/%d", name, expected,
                      expectedType, expectedIndex, actual, *type, *index);
    return actual;
}
EI_VERIFY(0x004B37D0, FindVariableVerify, 6, s_findVariable);

// ---- SSCCmd -------------------------------------------------------------------------------------

// SetParameters only copies variables into the arguments, so running it twice gives the same
// state: run ours, record the bound arguments, run the original and compare.
// 004B3F60: PUSH ECX; PUSH EBX; MOV EBX,[ESP+0xC] (6 bytes).
static void(__fastcall* s_setParameters)(SSCCmd*, void*, const MfcArray<ScriptBinding>*, ScriptVars*);

namespace {
std::uint32_t BoundValue(const SSCCmd* cmd, const ScriptBinding& b) {
    switch (b.type) {
    case kScriptFloat: return reinterpret_cast<const std::uint32_t*>(cmd->floatArgs.data)[b.argIndex];
    case kScriptString: return reinterpret_cast<std::uint32_t>(static_cast<const char*>(cmd->stringArgs.data[b.argIndex]));
    case kScriptObject: return reinterpret_cast<std::uint32_t>(cmd->objectArgs.data[b.argIndex]);
    case kScriptGroup: return reinterpret_cast<std::uint32_t>(cmd->groupArgs.data[b.argIndex]);
    }
    return 0;
}
} // namespace

static void __fastcall SetParametersVerify(SSCCmd* self, void*, const MfcArray<ScriptBinding>* bindings, ScriptVars* vars) {
    self->SetParameters(bindings, vars);
    std::vector<std::uint32_t> ours;
    for (int i = 0; i < bindings->size; ++i) ours.push_back(BoundValue(self, bindings->data[i]));
    s_setParameters(self, nullptr, bindings, vars);
    bool same = true;
    for (int i = 0; i < bindings->size; ++i) same &= ours[i] == BoundValue(self, bindings->data[i]);
    ei::verify::Check("SSCCmd::SetParameters", true, same, "cmd %d, %d bindings", self->id, bindings->size);
}
EI_VERIFY(0x004B3F60, SetParametersVerify, 6, s_setParameters);

// 004B6360: PUSH ECX; PUSH ESI; MOV ESI,[ESP+0xC] (6 bytes). Both write into capture streams; the
// original's bytes then go to the real stream. (Nested calls recurse through this wrapper.)
static void(__fastcall* s_cmdSave)(const SSCCmd*, void*, ScriptStream*);
static void __fastcall CmdSaveVerify(const SSCCmd* self, void*, ScriptStream* stream) {
    CaptureStream expected, actual;
    s_cmdSave(self, nullptr, &expected);
    self->Save(&actual);
    ei::verify::Check("SSCCmd::Save", true, expected.bytes == actual.bytes && expected.chunks == actual.chunks, "cmd %d: %u vs %u bytes", self->id,
                      static_cast<unsigned>(expected.bytes.size()), static_cast<unsigned>(actual.bytes.size()));
    expected.Forward(stream);
}
EI_VERIFY(0x004B6360, CmdSaveVerify, 6, s_cmdSave);

// ---- Evaluation ---------------------------------------------------------------------------------

// The Calc functions evaluate whole call trees. Apart from consuming random numbers (Random) and
// allocating temporaries in the CScript, re-evaluating a tree gives the same result: object
// variables set by Every/Any/Sum end on the same member, areas looked up are created once. So run
// ours with the generator saved, restore it, run the original, and compare the results and the
// generator state. The original's result is what the caller sees.
namespace {
IsaacRandom* GameRandom() { return reinterpret_cast<IsaacRandom*>(0x007A9D40); }

struct RandomSnapshot {
    unsigned char bytes[sizeof(IsaacRandom)];
    void Take() { std::memcpy(bytes, GameRandom(), sizeof(bytes)); }
    void Restore() const { std::memcpy(GameRandom(), bytes, sizeof(bytes)); }
    bool Matches() const { return std::memcmp(bytes, GameRandom(), sizeof(bytes)) == 0; }
};
} // namespace

// 004B41E0: PUSH EBP; MOV EBP,ESP; PUSH -1 (5 bytes).
static void(__fastcall* s_calcFloat)(SSCCmd*, void*, CScript*, ScriptVars*, float*);
static void __fastcall CalcFloatVerify(SSCCmd* self, void*, CScript* script, ScriptVars* vars, float* result) {
    RandomSnapshot before, afterOurs;
    before.Take();
    float actual = 0.0f;
    self->CalcFloat(script, vars, &actual);
    afterOurs.Take();
    before.Restore();
    s_calcFloat(self, nullptr, script, vars, result);
    bool same = std::memcmp(&actual, result, 4) == 0 && afterOurs.Matches();
    ei::verify::Check("SSCCmd::CalcFloat", true, same, "id %02X: %g vs %g", self->id, *result, actual);
}
EI_VERIFY(0x004B41E0, CalcFloatVerify, 5, s_calcFloat);

// 004B56C0: PUSH EBP; MOV EBP,ESP; MOV EAX,[EBP+0xC] (6 bytes). Each run returns its own
// temporary reference; their objects must match.
static void(__fastcall* s_calcObject)(SSCCmd*, void*, CScript*, ScriptVars*, ScriptObjectRef**);
static void __fastcall CalcObjectVerify(SSCCmd* self, void*, CScript* script, ScriptVars* vars, ScriptObjectRef** result) {
    RandomSnapshot before, afterOurs;
    before.Take();
    ScriptObjectRef* actual = nullptr;
    self->CalcObject(script, vars, &actual);
    afterOurs.Take();
    before.Restore();
    s_calcObject(self, nullptr, script, vars, result);
    bool same = actual && *result && actual->object == (*result)->object && afterOurs.Matches();
    ei::verify::Check("SSCCmd::CalcObject", true, same, "id %02X: %p vs %p", self->id, *result ? (*result)->object : nullptr,
                      actual ? actual->object : nullptr);
}
EI_VERIFY(0x004B56C0, CalcObjectVerify, 6, s_calcObject);

// 004B4030: MOV EAX,[ESP+0xC]; MOV EDX,[EAX] (6 bytes). Compares the argument the call fills.
static void(__fastcall* s_calcParameters)(SSCCmd*, void*, CScript*, ScriptVars*, const ScriptSubCall*);

namespace {
// What the evaluated argument holds: float bits, the referenced object, or a group's contents.
std::vector<std::uint32_t> ArgumentValue(const SSCCmd* cmd, const ScriptSubCall* call) {
    std::vector<std::uint32_t> v;
    switch (call->type) {
    case kScriptFloat: v.push_back(reinterpret_cast<const std::uint32_t*>(cmd->floatArgs.data)[call->argIndex]); break;
    case kScriptObject: {
        const ScriptObjectRef* ref = cmd->objectArgs.data[call->argIndex];
        v.push_back(reinterpret_cast<std::uint32_t>(ref ? ref->object : nullptr));
        break;
    }
    case kScriptGroup: {
        const ScriptGroup* g = cmd->groupArgs.data[call->argIndex];
        if (g)
            for (int i = 0; i < g->size; ++i) v.push_back(reinterpret_cast<std::uint32_t>(g->data[i].object));
        break;
    }
    }
    return v;
}
} // namespace

static void __fastcall CalcParametersVerify(SSCCmd* self, void*, CScript* script, ScriptVars* vars, const ScriptSubCall* call) {
    RandomSnapshot before, afterOurs;
    before.Take();
    self->CalcParameters(script, vars, call);
    std::vector<std::uint32_t> actual = ArgumentValue(self, call);
    afterOurs.Take();
    before.Restore();
    s_calcParameters(self, nullptr, script, vars, call);
    bool same = actual == ArgumentValue(self, call) && afterOurs.Matches();
    ei::verify::Check("SSCCmd::CalcParameters", true, same, "type %d cmd %02X", call->type, call->cmd->id);
}
EI_VERIFY(0x004B4030, CalcParametersVerify, 6, s_calcParameters);
