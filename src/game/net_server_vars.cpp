// The game server's script variables (+0x25B0): named floats the script reads and writes in a
// network game (00419570 .. 00419940), replicated to the clients as 0073BCE8 objects. See
// net_classes.h and docs/notes/unmapped_410_470.md ("Seventh round").
#pragma float_control(precise, on)

#include "net_classes.h"
#include "core_refcount.h"
#include "core_streams.h"

#include "ei/hook.h"
#include "lib/crt.h"

#include <cstring>

namespace net {

namespace {

__declspec(naked) void __fastcall StringConstruct(StringRep* /*s*/) EI_ORIGINAL(0x0041F460)
// STLport (library): string equality, erase(first, last), append(first, last, tag); the list's
// begin, insert and erase; a counted pointer from a raw one without a reference.
__declspec(naked) bool __fastcall StringEquals(const StringRep* /*a*/, const StringRep* /*b*/) EI_ORIGINAL(0x00427FA0)
__declspec(naked) void __fastcall StringErase(StringRep* /*s*/, void*, char* /*first*/, char* /*last*/) EI_ORIGINAL(0x0040B3C0)
__declspec(naked) void __fastcall StringAppend(StringRep* /*s*/, void*, const char* /*first*/, const char* /*last*/, void* /*tag*/) EI_ORIGINAL(0x0042AFB0)
__declspec(naked) void** __fastcall VariablesBegin(void* /*list*/, void*, void** /*out*/) EI_ORIGINAL(0x00420280)
__declspec(naked) void** __fastcall VariablesInsert(void* /*list*/, void*, void** /*out*/, void* /*pos*/, const RefPtr* /*value*/) EI_ORIGINAL(0x004221B0)
__declspec(naked) void** __fastcall VariablesErase(void* /*list*/, void*, void** /*out*/, void* /*position*/) EI_ORIGINAL(0x00420290)
__declspec(naked) RefPtr* __fastcall VariableRefFromRaw(RefPtr* /*ref*/, void* /*object*/) EI_ORIGINAL(0x00427FD0)

constexpr std::uintptr_t kVariableVtable = 0x0073BCE8;

// A script variable (0x20 bytes).
struct ScriptVariable {
    void* vtable;
    std::uint32_t refHeader;   // +0x04
    std::uint32_t version;     // +0x08 1 when made, bumped on every change
    StringRep name;            // +0x0C
    float value;               // +0x18
    std::uint32_t serial;      // +0x1C from 0079B608
};
static_assert(sizeof(ScriptVariable) == 0x20);

std::uint32_t& VariableCounter() { return *reinterpret_cast<std::uint32_t*>(0x0079B608); }
float GameFloat(std::uintptr_t a) { return *reinterpret_cast<const volatile float*>(a); }
// FCOMP then TEST AH,0x40 (C3): equal, or unordered.
bool EqualOrUnordered(double a, double b) { return !(a < b) && !(a > b); }

ScriptVariable* VariableAt(void** node) { return static_cast<ScriptVariable*>(node[2]); }

// The search the original inlines in each function: the first node whose variable's name has the
// same bytes as `name`, else the sentinel.
void** FindNode(void** sentinel, const StringRep* name) {
    const std::size_t length = static_cast<std::size_t>(name->finish - name->start);
    void** node = static_cast<void**>(sentinel[0]);
    for (; node != sentinel; node = static_cast<void**>(node[0])) {
        const StringRep& other = VariableAt(node)->name;
        if (static_cast<std::size_t>(other.finish - other.start) == length && std::memcmp(other.start, name->start, length) == 0)
            break;
    }
    return node;
}

void Erase(void*& list, void** node) {
    void* erased;
    VariablesErase(&list, nullptr, &erased, node);
}

} // namespace

// 00419570: the variable named `name` (its list node), or the sentinel.
void** ServerSession::FindVariable(void** out, const StringRep* name) {
    *out = FindNode(static_cast<void**>(list_25B0), name);
    return out;
}

// 004195D0: the variable named `name`, made (value 0) at the front of the list when missing.
void** ServerSession::FindOrAddVariable(void** out, const StringRep* name) {
    void* variables = &list_25B0;
    void** node = static_cast<void**>(static_cast<void**>(list_25B0)[0]);
    while (node != list_25B0) {
        if (StringEquals(&VariableAt(node)->name, name)) break;
        node = static_cast<void**>(node[0]);
    }
    if (node != list_25B0) {
        *out = node;
        return out;
    }

    auto* variable = static_cast<ScriptVariable*>(gamecrt::operator_new(sizeof(ScriptVariable)));
    variable->refHeader = 0;
    if (variable) {
        RefValueConstructBase(variable);
        std::uintptr_t vtable = kMessageVtable;
        std::memcpy(variable, &vtable, 4);
        variable->version = 1;
        StringConstruct(&variable->name);
        vtable = kVariableVtable;
        std::memcpy(variable, &vtable, 4);
    }
    RefPtr ref;
    const RefPtr* value = VariableRefFromRaw(&ref, variable);
    void* begin;
    VariablesBegin(variables, nullptr, &begin);
    void* inserted;
    VariablesInsert(variables, nullptr, &inserted, begin, value);
    void** first = static_cast<void**>(static_cast<void**>(list_25B0)[0]);

    // The name, assigned in place (string::operator= inlined).
    StringRep* to = &VariableAt(first)->name;
    if (name != to) {
        const char* from = name->start;
        const char* last = name->finish;
        char* d = to->start;
        bool append = false;
        if (from != last) {
            for (;;) {
                if (d == to->finish) {
                    append = from != last;
                    break;
                }
                *d++ = *from++;
                if (from == last) break;
            }
        }
        if (append) StringAppend(to, nullptr, from, last, out);
        else StringErase(to, nullptr, d, to->finish);
    }
    *out = first;
    return out;
}

// 00419730: sets the variable; a zero (or NaN) value removes it instead. A set bumps the version
// and takes the next serial.
void ServerSession::SetVariable(const StringRep* name, float value) {
    if (!EqualOrUnordered(value, GameFloat(0x0073B858))) {
        void* found;
        FindOrAddVariable(&found, name);
        ScriptVariable* variable = VariableAt(static_cast<void**>(found));
        std::memcpy(&variable->value, &value, 4);
        variable->serial = VariableCounter();
        ++VariableCounter();
        ++variable->version;
        return;
    }
    void* found;
    FindVariable(&found, name);
    if (found != list_25B0) Erase(list_25B0, static_cast<void**>(found));
}

// 004197D0: raises the variable to `value` (the larger of the two; `value` when either is NaN),
// made when missing; a result of zero (or NaN) removes it.
void ServerSession::RaiseVariable(const StringRep* name, float value) {
    void* found;
    FindOrAddVariable(&found, name);
    ScriptVariable* variable = VariableAt(static_cast<void**>(found));
    float current = variable->value;
    std::uint32_t currentBits, valueBits;
    std::memcpy(&currentBits, &current, 4);
    std::memcpy(&valueBits, &value, 4);
    std::uint32_t bits = !(current >= value) ? valueBits : currentBits;
    float selected;
    std::memcpy(&selected, &bits, 4);
    volatile double loaded = selected;   // FLD / FSTP
    VariableAt(static_cast<void**>(found))->value = static_cast<float>(loaded);
    VariableAt(static_cast<void**>(found))->serial = VariableCounter();
    ++VariableCounter();
    ++VariableAt(static_cast<void**>(found))->version;
    if (EqualOrUnordered(VariableAt(static_cast<void**>(found))->value, GameFloat(0x0073B858))) {
        void** node = FindNode(static_cast<void**>(list_25B0), name);
        if (node != list_25B0) Erase(list_25B0, node);
    }
}

// 004198D0: removes the variable when it exists.
void ServerSession::RemoveVariable(const StringRep* name) {
    void** node = FindNode(static_cast<void**>(list_25B0), name);
    if (node != list_25B0) Erase(list_25B0, node);
}

// 00419940: the variable's value, 0 when missing (returned unrounded in ST0).
double ServerSession::GetVariable(const StringRep* name) {
    void** node = FindNode(static_cast<void**>(list_25B0), name);
    if (node != list_25B0) return VariableAt(node)->value;
    return GameFloat(0x0073B858);
}

namespace {
void** __fastcall FindVariableHook(ServerSession* self, void*, void** out, const StringRep* name) { return self->FindVariable(out, name); }
void** __fastcall FindOrAddVariableHook(ServerSession* self, void*, void** out, const StringRep* name) {
    return self->FindOrAddVariable(out, name);
}
void __fastcall SetVariableHook(ServerSession* self, void*, const StringRep* name, float value) { self->SetVariable(name, value); }
void __fastcall RaiseVariableHook(ServerSession* self, void*, const StringRep* name, float value) { self->RaiseVariable(name, value); }
void __fastcall RemoveVariableHook(ServerSession* self, void*, const StringRep* name) { self->RemoveVariable(name); }
double __fastcall GetVariableHook(ServerSession* self, void*, const StringRep* name) { return self->GetVariable(name); }
} // namespace
EI_REPLACE(0x00419570, FindVariableHook);
EI_REPLACE(0x004195D0, FindOrAddVariableHook);
EI_REPLACE(0x00419730, SetVariableHook);
EI_REPLACE(0x004197D0, RaiseVariableHook);
EI_REPLACE(0x004198D0, RemoveVariableHook);
EI_REPLACE(0x00419940, GetVariableHook);

} // namespace net
