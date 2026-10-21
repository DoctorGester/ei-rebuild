// The game server's script variables (+0x25B0): named floats the script reads and writes in a
// network game (00419570 .. 00419940), replicated to the clients as VariableMessages. See
// net_messages.h and docs/notes/network.md.

#include "net_messages.h"
#include "core_refcount.h"
#include "core_streams.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "game_data.h"
#include "lib/nan_audit.h"

#include <cstring>

namespace net {

namespace {

std::uint32_t& VariableCounter() { return ei::GameData<std::uint32_t>(0x0079B608); }

VariableMessage* VariableAt(RefNode* node) { return static_cast<VariableMessage*>(node->data.ptr); }

// The search the original inlines in each function: the first node whose variable's name has the
// same bytes as `name`, else the sentinel.
RefNode* FindNode(const stlp::SharedRefList& list, const stlp::string* name) {
    RefNode* node = list.First();
    for (; node != list.End(); node = node->next)
        if (VariableAt(node)->name_ == *name) break;
    return node;
}

} // namespace

// 00419570: the variable named `name` (its list node), or the sentinel.
RefNode** ServerSession::FindVariable(RefNode** out, const stlp::string* name) {
    *out = FindNode(variables_, name);
    return out;
}

// 004195D0: the variable named `name`, made (value 0) at the front of the list when missing.
RefNode** ServerSession::FindOrAddVariable(RefNode** out, const stlp::string* name) {
    RefNode* node = FindNode(variables_, name);
    if (node != variables_.End()) {
        *out = node;
        return out;
    }

    auto* variable = NewMessageMemory<VariableMessage>();
    if (variable) {
        RefValueConstructBase(variable);
        ei::SetVtable(variable, kMessageVtable);
        variable->version_ = 1;
        stlp::StringElement::Default(&variable->name_);
        ei::SetVtable(variable, VariableMessage::kVtableAddress);
    }
    RefPtr ref;
    const RefPtr* value = RefPtrFromRaw(&ref, variable);   // 00427FD0
    variables_.Insert(variables_.First(), *value);
    RefNode* first = variables_.First();

    // The name, assigned in place (string::operator= inlined: overwrite, then cut or append).
    stlp::string* to = &VariableAt(first)->name_;
    if (name != to) to->AssignFrom(*name);
    *out = first;
    return out;
}

// 00419730: sets the variable; a zero value removes it instead. A set bumps the version
// and takes the next serial.
void ServerSession::SetVariable(const stlp::string* name, float value) {
    EI_NAN_AUDIT(value);
    if (value != 0.0f /*0073B858*/) {
        RefNode* found;
        FindOrAddVariable(&found, name);
        VariableMessage* variable = VariableAt(found);
        std::memcpy(&variable->value_, &value, 4);
        variable->serial_ = VariableCounter();
        ++VariableCounter();
        ++variable->version_;
        return;
    }
    RefNode* found;
    FindVariable(&found, name);
    if (found != variables_.End()) variables_.Erase(found);
}

// 004197D0: raises the variable to `value` (the larger of the two; `value` when either is NaN),
// made when missing; a result of zero (or NaN) removes it.
void ServerSession::RaiseVariable(const stlp::string* name, float value) {
    RefNode* found;
    FindOrAddVariable(&found, name);
    VariableMessage* variable = VariableAt(found);
    float current = variable->value_;
    std::uint32_t currentBits, valueBits;
    std::memcpy(&currentBits, &current, 4);
    std::memcpy(&valueBits, &value, 4);
    std::uint32_t bits = !(current >= value) ? valueBits : currentBits;
    float selected;
    std::memcpy(&selected, &bits, 4);
    volatile double loaded = selected;   // FLD / FSTP
    VariableAt(found)->value_ = static_cast<float>(loaded);
    VariableAt(found)->serial_ = VariableCounter();
    ++VariableCounter();
    ++VariableAt(found)->version_;
    EI_NAN_AUDIT(VariableAt(found)->value_);
    if (VariableAt(found)->value_ == 0.0f /*0073B858*/) {
        RefNode* node = FindNode(variables_, name);
        if (node != variables_.End()) variables_.Erase(node);
    }
}

// 004198D0: removes the variable when it exists.
void ServerSession::RemoveVariable(const stlp::string* name) {
    RefNode* node = FindNode(variables_, name);
    if (node != variables_.End()) variables_.Erase(node);
}

// 00419940: the variable's value, 0 when missing (returned unrounded in ST0).
double ServerSession::GetVariable(const stlp::string* name) {
    RefNode* node = FindNode(variables_, name);
    if (node != variables_.End()) return VariableAt(node)->value_;
    return 0.0f /*0073B858*/;
}

EI_REPLACE_METHOD(0x00419570, ServerSession::FindVariable);
EI_REPLACE_METHOD(0x004195D0, ServerSession::FindOrAddVariable);
EI_REPLACE_METHOD(0x00419730, ServerSession::SetVariable);
EI_REPLACE_METHOD(0x004197D0, ServerSession::RaiseVariable);
EI_REPLACE_METHOD(0x004198D0, ServerSession::RemoveVariable);
EI_REPLACE_METHOD(0x00419940, ServerSession::GetVariable);

} // namespace net
