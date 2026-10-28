// The game server's script variables: named floats the script reads and writes in a
// network game, replicated to the clients as VariableMessages. See net_messages.h and docs/notes/network.md.

#include "ei/x87.h"
#include "net_messages.h"
#include "core_refcount.h"
#include "core_streams.h"

#include "lib/nan_audit.h"

#include <bit>
#include <cstring>

namespace net {

// The search the original inlines in each function: the first variable whose name has the same bytes as
// `name`.
std::list<SharedRefTo<VariableMessage>>::iterator ServerSession::FindVariable(const std::string* name) {
    auto node = variables_.begin();
    for (; node != variables_.end(); ++node)
        if (node->Get()->name_ == *name) break;
    return node;
}

std::list<SharedRefTo<VariableMessage>>::iterator ServerSession::FindOrAddVariable(const std::string* name) {
    auto node = FindVariable(name);
    if (node != variables_.end()) return node;

    variables_.emplace_front(NewCounted<VariableMessage>());
    auto first = variables_.begin();

    // The name, assigned in place (string::operator= inlined: overwrite, then cut or append).
    std::string* to = &first->Get()->name_;
    if (name != to) *to = *name;
    return first;
}

// Sets the variable; a zero value removes it instead. A set bumps the version and takes the next serial.
void ServerSession::SetVariable(const std::string* name, float value) {
    EI_NAN_AUDIT(value);
    if (value != 0.0f) {
        VariableMessage* variable = FindOrAddVariable(name)->Get();
        x87::CopyBits(variable->value_, value);
        variable->serial_ = g_variableSerial;
        ++g_variableSerial;
        ++variable->version_;
        return;
    }
    auto found = FindVariable(name);
    if (found != variables_.end()) variables_.erase(found);
}

// Raises the variable to `value` (the larger of the two; `value` when either is NaN),
// made when missing; a result of zero (or NaN) removes it.
void ServerSession::RaiseVariable(const std::string* name, float value) {
    auto found = FindOrAddVariable(name);
    VariableMessage* variable = found->Get();
    float current = variable->value_;
    std::uint32_t currentBits, valueBits;
    currentBits = std::bit_cast<std::uint32_t>(current);
    valueBits = std::bit_cast<std::uint32_t>(value);
    std::uint32_t bits = current >= value ? currentBits : valueBits;
    float selected;
    selected = std::bit_cast<float>(bits);
    volatile double loaded = selected;   // reloaded through memory, as the original does
    found->Get()->value_ = static_cast<float>(loaded);
    found->Get()->serial_ = g_variableSerial;
    ++g_variableSerial;
    ++found->Get()->version_;
    EI_NAN_AUDIT(found->Get()->value_);
    if (found->Get()->value_ == 0.0f) {
        auto node = FindVariable(name);
        if (node != variables_.end()) variables_.erase(node);
    }
}

// Removes the variable when it exists.
void ServerSession::RemoveVariable(const std::string* name) {
    auto node = FindVariable(name);
    if (node != variables_.end()) variables_.erase(node);
}

// The variable's value, 0 when missing (returned unrounded).
double ServerSession::GetVariable(const std::string* name) {
    auto node = FindVariable(name);
    if (node != variables_.end()) return node->Get()->value_;
    return 0.0f;
}

} // namespace net
