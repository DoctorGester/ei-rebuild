// The game server's script variables (+0x25B0): named floats the script reads and writes in a
// network game (00419570 .. 00419940), replicated to the clients as VariableMessages. See
// net_messages.h and docs/notes/network.md.
#pragma float_control(precise, on)

#include "net_messages.h"
#include "core_refcount.h"
#include "core_streams.h"

#include "ei/hook.h"
#include "lib/crt.h"

#include <cstring>
#include "originals_library.h"

namespace net {

namespace {

// STLport (library): string equality, erase(first, last), append(first, last, tag); the list's
// begin, insert and erase; a counted pointer from a raw one without a reference.
__declspec(naked) void** __fastcall VariablesBegin(void* /*list*/, void*, void** /*out*/) EI_ORIGINAL(0x00420280)
__declspec(naked) void** __fastcall VariablesInsert(void* /*list*/, void*, void** /*out*/, void* /*pos*/, const RefPtr* /*value*/) EI_ORIGINAL(0x004221B0)
__declspec(naked) void** __fastcall VariablesErase(void* /*list*/, void*, void** /*out*/, void* /*position*/) EI_ORIGINAL(0x00420290)
__declspec(naked) RefPtr* __fastcall VariableRefFromRaw(RefPtr* /*ref*/, void* /*object*/) EI_ORIGINAL(0x00427FD0)

std::uint32_t& VariableCounter() { return *reinterpret_cast<std::uint32_t*>(0x0079B608); }
float GameFloat(std::uintptr_t a) { return *reinterpret_cast<const volatile float*>(a); }
// FCOMP then TEST AH,0x40 (C3): equal, or unordered.
bool EqualOrUnordered(double a, double b) { return !(a < b) && !(a > b); }

VariableMessage* VariableAt(void** node) { return static_cast<VariableMessage*>(node[2]); }

// The search the original inlines in each function: the first node whose variable's name has the
// same bytes as `name`, else the sentinel.
void** FindNode(void** sentinel, const StringRep* name) {
    const std::size_t length = static_cast<std::size_t>(name->finish - name->start);
    void** node = static_cast<void**>(sentinel[0]);
    for (; node != sentinel; node = static_cast<void**>(node[0])) {
        const StringRep& other = VariableAt(node)->name_;
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
    *out = FindNode(static_cast<void**>(variables_), name);
    return out;
}

// 004195D0: the variable named `name`, made (value 0) at the front of the list when missing.
void** ServerSession::FindOrAddVariable(void** out, const StringRep* name) {
    void* variables = &variables_;
    void** node = static_cast<void**>(static_cast<void**>(variables_)[0]);
    while (node != variables_) {
        if (StringEquals(&VariableAt(node)->name_, name)) break;
        node = static_cast<void**>(node[0]);
    }
    if (node != variables_) {
        *out = node;
        return out;
    }

    auto* variable = NewMessageMemory<VariableMessage>();
    if (variable) {
        RefValueConstructBase(variable);
        ei::SetVtable(variable, kMessageVtable);
        variable->version_ = 1;
        StringInitEmpty(&variable->name_);
        ei::SetVtable(variable, VariableMessage::kVtableAddress);
    }
    RefPtr ref;
    const RefPtr* value = VariableRefFromRaw(&ref, variable);
    void* begin;
    VariablesBegin(variables, nullptr, &begin);
    void* inserted;
    VariablesInsert(variables, nullptr, &inserted, begin, value);
    void** first = static_cast<void**>(static_cast<void**>(variables_)[0]);

    // The name, assigned in place (string::operator= inlined).
    StringRep* to = &VariableAt(first)->name_;
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
        else StringEraseOriginal(to, nullptr, d, to->finish);
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
        VariableMessage* variable = VariableAt(static_cast<void**>(found));
        std::memcpy(&variable->value_, &value, 4);
        variable->serial_ = VariableCounter();
        ++VariableCounter();
        ++variable->version_;
        return;
    }
    void* found;
    FindVariable(&found, name);
    if (found != variables_) Erase(variables_, static_cast<void**>(found));
}

// 004197D0: raises the variable to `value` (the larger of the two; `value` when either is NaN),
// made when missing; a result of zero (or NaN) removes it.
void ServerSession::RaiseVariable(const StringRep* name, float value) {
    void* found;
    FindOrAddVariable(&found, name);
    VariableMessage* variable = VariableAt(static_cast<void**>(found));
    float current = variable->value_;
    std::uint32_t currentBits, valueBits;
    std::memcpy(&currentBits, &current, 4);
    std::memcpy(&valueBits, &value, 4);
    std::uint32_t bits = !(current >= value) ? valueBits : currentBits;
    float selected;
    std::memcpy(&selected, &bits, 4);
    volatile double loaded = selected;   // FLD / FSTP
    VariableAt(static_cast<void**>(found))->value_ = static_cast<float>(loaded);
    VariableAt(static_cast<void**>(found))->serial_ = VariableCounter();
    ++VariableCounter();
    ++VariableAt(static_cast<void**>(found))->version_;
    if (EqualOrUnordered(VariableAt(static_cast<void**>(found))->value_, GameFloat(0x0073B858))) {
        void** node = FindNode(static_cast<void**>(variables_), name);
        if (node != variables_) Erase(variables_, node);
    }
}

// 004198D0: removes the variable when it exists.
void ServerSession::RemoveVariable(const StringRep* name) {
    void** node = FindNode(static_cast<void**>(variables_), name);
    if (node != variables_) Erase(variables_, node);
}

// 00419940: the variable's value, 0 when missing (returned unrounded in ST0).
double ServerSession::GetVariable(const StringRep* name) {
    void** node = FindNode(static_cast<void**>(variables_), name);
    if (node != variables_) return VariableAt(node)->value_;
    return GameFloat(0x0073B858);
}

EI_REPLACE_METHOD(0x00419570, ServerSession::FindVariable);
EI_REPLACE_METHOD(0x004195D0, ServerSession::FindOrAddVariable);
EI_REPLACE_METHOD(0x00419730, ServerSession::SetVariable);
EI_REPLACE_METHOD(0x004197D0, ServerSession::RaiseVariable);
EI_REPLACE_METHOD(0x004198D0, ServerSession::RemoveVariable);
EI_REPLACE_METHOD(0x00419940, ServerSession::GetVariable);

} // namespace net
